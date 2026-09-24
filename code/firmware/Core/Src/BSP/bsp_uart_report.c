/**
  ******************************************************************************
  * @file    bsp_uart_report.c
  * @brief   串口1 上报链路：帧组装、DMA 发送、停摆恢复与生命期监测
  ******************************************************************************
  */
#include "bsp_uart_report.h"
#include <stdio.h>
#include "app_config.h"
#include "usart.h"
#include "ssd1289_fsmc.h"   /* LCD_ValStr：屏上与串口共用同一个格式化函数 */
#include "bsp_fault.h"

/* 上报链路的 DMA 句柄：定义在 usart.c，本文件恢复链路时要直接复位它的状态
 * （HAL 在 DMA TE 之后留下"状态说空闲、硬件可能仍在跑"的通道，见 uart_stall_recover）
 * 同 stm32f1xx_it.c 的声明方式，保持与 CubeMX 生成代码一致的可见性 */
extern DMA_HandleTypeDef hdma_usart1_tx;

/* 串口1 上报（USART1 发、DMA1_Channel4 搬）：整帧一次交给 DMA，CPU 不逐字节搬运。
 * 帧缓冲静态分配，DMA 运行期间内容被硬件读取，故只在确认链路空闲后才重填。
 * 50Hz × 最长 12B 帧在 115200bps 下仅占约 5% 带宽，不会积压 */
static uint8_t uart_buf[32];
static uint32_t uart_sent_ms = 0;    /* 最后一次"成功发起发送"的时刻，上报链路的心跳 */
static uint16_t uart_last_len = 0;   /* 上一帧长度：停摆恢复后据此把最后一帧补发出去 */

volatile uint32_t uart_err_cnt = 0;  /* 串口1 DMA 传输错误（UART_DMAError 路径）次数。
                                      * 与 adc_err_cnt 一样只给调试器看：屏上不读它
                                      * （告警走 *_ms 时间戳，错误率只能靠计数器留存） */

void uart_report_pair(float g1, float g2)
{
  char s1[8], s2[8];
  int n;

  /* 数值直接取自 LCD_ValStr 的输出，与屏上三行数值逐字符相同，
   * 不再自行做定点拆分 */
  LCD_ValStr(s1, sizeof s1, g1);
  LCD_ValStr(s2, sizeof s2, g2);

  /* 帧格式 "<电导1><分隔符><电导2>\n" */
  n = snprintf((char *)uart_buf, sizeof uart_buf,
               "%s" UART_FRAME_SEP "%s\n", s1, s2);
  /* gState 是链路忙闲的唯一判据：DMA 发完由 HAL 置回 READY。
   * 忙时直接丢弃本帧——主循环还要喂狗/推波形/刷屏，不能在此等待。
   * n >= sizeof 说明被截断（snprintf 返回"本该写入的长度"），不得交给 DMA。
   * 返回值不能丢：HAL_UART_Transmit_DMA 只在"状态检查通过"时才返回 HAL_OK，
   * 只有 HAL_OK 才代表链路真的被占用、心跳才有效（见 uart_lifetime_monitor）。
   * 检查与启动必须是一个原子步：gState 会被 USART1 的 TC 中断（优先级 1，可抢占
   * 线程级的主循环）改写。若在两者之间正好"上一帧刚发完"，HAL_UART_Transmit_DMA
   * 会在 UART 还在吐最后一字节时就把新帧塞进去，丢掉（或混进）那个字节。
   * 关中断的窗口只有几条指令，且此时没有"正在跑"的 DMA（忙检查已排除），
   * 不会与 ADC 的 DMA 完成回调打架 */
  if (n > 0 && n < (int)sizeof uart_buf)
  {
    /* huart1.gState 是 HAL 的非 volatile 字段、却由 USART1 的 TC 中断改写：编译器可以把
     * 它的读取缓存进寄存器（内联进主循环后尤其危险），那会让忙检查永远看到旧值 →
     * 永久 BUSY_TX。经指向 volatile 的指针读，强制每次真的去取一次 */
    volatile HAL_UART_StateTypeDef *gstate = &huart1.gState;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (*gstate == HAL_UART_STATE_READY)
    {
      if (HAL_UART_Transmit_DMA(&huart1, uart_buf, (uint16_t)n) == HAL_OK)
      {
        uart_last_len = (uint16_t)n;   /* 先记长度再打时间戳：监测只看时间戳，
                                        * 两者次序不影响判据，但能保证恢复时长度已就绪 */
        uart_sent_ms = HAL_GetTick();
      }
    }
    __set_PRIMASK(primask);
  }
}

/* 串口1 上报链路停摆的恢复：中止发送、把 gState 与 DMA 通道都强行拉回 READY，
 * 再把最后一帧补发出去（发得出去才算真恢复，故用返回值直接驱动心跳）。
 * 为什么必须存在：gState 从 BUSY_TX 回到 READY 只发生在 USART1 的 TC 中断里，
 * 中间任何一步没走到就会永久卡死，而忙检查会让此后每一帧都静默丢弃
 * （详见 app_config.h 的 UART_STALL_RECOVER_MS 说明）。
 * HAL_UART_AbortTransmit 是"阻塞式中止"：内部 HAL_DMA_Abort 只做
 * 关中断/关通道/清标志/置 READY 四件不等待硬件的事，所以不会长时间占用主循环。
 * 清 ErrorCode 与 TCIE 是补 HAL 的边界，且刻意不依赖 HAL 内部实现：本版 AbortTransmit
 * 在函数首行就已无条件清 TXEIE|TCIE（stm32f1xx_hal_uart.c:1951），所以下面那行当前是
 * 冗余的；留着它是为了"换 HAL 版本后该函数改成条件清位"时，仍能挡住 TC 中断再改一次 gState */
static void uart_stall_recover(void)
{
  uint32_t primask, now = HAL_GetTick();
  HAL_StatusTypeDef tx;

  uart_stall_ms = now;   /* 在关中断前取时刻：临界区内 SysTick 被挡住，
                          * 那时的 HAL_GetTick() 会返回冻结的旧值 */

  /* 整段复位必须在临界区里做完：DMA 完成回调（优先级 0，最高）与 USART1 的 TC
   * 中断都会改写 gState / hdma 状态，若在复位中途插进来，就会留下一个
   * "我们以为复位好了、实际被改回 BUSY" 的链路 —— 那正是本函数要消除的东西。
   * 窗口只有几十条指令，且不等待任何硬件，不影响 ADC 采集 */
  primask = __get_PRIMASK();
  __disable_irq();

  (void)HAL_UART_AbortTransmit(&huart1);

  /* AbortTransmit 对"DMA 通道状态异常"无解：F1 的 DMA TE 会把 hdma->State 置成 READY
   * （stm32f1xx_hal_dma.c:668），HAL_DMA_Abort 见 State != BUSY 就直接返回 HAL_ERROR
   * 而不去关通道、不清标志，留下一个"状态说空闲、硬件可能仍在跑"的通道。
   * 状态机已经不可信，这里直接按硬件事实兜底（寄存器操作，不依赖任何 HAL 状态） */
  (void)HAL_DMA_Abort(&hdma_usart1_tx);
  CLEAR_BIT(DMA1_Channel4->CCR, DMA_CCR_EN);
  DMA1->IFCR = DMA_IFCR_CGIF4;
  hdma_usart1_tx.State = HAL_DMA_STATE_READY;

  huart1.gState = HAL_UART_STATE_READY;
  huart1.ErrorCode = HAL_UART_ERROR_NONE;
  CLEAR_BIT(huart1.Instance->CR1, USART_CR1_TCIE);

  /* 补发最后一帧：不重发的话，恢复后要等下一个 20ms 采样点才知道有没有真修好，
   * 而这里发一次能立刻把心跳刷新（也顺带保证链路真的能出字节）。
   * gState 刚被置回 READY，这一步在同一次临界区内完成，不会有别的上下文插进来 */
  tx = (uart_last_len > 0u && uart_last_len < (uint16_t)sizeof uart_buf)
       ? HAL_UART_Transmit_DMA(&huart1, uart_buf, uart_last_len)
       : HAL_ERROR;

  __set_PRIMASK(primask);

  if (tx == HAL_OK) { uart_sent_ms = HAL_GetTick(); }
  /* 补发失败（如 DMA 通道无法启动）→ 心跳保持不动，下一轮主循环会再试。
   * 若一直失败，uart_stall_ms 停留在故障时刻，屏上红块持续点亮 —— 如实反映"还在坏" */
}

/* 上报链路生命期监测：只要"距上次成功发起发送"超过 UART_STALL_RECOVER_MS 就恢复一次。
 * 与采集链路同一个形状（判据单一但兜底到位）：链路正常时 50Hz 刷新心跳，判据永不成立；
 * 一旦停摆，最多 UART_STALL_RECOVER_MS 后自动重启链路，无需人工干预、也不靠 IWDG
 * （狗在主循环里被喂，采集与上报都停着它也不会咬）。 */
void uart_lifetime_monitor(void)
{
  /* 0 有两种含义：还没发过第一帧（标定未完成，不该判停摆），或时间戳恰好回绕到 0
   * （上电 49.7 天后）。两者都按"不判停摆"处理是安全侧：下一帧发送就会重新打上
   * 非零时间戳，监测最多空转一个采样周期（20ms） */
  if (uart_sent_ms == 0u) { return; }
  if (HAL_GetTick() - uart_sent_ms < UART_STALL_RECOVER_MS) { return; }
  uart_stall_recover();
}

/* 串口1 DMA 传输错误：HAL 已经在 UART_DMAError 里把 gState 收尾回 READY
 * （stm32f1xx_hal_uart.c:3150-3154，条件是 gState==BUSY_TX 且 CR3.DMAT 置位），
 * 所以这里只留痕。留痕是必要的：它驱动的告警块能指出"是串口坏了"而不是"采集坏了"，
 * 而 uart_lifetime_monitor 负责在 HAL 没走到那条路径时兜底 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1) { uart_err_cnt++; }
}