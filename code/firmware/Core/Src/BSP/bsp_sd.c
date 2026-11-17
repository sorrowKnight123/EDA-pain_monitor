/**
  ******************************************************************************
  * @file    bsp_sd.c
  * @brief   SD 卡底层驱动：SDIO 初始化、卡识别、扇区读写
  *
  * 引脚（手册 7.55/7.57，见 docs/manual-kg/hardware/pin-map.md）：
  *   PC8=D0  PC9=D1  PC10=D2  PC11=D3  PC12=CK  PD2=CMD  PC6=NCD(在位检测)
  *
  * 时钟：STM32F1 的 SDIOCLK = HCLK = 72MHz（F1 没有独立的 48MHz SDIO 时钟），
  *   SDIO_CK = SDIOCLK / (CLKDIV + 2)。
  *   CLKDIV=14 → 4.5MHz（2.25MB/s）。上限 24MHz，但实际取值受 CPU 供给能力限制：
  *   HAL 用轮询方式往 TX FIFO 灌数据，FIFO 半空（64B）留给 CPU 的时间 = 64B ÷ 速率。
  *   18MHz 时只有约 7µs，-O0 编译下补满 32B 就要 6~8µs，再被 ADC 中断抢占就必然
  *   TX FIFO underrun(0x10)。4.5MHz 把余量放大到约 28µs，实测稳定。
  *   50Hz × 2 通道的 CSV 约 1.5KB/s，4.5MHz 绰绰有余。
  *
  * 容量限制：本板 SD 卡座最大支持 2GB（手册 p.683/692/705），
  *   因此只接受 SDSC 卡（FAT16）。SDHC 卡虽然 HAL 能识别，但不在板子标称范围内，
  *   这里不做拦截，仅通过 bsp_sd_get_info 暴露实际识别结果供诊断。
  ******************************************************************************
  */
#include "bsp_sd.h"
#include "app_config.h"
#include "bsp_iwdg.h"
#include "stm32f1xx_hal.h"

#include <string.h>

#define SD_TIMEOUT_MS        500U    /* 单次扇区读写超时。
                                      * SD 规范最坏写入编程时间约 250ms，500ms 留一倍余量；
                                      * 不能取太大：阻塞期间主循环停转，IWDG 只有 ≈1s */

SD_HandleTypeDef hsd;

static uint8_t  sd_ready = 0;
static uint32_t sd_sectors = 0;

/* 最近一次写失败现场（调试用）：把"写失败"拆成可分辨的几类，
 * 否则串口只有一句 write failed，无法判断是命令阶段、数据阶段还是等编程超时 */
static int      last_err_code = 0;
static uint32_t last_err_hal  = 0;
static uint32_t last_err_state = 0;

/* 卡在位：PC6 = SD_NCD，卡插入时为低电平（手册 7.57 引脚表） */
uint8_t bsp_sd_card_present(void)
{
  return (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_6) == GPIO_PIN_RESET) ? 1U : 0U;
}

static void sd_gpio_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_SDIO_CLK_ENABLE();

  /* PC8-PC11 = D0-D3，PC12 = CK：复用推挽，高速 */
  gpio.Mode  = GPIO_MODE_AF_PP;
  gpio.Pull  = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Pin   = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
  HAL_GPIO_Init(GPIOC, &gpio);

  /* PD2 = CMD：复用推挽 */
  gpio.Pin = GPIO_PIN_2;
  HAL_GPIO_Init(GPIOD, &gpio);

  /* PC6 = NCD 卡检测：输入上拉 */
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  gpio.Pin  = GPIO_PIN_6;
  HAL_GPIO_Init(GPIOC, &gpio);
}

uint8_t bsp_sd_init(void)
{
  HAL_SD_CardInfoTypeDef info;

  sd_ready = 0;
  sd_sectors = 0;

  /* 无卡也要把 GPIO/SDIO 时钟配好，之后插入卡时 bsp_sd_reinit 才能直接用 */
  sd_gpio_init();

  if (!bsp_sd_card_present())
  {
    return 1;   /* 不是错误：无卡是正常状态，由调用方决定是否提示 */
  }

  hsd.Instance = SDIO;

  /* 初始化阶段：SDIO_CK 必须 < 400kHz。
   * 注意 HAL_SD_InitCard 内部会用常量 SDIO_INIT_CLK_DIV(118) 覆盖这里的值，
   * 实际为 72MHz/(118+2) = 600kHz —— 略超规范上限但本卡能稳定识别；
   * 这里显式写出 118 只为与该常量一致，不产生额外影响 */
  hsd.Init.ClockEdge           = SDIO_CLOCK_EDGE_RISING;
  hsd.Init.ClockBypass         = SDIO_CLOCK_BYPASS_DISABLE;
  hsd.Init.ClockPowerSave      = SDIO_CLOCK_POWER_SAVE_DISABLE;
  hsd.Init.BusWide             = SDIO_BUS_WIDE_1B;
  hsd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
  hsd.Init.ClockDiv            = 118;

  if (HAL_SD_Init(&hsd) != HAL_OK)
  {
    return 2;   /* 卡识别失败：非 SDSC/SDHC 卡、接触不良、或卡已损坏 */
  }

  /* 识别成功后再切到 4 位总线 + 4.5MHz。
   * 顺序不能反：1 位/低速下完成识别，才能协商出卡支持的总线宽度。
   *
   * 两个关键点：
   *   ① 必须先改 hsd.Init.ClockDiv，再调 HAL_SD_ConfigWideBusOperation ——
   *      该函数内部会用 hsd->Init.ClockDiv 重新写 CLKCR，同时发 ACMD6 通知
   *      **卡**切到 4 位模式。只写 SDIO_Init 而不发 ACMD6，卡仍工作在 1 位，
   *      主机却按 4 位收数 → 读到的全是错位数据（表现为 FatFs 挂载失败）。
   *   ② HAL_SD_InitCard 在识别阶段用固定分频 SDIO_INIT_CLK_DIV(0x76≈118)，
   *      不显式改的话卡会一直跑低速，单次扇区写耗时过长，50Hz 记录会丢样本。
   *   ③ 分频取值必须按 F1 的真实时钟树算：SDIOCLK = HCLK = 72MHz（不是 36MHz），
   *      SDIO_CK = 72MHz / (ClockDiv + 2)。
   *      曾按"SDIOCLK=36MHz"误设 ClockDiv=2，实际跑成 18MHz：4 位下 9MB/s，
   *      TX FIFO 半空（64B）只有约 7µs 余量，而 -O0 编译下 CPU 补满 32B 需 6~8µs，
   *      再叠加 ADC 中断抢占，必然报 HAL_SD_ERROR_TX_UNDERRUN(0x10) ——
   *      表现为读（挂载、扫目录）正常、一写就失败、卡上不产生文件。
   *      72/(14+2) = 4.5MHz，2.25MB/s，对 50Hz × 2 通道的 CSV（约 1.5KB/s）绰绰有余 */
  hsd.Init.ClockDiv = 14;   /* 72MHz / (14 + 2) = 4.5MHz */

  if (HAL_SD_ConfigWideBusOperation(&hsd, SDIO_BUS_WIDE_4B) != HAL_OK)
  {
    /* 4 位协商失败：退回 1 位（卡已按 1 位初始化，无需再发 ACMD6），
     * 速度慢但数据仍正确 —— 宁可慢也不能读错 */
    (void)HAL_SD_ConfigWideBusOperation(&hsd, SDIO_BUS_WIDE_1B);
  }

  if (HAL_SD_GetCardState(&hsd) != HAL_SD_CARD_TRANSFER)
  {
    return 4;
  }

  HAL_SD_GetCardInfo(&hsd, &info);

  if (info.LogBlockSize == 0U || info.LogBlockNbr == 0U)
  {
    return 3;
  }

  sd_sectors = info.LogBlockNbr;
  sd_ready   = 1;
  return 0;
}

uint8_t bsp_sd_reinit(void)
{
  (void)HAL_SD_DeInit(&hsd);
  return bsp_sd_init();
}

uint8_t bsp_sd_is_ready(void)
{
  return sd_ready;
}

uint32_t bsp_sd_sector_count(void)
{
  return sd_sectors;
}

int bsp_sd_read_blocks(void *buf, uint32_t sector, uint32_t count)
{
  if (!sd_ready || buf == 0 || count == 0U)
  {
    last_err_code = -1; last_err_hal = 0; last_err_state = 0;
    return -1;
  }
  if (HAL_SD_ReadBlocks(&hsd, (uint8_t *)buf, sector, count, SD_TIMEOUT_MS) != HAL_OK)
  {
    last_err_code = -2; last_err_hal = hsd.ErrorCode; last_err_state = 0;
    return -2;
  }
  /* 等待卡内部读操作真正结束，否则紧接着的写会拿到 SDIO_STA 忙错误 */
  if (HAL_SD_GetCardState(&hsd) != HAL_SD_CARD_TRANSFER)
  {
    last_err_code = -3; last_err_hal = hsd.ErrorCode;
    last_err_state = (uint32_t)HAL_SD_GetCardState(&hsd);
    return -3;
  }
  return 0;
}

int bsp_sd_write_blocks(const void *buf, uint32_t sector, uint32_t count)
{
  uint32_t t0;
  HAL_SD_CardStateTypeDef st;

  if (!sd_ready || buf == 0 || count == 0U)
  {
    last_err_code = -1; last_err_hal = 0; last_err_state = 0;
    return -1;
  }
  if (HAL_SD_WriteBlocks(&hsd, (uint8_t *)buf, sector, count, SD_TIMEOUT_MS) != HAL_OK)
  {
    last_err_code = -2; last_err_hal = hsd.ErrorCode; last_err_state = 0;
    return -2;
  }
  /* 写完成后必须轮询到 TRANSFER 再返回：HAL_SD_WriteBlocks 收到 DATAEND 就返回 HAL_OK，
   * 此时数据刚到卡里，卡内部仍在编程，CMD13 报的状态是 PRG(7) 而不是 TRANSFER(4)。
   * 只查一次几乎必然拿到 PRG —— 后果是每次 f_write 都返回 FR_DISK_ERR（串口 write failed），
   * 而目录项与 FAT 改动仍留在 FatFs 的 RAM 窗口里（dir_register 只置 wflag 不落盘，
   * 见 ff.c），于是卡上一个文件都不会出现。
   * FatFs 还会连续写多个扇区，卡还在编程时下发下一条写命令同样会失败（随机的 RES_ERROR） */
  t0 = HAL_GetTick();
  for (;;)
  {
    st = HAL_SD_GetCardState(&hsd);
    if (st == HAL_SD_CARD_TRANSFER) { return 0; }

    /* HAL 内部单次写最长 SD_TIMEOUT_MS，与本轮询叠加后逼近 IWDG 的 ≈1s 超时：
     * 慢卡必须在这里喂狗，否则正常写入会被看门狗当成死机而复位 */
    bsp_iwdg_feed();
    if ((HAL_GetTick() - t0) >= SD_TIMEOUT_MS)
    {
      last_err_code = -3; last_err_hal = hsd.ErrorCode; last_err_state = (uint32_t)st;
      return -3;
    }
  }
}

void bsp_sd_get_info(bsp_sd_info_t *out)
{
  HAL_SD_CardInfoTypeDef info;

  if (out == 0) { return; }
  out->card_type   = 0;
  out->block_count = sd_sectors;
  out->block_size  = 512U;
  out->capacity_kb = (sd_sectors / 2U);   /* 512B/块 → 2 块 = 1KB */
  out->ready       = sd_ready;

  if (sd_ready)
  {
    HAL_SD_GetCardInfo(&hsd, &info);
    out->card_type   = info.CardType;
    out->block_count = info.LogBlockNbr;
    out->block_size  = info.LogBlockSize;
    if (info.LogBlockSize != 0U)
    {
      out->capacity_kb = (uint32_t)(((uint64_t)info.LogBlockNbr * info.LogBlockSize) / 1024U);
    }
  }
}

void bsp_sd_get_last_error(int *code, uint32_t *hal_err, uint32_t *card_state)
{
  if (code)       { *code = last_err_code; }
  if (hal_err)    { *hal_err = last_err_hal; }
  if (card_state) { *card_state = last_err_state; }
}

/* 扇区写入自检：把最后几个扇区中的第 1 个读出→改第 0 字节→写回→再读出比对。
 * 目的：不经过 FatFs，直接判定"裸扇区能不能写"，从而区分
 *   a) SDIO/HAL/硬件层写不进去（本函数就失败）
 *   b) 裸扇区能写，只是 FatFs 层的问题（文件系统结构、簇分配）
 * 用最后一个扇区而不是第 0 扇区：第 0 扇区是引导记录，写坏会毁掉文件系统。
 * 校验后把原内容写回，不改动卡上数据。
 * 返回 0 = 可写；-1 = 不可写（详情用 bsp_sd_get_last_error） */
int bsp_sd_selftest_write(void)
{
  static uint8_t buf[512];
  uint8_t saved;
  uint32_t last;

  if (!sd_ready || sd_sectors == 0U) { return -1; }

  last = sd_sectors - 1u;      /* 保留扇区之后的最后一个数据扇区，用作临时区 */
  last = last > 8u ? last - 8u : last;   /* 略微避开最末尾，某些卡末尾有特殊扇区 */

  /* 保留读阶段自己的错误码：bsp_sd_read_blocks 成功时不会清零上次的写错误，
   * 但失败时会覆盖成 -2/-3，那正是我们要报告的信息，直接沿用即可 */
  if (bsp_sd_read_blocks(buf, last, 1) != 0) { return -1; }

  saved = buf[0];
  buf[0] = (uint8_t)(saved ^ 0xA5u);     /* 取反一位，确保内容确实变了 */
  if (bsp_sd_write_blocks(buf, last, 1) != 0)
  {
    buf[0] = saved;                       /* 恢复 RAM 内容（写失败时卡上没变） */
    return -1;                            /* last_err_code 已由 write_blocks 填好 */
  }

  memset(buf, 0, sizeof buf);
  if (bsp_sd_read_blocks(buf, last, 1) != 0) { return -1; }

  if (buf[0] != (uint8_t)(saved ^ 0xA5u))
  {
    last_err_code = -4;                   /* 写成功但读回不一致：卡/总线数据损坏 */
    return -1;
  }

  /* 还原原始内容 */
  buf[0] = saved;
  if (bsp_sd_write_blocks(buf, last, 1) != 0) { return -1; }

  last_err_code = 0;
  return 0;
}

