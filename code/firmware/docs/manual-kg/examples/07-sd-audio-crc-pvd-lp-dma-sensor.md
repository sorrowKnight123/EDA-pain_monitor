# 第7章 例程：SD卡 / 音频 / CRC / PVD / 低功耗 / DMA / 传感器

> 手册 p.673-785

### 7.55 SD卡访问实验（p.673-687）

| 字段 | 内容 |
|------|------|
| 外设/模块 | SD卡、SDIO控制器（SD/SDIO MMC 卡主机模块）、串口1、TFT彩屏（LCD） |
| 接口/总线 | SDIO（原文 7.55.10 实验原理：本次实验采用 SDIO 模式）；但 7.55.4 正文称"本实验我们采用的是 SPI 模式，我们主要对这个模式进行分析"，两处口径不一致。SDIO 硬件工作在 4 位数据模式（见 7.57 节） |
| 关键引脚 | 本节未列出引脚，原文"配置 GPIO 管脚，具体的管脚通过原理图确定"；交叉参考 7.57 节 SDIO 4 位模式引脚表：PC6=SD_NCD 在位检测、PC8=SDIO_D0、PC9=SDIO_D1、PC10=SDIO_D2、PC11=SDIO_D3、PC12=SDIO_CK、PD2=SDIO_CMD |
| 时钟使能 | 手册未明确（本节未给出 RCC 使能宏） |
| 关键 API / 寄存器 | SD_InitAndConfig()，SD_Init()，SD_GetCardInfo()，SD_PowerON()，SD_InitializeCards()，GPIO_Configuration()，SDIO_Init(&SDIO_InitStructure)，SDIO_GetResponse(SDIO_RESP4)；寄存器 SDIO_RESPCMD、SDIO_RESPx、OCR、CID、CSD、RCA；命令 CMD0/CMD2/CMD3/CMD8/CMD9/CMD10/CMD16/CMD17/CMD24/CMD41/CMD55/CMD59、ACMD41 |
| 初始化步骤 | 1）进入 main 后初始化 SysTick、串口、TFT 彩屏；2）GPIO_Configuration() 完成 SDIO 端口底层配置；3）SD_PowerON() + SD_InitializeCards() 实现卡检测、识别流程；4）SDIO_Init() 配置 SDIO 时钟、数据线宽度、硬件流；5）SD_Init() 内调 SD_GetCardInfo() 获取卡信息，读取 SD 起始扇区（MBR）数据并串口打印 |
| 常见坑 / 注意 | SDIO_CK 分两阶段：初始化阶段频率必须小于 400KHz，初始化完成后可调成高速模式；高速模式超过 24M 要开启 bypass 模式，SD 存储卡即使开 bypass 最高频率不能超过 25MHz；发送 ACMD 命令前必须先发 CMD55；协议要求调用 ACMD41 前必须先调用 CMD8；ACMD41 循环发送后检查 OCR 的 pwr_up 位，HCS 位置 1 时靠 OCR 的 CCS 位区分 SDHC（1）/SDSC（0）；CMD8 的 check pattern 若 SDIO 支持卡工作电压会原样返回 |
| 依赖前置 | 串口1（打印运行提示）、TFT 彩屏、SD 卡座（神舟III号最大支持 2G SD 卡）；下载调试按 3.5 在 MDK 中用 JLINK 在线调试 |

**一句话作用**：以 SDIO 模式按协议初始化 SD 卡，读取规范版本、容量、生产日期等卡信息，并把 SD 卡起始扇区（MBR）数据通过串口1打印出来。

> **⚠️ SD 卡选型硬限制（采购前必看）**
> 手册在**三处**明确写着板子只支持到 2GB：
> - p.683（7.55.9 硬件设计）："SD 卡座：神舟III 号最大支持 2G 的 SD 卡"
> - p.692（7.56 硬件设计）：同上
> - p.705（7.57 下载与验证）："插入SD卡（最大支持 2G）"
>
> 因此：**买 SDSC 卡（≤2GB），不要买 SDHC（4GB 起，卡面印 "HC"）**；文件系统用 **FAT16**。
> 注意 7.55.2 节讲的"SDHC 最大 32GB / SDXC 最大 2TB"只是 SD 标准的通用分类科普，
> **不是本板的支持能力**，不要据此选卡。MicroSD/TF + SD 转接卡套可用（7.55.8 原文："本实验用的是 Micro SD 卡"）。

### 7.56 SD卡 FAT文件系统访问实验（p.688-692）

| 字段 | 内容 |
|------|------|
| 外设/模块 | SD卡、SDIO、FatFs 文件系统模块、TFT彩屏（LCD）、串口1 |
| 接口/总线 | SDIO（4 位数据模式，与上一章"SD卡访问实验"相同） |
| 关键引脚 | 手册未明确；原文注："本实验的硬件设计与上一章节'SD卡访问实验'完全一样" |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | SD_InitAndConfig()，SD_Init()，ReadSDFile(void)，f_opendir(&dirs, path)，auto_mount()，f_open()（权限 FA_OPEN_EXISTING \| FA_READ）；FatFs 结构体 DIR |
| 初始化步骤 | 1）SD_InitAndConfig() 调 SD_Init() 初始化 SD 卡，按返回值判断是否成功；2）TFT 彩屏显示信息；3）ReadSDFile() 中 f_opendir(&dirs, path) 打开目录 path；4）auto_mount() 把目录挂载到 DIR 结构体；5）f_open() 打开已存在且可读的文件；6）在 TFT 彩屏输出 SD 卡里的文件列表 |
| 常见坑 / 注意 | 一个簇中只能放置一个文件的内容，即使文件为 0 字节也不允许两个及以上文件共用一个簇，否则会造成数据混乱；文件占用空间只能是簇的整数倍，一般略大于实际大小；FAT16 使用 16 位表示扇区，最大限制 2G（65536 簇 × 32KB） |
| 依赖前置 | 必须先跑通上一章"SD卡访问实验"（硬件与部分软件完全重复）；需了解 FAT 文件系统、簇/扇区概念 |

**一句话作用**：在 SD 卡驱动之上移植 FatFs 文件系统，用 f_opendir/auto_mount/f_open 等接口打开 SD 卡目录，并在 TFT 彩屏上输出卡内文件列表。

### 7.57 SD卡读卡器实验（p.693-705）

| 字段 | 内容 |
|------|------|
| 外设/模块 | SD卡、SDIO控制器、USB 接口（Mass Storage，参考 ST Mass_Storage 例程）、LED 指示灯（DS1/DS2/DS3）、串口1 |
| 接口/总线 | SDIO（4 位数据模式）；USB（全速，J6 为 USB B 型座，U11 为 ESD 防护器件，满足 IEC61000-4-2） |
| 关键引脚 | USB：PA11=USB 差分信号负端、PA12=USB 差分信号正端、PG11=USB 上拉控制（输出高电平时电脑检测到全速 USB 设备）；SDIO：PC6=SC_NCD（SD 卡在位检测）、PC8=SDIO_D0、PC9=SDIO_D1、PC10=SDIO_D2、PC11=SDIO_D3、PC12=SDIO_CK、PD2=SDIO_CMD |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | MAL_Init(uint8_t lun)，MSD_WriteBuffer()，MSD_ReadBuffer()，MSD_READ_SINGLE_BLOCK(CMD17)，MSD_WRITE_BLOCK(CMD24)；中断 SDIO_IRQn、USB_HP_CAN1_TX_IRQn |
| 初始化步骤 | 1）初始化串口1 与 GPIO（LED）；2）MAL_Init() 初始化 SD 接口、获取 SD 卡信息；3）SD 卡典型初始化：初始化硬件条件→上电延时(>74 个 CLK)→复位卡→激活卡并获取类型→查询 OCR→是否使用 CRC→设置读写块数据长度→读 CSD→发 8CLK 后禁片选；4）中断配置（SDIO_IRQn 优先级最高）；5）USB 接口初始化（完成后 DS3 常亮）；6）检测 SD 卡插入后配置 USB，电脑上出现可移动磁盘 |
| 常见坑 / 注意 | 原文：一般设置读写块数据长度为 256 个字节，并禁止使用 CRC；读数据时若未开启 CRC，接收的 2 个字节 CRC 在读后可以丢掉；中断组级别 0 最高，同组内优先级 0 最高；SDIO_IRQn 优先级最高可抢占其他中断；两个 USB 中断同组时处理器自动优先执行 USB_HP_CAN1_TX_IRQn |
| 依赖前置 | USB 接口实验、串口1（见 4.5 串口1 的发送与接收实验）、LED、SD 卡座（最大支持 2G）。LED 含义：DS1 闪烁=USB 读卡器正在读写 SD 卡、DS2 亮=MAL 配置完成、DS3 亮=USB 接口初始化完成 |

**一句话作用**：用 STM32 的 SDIO 接口读写 SD 卡、配合 USB Mass Storage 协议，把神舟III号做成一个 USB SD 读卡器，插卡后电脑可识别到 U 盘并读写。

### 7.58 音频播放试验（p.706-713）

| 字段 | 内容 |
|------|------|
| 外设/模块 | D/A 芯片 PCM1770（TI 低电压、低功耗带耳机放大）、I2S3、SPI2、LED、串口1、SysTick |
| 接口/总线 | I2S3（音频数据，中断方式）；SPI2（配置/读取 PCM1770 内部数据） |
| 关键引脚 | I2S3：PA15=I2S3_WS（左右通道时钟，与 JTAG 的 JTDI 复用）、PB3=I2S3_CK（串行比特时钟，与 JTDO 复用）、PB5=I2S_SD（串行音频信号）、PC7=I2S3_MCK（系统时钟输入）；SPI2：PB11=SPI2_NSS、PB13=SPI2_SCK、PB15=SPI2_MOSI |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | LED_Config()，I2S_Codec_init()，I2S_CODEC_DataTransfer()，SPI2_Config()，SysTick_Config(SystemCoreClok/100)，InterruptConfig()；I2S3 中断服务程序 |
| 初始化步骤 | 1）LED_Config() 把 LED 配置为推挽输出、初始化串口1；2）初始化 I2S 接口（用 Remap 功能关闭 JTAG）；3）SPI2_Config() 初始化 SPI2 的 GPIO，初始化 CS 信号并输出高电平，设置 SPI2 工作模式；4）SysTick 初始化为 10ms（时钟源 72M，SystemCoreClok/100）；5）InterruptConfig() 设置中断组为 0、优先级为 0；6）I2S3 中断服务程序调用 I2S_CODEC_DataTransfer() 重复播放 |
| 常见坑 / 注意 | SPI3/I2S3 部分管脚与 JTAG 共享（SPI3_NSS/I2S3_WS 与 JTDI，SPI3_SCK/I2S3_CK 与 JTDO），复位后被默认保留为 JTAG 用途；想把这些管脚配给 SPI3/I2S3，必须（调试时）关闭 JTAG 切到 SWD，或（标准应用时）同时关闭 JTAG 和 SWD；使用本程序 JTAG 接口不能使用，调试/烧录固件必须配置成 SWD 模式 |
| 依赖前置 | 串口1（见 4.5）；硬件资源小节标题误写为"SD卡读卡器实验要用到的硬件资源"（应为音频播放）；音频文件预先放置在处理器内部；现象需将耳机插入音频口（J2） |

**一句话作用**：初始化 D/A 芯片 PCM1770 后，读取处理器内部预置音频文件并判断是否合法，按文件格式设置 I2S3 参数，用 I2S3 中断方式循环播放到耳麦接口。

### 7.59 硬件CRC循环冗余检验实验（p.714-718）

| 字段 | 内容 |
|------|------|
| 外设/模块 | STM32 内置硬件 CRC 计算模块、串口（软件设计称串口2，实验现象称串口1，原文口径不一致） |
| 接口/总线 | AHB（CRC 时钟由 RCC_AHBPeriphClockCmd 使能） |
| 关键引脚 | 无（处理器内部硬件组件，不需要硬件电路） |
| 时钟使能 | RCC_AHBPeriphClockCmd()（使能 CRC 时钟） |
| 关键 API / 寄存器 | SZ_STM32_SysTickInit(1000)，RCC_AHBPeriphClockCmd()，CRC_ResetDR()；寄存器 CRC_CR（写 0x01 复位模块）、CRC_DR（写入数据字/读出结果） |
| 初始化步骤 | 1）初始化串口2；2）SZ_STM32_SysTickInit(1000) 初始化系统定时器；3）RCC_AHBPeriphClockCmd() 使能 CRC 时钟；4）while 循环内先 CRC_ResetDR 置初值；5）计算已知数据的 CRC 校验结果并打印 |
| 常见坑 / 注意 | CRC_ResetDR 置初值很关键，否则循环打印出的 CRC 结果一次和一次不同；读写虽都针对 CRC_DR，实际访问的是不同物理寄存器；输入数据始终以 32 位为单位，少于 32 位需低位补 0（也可高位补 0）；假定 DWORD 数组按小端存储；输入数据按最高位最先、最低位最后计算；多项式 X32+X26+X23+X22+X16+X12+X11+X10+X8+X7+X5+X4+X2+X+1 = 0x04C11DB7。示例：输入 0x44434241（内存 0x41,0x42,0x43,0x44）结果 0xCF534AE1；输入 0x41424344 结果 0xABCF9A63 |
| 依赖前置 | 串口打印（见"串口2 的 Printf 输出实验"）；下载调试按 3.2/3.3/3.5 |

**一句话作用**：调用 STM32 内置硬件 CRC 模块，循环计算一段已知数据的 32 位 CRC 校验结果（多项式 0x04C11DB7）并串口打印，验证每次结果一致。

### 7.60 PVD电源电压监测实验（p.719-723）

| 字段 | 内容 |
|------|------|
| 外设/模块 | PVD 可编程电压监测器（Power supply supervisor）、串口1 |
| 接口/总线 | 内部连接到外部中断第 16 线（EXTI16） |
| 关键引脚 | 无（处理器内部硬件组件，不需要硬件电路） |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | PWR_PVDLevelConfig(PWR_PVDLevel_2V9)，SZ_STM32_PVDConfiguration()；寄存器 PWR_CR 的 PLS[2:0]（选择监控电压阈值）、PVDE（使能 PVD）；PWR_CSR 的 PVDO（标志 VDD 高于/低于阈值） |
| 初始化步骤 | 1）初始化串口等；2）PWR_PVDLevelConfig(PWR_PVDLevel_2V9) 设置监控阈值（本实验为 2.9V，可改参数换阈值）；3）使能 PVD 相关寄存器与参数；4）SZ_STM32_PVDConfiguration() 配置使能外部中断第 16 线的上升/下降边沿触发；5）设置外部中断第 16 线中断优先级；6）循环打印提示信息等待 PVD 中断，中断处理函数中串口打印"Bye!" |
| 常见坑 / 注意 | 供电下降的阈值与上升的 PVD 阈值有固定差值 VPVDhyst（PVD 迟滞），引入该差值是为防止电压在阈值上下小幅抖动而频繁产生中断；用户可用关闭神舟开发板电源的方式来产生 PVD 中断 |
| 依赖前置 | 串口1；下载调试按 3.3/3.4/3.5 |

**一句话作用**：利用 STM32 内部 PVD 监测 VDD，当电压低于设定阈值（示例 2.9V）时经外部中断第 16 线产生 PVD 中断，在中断中串口打印"Bye!"模拟执行紧急处理任务。

### 7.61 STM32低功耗之--STANDBY待机模式（p.724-733）

| 字段 | 内容 |
|------|------|
| 外设/模块 | 电源管理（PWR）待机模式、RTC、LED 指示灯、USER2 按键、WAKEUP 按键 |
| 接口/总线 | 内部（PWR_CR / PWR_CSR 寄存器） |
| 关键引脚 | WK_UP 引脚（用作唤醒源，原文未给出具体引脚号）；USER2 按键走 EXTI3/EXTI_Line3（上拉输入，下降沿触发）；LED 指示灯 |
| 时钟使能 | 使能电源时钟（原文：因为要配置电源控制寄存器，所以必须先使能电源时钟；未给出具体 RCC 宏名） |
| 关键 API / 寄存器 | PWR_EnterSTANDBYMode()，PWR_WakeUpPinCmd(FunctionalState NewState)，PWR_GetFlagStatus()，PWR_ClearFlag(PWR_FLAG_SB)，EXTI_GetITStatus()，EXTI3_IRQHandler()；寄存器 PWR_CR 的 PDDS（深度睡眠时进入待机）、CWUF（清除唤醒位）、PWR_CSR 的 EWUP（使能 WKUP 引脚唤醒）、WUF（检查是否发生唤醒事件）；PWR_FLAG_SB = 0x00000002 |
| 初始化步骤 | 1）使能电源时钟；2）设置 PWR_CSR 的 EWUP 位，使能 WK_UP 引脚作为唤醒源；3）设置 PWR_CR 的 PDDS 位，通过 CWUF 位清除之前的唤醒位，执行 WFI 指令进入待机模式；4）编写 WK_UP 中断函数；5）KEY1 中断 EXTI3_IRQHandler 中调用 PWR_EnterSTANDBYMode() 进入待机 |
| 常见坑 / 注意 | 进入待机模式 JTAG 无法仿真和下载程序，若程序使系统一直进待机，可通过切换 BOOT 引脚用串口 ISP 下载程序、清除 FLASH 解决；从待机唤醒后除电源控制/状态寄存器（PWR_CSR）外所有寄存器被复位，代码执行等同于复位后（采样启动模式引脚、读取复位向量等）；退出待机有 4 种方式：外部复位 NRST、IWDG 复位、WKUP 引脚上升沿、RTC 闹钟事件；待机模式下所有 I/O 引脚处于高阻态（复位引脚始终有效、被设为防侵入/校准输出的 TAMPER 引脚、被使能的唤醒引脚除外） |
| 依赖前置 | 按键、LED 指示灯相关章节；RTC 时钟初始化；实验现象：按 USER2 进待机 LED 全灭，再按 WAKEUP 唤醒 LED 全部闪烁 |

**一句话作用**：通过设置 PWR_CR 的 PDDS 位并执行 WFI 让 STM32 进入最低功耗的待机模式（1.8V 内核断电、约 2uA），用 USER2 按键进入、WKUP 引脚上升沿唤醒退出。

### 7.62 STM32低功耗之--STOP停止模式实验（p.734-742）

| 字段 | 内容 |
|------|------|
| 外设/模块 | 电源管理（PWR）停止模式、RTC 闹钟、USER2 按键、LED 指示灯、串口 |
| 接口/总线 | 内部（PWR_CR 的 LPDS 位）；RTC 闹钟走外部中断第 17 线 |
| 关键引脚 | USER2 按键走 EXTI3/EXTI_Line3（上拉输入，下降沿触发）；RTC 闹钟走 EXTI_Line17；LED 指示灯 |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | PWR_EnterSTOPMode()，EXTI_Configuration()，SysTick_Configuration()，EXTI_GetITStatus()，EXTI_ClearITPendingBit()，PWR_GetFlagStatus()，PWR_ClearFlag()，RTC_GetITStatus()；寄存器 PWR_CR 的 LPDS（内部调节器低功耗）、WUF 唤醒位；EXTI_PR；PWR_FLAG_WU = 0x00000001 |
| 初始化步骤 | 1）初始化按键、LED 指示灯；2）RTC 中断、系统定时器初始化；3）EXTI_Configuration() 配置使能外部中断第 17 线的上升边沿触发（RTC 闹钟报警中断）；4）配置嵌套中断向量表与外部中断第 17 线优先级；5）循环调用 PWR_EnterSTOPMode 进入停止模式，等待按键中断或 RTC 闹钟中断唤醒 |
| 常见坑 / 注意 | 停止模式若进入前 ADC 和 DAC 没关闭，这些外设仍消耗电流，需把 ADC_CR2 的 ADON 位、DAC_CR 的 ENx 位置 0 关闭；退出停止模式时 HSI RC 振荡器被选为系统时钟；电压调节器处于低功耗模式时退出有额外启动延时，保持内部调节器开启则退出启动时间缩短但功耗增加；正在进行闪存编程或 APB 访问时，要等访问完成后系统才进入停止模式；停止模式下所有 I/O 引脚保持运行模式时的状态，SRAM 和寄存器内容被保留 |
| 依赖前置 | 按键、LED、串口相关章节；RTC 中断。实验现象：复位后 4 个 LED 闪烁，按 USER2 进停止模式 LED 停闪，一段时间后 RTC 闹钟报警或按 USER2 唤醒，4 个 LED 恢复闪烁 |

**一句话作用**：通过 PWR_EnterSTOPMode 让 STM32 进入停止模式（1.8V 区域时钟全停、SRAM 与寄存器保留），用 USER2 按键外部中断或 RTC 闹钟中断唤醒回到正常工作模式，用 LED 指示当前模式。

### 7.63 DMA传输实验（p.743-748）

| 字段 | 内容 |
|------|------|
| 外设/模块 | DMA1（通道4）、串口1（USART1_TX）、TFTLCD 彩屏、SysTick |
| 接口/总线 | AHB（DMA 控制器独立于 Cortex-M3 内核；STM32F103ZET6 有 DMA1、DMA2，本章仅介绍 DMA1） |
| 关键引脚 | 手册未明确 |
| 时钟使能 | 手册未明确（原文未给出 DMA1 时钟使能宏名） |
| 关键 API / 寄存器 | STM32_Shenzhou_COMInit()，MYDMA_Config()，USART_DMACmd()，MYDMA_Enable()，DMA_ClearFlag(DMA1_FLAG_TC4)，SZ_STM32_SysTickInit()；寄存器 DMA_ISR、DMA_IFCR、DMA_CCRx、DMA_CNDTRx、DMA_CPARx、DMA_CMARx；DMA1_Channel4 |
| 初始化步骤 | 1）STM32_Shenzhou_COMInit() 初始化串口1（波特率配置成 2400）；2）MYDMA_Config() 配置 DMA（使能 DMA 传输、传入串口1 使用的通道4 等）；3）将待发送数据送入开辟的数组 SendBuff[]；4）USART_DMACmd() 使能串口1 的 DMA 发送（配置 USART_CR3）；5）MYDMA_Enable() 开始一次 DMA 传输；6）DMA_ClearFlag(DMA1_FLAG_TC4) 清除通道4 传输完成标志，为下一次传输做准备 |
| 常见坑 / 注意 | DMA_ISR 为只读寄存器，位被置位后只能通过其他操作清除；DMA_IFCR 用来清除 DMA_ISR 对应位，通过写 0 清除（原文如此表述）；DMA_CNDTRx 设置范围 0~65535，其值随传输递减，为 0 表示本次传输全部完成，可据此知道传输进度；源和目标地址必须按数据传输宽度对齐；通道选择要参考参考手册（本实验 USART1_TX 对应通道4）。原文口径不一致：实验原理写串口2，软件分析写串口1/通道4，现象写串口1；数据量出现 5200、5168 两个值；代码分析3 误写"MYDMA_Config() 函数初始化内部温度传感器"（应为配置 DMA） |
| 依赖前置 | 串口实验；打开 \MDK-ARM\Project.uvproj 工程 |

**一句话作用**：利用 STM32 的 DMA1 通道4，把预置在 SendBuff[] 数组中的数据以 DMA 方式从存储器搬到串口1（USART1_TX）发送，实现不占用 CPU 的串口数据传输。

### 7.64 STM32内部温度传感器实验（p.749-753）

| 字段 | 内容 |
|------|------|
| 外设/模块 | STM32 内部温度传感器、ADC1（内部通道 ADC1_IN16）、串口、TFT 彩屏 |
| 接口/总线 | ADC（内部温度传感器固定连接在 ADC 的通道16 上） |
| 关键引脚 | 无（温度传感器在 STM32 内部与 ADC1_IN16 输入通道相连，不需外部设置） |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | STM32_Shenzhou_COMInit()，T_Adc_Init()，ADC_TempSensorVrefintCmd(ENABLE)，T_Get_Adc_Average(ADC_CH_TEMP,10)；寄存器 ADC_CR2（AWDEN 位 bit23 / TSVREFE，CR2_TSVREFE_Set = 0x00800000）、ADC_DR |
| 初始化步骤 | 1）对串口参数进行配置；2）STM32_Shenzhou_COMInit() 初始化串口；3）T_Adc_Init() 初始化内部温度传感器（调 ADC_TempSensorVrefintCmd() 开启内部温度传感器，并用 ADC_CR2 做使能指定 ADC 等设置）；4）通过通道 ADC1_IN16 多次取数求平均（T_Get_Adc_Average(ADC_CH_TEMP,10)）得到 ADC 值；5）按公式计算温度并串口打印 |
| 常见坑 / 注意 | 使用内部温度传感器必须先激活 ADC 内部通道（ADC_CR2 的 AWDEN 位 bit23 置 1，置 1 即启用）；温度传感器模拟输入推荐采样时间 17.1μs；支持温度范围 -40~125 度，精度较差约 ±1.5℃；V25 典型值 1.43，Avg_Slope 典型值 4.3mV/℃；12 位精度下 Vdd_convert_value=4096。计算公式：VSENSE=ADC_ConveredValue*Vdd/Vdd_convert_value(0xFFF)，温度={(V25-VSENSE)/Avg_Slope}+25 |
| 依赖前置 | ADC 实验（ADC 通道与 GPIO 对应表、ADC 设置）；打开 \MDK-ARM\Project.uvproj 工程 |

**一句话作用**：通过 ADC1 的内部通道16 读取 STM32 内部温度传感器的电压值，按公式换算成温度并串口打印（彩屏显示）。

### 7.65 温度传感器（DS18B20）实验（p.754-769）

| 字段 | 内容 |
|------|------|
| 外设/模块 | DS18B20 单总线数字温度传感器、LED、系统定时器、延时函数、串口 |
| 接口/总线 | 单总线（1-Wire，仅需一根口线即可双向通讯） |
| 关键引脚 | PA0（原文：把温度传感器的数据引脚接到了 PA0 上；电源与地引脚分别接到开发板的电源与地） |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | 温度传感器复位初始化函数、读取温度传感器数据函数、读一个字节数据函数、写函数（写数据到温度传感器）、温度转换与读取温度函数；命令：温度转换 44H、读暂存器 BEH、写暂存器 4EH、复制暂存器 48H、重新调 E2RAM B8H、读电源供电方式 B4H、Skip ROM CCH |
| 初始化步骤 | 1）初始化 LED、系统定时器、延时函数与串口；2）初始化温度传感器并检测，失败则串口打印"18b20 err"；3）操作协议：初始化 DS18B20（发复位脉冲）→发 ROM 功能命令→发存储器操作命令→处理数据；4）温度转换 3 步：初始化→ROM 操作指令（Skip ROM）→存储器操作指令（Convert T[44h] 启动温度变换）；5）读取温度 5 步：复位 DS18B20→发 Skip ROM 命令(CCH)→发 Read 命令(BEH)→读两字节温度→温度格式转换 |
| 常见坑 / 注意 | 单总线数据读写均在同一条线完成，对读写时序要求严格，需做较精确的延时；复位脉冲拉低 480~960us（程序用 750us）后拉高，等 15~60us（程序 15us）收到 60~240us 存在脉冲，若 x≠0 则初始化失败；读时序至少 60μs，两次独立读时序间至少 1μs 恢复时间，DS18B20 发出数据在起始时序后 15μs 内保证可靠有效，主机必须在 15μs 内读取数据线状态；写时隙至少 60us，相邻两写时隙至少 1us 恢复，15us 之内应将位送到总线；Vcc 与 I/O 线间接 4.7KΩ 上拉电阻；分辨率越高转换时间越长（9 位 93.75ms、10 位 187.5ms、11 位 275.00ms、12 位 750.00ms），需在分辨率与转换时间间权衡；出厂默认 12 位，用户可编程实现 9~12 位读数；测温范围 -55~125℃，固有分辨率 0.5℃，数据以 0.625℃/LSB 表示；配置寄存器 TM 位出厂为 0，用户不要改动；支持多点组网（多个 DS18B20 并联于唯一三线） |
| 依赖前置 | LED、串口、系统定时器与延时函数相关章节 |

**一句话作用**：用单总线（1-Wire）协议驱动 DS18B20 数字温度传感器，经初始化/ROM 指令/存储器指令完成温度转换并读取温度，通过串口（或数码管/彩屏）显示当前环境温度。

### 7.66 DHT11数字温湿度传感器实验（p.770-773）

| 字段 | 内容 |
|------|------|
| 外设/模块 | DHT11 数字温湿度传感器（湿温度一体化）、串口 |
| 接口/总线 | 单总线（单个数据引脚完成输入输出双向传输） |
| 关键引脚 | PA11（原文接线表：3.3V↔VDD 红色供电、PA11↔DATA 蓝色串行数据单总线、GND↔NC 白色悬空、GND↔GND 绿色接地） |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | 手册未明确（本节未给出具体函数名/寄存器） |
| 初始化步骤 | 手册未明确（原文只给出发送流程时序）：主机发送开始信号——拉低数据线保持 t1（至少 18ms）→拉高数据线 t2（20~40us）→读取 DHT11 响应（正常则 DHT11 拉低数据线保持 t3（40~50us）作为响应信号，再拉高保持 t4（40~50us）后开始输出数据） |
| 常见坑 / 注意 | DHT11 与 MCU 一次通信最大约 3ms，建议主机连续读取时间间隔不要小于 100ms；数据 40Bit 一次性传给单片机，采用校验和方式校验；工作电压范围 3.3V-5.5V，工作电流平均 0.5mA（5V 下平均最大电流 0.5mA）；测量范围湿度 20~90%RH、温度 0~50℃；精度湿度±5%、温度±2℃；分辨率湿度 1%、温度 1℃ |
| 依赖前置 | 串口（连电脑超级终端显示温湿度）。数据格式：8bit 湿度整数+8bit 湿度小数+8bit 温度整数+8bit 温度小数+8bit 校验和，校验和=前四个字节相加 |

**一句话作用**：通过单总线读取 DHT11 输出的 40Bit 温湿度数据（含校验和），解析出湿度与温度值并串口显示。

### 7.67 三轴加速度模块实验（p.774-785）

| 字段 | 内容 |
|------|------|
| 外设/模块 | ADXL345 三轴加速度计（ADI，超低功耗 3 轴，13 位分辨率，±2/4/8/16g 量程，3.9mg/LSB，自带 32 级 FIFO）、串口 |
| 接口/总线 | I2C（2 线式；CS 引脚拉高至 VDD I/O 进入 I2C 模式；支持标准 100kHz 与快速 400kHz） |
| 关键引脚 | PB6=SCL（时钟线）、PB7=SDA（数据线），用 2 个普通 I/O 口模拟 I2C；模块 3.3V、GND 由板子供电 |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | ADXL345_Init()，ADXL345_RD_XYZ()，ADXL345_AUTO_Adjust()，ADXL345_Get_Angle()；寄存器 DATA_FORMAT、BW_RATE、POWER_CTL、INT_ENABLE、OFSX/OFSY/OFSZ |
| 初始化步骤 | 1）串口初始化；2）ADXL345_Init() 初始化模块（采用查询方式读数据，未开启中断，3 个偏移寄存器默认设为 0），按返回值判断模块 ID 是否正确（成功返回 0，失败返回 1）；3）ID 正确为 229（十六进制 0xE5），失败则串口打印读到的 ID 并循环；4）读模块速度原始值与角度值并串口打印；5）ADXL345_AUTO_Adjust() 校准 |
| 常见坑 / 注意 | ADXL345 初始化步骤：上电→等待 1.1ms→初始化命令序列→结束（最简只需配置 3 个寄存器）；I2C 地址由 SDO/ALT ADDRESS 决定：接地=0x53（写 0xA6、读 0xA7），接高=0x1D（写 0x3A、读 0x3B）；未使用的引脚无内部上拉/下拉，CS 或 ALT ADDRESS 悬空时无已知/默认状态，使用 I2C 时 CS 必须接 VDD I/O、ALT ADDRESS 必须接任一 VDD I/O 或接地；不进行校准读数可能有些偏差，通过校准可减少甚至消除；检测重力时当检测轴方向与重力方向相反时检测到正加速度 |
| 依赖前置 | 串口；IIC 通信章节；模块详细资料参考模块用户手册。模块引脚↔开发板：3.3V↔3.3V、SCL↔PB6、SDA↔PB7、GND↔GND |

**一句话作用**：用 PB6/PB7 模拟 I2C 驱动 ADXL345 三轴加速度计，读取 X/Y/Z 三方向加速度值并换算为与自然坐标系的角度，串口打印输出。
