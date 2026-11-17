/**
  ******************************************************************************
  * @file    bsp_adc_acquire.c
  * @brief   ADC 采集链路：TIM3 触发 + DMA 循环搬运 + 半区抽取滤波 → 有效样本(50Hz)，
  *          以及采集停摆/劣化的监测与分级恢复
  ******************************************************************************
  */
#include "bsp_adc_acquire.h"
#include "app_config.h"
#include "adc.h"
#include "tim.h"
#include "bsp_fault.h"

/* 超采样：TIM3 500Hz 触发，参数见 app_config.h（ADC_BLOCK_TRIG）；
 * DMA 半区满在中断里抽取为 1 个有效样本，有效输出率 50Hz */
#define ADC_DMA_BUF_LEN (4 * ADC_BLOCK_TRIG)   /* 2 半区 × 2 通道 × ADC_BLOCK_TRIG 次触发 */
static volatile uint16_t adc_dma_buf[ADC_DMA_BUF_LEN];

volatile uint16_t adc_value1;   /* 滤波后的电导1有效样本（PC1） */
volatile uint16_t adc_value2;   /* 滤波后的电导2有效样本（PC0） */
volatile uint16_t adc_seq = 0;  /* 有效样本计数，主循环据此消费（回绕安全） */

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
  else                               { adc_spike_ms = HAL_GetTick(); }

  if (adc_block_reduce(p + 1, &v))   adc_value2 = gfilter(&gf2, v);
  else                               { adc_spike_ms = HAL_GetTick(); }

  adc_seq++;   /* 心跳照常推进：停摆监测只关心采集链路是否活着，与数据质量无关 */
}

/* 启动采集链路：先校准 ADC（F1 必要，减小偏移温漂），再以 DMA 循环模式启动，
 * 最后打开触发源。与 adc_stall_recover 是同一串动作的两个入口 ——
 * 一个从上电走到运行，一个从故障走回运行（校准与 Start_DMA 不会清 EXTTRIG，
 * 所以启动顺序必须是"先停止触发源再启动"的反面：这里上电时触发源本来就没开） */
void adc_acquire_start(void)
{
  HAL_ADCEx_Calibration_Start(&hadc1);
  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buf, ADC_DMA_BUF_LEN);
  HAL_TIM_Base_Start(&htim3);
}

/* 采集停摆恢复：停止 TIM3 后重启 ADC+DMA，避免校准/重启窗口内
 * 外部触发干扰 ADC（HAL 的校准和 Start_DMA 不会清除 EXTTRIG）。
 * 返回 HAL_OK 仅代表恢复序列执行成功；是否真正恢复由 adc_stall_monitor 按 adc_seq 继续判定。 */
static HAL_StatusTypeDef adc_stall_recover(void)
{
  HAL_StatusTypeDef status;

  adc_stall_ms = HAL_GetTick();

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
void adc_stall_monitor(void)
{
  static uint16_t seen_seq = 0, rate_mark = 0;
  static uint32_t seen_tick = 0, last_stall_tick = 0, rate_tick = 0;
  static uint8_t burst = 0, rate_primed = 0, rate_low = 0;
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
    uint8_t low_now = ((uint16_t)(adc_seq - rate_mark) < ADC_STALL_MIN_SAMPLES) ? 1 : 0;
    /* 判据②必须"连续两个窗口都偏低"才算数，且用 latch（rate_low）跨恢复保持：
     *   · 单窗偶发偏低（中断被长 LCD 刷新挤占）不该误判 → 要连续两窗
     *   · 但 latch 不能像两个时间窗那样在恢复后清零。半速劣化（25Hz）是持续状态，
     *     恢复后每 1s 必然再次报出；若每次恢复都把"连续失败"计数重置，
     *     burst 永远攒不到 ADC_STALL_RESET_LIMIT，整机复位兜底形同虚设，
     *     设备就以一半采样率无限期跑下去（标定/滤波窗口与波形时基全部静默翻倍）。
     *     用 latch 后 burst 才能正常累加：约每 2s 一次 → 6s 内达上限复位。
     * 恢复后判据若不再成立（速率回到额定），下一个窗口会把 latch 清掉 */
    low = (uint8_t)(low_now & rate_low);
    rate_low = low_now;
    rate_tick = now;
    rate_mark = adc_seq;
  }

  if ((now - seen_tick < ADC_STALL_RECOVER_MS) && !low) { return; }

  if (adc_stall_recover() != HAL_OK) { NVIC_SystemReset(); }
  burst = (now - last_stall_tick <= ADC_STALL_BURST_MS) ? (uint8_t)(burst + 1) : 1;
  last_stall_tick = seen_tick = rate_tick = now;   /* 两个时间窗都重新起算（rate_low 不清） */
  rate_mark = adc_seq;
  if (burst >= ADC_STALL_RESET_LIMIT) { NVIC_SystemReset(); }
}

/* ADC/DMA 错误计数。注意：DMA 传输错误(TE)发生时通道已被硬件停转、
 * HAL 已关闭全部 DMA 中断（HAL_DMA_IRQHandler），采集随之停止——
 * 本回调只负责记录时刻留痕，实际恢复由 adc_stall_monitor() 统一执行 */
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1) { adc_err_ms = HAL_GetTick(); }
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