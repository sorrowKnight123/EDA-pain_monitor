/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include "fsmc.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "ssd1289_fsmc.h"
#include "app_config.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void MX_IWDG_Init(void);
static HAL_StatusTypeDef adc_stall_recover(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
volatile uint16_t adc_value1;   /* 滤波后的电导1有效样本（PC1） */
volatile uint16_t adc_value2;   /* 滤波后的电导2有效样本（PC0） */
volatile uint16_t adc_seq = 0;  /* 有效样本计数，主循环据此消费（回绕安全） */

/* 超采样：TIM3 500Hz 触发，参数见 app_config.h（ADC_BLOCK_TRIG）；
 * DMA 半区满在中断里抽取为 1 个有效样本，有效输出率 50Hz */
#define ADC_DMA_BUF_LEN (4 * ADC_BLOCK_TRIG)   /* 2 半区 × 2 通道 × ADC_BLOCK_TRIG 次触发 */
static volatile uint16_t adc_dma_buf[ADC_DMA_BUF_LEN];

volatile uint32_t adc_err_cnt = 0;   /* ADC/DMA 错误计数（overrun 等），调试器/诊断用 */
volatile uint32_t adc_stall_cnt = 0; /* 采集停摆（含已恢复）总次数，驱动屏上告警标记 */
volatile uint8_t  lcd_ready = 0;     /* LCD 可用标志：Error_Handler 据此决定能否上屏报错 */

/* 8 点整数滑动平均（窗口长度 FILT_N 见 app_config.h）：O(1) 更新、无软浮点 */
typedef struct {
  uint16_t buf[FILT_N];
  uint32_t sum;
  uint8_t idx;
  uint8_t cnt;
} GFilter;

static GFilter gf1 = { {0}, 0, 0, 0 };   /* 电导1 滤波状态 */
static GFilter gf2 = { {0}, 0, 0, 0 };   /* 电导2 滤波状态 */

static uint16_t gfilter(GFilter *f, uint16_t raw)
{
  f->sum -= f->buf[f->idx];
  f->buf[f->idx] = raw;
  f->sum += raw;
  f->idx = (uint8_t)((f->idx + 1) % FILT_N);
  if (f->cnt < FILT_N) f->cnt++;
  return (uint16_t)(f->sum / f->cnt);
}

/* 块内抽取：去最大/最小后取均值（10 取 8），抑制电极接触瞬间的野值 */
static uint16_t adc_block_reduce(const volatile uint16_t *p)
{
  uint32_t sum = 0;
  uint16_t mn = 0xFFFF, mx = 0;
  uint8_t i;
  for (i = 0; i < ADC_BLOCK_TRIG; i++)
  {
    uint16_t v = p[i * 2];
    sum += v;
    if (v < mn) mn = v;
    if (v > mx) mx = v;
  }
  return (uint16_t)((sum - mn - mx) / (ADC_BLOCK_TRIG - 2));
}

/* 处理一个 DMA 半区：偶数下标 = PC1（电导1），奇数下标 = PC0（电导2） */
static void adc_process_half(const volatile uint16_t *p)
{
  adc_value1 = gfilter(&gf1, adc_block_reduce(p));
  adc_value2 = gfilter(&gf2, adc_block_reduce(p + 1));
  adc_seq++;
}

/* 电导换算：G = 5*(4095-adc)/adc（μS），再减 0.7 零点偏移、乘 30 缩放；
 * 公式只在此处出现，adc_to_g100 直接复用它 */
static float adc_to_g(uint16_t adc)
{
  float a = (float)adc;
  return (a > 0.0f) ? ((5.0f * (4095.0f - a) / a)-0.7f)*30 : 0.0f;
}

/* 串口上报用：与屏上数值完全同源同格式——先共用 adc_to_g 取值，
 * 再共用 LCD_ValStr 格式化，因此串口字面与屏上字面必然一致（含四舍五入与 999.99 限幅） */
static void adc_to_g100(char *out, size_t cap, uint16_t adc)
{
  (void)LCD_ValStr(out, cap, adc_to_g(adc));
}

static float read_g1(void) { return adc_to_g(adc_value1); }
static float read_g2(void) { return adc_to_g(adc_value2); }

/* 串口1 上报（USART1 发、DMA1_Channel4 搬）：整帧一次交给 DMA，CPU 不逐字节搬运。
 * 帧缓冲静态分配，DMA 运行期间内容被硬件读取，故只在确认链路空闲后才重填。
 * 50Hz × 16B 帧在 115200bps 下仅占约 7% 带宽，不会积压 */
static uint8_t uart_buf[32];

static void uart_send_g100(const char *g1_str, const char *g2_str)
{
  /* 帧格式 "<电导1><分隔符><电导2>\n"；数值直接取自 LCD_ValStr 的输出，
   * 与屏上显示逐字符相同，不再自行做定点拆分 */
  int n = snprintf((char *)uart_buf, sizeof uart_buf,
                   "%s" UART_FRAME_SEP "%s\n", g1_str, g2_str);
  /* gState 是链路忙闲的唯一判据：DMA 发完由 HAL 置回 READY。
   * 忙时直接丢弃本帧——主循环还要喂狗/推波形/刷屏，不能在此等待。
   * n >= sizeof 说明被截断（snprintf 返回"本该写入的长度"），不得交给 DMA */
  if (n > 0 && n < (int)sizeof uart_buf && huart1.gState == HAL_UART_STATE_READY)
  {
    (void)HAL_UART_Transmit_DMA(&huart1, uart_buf, (uint16_t)n);
  }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_FSMC_Init();
  MX_ADC1_Init();
  MX_TIM3_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  /* 上电校准 ADC（F1 必要，减小偏移温漂），再以 DMA 循环模式启动：
   * TIM3 500Hz 触发双通道扫描 → DMA 搬入 adc_dma_buf →
   * 半区满(20ms)在中断里抽取+滤波 → 主循环按 adc_seq 消费 */
  HAL_ADCEx_Calibration_Start(&hadc1);
  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buf, ADC_DMA_BUF_LEN);
  HAL_TIM_Base_Start(&htim3);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
  HAL_Delay(100);
  LCD_Init();
  LCD_InitLayout();           /* 灰底 + 白色分隔线 + 纵轴刻度 */
  LCD_DrawSkinText(0.0f, 0.0f);
  lcd_ready = 1;              /* 此后 Error_Handler 可以上屏报错 */
  MX_IWDG_Init();             /* 独立看门狗 ≈1s，主循环喂狗 */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  IWDG->KR = 0xAAAA;   /* 喂狗 */

	  /* 采集停摆监测：adc_seq 预期 50Hz 增长，超时未增长说明采集链路已死
	   * （DMA 传输错误致通道停转、ADC 停转等），就地重启恢复；
	   * 短窗口内连续失败超限则整机复位。主循环喂狗不受影响，
	   * IWDG 察觉不了这类"数据通路死亡"，必须在此显式监测 */
	  {	static uint16_t seen_seq = 0;
		static uint32_t seen_tick = 0, last_stall_tick = 0;
		static uint8_t burst = 0;
		uint32_t now = HAL_GetTick();
		if (adc_seq != seen_seq)
		{	seen_seq = adc_seq;
			seen_tick = now;
		}
		else if (now - seen_tick >= ADC_STALL_RECOVER_MS)
		{	if (adc_stall_recover() != HAL_OK) { NVIC_SystemReset(); }
			burst = (now - last_stall_tick <= ADC_STALL_BURST_MS) ? (uint8_t)(burst + 1) : 1;
			last_stall_tick = now;
			seen_tick = now;   /* 给恢复一个完整的观察窗口 */
			if (burst >= ADC_STALL_RESET_LIMIT)
			{
				NVIC_SystemReset();
			}
		}
	  }

	  static uint16_t last_seq = 0;
	  if (adc_seq != last_seq)
	      {
	        last_seq = adc_seq;
	                   // 双波形实时更新（滤波后的平滑值）
	                   LCD_WaveformPush(read_g1(), read_g2());
	                   /* 串口1 DMA 上报：与有效样本同步（50Hz），格式 "<电导1>，<电导2>\n"，
	                    * 数值经 LCD_ValStr 格式化，与屏上显示字面一致 */
	                   {	char s1[8], s2[8];
	                   	adc_to_g100(s1, sizeof s1, adc_value1);
	                   	adc_to_g100(s2, sizeof s2, adc_value2);
	                   	uart_send_g100(s1, s2);
	                   }
	      }
	  static uint32_t last_lcd_update = 0;
	  if (HAL_GetTick() - last_lcd_update >= 500)
	  {
		  last_lcd_update = HAL_GetTick();
		  LCD_DrawSkinText(read_g1(), read_g2());
		  /* 告警标记：状态行右端红块 = 曾发生 ADC/DMA 错误或采集停摆 */
		  {	uint8_t alarm = (adc_err_cnt || adc_stall_cnt) ? 1 : 0;
			/* 每次刷新无条件重绘：LCD_DrawSkinText 在状态切换时会整行擦灰，
			 * 若只在状态变化时画红块，红块会被擦掉后永久消失 */
			LCD_Rect_Fill(304, LAYOUT_STATUS_Y, 16, 16, alarm ? RED : GRAY);
		  }
#if ADC_DEBUG_SHOW_RAW
		  {	char dbg[40];
			snprintf(dbg, sizeof dbg, "R1=%4u R2=%4u S=%5u E=%3lu",
					(unsigned)adc_value1, (unsigned)adc_value2,
					(unsigned)adc_seq, (unsigned long)adc_err_cnt);
			LCD_DebugLine(dbg);
		  }
#endif
	  }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    __WFI();   /* 睡眠模式（SLEEPDEEP=0）：TIM3/ADC/DMA 持续运行，仅内核时钟门控。
                * 任意中断唤醒——DMA 半区满(20ms)推波形，SysTick(1ms)兜底驱动 500ms
                * 文本刷新与喂狗。最坏唤醒延迟 1ms，相对 20ms 帧周期可忽略 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* 独立看门狗（寄存器级配置，不引入 HAL IWDG 驱动，看门狗尽量独立）：
 * 启动 LSI(40kHz)，预分频 /64，重载 625 → 约 1s 超时。
 * 主循环或任何 late-stage 卡死（含 Error_Handler 挂起）超过 1s 自动复位恢复 */
static void MX_IWDG_Init(void)
{
  SET_BIT(RCC->CSR, RCC_CSR_LSION);              /* 启动 LSI 并等待就绪 */
  while (READ_BIT(RCC->CSR, RCC_CSR_LSIRDY) == RESET) { }

  IWDG->KR  = 0x5555;                            /* 解锁 PR/RLR */
  IWDG->PR  = 2;                                 /* 预分频 /64 → 625Hz 计数 */
  IWDG->RLR = 625;                               /* 625 计数 ≈ 1.0s */
  IWDG->KR  = 0xAAAA;                            /* 重载计数 */
  IWDG->KR  = 0xCCCC;                            /* 启动看门狗（此后不可关闭） */
}

/* 采集停摆恢复：停止 TIM3 后重启 ADC+DMA，避免校准/重启窗口内
 * 外部触发干扰 ADC（HAL 的校准和 Start_DMA 不会清除 EXTTRIG）。
 * 返回 HAL_OK 仅代表恢复序列执行成功；是否真正恢复由主循环按 adc_seq 继续判定。
 * 注意：HAL_ADC_Stop_DMA 在 DMA 已因 TE 处于 READY 时不会清 ADC ErrorCode，
 * 实际由后续 HAL_ADC_Start_DMA 清错误码。 */
static HAL_StatusTypeDef adc_stall_recover(void)
{
  HAL_StatusTypeDef status;

  adc_stall_cnt++;   /* 检测到一次停摆（无论恢复是否成功都留痕） */

  if (HAL_TIM_Base_Stop(&htim3) != HAL_OK)
  {
    return HAL_ERROR;
  }

  status = HAL_ADC_Stop_DMA(&hadc1);
  if (status != HAL_OK)
  {
    (void)HAL_TIM_Base_Start(&htim3);
    return status;
  }

  status = HAL_ADCEx_Calibration_Start(&hadc1);
  if (status != HAL_OK)
  {
    (void)HAL_TIM_Base_Start(&htim3);
    return status;
  }

  status = HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buf, ADC_DMA_BUF_LEN);
  if (status != HAL_OK)
  {
    (void)HAL_TIM_Base_Start(&htim3);
    return status;
  }

  if (HAL_TIM_Base_Start(&htim3) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

/* ADC/DMA 错误计数。注意：DMA 传输错误(TE)发生时通道已被硬件停转、
 * HAL 已关闭全部 DMA 中断（HAL_DMA_IRQHandler），采集随之停止——
 * 本回调只负责计数留痕，实际恢复由主循环采集停摆监测统一执行 */
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    adc_err_cnt++;
  }
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc)
{
  if (hadc->Instance == ADC1)
  {
    /* 前 10 次触发（下标 0..19）已写入 DMA 缓冲前半区 */
    adc_process_half(&adc_dma_buf[0]);
  }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
  if (hadc->Instance == ADC1)
  {
    /* 后 10 次触发写入后半区 */
    adc_process_half(&adc_dma_buf[2 * ADC_BLOCK_TRIG]);
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  /* LCD 已初始化时整屏红底报错（不用字库，避免复制 4KB 字模表）；
   * 若 IWDG 已启动，约 1s 后复位重启 */
  if (lcd_ready)
  {
    LCD_Rect_Fill(0, 0, 320, 240, RED);
  }
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
