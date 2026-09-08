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

/* ---- 采集停摆监测（健壮性） ----
 * DMA 传输错误(TE)时 HAL 会硬件停转通道并关闭全部 DMA 中断（RM0008 行为），
 * 采集静默停止且 IWDG 无法察觉（主循环仍在运行喂狗）。
 * 以 adc_seq（预期 50Hz 增长）为心跳：超时未增长即就地重启 ADC+DMA；
 * 短窗口内连续失败超限则整机复位 */
#define ADC_STALL_RECOVER_MS  300    /* seq 无增长判定停摆的时长 */
#define ADC_STALL_BURST_MS    10000  /* 视为"连续失败"的窗口 */
#define ADC_STALL_RESET_LIMIT 3      /* 窗口内连续停摆次数上限，超过则复位 */

/* ---- 电导量程与判定 ---- */
/* 波形纵轴满量程（μS），LCD_InitLayout 的刻度数组 tick_v 需与此一致 */
#define G_RANGE_MAX           50.0f

/* 麻醉状态判定阈值（μS）：|G1-G2| < G_DIFF_THRESH → 麻醉完全 */
#define G_DIFF_THRESH         10.0f

/* ---- 调试 ---- */
/* 1 = 屏幕底部显示原始诊断行（R1/R2 原始码值、SEQ 有效样本计数、ERR 错误计数）。
 * 排查采集问题用，定位完成后置 0 */
#define ADC_DEBUG_SHOW_RAW    0

#ifdef __cplusplus
}
#endif

#endif /* __APP_CONFIG_H__ */
