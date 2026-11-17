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

/* 最近一次写失败的原因（调试用）。code 含义：
 *   0 = 无失败记录      -1 = 参数/未就绪     -2 = HAL 写命令或数据阶段失败
 *   -3 = 等卡编程完成超时
 * hal_err = 该次失败时 hsd.ErrorCode 的快照；card_state = 失败时 CMD13 读到的卡状态
 * （0=IDLE 1=READY 2=IDENT 3=STBY 4=TRAN 5=DATA 6=RCV 7=PRG 8=DIS） */
void bsp_sd_get_last_error(int *code, uint32_t *hal_err, uint32_t *card_state);

/* 裸扇区写入自检（不经过 FatFs）：读一个扇区→改一字节→写回→读回比对→还原原内容。
 * 用来把"SDIO/HAL 层写不进去"与"FatFs 层问题"分开。
 * 返回 0 = 可写；-1 = 不可写（原因用 bsp_sd_get_last_error，-4 = 写后读回不一致） */
int bsp_sd_selftest_write(void);

/* 卡信息（供串口打印诊断用） */
typedef struct {
  uint32_t card_type;        /* HAL_SD_CARD_SDSC / SDHC / SDHC_HIGH_CAPACITY */
  uint32_t block_count;      /* 逻辑块数（512B/块） */
  uint32_t block_size;       /* 每块字节数 */
  uint32_t capacity_kb;      /* 计算出的容量（KB） */
  uint8_t  ready;            /* 1 = 已识别 */
} bsp_sd_info_t;

void bsp_sd_get_info(bsp_sd_info_t *out);

/* 重新初始化（卡拔出后重新插入时调用）：会先 DeInit 再 Init */
uint8_t bsp_sd_reinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SD_H__ */
