/*---------------------------------------------------------------------------/
/  FatFs - 本工程配置（基于 R0.11 模板裁剪）
/  仅保留 SD 卡 CSV 记录所需的最小功能集：
/    - 读写、f_sync、f_lseek（续写/定位）
/    - 不使用 LFN（8.3 文件名足够：LOG0001.CSV）
/    - 不使用 exFAT（SDSC 2GB 卡是 FAT16）
/    - 不使用 RTOS、不使用可重入
/---------------------------------------------------------------------------*/
#ifndef _FFCONF
#define _FFCONF 32020

/* 代码页：简体中文 GBK。仅影响 FAT 目录中非 ASCII 长文件名的解析；
 * 本工程只写 8.3 短文件名，实际不会用到该表 */
#define _CODE_PAGE      936

/* 不使用长文件名：省 RAM（LFN 工作区最大 512 字节），8.3 名字够用 */
#define _USE_LFN        0

/* 扇区大小：SD 卡固定 512 字节。_MIN_SS 与 _MAX_SS 相等可省去运行时校验 */
#define _MAX_SS         512
#define _MIN_SS         512

/* 可写（0 = 读写） */
#define _FS_READONLY    0

/* 功能裁剪级别 0 = 保留全部基础 API（f_open/f_write/f_sync/f_lseek/f_close） */
#define _FS_MINIMIZE    0

/* 字符串函数关闭：本工程用 snprintf 自行格式化，不用 f_printf */
#define _USE_STRFUNC    0

/* 关闭时间戳：本板未启用 RTC，写入文件时间为固定值。
 * 数据行内自带 t_ms 相对时间戳，文件时间不影响分析 */
#define _FS_NORTC       1
#define _NORTC_MON      1
#define _NORTC_MDAY     1
#define _NORTC_YEAR     2010

/* 不使用擦除/TRIM 指令：SD 卡内部自行管理，且可避免与 HAL 的 CMD38 兼容问题 */
#define _USE_ERASE      0
#define _USE_TRIM       0

/* 不启用文件锁 / 可重入（裸机单任务） */
#define _FS_LOCK        0
#define _FS_REENTRANT   0

/* 不使用相对路径（省代码）；不支持快搜、分区标签、NT 标志 */
#define _FS_RPATH       0
#define _USE_FASTSEEK   0
#define _USE_LABEL      0
#define _USE_NTFLAG     0
#define _USE_FIND       0
#define _USE_FORWARD    0

/* 格式化功能：保留（首次使用可用电脑格式化为 FAT16；保留 f_mkfs 便于现场自救） */
#define _USE_MKFS       1

/* 不使用 FSINFO（FAT32 才用，本卡 FAT16） */
#define _FS_NOFSINFO    0

/* 逻辑驱动器数量：只有 SD 卡一个（0:） */
#define _VOLUMES        1

/* 不使用多分区：SD 卡按整盘使用，不按 MBR 分区表切分 */
#define _MULTI_PARTITION 0

#endif /* _FFCONF */
