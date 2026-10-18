# 第7章 例程：GPIO / 按键 / 中断 / SysTick

> 手册 p.273-332

### 7.1 LED流水灯实验（p.273-288）

| 字段 | 内容 |
|------|------|
| 外设/模块 | GPIO（GPIOF 端口） |
| 接口/总线 | APB2 总线（原文分析 GPIOF 时钟路径：HSE 8MHz→PLL×9=72MHz→SYSCLK→AHB 不分频→APB2 不分频，得 72MHz） |
| 关键引脚 | PF6、PF7、PF8、PF9（对应 DS1~DS4 四个 LED，p.274）；LED 一端接 3.3V，另一端经 470R 电阻接芯片管脚，输出低电平点亮、高电平熄灭 |
| 时钟使能 | 手册未明确具体宏名；原文仅说明"使用外设时需要使用使能外设时钟的函数，这些函数在 stm32f10x_rcc.c 中" |
| 关键 API / 寄存器 | GPIO_InitTypeDef 结构体、GPIO_Init()、GPIO_SetBits()、GPIO_ResetBits()、LED_config()（7.3 节又称 LED_Init()）；寄存器 GPIOx_CRL、GPIOx_CRH、GPIOx_IDR、GPIOx_ODR、GPIOx_BSRR、GPIOx_BRR、GPIOx_LCKR；结构体成员 GPIO_Pin（GPIO_Pin_6/7/8/9）、GPIO_Speed（GPIO_Speed_50MHz）、GPIO_Mode（GPIO_Mode_Out_PP） |
| 初始化步骤 | 1）选择要配置的外设：由原理图确定用 GPIOF 的 PF6~PF9；2）配置外设特定功能：GPIO_InitTypeDef 结构体设引脚 GPIO_Pin_6~9、速率 GPIO_Speed_50MHz、模式 GPIO_Mode_Out_PP 推挽输出；3）调用 GPIO_Init(GPIOF, &GPIO_InitStructure) 完成初始化；4）main 的 while 死循环中调用 GPIO_ResetBits()/GPIO_SetBits() 并加延时，使各 LED 轮流亮灭 |
| 常见坑 / 注意 | 原文强调 GPIO 配置遵循"①选外设②配功能③实现目标"三步，适用于所有外设；PF6/PF7 由 GPIOF_CRL 配置，PF8/PF9 由 GPIOF_CRH 配置；GPIOx_BSRR 的 BSy 位写 1 输出高电平、BRy 位写 1 输出低电平，对两位写 0 均无效；GPIOx_LCKR 用得较少 |
| 依赖前置 | 需先按"新建模板工程"章节建立工程（工程结构/配置一致）；使用 STM32F10x_StdPeriph_Lib_V3.5.0 库、RVMDK 4.22 环境；下载/验证见 3.2（JLINK）、3.3（串口）、3.5（MDK 在线调试） |

**一句话作用**：控制 GPIOF 的 PF6~PF9 输出高低电平，使 4 个 LED（LED1~4）轮流亮灭，实现流水灯效果（实验现象 p.289）。

### 7.2 蜂鸣器实验（p.289-294）

| 字段 | 内容 |
|------|------|
| 外设/模块 | GPIO（PB10 控制蜂鸣器）；蜂鸣器（原文 7.2.2 称开发板提供"电磁式无源蜂鸣器"，7.2.3 又称本实验介绍"如何通过一个 GPIO 管脚控制有源直流蜂鸣器"） |
| 接口/总线 | 手册未明确（沿用 GPIO 的时钟配置，SystemInit() 配为 72MHz） |
| 关键引脚 | PB10（蜂鸣器控制管脚，p.291）；PB10 上串限流电阻 R14（实际安装 22 欧姆，使声音柔和）；输出低电平鸣响，输出高电平或 OD 开漏停止鸣响 |
| 时钟使能 | 手册未明确（启动文件调用 SystemInit()，经 SetSysClock()→SetSysClockTo72() 配为 72MHz） |
| 关键 API / 寄存器 | SZ_STM32_BEEPInit()、GPIO_SetBits()（关闭蜂鸣器）、GPIO_ResetBits()（开启蜂鸣器）；宏 GPIO_BEEPER、BEEPER_PIN；寄存器 GPIOx_BSRR（SetBits 控制）、GPIOx_BRR（ResetBits 控制） |
| 初始化步骤 | 1）启动文件 SystemInit() 完成 72MHz 时钟初始化；2）SZ_STM32_BEEPInit() 初始化蜂鸣器 GPIO 管脚并配置模式；3）GPIO_SetBits(GPIO_BEEPER, BEEPER_PIN) 输出高电平关闭蜂鸣器；4）GPIO_ResetBits(GPIO_BEEPER, BEEPER_PIN) 输出低电平使蜂鸣器鸣响；5）while 循环中加延时，使蜂鸣器发出"滴滴"声 |
| 常见坑 / 注意 | 蜂鸣器两端电压大于 4V（典型 5V）时发出固定频率声音；STM32 IO 口可承受电流为 25mA，蜂鸣器额定工作电流为 30mA，故必须串限流电阻 R14 防止损坏 GPIO 管脚 |
| 依赖前置 | 流水灯例程（GPIO 配置讲解，本实验不重复）；需板载蜂鸣器；下载/验证见 3.3（JLINK）、3.4（串口）、3.8（MDK 在线调试） |

**一句话作用**：通过 PB10 管脚输出低/高电平控制蜂鸣器鸣响与停止，在 while 循环中加延时使蜂鸣器发出间断的"滴滴"声（实验现象 p.294）。

### 7.3 按键检测实验（p.295-301）

| 字段 | 内容 |
|------|------|
| 外设/模块 | GPIO（4 个功能按键） |
| 接口/总线 | 手册未明确（按键对应 GPIO 的时钟） |
| 关键引脚 | 4 个按键分别接 PD3、PA8、PC13、PA0（p.299）；按下时对应 GPIO 为低电平，未按下为高电平；PA0（STM32 的 WKUP 引脚）可作唤醒输入，PC13 可实现备份区寄存器入侵功能，本实验均作普通 IO 使用 |
| 时钟使能 | 使能按键对应 GPIO 的 Clock 时钟（原文未给出具体宏名） |
| 关键 API / 寄存器 | 用户文件 key.c/key.h；SZ_STM32_KEYInit()、SZ_STM32_KEYScan()、GPIO_Init()、GPIO_ReadInputDataBit()、LED_Init()；寄存器 GPIOx_IDR；GPIO 模式常量 GPIO_Mode_IPU、GPIO_Mode_IPD、GPIO_Mode_IN_FLOATING、GPIO_Mode_AIN、GPIO_Mode_Out_PP、GPIO_Mode_Out_OD、GPIO_Mode_AF_PP、GPIO_Mode_AF_OD |
| 初始化步骤 | 1）SZ_STM32_KEYInit()：先使能按键对应 GPIO 的时钟；2）配置 4 个按键管脚 PA0、PD3、PA8、PC13 为浮空输入模式（GPIO_Mode_IN_FLOATING）；3）调用库函数 GPIO_Init() 完成 4 个 GPIO 管脚初始化；4）SZ_STM32_KEYScan() 用 GPIO_ReadInputDataBit()（经 GPIOx_IDR）判断管脚是否被拉低；5）main：先 LED_Init() 初始化 LED，再 SZ_STM32_KEYInit()，再 SZ_STM32_KEYScan()，有按键按下则反转对应 LED 状态 |
| 常见坑 / 注意 | 按键为机械器件，按下/松开有 5ms~10ms 抖动，需去抖（软件去抖：检测到按下后延时 5~10ms 跳过抖动区，再检测仍按下才认为真正按下；释放后同样去抖延时）；GPIO 引脚若误配为模拟输入模式必然导致错误；使用任何一种开漏模式都需接上拉电阻；本实验按键配置为浮空输入，因硬件外部已接上拉电阻，外部上拉与内部上拉效果相同 |
| 依赖前置 | LED 例程（LED_Init）；需 4 个板载功能按键（USER1/USER2/TAMPER/WAKEUP）；下载/验证见 3.3/3.4/3.5 对应小节 |

**一句话作用**：轮询读取 4 个按键（PA0/PC13/PA8/PD3）输入电平，按键按下时反转对应 LED 状态（现象表：DS1←USER1、DS2←USER2、DS3←TAMPER、DS4←WAKEUP，p.302）。

### 7.4 BitBand按键扫描检测实验（p.302-311）

| 字段 | 内容 |
|------|------|
| 外设/模块 | GPIO（位带操作 LED/按键）+ Cortex-M3 位带（BitBand）别名区 |
| 接口/总线 | 位带别名区：片上外设区最低 1MB（0x4000_0000-0x400F_FFFF）对应别名区 0x4200_0000-0x43FF_FFFF；SRAM 区最低 1MB（0x2000_0000-0x200F_FFFF）对应别名区 0x2200_0000-0x23FF_FFFF（p.304） |
| 关键引脚 | LED 用 PF6~PF9（例中 PF.06 对应 LED1_PIN_NUM=6，操作 GPIOF->ODR）；按键同 7.3（USER1/USER2/TAMPER/WAKEUP） |
| 时钟使能 | 手册未明确（调用 SZ_STM32_LEDInit() 初始化板载 LED，细节前文已述） |
| 关键 API / 寄存器 | SZ_STM32_LEDInit()、Periph_BB()、PFSetBit(n)、PFOutBit(n)、BITBAND(addr, bitnum) 宏；宏 Periph_BASE=0x40000000、Periph_BB_BASE=0x42000000；寄存器 GPIOF->ODR；别名区地址公式 SRAM:0x22000000+((A-0x20000000)*8+n)*4、FLASH:0x42000000+((A-0x40000000)*8+n)*4 |
| 初始化步骤 | 1）启动文件 SystemInit() 初始化 72MHz 时钟；2）SZ_STM32_LEDInit() 初始化板载 LED 指示灯；3）用位带 BitBand 方式（Periph_BB 宏）使管脚输出高电平熄灭指示灯；4）定义 LED1OBB=Periph_BB((uint32_t)&LED1_GPIO_PORT->ODR, LED1_PIN_NUM) 定位到 PF.06；5）PFSetBit(6) 即 PFOutBit(6)=1，使 PF.06 依次输出高电平；6）按键扫描逻辑（同按键实验） |
| 常见坑 / 注意 | STM32 内部区域访问只能是 32 位字，不能按字节/半字/单 bit 访问，故引入位带别名区（1 个 bit 映射到 32 bit）；位带仅支持两个 1MB 区域（SRAM 最低 1MB、片上外设最低 1MB）；别名区 1 位膨胀到 32 位，1MB SRAM 对应 32MB 别名区；寄存器内部从 0 开始计数，第 14 bit 相当于第 13（BS13）；宏 BITBAND 化简利用 ×32 即左移 5 位、×4 即左移 2 位 |
| 依赖前置 | 按键检测实验（7.3）、流水灯（7.1）；下载/验证见 3.2/3.3/3.5 |

**一句话作用**：用 Cortex-M3 位带（BitBand）方式对 GPIOF 的 ODR 进行单独位操作（如 PF.06），以按键扫描检测方式实现按键按下时对应 LED 状态切换（现象：无按键时 4 灯全灭，按一下亮、再按一下灭）。

### 7.5 EXTI外部按键中断实验（p.312-324）

| 字段 | 内容 |
|------|------|
| 外设/模块 | EXTI（外部中断）、NVIC（向量中断控制器）、GPIO、AFIO |
| 接口/总线 | APB2（GPIO 与 AFIO）；EXTI_Line3 |
| 关键引脚 | 按键 PD3（连接 EXTI_Line3，p.322）；按键与 GPIO 对应：USER1=PA8、USER2=PD3、KEY3/TAMPER=PC13、KEY4/WAKEUP=PA0（p.320-321）；LED 与 GPIO 对应：DS1=PF6、DS2=PF7、DS3=PF8、DS4=PF9（p.321） |
| 时钟使能 | RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE)（GPIO 管脚用做 EXTI 外部中断时必须打开 AFIO 时钟，p.323） |
| 关键 API / 寄存器 | NVIC_PriorityGroupConfig()、NVIC_Init()、EXTI_Init()、GPIO_EXTILineConfig()、EXTI_GetITStatus()、EXTI_ClearITPendingBit()、LED_Init()、SZ_STM32_KEYInit()、NVIC_GroupConfig()、EXTIX_Init()；结构体 EXTI_InitTypeDef（成员 EXTI_Line、EXTI_Mode、EXTI_Trigger、EXTI_LineCmd）、NVIC_InitTypeDef（成员 NVIC_IRQChannel、NVIC_IRQChannelPreemptionPriority、NVIC_IRQChannelSubPriority、NVIC_IRQChannelCmd）；中断服务函数 EXTI3_IRQHandler（对应 EXTI3_IRQn，定义于 stm32f10x_it.c）；寄存器 AIRC（PRIGROUP 决定中断分组）；新增文件 exti.c/exti.h、stm32f10x_exti.c、misc.c、stm32f10x_it.c |
| 初始化步骤 | 原文列出外部中断初始化 5 步：1）初始化 IO 口为输入；2）开启 IO 口复用时钟，设置 IO 口与中断线的映射关系；3）初始化线上中断，设置触发条件（参考《STM32F10XXX 参考手册》）；4）配置中断分组（NVIC），并使能中断（参考《STM32F10xxx Cortex-M3 编程手册》）；5）编写中断服务函数。代码具体：NVIC_GroupConfig() 用 2 比特主优先级+2 比特次优先级（取值 0~3，越小优先级越高）；EXTIX_Init() 中 EXTI_Line=EXTI_Line3、EXTI_Mode=EXTI_Mode_Interrupt、EXTI_Trigger=EXTI_Trigger_Falling、EXTI_LineCmd=ENABLE，NVIC_IRQChannel=EXTI3_IRQn、抢占优先级 0x02、子优先级 0x03 |
| 常见坑 / 注意 | 中断服务函数名不能随意定义，必须与启动文件 startup_stm32f10x_hd.s 中断向量表一致；STM32 IO 外部中断函数只有 6 个：EXTI0~EXTI4 各一个，中断线 5-9 共用 EXTI9_5_IRQHandler、10-15 共用 EXTI15_10_IRQHandler；GPIO 用做 EXTI 时必须打开 AFIO 时钟；PAx~PGx 共享 EXTIx，同一时刻 EXTIx 只能响应一个端口事件（可分时复用）；优先级数值越小级别越高，抢先属性可嵌套打断，响应属性仅在抢先相同且同时到达时决定先后；EXTI_GetITStatus() 在中断服务函数开头判断标志位，EXTI_ClearITPendingBit() 在结束前清除标志位；注意原文代码示例写作 GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource4)，与正文"按键的 PD3 连接到了 EXTI_Line3"不一致（疑为笔误） |
| 依赖前置 | 按键检测实验（7.3）、LED 例程；需 4 个板载按键；下载/验证见 3.2（JLINK） |

**一句话作用**：将按键 PD3 接 EXTI_Line3 并配置为下降沿触发中断，经 NVIC 使能 EXTI3_IRQn，中断时执行服务函数（延时去抖+反转 LED）；无中断时 4 个 LED 呈流水灯，按键按下流水灯停止、松开恢复。

### 7.6 SysTick系统滴答实验（p.325-332）

| 字段 | 内容 |
|------|------|
| 外设/模块 | SysTick（系统滴答定时器，24 位倒计数）、NVIC |
| 接口/总线 | 内核（Cortex-M3 内核，SysTick_Config() 位于 core_cm3.h）；时钟源可为 AHB 时钟（72MHz）或 AHB/8，由 CTRL 寄存器 CLKSOURCE 位选择（=1 为 AHB，=0 为 AHB/8） |
| 关键引脚 | 手册未明确（用于精确延时；测试时用仪器测量 LED 的 GPIO 管脚电平变化波形） |
| 时钟使能 | 配置 SysTick 时钟源为 AHB（72MHz），由 SysTick_Config() 末行设置 CTRL 寄存器（CLKSOURCE=1） |
| 关键 API / 寄存器 | SZ_STM32_SysTickInit()、SysTick_Config()、NVIC_SetPriority()、SysTick_Handler()、TimingDelay_Decrement()、Delay_us()、Delay_ms()、LED_Init()；寄存器 SysTick_CTRL(0xE000E010)、SysTick_LOAD(0xE000E014)、SysTick_VAL(0xE000E018)、SysTick_CALIB(0xE000E01C)、STK_LOAD、STK_VAL；宏 SysTick_LOAD_RELOAD_Msk、SysTick_LOAD_RELOAD_Pos、SysTick_IRQn、__NVIC_PRIO_BITS |
| 初始化步骤 | 1）LED_Init() 初始化板载 LED；2）SZ_STM32_SysTickInit() 调用 SysTick_Config()，配置不成功则进入死循环；3）SysTick_Config(ticks) 配置 24 位倒计数定时器（ticks-1 送 STK_LOAD，超过最大值返回 1，NVIC_SetPriority 配 SysTick 中断，STK_VAL 清 0，末行设 CTRL 使能定时器与中断、时钟源 AHB）；4）中断服务函数 SysTick_Handler() 调用 TimingDelay_Decrement()，每中断一次 TimingDelay 减 1（即过 1 微秒）；5）Delay_us(nTime)：对 TimingDelay 赋值→置 CTRL ENABLE 位使能 SysTick→while(TimingDelay!=0) 等待→清 ENABLE 位关闭；6）main while 循环点亮 4 个 LED，延时 1 秒，熄灭 LED，再延时 1 秒，如此循环 |
| 常见坑 / 注意 | 传统 delay(nCount) 循环自减难以计算精确延时，故用 SysTick 精确延时；STK_LOAD 与 STK_VAL 均为 24 位，ticks 超过可存储最大值会返回错误；SysTick_Config 内部已用 NVIC_SetPriority 配置 SysTick 中断，故外部无需再用 NVIC 配置；处理器在调试期间被 halt 时 SysTick 定时器亦将暂停运作；实测 10us 延时实际高电平持续 10.5833 微秒，多出的 0.5833us 来自赋值语句、设置寄存器语句及调用库函数拉低 GPIO 的耗时；注意原文定时时间计算处写作 72M/10000000，与"72M/1000000 得 1us"的结论不一致（疑为笔误） |
| 依赖前置 | LED 例程（LED_Init）；深入理解需参考《STM32F10xxx 的 Cortex-M3 编程手册.pdf》；下载后用逻辑分析仪测量管脚电平变化验证 |

**一句话作用**：用 SysTick 24 位倒计数定时器产生 1 微秒节拍中断，实现精确的微秒/毫秒级延时（实验中以 1 秒延时循环点亮与熄灭 4 个 LED）。
