/**
  ******************************************************************************
  * @file    bsp_adc_acquire.h
  * @brief   ADC 采集链路：DMA 循环缓冲、块内抽取、滑动平均、停摆监测与恢复
  ******************************************************************************
  */
#ifndef __BSP_ADC_ACQUIRE_H__
#define __BSP_ADC_ACQUIRE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/* 有效样本（由 DMA 半区中断抽取+滤波后写入，主循环按 adc_seq 消费）。
 * adc_seq 同时是采集链路的心跳：额定 50Hz 增长，停摆/劣化监测据此判定 */
extern volatile uint16_t adc_value1;   /* 滤波后的电导1有效样本（PC1） */
extern volatile uint16_t adc_value2;   /* 滤波后的电导2有效样本（PC0） */
extern volatile uint16_t adc_seq;      /* 有效样本计数（回绕安全） */
extern volatile uint32_t adc_spike_cnt;/* 因块内极差超限被剔除的块数：诊断行 K 用 */

/* 启动采集链路：校准 ADC（F1 必要，减小偏移温漂）→ 以 DMA 循环模式启动 →
 * 打开 TIM3 触发源。上电时调用一次；这里的校准/启动失败不单独报错，
 * 后续 seq 不增长会被 adc_stall_monitor 抓到并分级恢复 */
void adc_acquire_start(void);

/* 采集停摆/劣化监测：adc_seq 不再增长（链路死透）或统计窗口内样本数不足
 * （还活着但节奏不对）都就地重启 ADC+DMA，短窗口内连续失败超限则整机复位。
 * 两个判据为什么都要有、恢复后窗口为什么必须重新起算：见其实现与
 * app_config.h 的 ADC_STALL_* 说明。主循环每轮调用 */
void adc_stall_monitor(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_ADC_ACQUIRE_H__ */