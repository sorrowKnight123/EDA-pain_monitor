/**
  ******************************************************************************
  * @file    bsp_sd.h
  * @brief   SD 卡底层驱动（SDIO + HAL SD），供 FatFs 的 diskio.c 调用
  ******************************************************************************
  */
#ifndef __BSP_SD_H__
#define __BSP_SD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 返回 0 = 成功，非 0 = 失败（与 FatFs DRESULT 解耦，diskio 负责转换） */
uint8_t bsp_sd_init(void);

/* 1 = 驱动已初始化且卡已识别；0 = 未就绪。diskio 用它判 RES_NOTRDY */
uint8_t bsp_sd_is_ready(void);

/* 1 = 卡在位（NCD 引脚为低），0 = 卡被拔出 */
uint8_t bsp_sd_card_present(void);

/* 扇区读写：sector = 逻辑块地址（LBA，512 字节/块）。
 * 内部为阻塞式调用，返回时数据传输已完成 */
int bsp_sd_read_blocks(void *buf, uint32_t sector, uint32_t count);
int bsp_sd_write_blocks(const void *buf, uint32_t sector, uint32_t count);

uint32_t bsp_sd_sector_count(void);

/* 重新初始化（卡拔出后重新插入时调用）：会先 DeInit 再 Init */
uint8_t bsp_sd_reinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SD_H__ */
