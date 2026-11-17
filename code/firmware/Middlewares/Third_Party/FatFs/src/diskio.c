/*-----------------------------------------------------------------------*/
/* 本工程的 FatFs 底层磁盘 I/O —— 直接对接 STM32F1 HAL SD 驱动             */
/*                                                                       */
/* 不使用 ST 的 ff_gen_drv 注册层（DRV_* 那一套），原因：                 */
/*   1. 本工程只有一个物理驱动器（SD 卡），注册层没有收益；               */
/*   2. 少一层函数指针间接调用，出错时调用栈更短、更好查；                 */
/*   3. 避免把 ff_gen_drv.c / ff_gen_drv.h 一并拖进工程。                  */
/*-----------------------------------------------------------------------*/
#include "diskio.h"
#include "bsp_sd.h"

DSTATUS disk_initialize(BYTE pdrv)
{
  if (pdrv != 0) { return STA_NOINIT; }
  return (bsp_sd_is_ready() != 0U) ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
  if (pdrv != 0) { return STA_NOINIT; }
  /* 卡被拔出（NCD 有效）或驱动未初始化，都报未就绪，
   * 让 FatFs 的调用方拿到明确错误而不是对着旧缓冲继续读写 */
  if (bsp_sd_is_ready() == 0U) { return STA_NOINIT; }
  return (bsp_sd_card_present() != 0U) ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
  if (pdrv != 0 || count == 0U) { return RES_PARERR; }
  if (bsp_sd_is_ready() == 0U)  { return RES_NOTRDY; }
  return (bsp_sd_read_blocks(buff, sector, count) == 0) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
  if (pdrv != 0 || count == 0U) { return RES_PARERR; }
  if (bsp_sd_is_ready() == 0U)  { return RES_NOTRDY; }
  return (bsp_sd_write_blocks(buff, sector, count) == 0) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
  if (pdrv != 0) { return RES_PARERR; }
  if (bsp_sd_is_ready() == 0U) { return RES_NOTRDY; }

  switch (cmd)
  {
    case CTRL_SYNC:
      /* 写操作在 bsp_sd_write_blocks 里已是同步等待完成，无需额外动作 */
      return RES_OK;

    case GET_SECTOR_COUNT:
      *(DWORD *)buff = bsp_sd_sector_count();
      return RES_OK;

    case GET_SECTOR_SIZE:
      *(WORD *)buff = 512U;
      return RES_OK;

    case GET_BLOCK_SIZE:
      /* 擦除块大小（扇区数）。SD 卡按扇区管理，填 1 表示"不关心" */
      *(DWORD *)buff = 1U;
      return RES_OK;

    default:
      return RES_PARERR;
  }
}
