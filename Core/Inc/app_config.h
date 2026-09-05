/**
  ******************************************************************************
  * @file    app_config.h
  * @brief   应用层可调参数集中定义（采集/滤波/判定阈值/量程）
  ******************************************************************************
  */
#ifndef __APP_CONFIG_H__
#define __APP_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 采集与滤波 ---- */
/* 超采样抽取比：每个 DMA 半区的 TIM3 触发次数。
 * 块时长 = ADC_BLOCK_TRIG / 500Hz = 20ms，块内"去最大/最小后均值"抽取 1 个有效样本 */
#define ADC_BLOCK_TRIG        10

/* 滤波器：有效样本滑动平均窗口长度。
 * 群延迟 ≈ (FILT_N-1)/2 × 20ms；FILT_N=8 时约 70ms，加大更平滑但延迟更高 */
#define FILT_N                8

/* ---- 电导量程与判定 ---- */
/* 波形纵轴满量程（μS），LCD_InitLayout 的刻度数组 tick_v 需与此一致 */
#define G_RANGE_MAX           50.0f

/* 麻醉状态判定阈值（μS）：|G1-G2| < G_DIFF_THRESH → 麻醉完全 */
#define G_DIFF_THRESH         10.0f

#ifdef __cplusplus
}
#endif

#endif /* __APP_CONFIG_H__ */
