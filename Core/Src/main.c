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
#include "tim.h"
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

/* G_skin = 5*(4095-adc)/adc（μS），公式只在此处出现 */
static float adc_to_g(uint16_t adc)
{
  float a = (float)adc;
  return (a > 0.0f) ? 5.0f * (4095.0f - a) / a : 0.0f;
}

static float read_g1(void) { return adc_to_g(adc_value1); }
static float read_g2(void) { return adc_to_g(adc_value2); }
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
  MX_FSMC_Init();
  MX_ADC1_Init();
  MX_TIM3_Init();
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
	  static uint16_t last_seq = 0;
	  if (adc_seq != last_seq)
	      {
	        last_seq = adc_seq;
	                   // 双波形实时更新（滤波后的平滑值）
	                   LCD_WaveformPush(read_g1(), read_g2());
	      }
	  static uint32_t last_lcd_update = 0;
	  if (HAL_GetTick() - last_lcd_update >= 500)
	  {
		  last_lcd_update = HAL_GetTick();
		  LCD_DrawSkinText(read_g1(), read_g2());
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

/* ADC/DMA 错误（overrun、DMA 传输错误等）：计数供诊断，采集继续 */
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
