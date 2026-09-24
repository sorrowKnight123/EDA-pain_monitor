/**
  ******************************************************************************
  * @file    bsp_iwdg.h
  * @brief   独立看门狗（寄存器级配置，不引入 HAL IWDG 驱动，看门狗尽量独立）
  ******************************************************************************
  */
#ifndef __BSP_IWDG_H__
#define __BSP_IWDG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/* 启动独立看门狗：LSI(40kHz)、预分频 /64、重载 625 → 约 1s 超时。
 * 主循环或任何 late-stage 卡死（含 Error_Handler 挂起）超过 1s 自动复位恢复。
 * 含 LSI 就绪的有界等待 —— LSI 不起振时若在此死等，设备会永久停在"看着正常、不出数据"
 * 的状态；此处也没有错误路径可走（狗还没启动，调 Error_Handler 只会变成红屏+永久挂起），
 * 所以超时后继续往下走（写入无效的 IWDG 配置无害） */
void bsp_iwdg_init(void);

/* 主循环每轮喂狗 */
void bsp_iwdg_feed(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_IWDG_H__ */