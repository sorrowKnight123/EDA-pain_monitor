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
static void adc_stall_monitor(void);
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

volatile uint32_t adc_err_cnt = 0;   /* ADC/DMA 错误计数。F1 的 ADC 没有 overrun 标志
                                      * （stm32f1xx_hal_adc.h 注明 Not available on STM32F1），
                                      * 唯一可达路径是 DMA 传输错误 TE —— 所以这里数的是 TE */
volatile uint32_t adc_spike_cnt = 0; /* 因块内极差超限被剔除的块数，用于校核阈值是否合适 */
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

/* 块内抽取：去最大/最小后取均值（10 取 8），抑制电极接触瞬间的野值；
 * 同时用「去极值后剩余样本」的极差做质量判据（取次大/次小），超限则整块作废
 * （参数见 app_config.h）。单个野值仍由去极值吸收、不会误废整块；
 * 只有干扰跨多个采样点时才判废。返回 0 = 该块受扰，调用方保持原值 */
static uint8_t adc_block_reduce(const volatile uint16_t *p, uint16_t *out)
{
  uint32_t sum = 0;
  uint16_t mx1 = 0, mx2 = 0;          /* 最大、次大 */
  uint16_t mn1 = 0xFFFF, mn2 = 0xFFFF;/* 最小、次小 */
  uint8_t i;

  for (i = 0; i < ADC_BLOCK_TRIG; i++)
  {
    uint16_t v = p[i * 2];
    sum += v;
    if (v > mx1)      { mx2 = mx1; mx1 = v; }
    else if (v > mx2) { mx2 = v; }
    if (v < mn1)      { mn2 = mn1; mn1 = v; }
    else if (v < mn2) { mn2 = v; }
  }

  /* 判据用 mx2-mn2（= 真正参与求均值的 8 个样本的极差） */
  if ((uint16_t)(mx2 - mn2) >
      ((uint32_t)mn1 * ADC_BLOCK_SPREAD_PCT / 100u + ADC_BLOCK_SPREAD_ABS))
  {
    return 0;                                   /* 去极值后仍摆动过大 → 判为受扰块 */
  }

  *out = (uint16_t)((sum - mx1 - mn1) / (ADC_BLOCK_TRIG - 2));
  return 1;
}

/* 处理一个 DMA 半区：偶数下标 = PC1（电导1），奇数下标 = PC0（电导2）。
 * 两通道分别判据：单侧电极受扰只冻结该侧，不影响另一侧 */
static void adc_process_half(const volatile uint16_t *p)
{
  uint16_t v;

  if (adc_block_reduce(p, &v))       adc_value1 = gfilter(&gf1, v);
  else                               adc_spike_cnt++;

  if (adc_block_reduce(p + 1, &v))   adc_value2 = gfilter(&gf2, v);
  else                               adc_spike_cnt++;

  adc_seq++;   /* 心跳照常推进：停摆监测只关心采集链路是否活着，与数据质量无关 */
}

/* 电导换算：G = 5*(4095-adc)/adc（μS），即前端 0..5μS 量程的传输函数。
 * 公式只在此处出现：标定零点换算、屏上显示、串口上报都经它取值 */
static float adc_g_raw(uint16_t adc)
{
  float a = (float)adc;
  return (a > 0.0f) ? (5.0f * (4095.0f - a) / a) : 0.0f;
}

/* ---- 上电零点标定（每通道独立，零点的唯一来源） ----
 * 上电后取静息均值作各通道零点（腹部/腿部电极接触电阻不同，固定偏移对不准两路），
 * 之后输出"相对基线的变化量"，两路零点天然对齐。
 * 两路都通过量程+稳定性校验才锁定零点，否则整窗丢弃重采；期间不显示数值。
 * 校验为什么不能省、阈值为什么取那个值：见 app_config.h 的 CAL_* 说明 */
static float   cal_off1 = 0.0f;               /* 通道1 零点（μS）；仅 cal_done 置位后有效 */
static float   cal_off2 = 0.0f;               /* 通道2 零点（μS） */
static uint16_t cal_z1 = 0, cal_z2 = 0;       /* 最近一个标定窗口的均值（adc 码），诊断用 */
static uint8_t cal_done = 0;                  /* 1 = 已取得合格零点 */
static uint8_t cal_fail = 0;                  /* 1 = 最近一个窗口被判不合格（屏上提示转红） */

/* 标定判据：均值须落在前端可用量程内，且窗口内足够稳定（挡掉电极接入等突发过程） */
static uint8_t cal_accept(uint16_t mean, uint16_t spread)
{
  return (mean >= CAL_ADC_MIN) && (mean <= CAL_ADC_MAX) && (spread <= CAL_SPREAD_MAX);
}

static void adc_cal_task(void)
{
  static uint32_t acc1 = 0, acc2 = 0;
  static uint16_t n = 0, seen = 0;
  static uint16_t lo1 = 0xFFFF, hi1 = 0, lo2 = 0xFFFF, hi2 = 0;
  uint16_t v1, v2;

  /* 必须按 adc_seq 取样：本函数每轮主循环都被调用（约 1kHz），
   * 若不看 seq 就会把同一个样本重复累加 50 次，"1s 均值"实际只是一瞬间的一个值 */
  if (cal_done || HAL_GetTick() < CAL_START_MS || adc_seq == seen)
  {
    return;
  }
  seen = adc_seq;

  v1 = adc_value1;
  v2 = adc_value2;
  acc1 += v1;  acc2 += v2;
  if (v1 < lo1) { lo1 = v1; }
  if (v1 > hi1) { hi1 = v1; }
  if (v2 < lo2) { lo2 = v2; }
  if (v2 > hi2) { hi2 = v2; }

  if (++n < CAL_SAMPLES)
  {
    return;
  }

  /* 窗口集齐：逐通道校验，两路同时合格才锁定零点。
   * 只要有一路不合格就整窗作废 —— "两路之差"要求两个零点取自同一时刻，
   * 一路用旧零点另一路用新零点是没意义的 */
  cal_z1 = (uint16_t)(acc1 / CAL_SAMPLES);
  cal_z2 = (uint16_t)(acc2 / CAL_SAMPLES);
  if (cal_accept(cal_z1, (uint16_t)(hi1 - lo1)) && cal_accept(cal_z2, (uint16_t)(hi2 - lo2)))
  {
    cal_off1 = adc_g_raw(cal_z1);
    cal_off2 = adc_g_raw(cal_z2);
    cal_done = 1;
  }
  else
  {
    cal_fail = 1;                 /* 只置标志：提示转红，让用户知道是信号不合格而非死机 */
  }

  acc1 = 0; acc2 = 0; n = 0;      /* 无论成败都开一个新的 1s 窗口 */
  lo1 = lo2 = 0xFFFF; hi1 = hi2 = 0;
}

/* 输出 = (当前电导 - 本通道零点) × 30。低于本通道零点（负电导）在此统一截零：
 * 屏上三行、差值、状态判定、波形、串口全部取自本函数，截零后各处必然自洽。
 * 若只在对字符串格式化时截零，就会出现"两路都显示 0、差值却显示 70"这种
 * 自相矛盾的画面（差值当时仍按未截零的负值相减） */
static float adc_to_g(uint16_t adc, float off)
{
  float g = (adc_g_raw(adc) - off) * 30.0f;
  return (g > 0.0f) ? g : 0.0f;   /* 顺带挡住 NaN：与 0 比较为假时归零 */
}

static float read_g1(void) { return adc_to_g(adc_value1, cal_off1); }
static float read_g2(void) { return adc_to_g(adc_value2, cal_off2); }

/* 串口1 上报（USART1 发、DMA1_Channel4 搬）：整帧一次交给 DMA，CPU 不逐字节搬运。
 * 帧缓冲静态分配，DMA 运行期间内容被硬件读取，故只在确认链路空闲后才重填。
 * 50Hz × 最长 12B 帧在 115200bps 下仅占约 5% 带宽，不会积压 */
static uint8_t uart_buf[32];

static void uart_send_pair(const char *g1_str, const char *g2_str)
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
  LCD_InitLayout();           /* 灰底 + 白色分隔线 + 三行固定标签 */
  /* 此处不预画数值：零点尚未标定，三行数值保持空白由状态行的 CAL 提示说明，
   * 免得把没有意义的 0.00 当成读数（零点标定合格后首次刷新即填入） */
  lcd_ready = 1;              /* 此后 Error_Handler 可以上屏报错 */
  MX_IWDG_Init();             /* 独立看门狗 ≈1s，主循环喂狗 */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint16_t last_seq = 0;         /* 已消费到的有效样本序号（adc_seq 回绕安全） */
  uint32_t last_lcd_update = 0;  /* 上次刷屏时刻 */

  while (1)
  {
	  IWDG->KR = 0xAAAA;   /* 喂狗 */

	  adc_stall_monitor();   /* 采集停摆监测（见函数说明） */

	  adc_cal_task();        /* 上电零点标定：取到合格零点前一直重采 */

	  if (adc_seq != last_seq)
	  {
		  last_seq = adc_seq;
		  /* 零点没标定出来之前不推波形、不发串口：此时 cal_off 还是 0，
		   * 波形画的、串口发的是"没减零点的原始电导"，而屏上数值是空白的 ——
		   * 上报与显示不一致，且标定一直不成功时会无限期发这种无意义数据。
		   * 与屏上保持一致：不确定就不输出 */
		  if (cal_done)
		  {
			  /* 双波形实时更新（滤波后的平滑值） */
			  LCD_WaveformPush(read_g1(), read_g2());
			  /* 串口1 DMA 上报：与有效样本同步（50Hz），格式 "<电导1>，<电导2>\n"，
			   * 数值经 LCD_ValStr 格式化，与屏上显示字面一致 */
			  {
				  char s1[8], s2[8];
				  LCD_ValStr(s1, sizeof s1, read_g1());
				  LCD_ValStr(s2, sizeof s2, read_g2());
				  uart_send_pair(s1, s2);
			  }
		  }
	  }

	  if (HAL_GetTick() - last_lcd_update >= 500)
	  {
		  last_lcd_update = HAL_GetTick();
		  if (!cal_done)
		  {
			  /* 标定中：状态行提示，避免把未减零点的数值当读数；
			   * 最近一轮均值不合格则转红，提示是信号不合规而非死机 */
			  LCD_DrawCalText(cal_fail);
		  }
		  else
		  {
			  /* 标定合格后的首次调用会覆盖掉 CAL 提示（状态切换必重绘） */
			  LCD_DrawSkinText(read_g1(), read_g2());
		  }
		  /* 告警标记：状态行右端红块 = 曾发生 ADC/DMA 错误或采集停摆 */
		  {	uint8_t alarm = (adc_err_cnt || adc_stall_cnt) ? 1 : 0;
			/* 每次刷新无条件重绘：LCD_DrawSkinText 在状态切换时会整行擦灰，
			 * 若只在状态变化时画红块，红块会被擦掉后永久消失 */
			LCD_Rect_Fill(SCREEN_W - 16, LAYOUT_STATUS_Y, 16, 16, alarm ? RED : GRAY);
		  }
#if ADC_DEBUG_SHOW_RAW
		  {	char dbg[48];
			/* R1/R2 = 当前原始码值，Z1/Z2 = 最近一个标定窗口的均值。
			 * Z 落在 CAL_ADC_MIN..CAL_ADC_MAX 之外、或两路差异过大，
			 * 说明该窗口被拒、仍在重采（屏上提示转红）。K = 被剔除的受扰块数。
			 * 宽度上限：4 个 %4u + K 取模到 4 位 = 38 字符 = 304px ≤ 320px，
			 * 不会写出行尾之外（K 不取模时最多 10 位，会溢出屏宽） */
			snprintf(dbg, sizeof dbg, "R1=%4u R2=%4u Z1=%4u Z2=%4u K=%4lu",
					(unsigned)adc_value1, (unsigned)adc_value2,
					(unsigned)cal_z1, (unsigned)cal_z2,
					(unsigned long)(adc_spike_cnt % 10000u));
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
 * 返回 HAL_OK 仅代表恢复序列执行成功；是否真正恢复由 adc_stall_monitor 按 adc_seq 继续判定。 */
static HAL_StatusTypeDef adc_stall_recover(void)
{
  HAL_StatusTypeDef status;

  adc_stall_cnt++;   /* 检测到一次停摆（无论恢复是否成功都留痕） */

  if (HAL_TIM_Base_Stop(&htim3) != HAL_OK) { return HAL_ERROR; }

  /* 三步必须依次成功才继续（后一步依赖前一步的硬件状态），任一失败都跳到最后
   * 统一重启 TIM3：失败路径也必须把触发源还回去，否则采集会一直停着 */
  status = HAL_ADC_Stop_DMA(&hadc1);
  if (status == HAL_OK)
  {
    /* 必须自己清 ADC 的错误状态位，HAL 全流程都不会清它：
     * DMA TE 时 ADC_DMAError 置 hadc->State |= HAL_ADC_STATE_ERROR_DMA
     * (stm32f1xx_hal_adc.c:2404)，而 ADC_DMAConvCplt 用这一位挡掉完成回调
     * (同文件 :2339)；F1 的 HAL_DMA_IRQHandler 在 TE 时把 hdma->State 置成
     * READY 而不是 BUSY (stm32f1xx_hal_dma.c:668)，于是 HAL_ADC_Stop_DMA 里
     * "if (DMA_Handle->State == BUSY)" 不成立，既不 abort 通道也不清状态；
     * HAL_ADC_Start_DMA 只清 ErrorCode 字段和 READY/REG_EOC/REG_OVR/REG_EOSMP，
     * 同样不含这一位。留着它的后果是完全静默的：半满回调没有这个门，
     * adc_seq 仍以 25Hz 增长（额定 50Hz），停摆监测按"seq 是否增长"判定，
     * 永远发现不了 —— 标定窗口、滤波窗口、波形时基全部默默翻倍 */
    CLEAR_BIT(hadc1.State, HAL_ADC_STATE_ERROR_INTERNAL | HAL_ADC_STATE_ERROR_DMA);
    ADC_CLEAR_ERRORCODE(&hadc1);
    status = HAL_ADCEx_Calibration_Start(&hadc1);
  }
  if (status == HAL_OK) { status = HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buf, ADC_DMA_BUF_LEN); }
  if (HAL_TIM_Base_Start(&htim3) != HAL_OK) { status = HAL_ERROR; }

  return status;
}

/* 采集停摆/劣化监测：adc_seq 额定 50Hz 增长（DMA 半区满各 +1）。
 * 两个判据都要有，缺一不可：
 *   ① seq 在 ADC_STALL_RECOVER_MS 内完全不增长 —— 链路死透（通道停转、ADC 停转）
 *   ② 统计窗口内样本数不足 —— 链路活着但节奏不对。②不是多余的：DMA TE 后若
 *      ADC 的 ERROR_DMA 状态位没清，只有"全满"回调失效，seq 仍以 25Hz 增长，
 *      判据①永远发现不了，表现为标定/滤波窗口与波形时基全部默默翻倍
 * 命中任一判据就地重启 ADC+DMA；短窗口内连续失败超限则整机复位。
 * 注意恢复后必须把两个窗口都重新起算：否则刚发生的停摆还留在统计窗口里，
 * 会让判据②紧接着再报一次，一次停摆被记成两次、提前触发复位 */
static void adc_stall_monitor(void)
{
  static uint16_t seen_seq = 0, rate_mark = 0;
  static uint32_t seen_tick = 0, last_stall_tick = 0, rate_tick = 0;
  static uint8_t burst = 0, rate_primed = 0;
  uint32_t now = HAL_GetTick();
  uint8_t low = 0;

  if (adc_seq != seen_seq) { seen_seq = adc_seq; seen_tick = now; }

  if (!rate_primed)   /* 首窗只做基准（此时可能还在初始化，样本数不足） */
  {
    rate_primed = 1;
    rate_tick = now;
    rate_mark = adc_seq;
  }
  else if (now - rate_tick >= ADC_STALL_RATE_WIN_MS)
  {
    low = ((uint16_t)(adc_seq - rate_mark) < ADC_STALL_MIN_SAMPLES) ? 1 : 0;  /* 回绕安全 */
    rate_tick = now;
    rate_mark = adc_seq;
  }

  if ((now - seen_tick < ADC_STALL_RECOVER_MS) && !low) { return; }

  if (adc_stall_recover() != HAL_OK) { NVIC_SystemReset(); }
  burst = (now - last_stall_tick <= ADC_STALL_BURST_MS) ? (uint8_t)(burst + 1) : 1;
  last_stall_tick = seen_tick = rate_tick = now;   /* 两个窗口都重新起算 */
  rate_mark = adc_seq;
  if (burst >= ADC_STALL_RESET_LIMIT) { NVIC_SystemReset(); }
}

/* ADC/DMA 错误计数。注意：DMA 传输错误(TE)发生时通道已被硬件停转、
 * HAL 已关闭全部 DMA 中断（HAL_DMA_IRQHandler），采集随之停止——
 * 本回调只负责计数留痕，实际恢复由 adc_stall_monitor() 统一执行 */
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1) { adc_err_cnt++; }
}

/* 两个半区回调：前 10 次触发（下标 0..19）落在前半区，后 10 次落在后半区，
 * 都交给 adc_process_half 抽取（见其说明） */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc)
{
  if (hadc->Instance == ADC1) { adc_process_half(&adc_dma_buf[0]); }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
  if (hadc->Instance == ADC1) { adc_process_half(&adc_dma_buf[2 * ADC_BLOCK_TRIG]); }
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
    LCD_Rect_Fill(0, 0, SCREEN_W, SCREEN_H, RED);
  }
  else
  {
    /* 屏还没起来（典型：SystemClock_Config 里 HSE 起振失败）——
     * 此时看门狗尚未启动，若只是死等就彻底没救了（屏亮着但全黑、不重启）。
     * 先把自己交给看门狗：万一只是上电瞬态，约 1s 后能自动重来一次 */
    MX_IWDG_Init();
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
