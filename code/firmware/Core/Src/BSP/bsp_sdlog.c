/**
  ******************************************************************************
  * @file    bsp_sdlog.c
  * @brief   SD 卡 CSV 数据记录：双阈值触发 + 环形队列 + 批量落盘
  *
  * 数据流：
  *   adc_read_sample() → bsp_sdlog_feed(&s)    只做判定+入队（微秒级）
  *                     → bsp_sdlog_task()        主循环里批量写卡（ms 级）
  *
  * 为什么不在 feed 里直接写卡：一次 SD 扇区写是 ms 级，而采样间隔只有 20ms。
  * 同步写会挤占主循环，导致 adc_seq 被跨过（已有 adc_drop_ms 告警会亮），
  * 波形时基也会被压缩。队列把"采样节奏"与"存储节奏"解耦。
  ******************************************************************************
  */
#include "bsp_sdlog.h"
#include "bsp_sd.h"
#include "bsp_iwdg.h"
#include "app_config.h"
#include "ff.h"
#include "usart.h"          /* 诊断输出用 huart1 */
#include "stm32f1xx_hal.h"

#include <stdio.h>
#include <string.h>

/* ---- 定点数值格式化 ----
 * 不能用 "%.3f"：本工程链接 newlib-nano 且未开 -u _printf_float
 * （.cproject 里 nanoprintffloat=false），%f 在运行期会输出空串 ——
 * 编译、链接都没有任何警告，只有上机看 CSV 才发现整列是空的。
 * 现有 LCD_ValStr 也正因如此全部用整数格式化。这里同理：
 * 先转成"千分之一显示单位"的整数，再用 %lu 拼出 "123.456"。 */
static void fmt_f3(char *out, size_t cap, float v)
{
  uint32_t u;

  if (!(v >= 0.0f)) { v = 0.0f; }          /* 同时挡住负数和 NaN */
  if (v > 99999.999f) { v = 99999.999f; }  /* 限幅：防 float→uint32 溢出（显示上限 1999.99） */

  u = (uint32_t)(v * 1000.0f + 0.5f);
  snprintf(out, cap, "%lu.%03lu", (unsigned long)(u / 1000u), (unsigned long)(u % 1000u));
}

/* ---- 串口诊断输出 ----
 * 50Hz 数据上报已移除，USART1 现在只用于这里的诊断输出（阻塞发送，独占外设）。
 * 保留 gState 忙检查：万一以后重新引入 DMA 发送，忙时直接放弃本条诊断，
 * 不去打断正在进行的传输。所有调用点都在主循环线程，不存在并发启动发送的问题。 */
static void diag_puts(const char *s)
{
  uint16_t len;

  if (s == 0 || huart1.gState != HAL_UART_STATE_READY) { return; }
  len = (uint16_t)strlen(s);
  if (len == 0U) { return; }
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)s, len, 100U);
}

/* 错误类诊断限速：坏卡时 open/write 每秒重试一次，不限速会刷屏 */
static void diag_err(const char *s)
{
  static uint32_t last_ms = 0;
  static uint8_t  first = 1;
  uint32_t now = HAL_GetTick();

  if (!first && (now - last_ms) < 5000u) { return; }
  first = 0;
  last_ms = now;
  diag_puts(s);
}

/* ---- 队列条目：一行 CSV 的原始数值（不是格式化后的字符串） ---- */
typedef struct {
  uint32_t     t_ms;   /* 相对上电的毫秒时间戳 */
  adc_sample_t s;      /* 本样本快照：原始电导、零点、显示值、质量位 */
} sdlog_row_t;

static sdlog_row_t q[SDLOG_QUEUE_LEN];
static uint16_t q_head = 0;   /* 写入位置（feed 侧） */
static uint16_t q_tail = 0;   /* 读取位置（task 侧） */

static FIL      logfile;
static FATFS    fatfs;          /* 必须是静态对象：f_mount 保存的是它的指针，
                                 * 传复合字面量的临时对象会在函数返回后失效 */
static uint8_t  file_open = 0;
static uint32_t file_index = 0;
static uint32_t rows_since_sync = 0;

static sdlog_state_t state = SDLOG_IDLE;

/* 运行状态：只需缓存"卡是否可用/是否在位"用于边沿检测与早退，
 * 以及本次已写行数（停止时打印）。其余统计量没有读取方，不保留 */
static uint8_t  sd_ready = 0;
static uint8_t  sd_present = 0;
static uint32_t rows_written = 0;

/* 迟滞状态机 */
static uint8_t  recording = 0;
static uint32_t below_since = 0;      /* 低于退出阈值的起始时刻；0 = 当前不在低位 */
static uint8_t  below_active = 0;

/* 本次记录的零点：CSV 的 t_ms 一律相对它计算，使每个文件都从 0 开始。
 * 取"判定开始记录时第一个样本的采样时刻"，不取文件打开时刻 —— 文件是 task 里
 * 才建的（开文件是 I/O），比入队晚，用打开时刻会让第一个样本变成负数 */
static uint32_t log_t0_ms = 0;

static uint8_t fs_mounted = 0;

/* 失败退避：I/O 出错后至少隔 SDLOG_RETRY_MS 再试。
 * 没有它的话，task 每轮主循环（约 1kHz）都会重试一次失败的写/建文件，
 * 既把主循环拖死（每轮都阻塞在超时上），也会飞快耗尽文件名序号 */
#define SDLOG_RETRY_MS   1000u
static uint32_t retry_at = 0;

static uint8_t retry_ready(void)
{
  /* 用有符号差判到期：HAL_GetTick 回绕（约 49.7 天）时仍然正确 */
  return ((int32_t)(HAL_GetTick() - retry_at) >= 0) ? 1U : 0U;
}

static void retry_backoff(void)
{
  retry_at = HAL_GetTick() + SDLOG_RETRY_MS;
}

/* ------------------------------------------------------------------------- */
/* 文件系统挂载与文件序号扫描                                                  */
/* ------------------------------------------------------------------------- */

/* 扫描根目录里已存在的 LOGxxxx.CSV，返回最大序号。
 * 每次上电都扫描而不是把序号存 Flash：换卡后序号从头开始，
 * 也不会因为 Flash 里存了旧序号而跳号或覆盖 */
static uint32_t scan_max_index(void)
{
  DIR     dir;
  FILINFO fno;
  uint32_t maxidx = 0;

  if (f_opendir(&dir, "/") != FR_OK) { return 0; }

  while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0)
  {
    /* 只认 LOGnnnn.CSV（8.3 短名，大小写不敏感比较） */
    const char *n = fno.fname;
    if ((n[0] == 'L' || n[0] == 'l') &&
        (n[1] == 'O' || n[1] == 'o') &&
        (n[2] == 'G' || n[2] == 'g'))
    {
      uint32_t v = 0;
      int i = 3;
      while (n[i] >= '0' && n[i] <= '9') { v = v * 10u + (uint32_t)(n[i] - '0'); i++; }
      if (v > maxidx) { maxidx = v; }
    }
  }
  f_closedir(&dir);
  return maxidx;
}

static uint8_t mount_fs(void)
{
  FRESULT fr;

  if (!bsp_sd_is_ready()) { return 0; }

  /* 不格式化：卡里必须已有 FAT16 文件系统（电脑上先格好）。
   * 现场自动格式化风险太大 —— 卡里若有别的数据会被抹掉 */
  fr = f_mount(&fatfs, "", 1);
  if (fr != FR_OK)
  {
    fs_mounted = 0;
    return 0;
  }
  fs_mounted = 1;
  file_index = scan_max_index();
  return 1;
}

void bsp_sdlog_init(void)
{
  sd_ready = 0;
  sd_present = 0;
  rows_written = 0;

  (void)bsp_sd_init();
  sd_present = bsp_sd_card_present();
  sd_ready   = bsp_sd_is_ready();

  if (sd_ready && !mount_fs())
  {
    sd_ready = 0;
  }

  state = SDLOG_IDLE;
}

/* ------------------------------------------------------------------------- */
/* 采样侧：判定 + 入队                                                         */
/* ------------------------------------------------------------------------- */

void bsp_sdlog_feed(const adc_sample_t *s)
{
  uint32_t now = HAL_GetTick();
  uint16_t next;
  float peak;

  /* 卡不可用时不做任何事：不判定、不入队，避免队列被填满后
   * 卡恢复时把几秒前的旧数据当成新数据写进去 */
  if (!sd_ready || s == 0) { return; }

  /* 两路取较大者参与判定 */
  peak = (s->g1 > s->g2) ? s->g1 : s->g2;

  if (!recording)
  {
    if (peak >= SDLOG_START_TH)
    {
      recording = 1;
      log_t0_ms = now;    /* 新一次记录的零点：本文件的时间戳从这里归零 */
      below_active = 0;
      /* 不在这里开文件（开文件是 I/O，交给 task）；
       * 用一个标志让 task 下一轮立刻建文件 */
      state = SDLOG_RECORDING;
    }
    else
    {
      return;   /* 未达阈值：不记录 */
    }
  }
  else
  {
    /* 已在记录中：检查退出条件 */
    if (peak <= SDLOG_STOP_TH)
    {
      if (!below_active) { below_active = 1; below_since = now; }
      else if ((now - below_since) >= SDLOG_STOP_HOLD_MS)
      {
        /* 只清 recording，state 留给 task：文件里还有队列中未落盘的行，
         * task 真正关闭文件后才置 IDLE。否则 REC 指示会提前熄灭，
         * 而卡其实还在写最后一秒的数据 */
        recording = 0;
        below_active = 0;
        return;   /* 本样本不记录（已判停） */
      }
    }
    else
    {
      below_active = 0;   /* 回到阈值以上：清除计时 */
    }
  }

  /* 入队（单生产者单消费者，用下标差判满，不需要关中断：
   * feed 只在主循环调用，task 也在主循环，二者不会并发） */
  next = (uint16_t)((q_head + 1u) % SDLOG_QUEUE_LEN);
  if (next == q_tail)
  {
    /* 队列满：丢最旧的会打乱时间顺序，这里选择丢新样本 */
    return;
  }

  q[q_head].t_ms = now;
  q[q_head].s    = *s;
  q_head = next;
}

/* ------------------------------------------------------------------------- */
/* 存储侧：建文件 / 批量写 / 关闭                                              */
/* ------------------------------------------------------------------------- */

static uint8_t open_new_file(void)
{
  char name[16];
  FRESULT fr;
  uint32_t next_index;

  if (!fs_mounted)
  {
    if (!mount_fs()) { return 0; }
  }

  /* 序号只在**打开成功**后才提交。失败时若也自增，配合 task 每轮重试
   * （主循环约 1kHz），几十秒就能绕完 9999 个序号开始覆盖旧文件。
   * 同样的原因，失败后的重试还加了退避（见 task 里的 retry_at） */
  next_index = file_index + 1u;
  if (next_index > SDLOG_MAX_FILES) { next_index = 1u; }

  snprintf(name, sizeof name, "LOG%04lu.CSV", (unsigned long)next_index);

  /* f_open(FA_CREATE_ALWAYS) 会分配簇、更新 FAT 与目录项，可能连续写几个扇区。
   * IWDG 只有 ≈1s，先喂一次，把整段 I/O 的预算给足 */
  bsp_iwdg_feed();
  fr = f_open(&logfile, name, FA_WRITE | FA_CREATE_ALWAYS);
  if (fr != FR_OK)
  {
    state = SDLOG_ERROR;
    return 0;
  }

  file_index = next_index;

  /* 表头：把单位、判据和每一列的含义写进文件，避免以后拿到数据不知道 1.0 是什么。
   * 阈值用整数拼（X10 常数），不用 %f —— 原因见 fmt_f3 的说明。
   * 缓冲声明为 static：七行注释约 400 字节，放栈上会把 snprintf 的栈用量
   * 推到深水区（本工程默认栈 1KB），而这个函数只在主循环里被调、没有重入问题 */
  {
    static char hdr[448];
    int n = snprintf(hdr, sizeof hdr,
        "# GSR log fs=50Hz start_th=%lu.%01lu\r\n"
        "# t_ms: ms from this file's first sample (first row = 0)\r\n"
        "# raw1,raw2: conductivity in uS, before zero offset\r\n"
        "# off1,off2: zero offset in uS (fixed after power-on calibration)\r\n"
        "# g1,g2,diff: display unit = (raw-off)*30; diff = abs(g1-g2)\r\n"
        "# q: 1 = valid block, 0 = disturbed block rejected by range check\r\n"
        "# t_ms,raw1,raw2,off1,off2,g1,g2,diff,q\r\n",
        (unsigned long)(SDLOG_START_TH_X10 / 10u),
        (unsigned long)(SDLOG_START_TH_X10 % 10u));
    UINT bw = 0;
    f_write(&logfile, hdr, (UINT)n, &bw);
  }

  file_open = 1;
  rows_since_sync = 0;
  rows_written = 0;
  return 1;
}

static void close_file(void)
{
  if (file_open)
  {
    bsp_iwdg_feed();     /* f_sync + f_close 会写 FAT 与目录项，阻塞前先喂狗 */
    f_sync(&logfile);
    f_close(&logfile);
    file_open = 0;
  }
}

/* 把队列里的行批量写入文件。返回本次写入行数 */
static uint16_t drain_queue(void)
{
  uint16_t written = 0;
  char line[96];

  while (q_tail != q_head && written < SDLOG_WRITE_PER_TASK)
  {
    sdlog_row_t *r = &q[q_tail];
    const adc_sample_t *s = &r->s;
    float diff = (s->g1 > s->g2) ? (s->g1 - s->g2) : (s->g2 - s->g1);
    char v1[12], v2[12], v3[12], v4[12], v5[12], v6[12], v7[12];
    int n;
    UINT bw = 0;
    FRESULT fr;

    /* 固定三位小数：显示单位下 0.001 ≈ 0.000033μS，远小于 ADC 有效分辨率，
     * 不丢有效信息；固定格式也便于 Python 解析。
     * 用 fmt_f3 而不是 %f —— newlib-nano 未开浮点 printf。
     * 列顺序：原始电导两路 → 零点两路 → 显示值两路 → 差值 → 质量位。
     * 原始值与零点每行都存：事后既能用原始值重跑离线算法，
     * 也能不依赖任何外部记录就完成 μS 换算（零点在上电标定后固定不变） */
    fmt_f3(v1, sizeof v1, s->r1);
    fmt_f3(v2, sizeof v2, s->r2);
    fmt_f3(v3, sizeof v3, s->off1);
    fmt_f3(v4, sizeof v4, s->off2);
    fmt_f3(v5, sizeof v5, s->g1);
    fmt_f3(v6, sizeof v6, s->g2);
    fmt_f3(v7, sizeof v7, diff);

    n = snprintf(line, sizeof line, "%lu,%s,%s,%s,%s,%s,%s,%s,%u\r\n",
                 (unsigned long)(r->t_ms - log_t0_ms),
                 v1, v2, v3, v4, v5, v6, v7, (unsigned)s->valid);

    fr = f_write(&logfile, line, (UINT)n, &bw);
    if (fr != FR_OK || bw != (UINT)n)
    {
      state = SDLOG_ERROR;
      diag_err("[SD] write failed\r\n");
      /* 不消费队列条目：保留数据，退避后重试（若卡真的坏了，
       * 队列会满并开始计入 drop_cnt，这是有意的降级） */
      retry_backoff();
      break;
    }

    q_tail = (uint16_t)((q_tail + 1u) % SDLOG_QUEUE_LEN);
    written++;
    rows_written++;
    rows_since_sync++;

    if (rows_since_sync >= SDLOG_SYNC_EVERY)
    {
      /* f_sync 可能连续写数据扇区 + FAT + 目录项（最多 3~4 个扇区），
       * 是单次阻塞最长的操作。喂狗后再做，避免 IWDG 在正常写入时复位 */
      bsp_iwdg_feed();
      f_sync(&logfile);   /* 定期落盘：掉电最多丢 SDLOG_SYNC_EVERY 行 */
      rows_since_sync = 0;
    }
  }

  return written;
}

void bsp_sdlog_task(void)
{
  /* 卡状态变化处理：拔卡 → 关文件；插卡 → 重新初始化并挂载 */
  {
    uint8_t present = bsp_sd_card_present();

    if (present != sd_present)
    {
      sd_present = present;
      if (!present)
      {
        close_file();
        recording = 0;
        below_active = 0;
        state = SDLOG_IDLE;
        sd_ready = 0;
        fs_mounted = 0;
        /* 必须清空队列：否则卡恢复后、下一次测量开始时，
         * 这些"拔卡前"的旧样本会被写进新文件，时间戳前后错乱。
         * 丢掉的样本计入 drop_cnt，如实反映损失 */
        q_tail = q_head;
        diag_puts("[SD] card removed\r\n");
      }
      else
      {
        /* 插卡：重新识别 + 挂载。初始化要等卡上电（手册：最长 1s），
         * 先喂狗再进来，否则识别慢的卡会把 IWDG 拖到复位 */
        bsp_iwdg_feed();
        if (bsp_sd_reinit() == 0)
        {
          sd_ready = bsp_sd_is_ready();
          if (sd_ready && !mount_fs()) { sd_ready = 0; }
        }
        diag_puts(sd_ready ? "[SD] card inserted, mounted\r\n"
                                : "[SD] card inserted, but NOT mounted\r\n");
      }
    }
  }

  if (!sd_ready)
  {
    /* 卡不可用：定期尝试重挂载（不阻塞主循环） */
    static uint32_t last_try = 0;
    uint32_t now = HAL_GetTick();
    if (bsp_sd_card_present() && (now - last_try) >= 2000u)
    {
      last_try = now;
      bsp_iwdg_feed();
      if (bsp_sd_reinit() == 0)
      {
        sd_ready = bsp_sd_is_ready();
        if (sd_ready && !mount_fs()) { sd_ready = 0; }
      }
    }
    return;
  }

  /* 已判定开始记录但文件还没建：现在建（I/O 在这里做，不在 feed 里）。
   * 失败后按 SDLOG_RETRY_MS 退避重试，不能每轮主循环都试 —— 那会把主循环
   * 拖死在超时等待上，并且飞快耗尽文件名序号 */
  if (recording && !file_open && retry_ready())
  {
    if (!open_new_file())
    {
      retry_backoff();
      diag_err("[SD] open failed, retrying\r\n");
      return;   /* 建文件失败：state 已是 ERROR，退避后重试 */
    }
    state = SDLOG_RECORDING;
    {
      char buf[48];
      snprintf(buf, sizeof buf, "[SD] REC start -> LOG%04lu.CSV\r\n",
               (unsigned long)file_index);
      diag_puts(buf);
    }
  }

  if (file_open && retry_ready())
  {
    (void)drain_queue();

    /* 停止记录且队列已排空 → 关文件 */
    if (!recording && q_tail == q_head)
    {
      uint32_t rows = rows_written;
      close_file();
      state = SDLOG_IDLE;
      {
        char buf[48];
        snprintf(buf, sizeof buf, "[SD] REC stop  rows=%lu\r\n", (unsigned long)rows);
        diag_puts(buf);
      }
    }
  }
}

sdlog_state_t bsp_sdlog_state(void)
{
  return state;
}

