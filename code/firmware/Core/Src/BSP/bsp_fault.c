/**
  ******************************************************************************
  * @file    bsp_fault.c
  * @brief   故障/劣化标记的时间窗：四个"最近发生时刻"与窗口判定
  ******************************************************************************
  */
#include "bsp_fault.h"
#include "app_config.h"

/* 故障/劣化最近发生时刻（0 = 从未发生）。计数器与时间戳都只增不减，不能直接拿来点亮
 * 告警 —— 那样一次上电瞬态就会让红块永久保持。告警改看"窗口内是否发生过"，
 * 由 mark_alive() 统一判"还在不在窗口内"（回绕安全，见其说明） */
volatile uint32_t adc_err_ms = 0;    /* 最近一次 ADC/DMA 错误时刻 */
volatile uint32_t adc_stall_ms = 0;  /* 最近一次采集停摆时刻 */
volatile uint32_t adc_spike_ms = 0;  /* 最近一次抗扰块剔除时刻（数据质量，非链路故障） */
volatile uint32_t adc_drop_ms = 0;   /* 最近一次消费端丢样本时刻：一轮循环跳过了多个样本 */

uint8_t mark_alive(volatile uint32_t *stamp)
{
  return (uint8_t)(HAL_GetTick() - (*stamp + ALARM_WINDOW_MS) < 0x80000000u);
}