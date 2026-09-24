/**
  ******************************************************************************
  * @file    bsp_uart_report.h
  * @brief   串口1 上报链路：帧组装、DMA 发送、停摆恢复与生命期监测
  ******************************************************************************
  */
#ifndef __BSP_UART_REPORT_H__
#define __BSP_UART_REPORT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/* 上报一帧两路电导（与有效样本同步，50Hz）。数值经 LCD_ValStr 格式化，与屏上显示
 * 逐字符相同；帧格式由 app_config.h 的 UART_FRAME_SEP 决定。
 * 链路忙（上一帧还没发完）时直接丢弃本帧：主循环还要喂狗/推波形/刷屏，不能在此等待 */
void uart_report_pair(float g1, float g2);

/* 上报链路生命期监测：距上次成功发起发送超过 UART_STALL_RECOVER_MS 就中止发送、
 * 把 gState 与 DMA 通道强行拉回 READY、再补发最后一帧。主循环每轮调用。
 * 与采集链路同一个形状：链路正常时 50Hz 刷新心跳，判据永不成立；一旦停摆最多
 * UART_STALL_RECOVER_MS 后自动重启，无需人工干预、也不靠 IWDG（狗在主循环里被喂） */
void uart_lifetime_monitor(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_UART_REPORT_H__ */