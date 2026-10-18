# 第7章 例程：唯一ID / ADC / DAC / RTC / EEPROM / FLASH

> 手册 p.388-451

### 7.13 产品唯一身份标识（Unique Device ID）实验（p.388-391）

| 字段 | 内容 |
|------|------|
| 外设/模块 | 处理器内部系统存储区（96 位产品唯一身份标识 Unique Device ID）；闪存容量寄存器 |
| 接口/总线 | 串口 2（仅用于打印输出） |
| 关键引脚 | 无（原文明确：为处理器内部组件，这部分不需要硬件电路）；串口 2 使用需 J14 和 12 位置短接 2=3 为串口模式 |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | Get_ChipSerialNum()，Printf，Delay_ARMJISHU()；地址 0x1FFFF7E8~0x1FFFF7F3（96 位 ID），0x1FFFF7E0（闪存容量寄存器） |
| 初始化步骤 | 1）调用 Get_ChipSerialNum() 读取 96 位 ID；2）用 Printf 打印唯一标识 DeviceSerial；3）扩展：读取 0x1FFFF7E0 闪存容量寄存器；4）用 Printf 打印内部 Flash 大小 |
| 常见坑 / 注意 | 96 位 ID 对任意 STM32 在任何情况下都唯一，用户不能修改；0x1FFFF7E8 中为最低字节，0x1FFFF7F3 中为最高字节（原文此处写作 0x1FFFFF3，疑为笔误） |
| 依赖前置 | 串口（串口 2）输入输出例程 |

**一句话作用**：读取处理器内部 96 位产品唯一身份标识（并顺带读取闪存容量寄存器），通过串口打印出来。

### 7.14 ADC 模数转换实验（p.392-400）

| 字段 | 内容 |
|------|------|
| 外设/模块 | ADC1（通道 13）、DMA1 |
| 接口/总线 | ADC1 挂 APB2；DMA1_Channel1；ADC 输入时钟由 PCLK2 分频产生 |
| 关键引脚 | PC3（ADC1_IN13，接板载电位器 R51 滑片输出） |
| 时钟使能 | 开启 PC 口时钟、开启 DMA 时钟、开启 ADC1 时钟 |
| 关键 API / 寄存器 | SZ_STM32_ADC_Configuration()，ADC_GPIO_Configuration()，GetADCConvertedValue()，SZ_STM32_LEDInit()，SZ_STM32_COMInit()；寄存器 ADC_CR、ADC_SMPR、ADC_SQR、ADC_DR（ADC1 基地址 0x40012400，ADC_DR 偏移 0x4C）；DMA1_Channel1 配置 |
| 初始化步骤 | 1）开启 PC 口时钟，设置 PC3 为模拟输入模式；2）开启 DMA 时钟，配置 DMA；3）开启 ADC1 时钟，配置 ADC1 参数；4）设置通道组（转换顺序、转换时间）；5）使能 ADC 并校准；6）读取 ADC 值 |
| 常见坑 / 注意 | ADC 输入时钟不得超过 14MHz，并由 PCLK2 经分频产生；转换多个规则通道时必须使用 DMA，否则 ADC_DR 中已存数据会丢失；ADC 采集一次数据不准确，要多采几次；规则通道转换结束时才产生 DMA 请求，将数据从 ADC_DR 传至目标地址；ADC 结果 12 位（原文称最小 0x0000、最大 0xFFFF，实为 12 位范围） |
| 依赖前置 | LED 指示灯、串口例程；需板载电位器 R51（10K） |

**一句话作用**：读取 PC3 管脚（电位器）输入的模拟信号，经 ADC1 转换后换算成电压值并通过串口打印。

### 7.15 ADC 数据多路采集（p.401-401）

| 字段 | 内容 |
|------|------|
| 外设/模块 | 手册未明确（仅有小节标题 7.15.1~7.15.6，正文内容缺失） |
| 接口/总线 | 手册未明确 |
| 关键引脚 | 手册未明确 |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | 手册未明确 |
| 初始化步骤 | 手册未明确 |
| 常见坑 / 注意 | 手册未明确 |
| 依赖前置 | 手册未明确（原文该小节仅列出 7.15.1 意义与作用 / 7.15.2 实验原理 / 7.15.3 硬件设计 / 7.15.4 软件设计 / 7.15.5 下载与验证 / 7.15.6 实验现象 六个标题，各标题下均无内容） |

**一句话作用**：手册该小节仅有标题、无正文内容，无法提炼。

### 7.16 DAC 数模转换实验（p.401-407）

| 字段 | 内容 |
|------|------|
| 外设/模块 | DAC（DAC1/DAC2，双通道）、TIM2（定时器 2）、DMA2 |
| 接口/总线 | 定时器 2 触发 DAC；DMA2_Channel4；DAC 数据保持寄存器 DAC_DHR12RD（地址 0x40007420） |
| 关键引脚 | PA4（DAC 输出通道 1，DAC_OUT1）、PA5（DAC 输出通道 2，DAC_OUT2） |
| 时钟使能 | 使能 GPIOA 端口时钟 |
| 关键 API / 寄存器 | DAC_Config()，DAC_TIM_Config()，DAC_DMA_Config()，DAC_Init()，DAC_Cmd()，TIM_SelectOutputTrigger()，TIM_Cmd()，DMA_Init()，DMA_Cmd()；寄存器 DAC_DHR12RD；结构体成员 DAC_Trigger（DAC_Trigger_T2_TRGO）、DAC_WaveGeneration（DAC_WaveGeneration_None）、DAC_LFSRUnmask_TriangleAmplitude（DAC_LFSRUnmask_Bit0）、DAC_OutputBuffer（DAC_OutputBuffer_Disable）、DMA_PeripheralBaseAddr、DMA_MemoryBaseAddr、DMA_DIR、DMA_BufferSize、DMA_PeripheralInc、DMA_MemoryInc、DMA_PeripheralDataSize、DMA_MemoryDataSize、DMA_Mode（DMA_Mode_Circular） |
| 初始化步骤 | 1）使能 GPIOA 时钟，设置 PA4、PA5 为模拟输入模式；2）配置 DAC 结构体（触发源 T2_TRGO、不使用波形发生器、不使用输出缓冲）；3）调用 DAC_Init()、DAC_Cmd() 初始化并使能 DAC_Channel_1、DAC_Channel_2；4）配置定时器 2（向上计数，重载寄存器赋值 19，TIM_SelectOutputTrigger 选择触发方式，TIM_Cmd 使能）；5）配置 DMA2_Channel4（外设地址 DAC_DHR12RD_Address、内存地址 &DualSine12bit、方向内存到外设、缓存大小 32、外设地址不变内存递增、16 位数据宽度、循环模式），DMA_Init() 后 DMA_Cmd(DMA2_Channel4, ENABLE)；6）用 for 循环填充正弦波数据 DualSine12bit |
| 常见坑 / 注意 | 使能 DACx 通道后相应 GPIO（PA4/PA5）会自动与 DAC 模拟输出相连，设置为输入是为了避免额外干扰；使用了 DAC_Channel1 和 DAC_Channel2，但 DMA 代码中只配置了 DAC_Channel2（原因见 ST 提供的手册说明） |
| 依赖前置 | 定时器、DMA 相关例程；验证需用杜邦线把 PA4、PA5 接到逻辑分析仪的通道 0 和通道 1 |

**一句话作用**：使用双 DAC、定时器 2 触发、DMA 传输正弦波数据，从 PA4、PA5 输出两路模拟正弦波。

### 7.17 RTC 实时时钟实验（p.408-419）

| 字段 | 内容 |
|------|------|
| 外设/模块 | RTC、BKP（后备寄存器）、PWR（电源接口）、NVIC |
| 接口/总线 | APB1（RTC 的 APB1 接口）；RTC 模块与 RCC_BDCR 处于后备区域 |
| 关键引脚 | 外部 32.768KHz 晶振（原文：外部增加 32.768KHz 晶振电路驱动 RTC）；VBAT（CR1220 纽扣电池 BT1 与 VCC3.3 混合供电） |
| 时钟使能 | 设置 RCC_APB1ENR 的 PWREN 和 BKPEN 位（使能电源和后备接口时钟）；设置 PWR_CR 的 DBP 位（使能对后备寄存器和 RTC 的访问） |
| 关键 API / 寄存器 | RTC_Configuration()，RTC_GetCounter()，Time_Display()，Time_Show()，Time_Adjust()，Time_Regulate()，USART_Scanf()，InterruptConfig()，NVIC_Configuration()，NVIC_PriorityGroupConfig()，RCC_ClearFlag()，RTC_IRQHandler()；寄存器 RTC_CR、RTC_ALR、RTC_CRL（RSF 位）、RCC_BDCR、RCC_APB1ENR、PWR_CR |
| 初始化步骤 | 1）使能电源和后备接口时钟（RCC_APB1ENR 的 PWREN、BKPEN）；2）设置 PWR_CR 的 DBP 位使能后备寄存器和 RTC 访问；3）配置 RTC 时钟源与预分频（RTC_Configuration）；4）配置 NVIC 中断优先级并使能秒中断；5）Time_Adjust() 设置初始时间；6）Time_Show() 中每秒打印一次系统时间 |
| 常见坑 / 注意 | RTC 时钟频率必须小于 PCLK1 时钟频率的四分之一以上；RTC 核心（预分频器、闹钟、计数器、分频器）只能由后备域复位，APB1 接口由系统复位；读取 RTC 寄存器时若 APB1 接口曾处于禁止状态，软件必须先等待 RTC_CRL 的 RSF 位被硬件置 1（否则可能读到 0）；首次运行串口输出 "RTC not yet configured...."，已有设置时输出 "No need to configure RTC...." |
| 依赖前置 | LED 指示灯、串口（串口 2）输入输出例程；硬件需 32.768KHz 晶振与 VBAT 供电电池 |

**一句话作用**：配置 RTC 实时时钟并每秒通过串口打印一次当前系统时间，实现掉电/复位后时间维持。

### 7.18 Calendar 实时时钟与农历年月日实验（p.420-431）

| 字段 | 内容 |
|------|------|
| 外设/模块 | RTC、BKP（后备寄存器）、PWR（电源接口）、NVIC、SysTick |
| 接口/总线 | APB1（RTC 的 APB1 接口）；RTC 模块与 RCC_BDCR 处于后备区域 |
| 关键引脚 | 外部 32.768KHz 晶振；VBAT（CR1220 纽扣电池 BT1 与 VCC3.3 混合供电） |
| 时钟使能 | 设置 RCC_APB1ENR 的 PWREN 和 BKPEN 位；设置 PWR_CR 的 DBP 位 |
| 关键 API / 寄存器 | SZ_STM32_LEDInit()，SZ_STM32_SysTickInit()，SZ_STM32_COMInit()，NVIC_GroupConfig()，RTC_NVIC_Configuration()，BKP_ReadBackupRegister(BKP_DR1)，BKP_WriteBackupRegister()，Time_Adjust()，Time_Regulate()，Time_Display()，RTC_GetCounter()，mktimev()，RTC_IRQHandler()；寄存器 RTC_CR、RTC_ALR、RTC_CRL（RSF 位）、RCC_BDCR；源文件 calendar.c、calendar.h、date.c |
| 初始化步骤 | 1）SZ_STM32_LEDInit() 初始化 LED；2）SZ_STM32_SysTickInit() 初始化 SysTick；3）SZ_STM32_COMInit() 初始化串口 2；4）NVIC_GroupConfig() 配置中断优先级分组，RTC_NVIC_Configuration() 配置 RTC 中断；5）通过 BKP_ReadBackupRegister(BKP_DR1) 判断是否为 0xA5A5，未设置则 Time_Adjust() 设时间并写后备寄存器，已设置则打印复位原因并使能秒中断；6）while 循环每秒显示当前系统时钟（含农历年月日、节气、公历） |
| 常见坑 / 注意 | RTC 时钟频率必须小于 PCLK1 时钟频率的四分之一以上；RTC 核心只能由后备域复位；读 RTC 寄存器时若 APB1 接口曾被禁止，需先等待 RTC_CRL 的 RSF 位置 1；首次上电用 BKP_DR1 写入 0xA5A5 标记已配置；进入 if/else 判断前需清除上次复位原因记录，否则打印的复位原因不变；日历以 1970 年为元年，32bit 时间寄存器可运行到 2100 年左右 |
| 依赖前置 | 7.17 RTC 实时时钟实验；硬件需 32.768KHz 晶振与 VBAT 供电电池 |

**一句话作用**：在 RTC 每秒显示时分秒的基础上，增加 Calendar 农历年月日、节气与公历日期的计算和显示。

### 7.19 EEPROM 读写测试实验（p.432-447）

| 字段 | 内容 |
|------|------|
| 外设/模块 | I2C1、板载 EEPROM 24C02（容量 2Kbit = 256 字节） |
| 接口/总线 | I2C1（原文明确：通过处理器自带硬件 I2C1 接口与 EEPROM 24C02 连接）；串口 2 用于打印 |
| 关键引脚 | PB6（I2C1_SCL）、PB7（I2C1_SDA）；A0~A2 全部接地，对应 I2C 硬件地址 0xA0；WP（第 7 脚）接地禁止写保护；SDA/SCL 各接 5.1K 上拉电阻 |
| 时钟使能 | 手册未明确（原文未列出 I2C1 时钟使能语句，仅说明将 I2C1 接口初始化为复用功能开漏输出） |
| 关键 API / 寄存器 | I2C_WriteS_24C()（页写入，页大小 8），I2C_ReadS_24C()（页读出，页大小 8）；寄存器 I2C_CR1（CR1_START_Set=0x0100、CR1_START_Reset=0xFEFF、CR1_STOP_Set=0x0200、CR1_STOP_Reset=0xFDFF）；I2C 模式/SMBA 模式、I2C 主机地址、I2C 速率等初始化项 |
| 初始化步骤 | 1）地址数据定义；2）初始化（LED 初始化、I2C1 接口初始化为复用功能开漏输出、设置 I2C 工作模式/主机地址/速率）；3）将 0x00~0xFF 顺序写入 EEPROM；4）依次读出并通过串口打印；5）写入与读出数据比较，相同则测试通过，反之失败 |
| 常见坑 / 注意 | 24C02 页大小为 8，即单次最多连续写 8 个字节；24C02 带片内地址寄存器，每读写一个字节后自动加 1；SDA 和 SCL 均需与正电源间各接 5.1K 上拉电阻；WP 管脚接地禁止写保护；AT24C02 写入速度小于 10ms、擦写次数大于 10000 次、工作电压 2.5~5.5V |
| 依赖前置 | 串口 2 输入输出例程 |

**一句话作用**：通过 I2C1 接口向板载 24C02 EEPROM 写入有规律数据再顺序读出比对，验证 EEPROM 能否正常访问。

### 7.20 FLASH 模拟 EEPROM（p.448-451）

| 字段 | 内容 |
|------|------|
| 外设/模块 | STM32 内部 FLASH（主存储器）、FPEC（闪存编程/擦除控制器） |
| 接口/总线 | 内部 FLASH（IAP 在应用编程），通用地址空间直接寻址；主存储器起始地址 0x08000000 |
| 关键引脚 | 无专用引脚（B0、B1 接 GND 时从 0x08000000 运行代码）；硬件用到串口和 TFT 彩屏 |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | 寄存器 FLASH_KEYR、FLASH_OPTKEYR、FLASH_CR、FLASH_SR、FLASH_AR、FLASH_OBR、FLASH_WRPR、FLASH_ACR；键值 KEY1=0x45670123、KEY2=0xCDEF89AB、RDPRT 键=0x000000A5；FLASH_CR 的 PG/PER/STRT/LOCK 位、FLASH_SR 的 BSY/PGERR 位 |
| 初始化步骤 | 编程顺序：1）检查 FLASH_CR 的 LOCK 是否解锁，未解锁则先解锁；2）检查 FLASH_SR 的 BSY 位确认无进行中操作；3）设置 FLASH_CR 的 PG 位为 1；4）在指定地址写入要编程的半字；5）等待 BSY 位变 0；6）读出写入地址并验证数据。页擦除顺序：1）检查 LOCK 解锁；2）检查 BSY；3）设置 FLASH_CR 的 PER 位为 1；4）用 FLASH_AR 选择要擦除的页；5）设置 FLASH_CR 的 STRT 位为 1；6）等待 BSY 变 0；7）读出被擦除的页并验证 |
| 常见坑 / 注意 | 闪存编程每次必须写入 16 位（不能只写 8 位），写入非半字数据 FPEC 会产生总线错误；编程时（BSY 为 1）任何读写闪存的操作都会使 CPU 暂停；写入地址必须先擦除（值须为 0xFFFF），否则无法写入并在 FLASH_SR 的 PGERR 位得到警告；STM32F103 的 FLASH 最快访问速度≤24MHz，72MHz 主频下 FLASH 等待周期必须设置为 2（通过 FLASH_ACR 寄存器设置）；复位后 FPEC 被保护，需向 FLASH_KEYR 写入 KEY1 和 KEY2 解锁；大容量产品（512K，如 STM32F103ZET6）主存储器分 256 页每页 2K 字节，中小容量产品每页 1K 字节 |
| 依赖前置 | 串口、TFT 彩屏例程 |

**一句话作用**：利用 STM32 内部 FLASH 的 IAP 功能把 FLASH 当作 EEPROM 使用，写入数据再读出并通过串口显示比对。
