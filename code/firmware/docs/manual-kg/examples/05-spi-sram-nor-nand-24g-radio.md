# 第7章 例程：SPI FLASH / SRAM / 内存管理 / Nor / Nand / 2.4G / 收音机

> 手册 p.527-601

### 7.29 SPI FLASH读写实验（p.527-536）

| 字段 | 内容 |
|------|------|
| 外设/模块 | SPI1（主模式）、板载 SPI FLASH W25X16（华邦，16M bit / 2M 字节） |
| 接口/总线 | SPI1（原文：神舟III号硬件上使用 SPI 接口的有触摸屏、音频 DA 芯片 PCM1770、W25X16、ENC28J60 以太网芯片以及 2.4G 无线模块） |
| 关键引脚 | 手册未明确（正文仅给出 W25X16 芯片管脚定义图，未列出与 STM32 连接的具体 GPIO） |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | SPIx_init()（初始化 SPI1 为主模式读取 FLASH），SPI 接口读写 FLASH 一个字节的函数，获取 SPI FLASH ID 函数，SPI FLASH 读函数，SPI FLASH 写函数；寄存器 SPI_CR1（CPOL、CPHA、DFF、LSBFIRST、MSTR、SPE、SSM、SSI、SSOE），SPI_CR（CPOL/CPHA） |
| 初始化步骤 | 1）配置 SPI 串行时钟波特率；2）定义数据传输和串行时钟间的相位关系（CPOL/CPHA）；3）设置 8 位或 16 位数据帧格式；4）NSS 引脚：输入模式时（硬件模式接高电平，软件模式设置 SSM 和 SSI 位），输出模式时设置 SSOE 位；5）设置 MSTR 位和 SPE 位（仅当 NSS 接高电平这些位才能保持置位） |
| 常见坑 / 注意 | W25X16 与网口 ENC28J60 共用 SPI1，不可同时拉低 CS；W25X16 最少擦除单位为一个扇区（4K 字节），需为它开辟至少 4K 的缓存区；改变 CPOL/CPHA 位之前必须清除 SPE 位将 SPI 禁止；主和从必须配置成相同的时序模式；SCK 空闲状态必须与 SPI_CR1 指定的极性一致；数据帧格式（8/16 位）由 SPI_CR1 的 DFF 位选择 |
| 依赖前置 | 串口、点灯、按键配置与按键扫描例程（主程序上电初始化后读 Flash ID 判在位，按键1写、按键2读）；JLINK V8 或 MDK 下载 |

**一句话作用**：用 STM32 的 SPI1（主模式）读写板载 SPI FLASH W25X16，上电读 Flash ID 判在位，按键1把字符组"神舟III号 SPI 读写访问程序"写入 Flash，按键2读回并通过串口显示。

### 7.30 SRAM 访问实验（p.537-548）

| 字段 | 内容 |
|------|------|
| 外设/模块 | FSMC（Nor Flash 控制器）、板载 SRAM IS62WV25616（正文另处写作 IS61LV25616，16 位宽 256K，即 512K 字节） |
| 接口/总线 | FSMC Bank1 子区域3（硬件上与 FSMC_NE3 相连） |
| 关键引脚 | A[0:18] 接 FSMC_A[0:18]；D[0:15] 接 FSMC_D[0:15]；UB 接 FSMC_NBL1；LB 接 FSMC_NBL0；OE 接 FSMC_NOE；WE 接 FSMC_NWE；CE 接 FSMC_NE3 |
| 时钟使能 | RCC_AHBPeriphClockCmd(RCC_AHBPeriph_FSMC, ENABLE)；并使能各 IO 组的时钟 |
| 关键 API / 寄存器 | FSMC_NORSRAMInit()，FSMC_NORSRAMCmd()，RCC_AHBPeriphClockCmd()；结构体 FSMC_NORSRAMInitTypeDef |
| 初始化步骤 | 1）使能 FSMC 时钟，并把 FSMC_D0~15、FSMCA0~18 等相关 IO 配置为复用输出并各 IO 组时钟使能；2）设置 FSMC BANK1 区域3（模式 A、16 位宽、读写共用一个时序寄存器），调用 FSMC_NORSRAMInit()；3）使能 BANK1 区域3，调用 FSMC_NORSRAMCmd() |
| 常见坑 / 注意 | 使用 BANK1 的区域3，HADDR[27:26]=10，故外部内存首地址为 0X68000000；因 IS62WV25616 读写时间基本一致，设置读写相同时序（与 LCD 章节读写不同时序相反）；SRAM 数据宽度为 16 位；原文称 SRAM 容量 512K 字节，但另处又写作 IS61LV25616 4M 比特，存在不一致；原文"挂到 FSMC 总线的 Bank 3 上"与"BANK1 子区域3"表述混用 |
| 依赖前置 | FSMC 详细配置见 LCD 实验章节；串口1（打印提示与结果）、LED 指示灯（DS1 闪烁=执行中，DS2 亮=读写一致，DS3 亮=读写不一致） |

**一句话作用**：用 STM32 的 FSMC Bank1 子区域3 访问板载 IS62WV25616 SRAM，往固定地址写入一串确定值再读回校验，通过串口和 LED 指示访问成功/失败。

### 7.31 内存管理（p.549-554）

| 字段 | 内容 |
|------|------|
| 外设/模块 | 外部 SRAM（内存池2）、内部 SRAM（内存池1）、按键、串口、TFT 彩屏 |
| 接口/总线 | 外部内存池首地址 0x68000000（外部 SRAM）；分块式内存管理（内存池 + 内存管理表） |
| 关键引脚 | 手册未明确（按键、串口、TFT、外部 SRAM 原理图见对应例程） |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | mymalloc()（内存申请）、myfree()（内存释放），FSMC_SRAM_Init()，LCD_Init()，KEY_Init()，__align(4)；宏 MEM1_BLOCK_SIZE、MEM2_BLOCK_SIZE、MEM1_ALLOC_TABLE_SIZE、MEM2_ALLOC_TABLE_SIZE |
| 初始化步骤 | 1）LCD_Init() 初始化 TFT 彩屏；2）KEY_Init() 初始化按键；3）FSMC_SRAM_Init() 初始化外部 SRAM；4）定义内存池与等大小的内存管理表（分配方向从顶到底，初始化时管理表全清零）；5）按键 USER1 申请内存、TAMPER 释放内存、WAKEUP 切换使用外部/内部 SRAM |
| 常见坑 / 注意 | 内存池必须 __align(4) 定义为 4 字节对齐，否则分配内存给结构体指针等情况下可能出错；内存块大小 MEM1_BLOCK_SIZE / MEM2_BLOCK_SIZE 均为 32 字节，内部内存池 40K、外部内存池 200K（最大支持近 1M 字节）；内存分块越小管理表越大（分块 2 字节时管理表与内存池一样大）；对一个指针多次申请而不释放会造成"内存泄露" |
| 依赖前置 | SRAM 访问实验（7.30，FSMC_SRAM_Init）；按键、串口、TFT 彩屏例程 |

**一句话作用**：用分块式内存管理（mymalloc/myfree）动态管理内部 40K 与外部 200K 内存池，按键申请/释放内存，TFT 彩屏显示内外内存使用率并演示内存泄露现象。

### 7.32 Nor Flash访问程序实验（p.555-566）

| 字段 | 内容 |
|------|------|
| 外设/模块 | FSMC（Nor Flash 控制器）、板载 Nor Flash SST39VF1601（正文另处写作 SST39VF160，16M 比特） |
| 接口/总线 | FSMC Bank1 子区域2（硬件上与 FSMC_NE2 相连） |
| 关键引脚 | FSMC_D0~D15（16 位数据线）；CE 接 FSMC_NE2 |
| 时钟使能 | 手册未明确（使用前需对 FSMC BANK 进行参数配置并初始化 FSMC 引脚） |
| 关键 API / 寄存器 | FSMC_NORSRAMInit()，FSMC_NORSRAMCmd()；结构体 FSMC_NorSRAMTimingInitTypeDef（对应 FSMC_BTR、FSMC_BWTR），FSMC_NORSRAMInitTypeDef（对应 FSMC_BCR） |
| 初始化步骤 | 1）确定映射地址空间（Nor Flash 只能选用 BANK1 中的 4 个子 BANK）；2）配置存储器基本特征：存储器类型 MTYPE、地址/数据是否复用 MUXEN、数据线宽度 MWID、同步突发 BURSTEN、NWAIT 特性 WAITEN/WAITCFG/WAITPOL、是否读写相同时序 EXTMOD；3）配置存储器时序参数：同步（CLKDIV 分频、DATLAT 等待延迟）或异步（ADDSET 地址建立、DATAST 数据建立、ADDHLD 地址保持） |
| 常见坑 / 注意 | Nor Flash 只能选用 BANK1 中的 4 个子 BANK；擦除 Nor 以 64~128KB 块进行，执行一次写入/擦除约 5s（Nand 仅 4ms）；Nor 擦除前需先把目标块内所有位写为 0；Nor 擦写次数约十万次（Nand 为百万次）；Nor 带 SRAM 接口、可直接运行代码 |
| 依赖前置 | SRAM 访问实验（7.30，FSMC 配置类似）；串口1（打印提示与结果）、LED 指示灯（DS1 闪烁=执行中，DS2 亮=读写一致，DS3 亮=读写不一致） |

**一句话作用**：用 STM32 的 FSMC Bank1 子区域2 访问板载 SST39VF160 Nor Flash，往固定地址写入一串确定值再读回校验，通过串口和 LED 指示访问成功/失败。

### 7.33 Nand Flash访问实验（p.567-578）

| 字段 | 内容 |
|------|------|
| 外设/模块 | FSMC（NAND 控制器）、板载 Nand Flash HY27UF081G2A（Hynix，1G 比特） |
| 接口/总线 | FSMC BANK2（正文"硬件上与 FSMC_NE2 相连"） |
| 关键引脚 | AL→ALE/A17→PD11（地址锁存信号）；CL→CLE/A16→PD12（命令锁存使能）；I/O0~7→D0~7→端口D/端口E（数据总线 D0~7）；/E→NCE2→PD7（片选使能）；/R→NOR→PD4（输出使能）；/W→NWE→PD5（写使能）；R/B→NWAIT/INT2→PD6/PG6（就绪/繁忙信号） |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | FSMC_NAND_PCCARDTimingInitTypeDef（对应时间参数寄存器）；FSMC 初始化结构体（原文此处误写作 FSMC_NorSRAMinitTypeDef/FSMC_BCR，实为 Nand 初始化）；Nand Flash ID 读取 |
| 初始化步骤 | 1）配置 NAND 存储器特征：开启/关闭 R/B 信号作 FSMC 输入等待、作中断输入源（上升沿/下降沿/高电平触发）、数据总线宽度 8/16 位、ECC 计算开关、ECC 页大小（256/512/1024/2048/4096/8192 字节）；2）配置时序（公共段和属性段）：建立时间、等待时间、保持时间、数据总线高阻时间；3）初始化 FSMC 引脚（只初始化与 Nand 连接的相关引脚）；4）使能 BANK2。地址建立时间 0x1、地址保持时间 0x3、数据建立时间 0x2、数据总线高阻时间 0x1 |
| 常见坑 / 注意 | Nand 数据线和地址线复用，操作需特别的访问控制协议；FSMC NAND 存储块划分为数据段、地址段、命令段三段；根据 Nand 构造通常需要 4~5 个写地址段才能写入一个读写操作的地址；R/B 信号接 NWAIT 时用等待功能管理，接 INT2 时作中断源；Nand 存在坏块与位交换问题，供应商建议使用 EDC/ECC 算法；ECC 页大小配置为 2048 字节 |
| 依赖前置 | Nor Flash 访问实验（FSMC 配置）；串口（打印 Nand Flash ID 与结果）、LED（DS1 闪烁=执行中，DS2 亮=读写一致，DS3 亮=读写不一致，DS4 亮=未读到 ID 或 ID 不正确） |

**一句话作用**：用 STM32 的 FSMC BANK2 访问板载 HY27UF081G2A Nand Flash，先读取 Nand Flash ID 并打印，成功后往固定地址写入一串确定值再读回校验，通过串口和 LED 指示结果。

### 7.34 2.4G无线模块实验（p.579-586）

| 字段 | 内容 |
|------|------|
| 外设/模块 | nRF24L01/nRF24L01+ 2.4G 无线模块、SPI2 |
| 接口/总线 | SPI2（SPI2_SCK/SPI2_MISO/SPI2_MOSI）；PC5 作 2.4G 模块的 SPI2 接口 CS，PF10 作模块选择信号 CSN |
| 关键引脚 | SCK→PB14（SPI2_SCK，PCB 脚5）；MISO→PB13（SPI2_MISO，脚7）；MOSI→PB15（SPI2_MOSI，脚6）；CE→PC5（脚3）；CSN→PF10（脚4）；IRQ→PG15（脚8） |
| 时钟使能 | 手册未明确（LED GPIO 配置为推挽输出、按键 GPIO 配置为上拉输入、串口1 波特率 115200） |
| 关键 API / 寄存器 | NRF24L01_Init()（初始化 CE、SPI CS 及 SPI2 接口），SPI2_Init()，RX_Mode()，Tx_Mode()，NRF24L01_RxPacket()，NRF24L01_TxPacket() |
| 初始化步骤 | 1）初始化串口、LED、按键；2）NRF24L01_Init() 完成无线模块接口初始化（分配 CE 信号、SPI CS 信号及 SPI2 接口初始化）；3）SPI2_Init() 配置 SPI2 时钟频率和工作模式；4）RX_Mode/Tx_Mode 设置无线模块工作模式；5）NRF24L01_RxPacket/NRF24L01_TxPacket 启动收发流程 |
| 常见坑 / 注意 | SPI2 上除 2.4G 模块 nRF24L01 外还接音频 DA 芯片 PCM1770 及 TFT 触摸芯片，SPI2 本身只有一根 CS，故用 PC5 作为 nRF24L01 的 SPI2 接口 CS 信号；PG15 除接 nRF24L01 中断输出外还与 LCD 模块上 SD 卡片选信号相连，2.4G 无线模块与屏上的 SD 卡不能同时使用；发送时需在 SPI 片选 CSN 为低时连续写入数据，使能管脚置高并保持 10uS 以上，延迟 130uS 后发送数据 |
| 依赖前置 | 需两块带 2.4G 无线模块的神舟III号开发板；SPI FLASH（W25X16）例程（SPI2 初始化与 SPI1 基本相同，区别在工作模式和工作频率）；串口1、LED、按键例程；用随板杜邦线连接 2.4G 模块与板上扩展座 |

**一句话作用**：用 STM32 的 SPI2 控制 nRF24L01 无线模块，一块板按 USER1 设为接收模式、另一块按 USER2 设为发送模式，实现点对点无线收发并串口打印。

### 7.35 收音机实验（p.587-601）

| 字段 | 内容 |
|------|------|
| 外设/模块 | PHILIPS TEA5767 / TEA5767HL FM 收音模块、I2C1、串口1 |
| 接口/总线 | 软件模式 I2C（PB6/PB7） |
| 关键引脚 | PB6、PB7（STM32 本身的 I2C 总线 GPIO 管脚，本例程用软件模式 I2C 访问模块）；LED 用 PF6~9；USART1_TX/USART1_RX |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | GPIO_Configuration()，NVIC_Configuration()（配置串口中断模式），USART1_IRQHandler()，SetPLL()；Tx_Buffer/TxBuffer（TEA5767 模式设置） |
| 初始化步骤 | 1）NVIC_Configuration() 配置串口中断；2）GPIO_Configuration()：PB6/PB7 初始化为开漏输出，PF6~9 初始化为推挽输出，USART1_TX 复用推挽输出、USART1_RX 复用开漏输入；3）串口1 初始化（选择中断模式接收键盘数据）；4）TEA5767 模式设置（Tx_Buffer 设置静音、处于搜台模式）；5）SetPLL() 按 FM_FREQ 计算 PLL 值并依据 rec_f 标志启动搜台或只设置频率 |
| 常见坑 / 注意 | TEA5767 芯片本身通过三线接口访问（WRITE/READ、CLOCK、DATA），WRITE/READ 管脚上升沿表示写、下降沿表示读；写操作和读操作都是 5 个字节连续；播放选定频道之前需要先搜台；收音机模块 ANT 管脚通过电容与音频座相连，耳机插入音频座后作为收音天线使用；串口按键 S 开始搜台、1P 选择第一个频道 |
| 依赖前置 | 串口1（控制与提示信息，采用串口中断模式）、LED、流水灯/GPIO 例程；TEA5767 收音机模块；耳机插入 J27（收音机音频接口） |

**一句话作用**：通过软件模拟 I2C（PB6/PB7）访问 TEA5767 FM 收音模块，用串口1 控制收音模块自动搜台并选择频段，将收到的音乐通过耳机播放。
