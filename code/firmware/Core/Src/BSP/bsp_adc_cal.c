/**
  ******************************************************************************
  * @file    bsp_adc_cal.c
  * @brief   上电零点标定（每通道独立，零点的唯一来源）+ 电导换算
  ******************************************************************************
  */
#include "bsp_adc_cal.h"
#include "app_config.h"
#include "bsp_adc_acquire.h"

static float   cal_off1 = 0.0f;               /* 通道1 零点（μS）；仅 cal_done 置位后有效 */
static float   cal_off2 = 0.0f;               /* 通道2 零点（μS） */
volatile uint16_t cal_z1 = 0, cal_z2 = 0;     /* 最近一个标定窗口的均值（adc 码），诊断用 */
volatile uint8_t cal_done = 0;                /* 1 = 已取得合格零点 */
volatile uint8_t cal_fail = 0;                /* 1 = 最近一个窗口被判不合格（屏上提示转红） */

/* 电导换算：G = 5*(4095-adc)/adc（μS），即前端 0..5μS 量程的传输函数。
 * 公式只在此处出现：标定零点换算、屏上显示、串口上报都经它取值 */
static float adc_g_raw(uint16_t adc)
{
  float a = (float)adc;
  return (a > 0.0f) ? (5.0f * (4095.0f - a) / a) : 0.0f;
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

void adc_read_g_pair(float *g1, float *g2)
{
  /* 本样本的两路码值只读一次，波形与串口共用同一份：adc_value1/2 由半区中断逐个改写，
   * "各读各的"可能让波形那一列与串口那一帧来自相邻两个块（差 20ms），
   * 而两处字面一致是本工程的承诺 */
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  uint16_t a1 = adc_value1, a2 = adc_value2;
  __set_PRIMASK(primask);

  *g1 = adc_to_g(a1, cal_off1);
  *g2 = adc_to_g(a2, cal_off2);
}

/* 标定判据：均值须落在前端可用量程内，且窗口内足够稳定（挡掉电极接入等突发过程） */
static uint8_t cal_accept(uint16_t mean, uint16_t spread)
{
  return (mean >= CAL_ADC_MIN) && (mean <= CAL_ADC_MAX) && (spread <= CAL_SPREAD_MAX);
}

void adc_cal_task(void)
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