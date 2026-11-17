/**
  ******************************************************************************
  * @file    bsp_sdlog.h
  * @brief   SD 卡数据记录：把屏上显示的电导值按 50Hz 写入 CSV
  *
  * 设计要点：
  *   1. 采样点只入队（纯内存，微秒级），实际写卡在主循环的 sdlog_task 里做。
  *      写一个扇区可能耗时数 ms，若在采样消费点同步写，会拖慢主循环、
  *      触发 adc_drop_ms 告警（主循环跨过样本）。
  *   2. 双阈值 + 迟滞：显示值超过 SDLOG_START_TH 判为"开始测量"，
  *      持续 SDLOG_STOP_HOLD_MS 低于 SDLOG_STOP_TH 才判"测量结束"。
  *      单阈值会在阈值附近反复开关，产生大量碎片文件。
  *   3. 每次测量一个文件：LOG0001.CSV、LOG0002.CSV……
  *   4. 写入的是 adc_read_g_pair 输出的原始浮点值（显示单位），
  *      不是 LCD_ValStr 格式化后的字符串 —— 后者小数位随量级自适应，
  *      直接写会丢精度且格式不统一，Python 解析困难。
  ******************************************************************************
  */
#ifndef __BSP_SDLOG_H__
#define __BSP_SDLOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 记录状态，供 LCD/串口显示 */
typedef enum {
  SDLOG_IDLE = 0,     /* 未开始测量（或已结束） */
  SDLOG_RECORDING,    /* 正在写卡 */
  SDLOG_ERROR         /* SD 卡不可用 / 写失败 */
} sdlog_state_t;

/* 初始化：挂载文件系统、扫描已有 LOGxxxx.CSV 确定下一个序号。
 * 卡未插入不算失败 —— 之后插卡由 sdlog_task 自动重试挂载 */
void bsp_sdlog_init(void);

/* 采样点投递（在主循环消费到新样本时调用，与 LCD 显示同一处取数）。
 * 内部只做判断+入队，不做任何 I/O */
void bsp_sdlog_feed(float g1, float g2);

/* 主循环任务：负责开/停判定、把队列里的行批量写入卡。
 * 非阻塞设计 —— 每次最多写若干行，剩下的留给下一轮循环 */
void bsp_sdlog_task(void);

sdlog_state_t bsp_sdlog_state(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SDLOG_H__ */
