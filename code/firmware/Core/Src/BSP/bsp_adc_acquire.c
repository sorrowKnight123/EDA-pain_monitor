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

/* 最近一个有效样本的质量：1 = 两路都通过块内判据，0 = 至少一路被剔除。
 * 被剔除时 adc_value 保持上一块的旧值（不冻结滤波、不丢时基），
 * 但如果不说"这一块是旧的"，消费端就会把旧值当成新样本推进波形和记入 CSV，
 * 事后完全无法区分"信号真的没变"和"这块被剔除了" */
volatile uint8_t adc_last_valid = 1;

/* 有效样本滤波：4 阶 Butterworth 低通 3Hz，2 个双二阶级联（DF2T 结构）。
 * 输入/输出都是 ADC 码（0..4095），量化到 1 码。
 *
 * 系数（fs=50Hz, fc=3Hz，双线性变换 + 频率预畸变）：
 *   K = tan(pi*fc/fs) = 0.190760，极点 Q = 0.541196 / 1.306563
 * 独立复算核对过：与 Q=1/(2cos(pi/8))、1/(2cos(3pi/8)) 的标准 4 阶 Butterworth
 * 逐项相差 < 4e-10（float32 精度内），极点模长 0.702 / 0.868（在单位圆内）。
 * 幅频（含前级 20ms 块窗）：3Hz -3.01dB、5Hz -18.6dB、10Hz -46.5dB。
 * 代价：阶跃过冲 11.2%、50% 上升时间 160ms、相位非线性。 */
#define BQ1_B0   0.026200795f
#define BQ1_B1   0.052401590f
#define BQ1_B2   0.026200795f
#define BQ1_A1  (-1.387619708f)
#define BQ1_A2   0.492422887f
#define BQ2_B0   0.030776160f
#define BQ2_B1   0.061552321f
#define BQ2_B2   0.030776160f
#define BQ2_A1  (-1.629935531f)
#define BQ2_A2   0.753040172f

typedef struct {
  float   s1, s2;   /* 第 1 节状态（DF2T） */
  float   t1, t2;   /* 第 2 节状态 */
  uint8_t primed;   /* 0 = 尚未收到首个样本 */
} GFilter;

/* 一节双二次（Direct Form II Transposed）：每样本 5 次乘加 */
inline static float biquad(float *s1, float *s2, float x,
                           float b0, float b1, float b2, float a1, float a2)
{
  float y = b0 * x + *s1;
  *s1 = b1 * x - a1 * y + *s2;
  *s2 = b2 * x - a2 * y;
  return y;
}

/* 首样本预置为稳态，避免从 0 起步产生一个虚假的阶跃（滤波器要 160ms 才追上）。
 * 稳态条件：输入恒为 x 时输出恒为 x，据此反解两节的状态值（见 DF2T 递推式）。
 * 实测：恒定输入下 300 个样本输出偏差 0 码 */
static void gfilter_prime(GFilter *f, float x)
{
  f->primed = 1;
  f->s1 = x * (1.0f - BQ1_B0);
  f->s2 = x * (1.0f - BQ1_B0 - BQ1_B1 + BQ1_A1);
  f->t1 = x * (1.0f - BQ2_B0);
  f->t2 = x * (1.0f - BQ2_B0 - BQ2_B1 + BQ2_A1);
}

static uint16_t gfilter(GFilter *f, uint16_t raw)
{
  float x = (float)raw;
  float y;

  if (!f->primed) { gfilter_prime(f, x); return raw; }

  y = biquad(&f->s1, &f->s2, x, BQ1_B0, BQ1_B1, BQ1_B2, BQ1_A1, BQ1_A2);
  y = biquad(&f->t1, &f->t2, y, BQ2_B0, BQ2_B1, BQ2_B2, BQ2_A1, BQ2_A2);

  /* 输出回 ADC 码域：越界夹紧（IIR 在极端输入下可能冲出 0..4095），四舍五入 */
  if (!(y > 0.0f)) { y = 0.0f; }              /* 同时挡住 NaN */
  if (y > 4095.0f) { y = 4095.0f; }
  return (uint16_t)(y + 0.5f);
}

static GFilter gf1 = {0};   /* 电导1 滤波状态 */
static GFilter gf2 = {0};   /* 电导2 滤波状态 */

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
 * 两通道分别判据：单侧电极受扰只冻结该侧，不影响另一侧。
 * 任一侧被剔除都把整样本标为无效 —— 两路要成对使用（差值判据、波形同列），
 * 一侧是旧值时这一列的"两路关系"已经不成立 */
static void adc_process_half(const volatile uint16_t *p)
{
  uint16_t v;
  uint8_t  valid = 1;

  if (adc_block_reduce(p, &v))       adc_value1 = gfilter(&gf1, v);
  else                               { adc_spike_ms = HAL_GetTick(); valid = 0; }

  if (adc_block_reduce(p + 1, &v))   adc_value2 = gfilter(&gf2, v);
  else                               { adc_spike_ms = HAL_GetTick(); valid = 0; }

  adc_last_valid = valid;
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