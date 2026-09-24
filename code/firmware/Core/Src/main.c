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
/* 应用实现按功能拆在 Core/Src/BSP/，这里只 include 各模块的对外接口 */
#include "bsp_adc_acquire.h"
#include "bsp_adc_cal.h"
#include "bsp_uart_report.h"
#include "bsp_anes.h"
#include "bsp_iwdg.h"
#include "bsp_fault.h"
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
volatile uint8_t  lcd_ready = 0;     /* LCD 可用标志：Error_Handler 据此决定能否上屏报错 */
volatile uint32_t error_cause = 0;   /* Error_Handler 反推出的失败阶段位图（见其实现） */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* 本文件只保留"编排"：初始化顺序、主循环节拍、界面刷新与错误处理。
 * 手写实现按功能拆在 Core/Src/BSP/（对外接口见上面的 Includes）：
 *   bsp_adc_acquire.c 采集链路：DMA 缓冲、块内抽取、滑动平均、停摆监测与恢复
 *   bsp_adc_cal.c     上电零点标定 + 电导换算（零点的唯一来源）
 *   bsp_uart_report.c 串口1 上报：帧组装、DMA 发送、停摆恢复
 *   bsp_anes.c        麻醉状态判定（相对差 + 迟滞）
 *   bsp_iwdg.c        独立看门狗（启动与喂狗）
 *   bsp_fault.c       故障/劣化标记的时间窗（告警红块的判据来源） */
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
  /* 启动采集链路：上电校准 ADC（F1 必要，减小偏移温漂）后以 DMA 循环模式启动 ——
   * TIM3 500Hz 触发双通道扫描 → DMA 搬入循环缓冲 → 半区满(20ms)在中断里抽取+滤波
   * → 主循环按 adc_seq 消费（见 bsp_adc_acquire.c） */
  adc_acquire_start();
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);   /* PA1 = 背光：上电点亮 */
  /* 背光点亮后的稳定等待：这 100ms 没有手册依据，是"等面板上电稳定"的经验判断
   * （实机上这版时序可用）。没有实物对比时不要删掉或改小 —— 后面的初始化都以此为前提 */
  HAL_Delay(100);
  LCD_Init();
  LCD_InitLayout();           /* 灰底 + 白色分隔线 + 三行固定标签 */
  /* 此处不预画数值：零点尚未标定，三行数值保持空白由状态行的 CAL 提示说明，
   * 免得把没有意义的 0.00 当成读数（零点标定合格后首次刷新即填入） */
  lcd_ready = 1;              /* 此后 Error_Handler 可以上屏报错 */
  bsp_iwdg_init();            /* 独立看门狗 ≈1s，主循环喂狗 */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint16_t last_seq = 0;         /* 已消费到的有效样本序号（adc_seq 回绕安全） */
  uint32_t last_lcd_update = 0;  /* 上次刷屏时刻 */

  while (1)
  {
	  bsp_iwdg_feed();       /* 喂狗 */

	  adc_stall_monitor();   /* 采集链路停摆监测（见其头文件说明） */
	  uart_lifetime_monitor(); /* 上报链路停摆监测：IWDG 管不到它（狗在主循环里被喂） */

	  adc_cal_task();        /* 上电零点标定：取到合格零点前一直重采 */

	  if (adc_seq != last_seq)
	  {
		  /* 一次消费跨过多个样本 = 主循环被拖慢（典型：整屏列重写叠加 500ms 文本刷新），
		   * 中间样本被静默丢弃：波形横轴按"每次 push 一列"推进，时基会悄悄压缩。
		   * 只记时间戳交给告警块 —— 补不回来，也不该让波形假装没丢。
		   * 判据只在 cal_done 之后生效：采集链路在 LCD 初始化之前就启动了，第一次进
		   * 循环时 adc_seq 已经十几，这段"消费端还没上班"的差额不是主循环被拖慢；
		   * 下面的 last_seq = adc_seq 会在首次进本分支时就把序号对齐，其后的差额才算。
		   * 标定期间也不需要它：那时不推波形、不显示数值，没有任何东西被消费 */
		  if (cal_done && (uint16_t)(adc_seq - last_seq) > 1u) { adc_drop_ms = HAL_GetTick(); }
		  last_seq = adc_seq;
		  /* 零点没标定出来之前不推波形、不发串口：此时 cal_off 还是 0，
		   * 波形画的、串口发的是"没减零点的原始电导"，而屏上数值是空白的 ——
		   * 上报与显示不一致，且标定一直不成功时会无限期发这种无意义数据。
		   * 与屏上保持一致：不确定就不输出 */
		  if (cal_done)
		  {
			  float g1, g2;

			  /* 两路电导取自同一次快照：波形与串口共用同一份，两处字面必然一致
			   * （快照为什么必要见 adc_read_g_pair 的说明） */
			  adc_read_g_pair(&g1, &g2);

			  LCD_WaveformPush(g1, g2);   /* 双波形实时更新（滤波后的平滑值） */
			  /* 串口1 DMA 上报：与有效样本同步（50Hz），格式 "<电导1>，<电导2>\n"，
			   * 数值经 LCD_ValStr 格式化，与屏上显示字面一致 */
			  uart_report_pair(g1, g2);
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
			  /* 判定与显示用同一份数值：两路各读一次进局部量，免得"状态按 A 判、
			   * 数值显示 B"；anes_update 内部持有迟滞记忆，返回值就是要显示的状态。
			   * 标定合格后的首次调用会覆盖掉 CAL 提示（见 status_cal） */
			  float g1 = read_g1(), g2 = read_g2();
			  LCD_DrawSkinText(g1, g2, anes_update(g1, g2));
		  }
		  /* 告警标记：状态行右端红块 = 时间窗内发生过 ADC/DMA 错误、采集停摆、上报停摆、
		   * 抗扰块剔除（数据质量）或消费端丢样本（时基被压缩）。这些计数器都只增不减，
		   * 不能直接拿来判据 —— 那样一次上电瞬态或一次已成功恢复的故障会让红块永久保持
		   * 红色，"已恢复的过去"与"正在发生的现在"在屏上无法区分，红块就失去了告警意义。
		   * 改为只看窗口内是否发生过（mark_alive）：故障结束 ALARM_WINDOW_MS 后自动熄灭，
		   * 故障持续则每次恢复都刷新时间戳、持续点亮。
		   * 后两项不是链路故障而是数据质量：受扰块被剔除时显示值保持原值（曲线看着"很稳"，
		   * 滤波窗也不推进），丢样本时波形时基悄悄压缩 —— 不接进来就完全看不见。
		   * 整块受 ALARM_SHOW 总开关约束（app_config.h）：置 0 时不计算也不画 */
		  if (ALARM_SHOW)
		  {	uint8_t alarm = (mark_alive(&adc_err_ms) ||
		                     mark_alive(&adc_stall_ms) ||
		                     mark_alive(&uart_stall_ms) ||
		                     mark_alive(&adc_spike_ms) ||
		                     mark_alive(&adc_drop_ms)) ? 1 : 0;
			/* 每次刷新无条件重绘：LCD_DrawSkinText 在状态切换时会整行擦灰，
			 * 若只在状态变化时画红块，红块会被擦掉后永久消失 */
			LCD_Rect_Fill(SCREEN_W - 16, LAYOUT_STATUS_Y, 16, 16, alarm ? RED : GRAY);
		  }
		  /* 诊断行：ADC_DEBUG_SHOW_RAW=1 时恒显；否则只在"最近有受扰块被剔除 / 丢了样本"
		   * 时自动上屏 —— 现场核对 ADC_BLOCK_SPREAD_* 阈值只需看这行的 K 与 R/Z 是否随
		   * 干扰增长，不必再改宏重编译固件；无异常时清空该行（整行灰底重绘约 1ms，可忽略）。
		   * R1/R2 = 当前原始码值，Z1/Z2 = 最近一个标定窗口的均值（0 = 未标定），
		   * K = 被剔除的受扰块数。宽度上限：4 个 %4u + K 取模到 4 位 = 38 字符 = 304px
		   * ≤ 320px，不会写出行尾之外（K 不取模时最多 10 位，会溢出屏宽）。
		   * 整段受 ALARM_SHOW 总开关约束：置 0 时既不画也不清（该行保持灰底） */
		  if (ALARM_SHOW && (ADC_DEBUG_SHOW_RAW || mark_alive(&adc_spike_ms) || mark_alive(&adc_drop_ms)))
		  {	char dbg[48];
			snprintf(dbg, sizeof dbg, "R1=%4u R2=%4u Z1=%4u Z2=%4u K=%4lu",
					(unsigned)adc_value1, (unsigned)adc_value2,
					(unsigned)cal_z1, (unsigned)cal_z2,
					(unsigned long)(adc_spike_cnt % 10000u));
			LCD_DebugLine(dbg);
		  }
		  else if (ALARM_SHOW)
		  {
			LCD_DebugLine("");   /* 清掉上一次的残留 */
		  }
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
/* 本区原有实现已按功能拆到 Core/Src/BSP/：
 *   bsp_iwdg.c         bsp_iwdg_init（原 MX_IWDG_Init）
 *   bsp_adc_acquire.c  adc_stall_recover / adc_stall_monitor /
 *                      HAL_ADC_ErrorCallback / HAL_ADC_ConvHalfCpltCallback /
 *                      HAL_ADC_ConvCpltCallback
 *   bsp_uart_report.c  HAL_UART_ErrorCallback */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();

  /* 失败原因位图：不在 CubeMX 生成的初始化代码里插桩（那些行重新生成就会被改掉），
   * 而是直接读硬件事实 —— 振荡器就绪位 + 各外设时钟使能位。位含义：
   *   0x01 HSE 就绪  0x02 PLL 就绪  0x04 FSMC  0x08 ADC1  0x10 TIM3  0x20 USART1
   * 全 0 → 卡在时钟配置；逐位补齐 → 卡在后面对应的那个外设初始化上 */
  error_cause = (READ_BIT(RCC->CR, RCC_CR_HSERDY) ? 0x01u : 0u)
              | (READ_BIT(RCC->CR, RCC_CR_PLLRDY) ? 0x02u : 0u)
              | (READ_BIT(RCC->AHBENR, RCC_AHBENR_FSMCEN) ? 0x04u : 0u)
              | (READ_BIT(RCC->APB2ENR, RCC_APB2ENR_ADC1EN) ? 0x08u : 0u)
              | (READ_BIT(RCC->APB1ENR, RCC_APB1ENR_TIM3EN) ? 0x10u : 0u)
              | (READ_BIT(RCC->APB2ENR, RCC_APB2ENR_USART1EN) ? 0x20u : 0u);

  /* LCD 已初始化时整屏红底 + 原因码报错（不用字库，避免复制 4KB 字模表）。
   * 字符格内的灰底来自 LCD_Ascii8x16 的实现（非亮点写 GRAY），红屏上会看到灰字格 ——
   * 诊断优先，不为错误屏单独做反色绘制。若 IWDG 已启动，约 1s 后复位重启 */
  if (lcd_ready)
  {
    char msg[16];

    LCD_Rect_Fill(0, 0, SCREEN_W, SCREEN_H, RED);
    snprintf(msg, sizeof msg, "ERR=%02lX", (unsigned long)error_cause);
    LCD_Ascii8x16(8, 8, msg, WHITE);
  }
  else
  {
    /* 屏还没起来（典型：SystemClock_Config 里 HSE 起振失败）——
     * 此时看门狗尚未启动，若只是死等就彻底没救了（屏亮着但全黑、不重启）。
     * 先把自己交给看门狗：万一只是上电瞬态，约 1s 后能自动重来一次。
     * 这条路径上 error_cause 上不了屏，但仍留在 RAM 里，接调试器可读 */
    bsp_iwdg_init();
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