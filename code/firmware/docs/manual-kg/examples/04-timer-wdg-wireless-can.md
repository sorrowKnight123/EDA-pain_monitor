# 第7章 例程：定时器 / 看门狗 / 输入捕获 / 315M无线 / CAN

> 手册 p.452-526

### 7.21 TIMER定时器中断实验（p.452-456）

| 字段 | 内容 |
|------|------|
| 外设/模块 | 通用定时器 TIM5、系统嘀嗒定时器 SysTick、LED1、串口2 |
| 接口/总线 | APB1（TIM5 挂在 APB1，由 PCLK1 得到）；原文称本次示例 PCLK2=HCLK=72MHz、TIM5 模块入口时钟 72MHz |
| 关键引脚 | 手册未明确（本节未给出 LED、串口的 GPIO 管脚） |
| 时钟使能 | RCC_APB1Periph_TIM5（调用 RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM5, ENABLE)） |
| 关键 API / 寄存器 | SZ_STM32_LEDInit()，SZ_STM32_COMInit()，SZ_STM32_SysTickInit()，SZ_STM32_LEDToggle()，RCC_APB1PeriphClockCmd()，TIM_TimeBaseInit()，TIM5_IRQHandler()；结构体 TIM_TimeBaseInitTypeDef；寄存器 TIMx_PSC、TIMx_ARR、TIMx_CNT |
| 初始化步骤 | 1）TIM5 时钟使能（RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM5, ENABLE)）；2）TIM_TimeBaseInit(TIM5, &TIM_TimeBaseStructure) 设置自动重装值、分频系数、计数方式等；3）清除 TIM5 溢出中断标志、打开溢出中断、使能 TIM5，并配置 TIM5 中断参数；4）编写中断服务函数 TIM5_IRQHandler() |
| 常见坑 / 注意 | 不建议在中断中使用 Printf（原文提示此示例只是演示）；中断里计数必须用 static 变量 counter，去掉 static 则每次进中断 counter 为 0，打印值无法累加；要产生 1 秒一次中断需使计数器值×预分频值=系统时钟 72MHz，且计数器的值和预分频值都必须小于 65535（示例取预分频 7200、计数器值 10000） |
| 依赖前置 | 使用 GPIO 控制 LED 的相关知识（LED 跑马灯实验）、串口相关知识（串口输入输出实验）；SysTick 已介绍 |

**一句话作用**：以通用定时器 TIM5 为例产生 1 秒一次的溢出中断，在中断服务函数中翻转 LED1 并用串口打印累加计数值。

### 7.22 IWDG独立看门狗（p.457-462）

| 字段 | 内容 |
|------|------|
| 外设/模块 | 独立看门狗 IWDG（基于一个 12 位递减计数器和一个 8 位预分频器） |
| 接口/总线 | 由芯片内部独立的 40kHz RC 振荡器提供时钟（LSI，独立于主时钟，可运行于停机和待机模式） |
| 关键引脚 | 无（处理器内部组件） |
| 时钟使能 | 手册未明确（原文：独立看门狗由内部专门的 40Khz 低速时钟驱动，不存在使能问题） |
| 关键 API / 寄存器 | IWDG_KR（键值寄存器）、IWDG_PR（预分频寄存器）、IWDG_RLR（重装载寄存器）、IWDG_SR（状态寄存器）；键值 0x5555（允许访问 IWDG_PR/IWDG_RLR）、0xAAAA（喂狗/重装载）、0xCCCC（启动看门狗）；寄存器位 PR[2:0]、RL[11:0]、RVU、PVU |
| 初始化步骤 | 1）向 IWDG_KR 写入 0x5555，取消 IWDG_PR 和 IWDG_RLR 的写保护；2）设置 IWDG_PR 和 IWDG_RLR 的值（分频系数和重装载值）；3）向 IWDG_KR 写入 0xAAAA，重新加载 IWDG_RLR 到看门狗计数器（喂狗）；4）向 IWDG_KR 写入 0xCCCC，启动看门狗 |
| 常见坑 / 注意 | IWDG 一旦启用就不能再被关闭，想关闭只能重启，且重启之后不能打开 IWDG；看门狗时钟不是准确的 40Khz 而是在 30~60Khz 之间变化，喂狗时最好不要太晚；IWDG_PR 和 IWDG_RLR 具有写保护，写入其他值会打乱操作顺序并重新被保护，重装载操作（写 0xAAAA）也会启动写保护；只有 IWDG_SR 的 PVU 位（读 PR）或 RVU 位（读 RLR）为 0 时读出的值才有效；PR[2:0] 编码：000=4、001=8、010=16、011=32、100=64、101=128、110=256、111=256 |
| 依赖前置 | 串口2（连接电脑用超级终端观察现象）；LED |

**一句话作用**：使用 STM32 内部独立看门狗（内部约 40kHz RC 时钟驱动）实现程序跑飞时复位整个系统，溢出时间 Tout=((4×2^prer)×RLR)/40KHz（示例 prer=4、RLR=625 得 1000ms）。

### 7.23 窗口看门狗（p.463-470）

| 字段 | 内容 |
|------|------|
| 外设/模块 | 窗口看门狗 WWDG（可编程自由运行递减计数器）、LED1、LED2、串口2 |
| 接口/总线 | APB1（WWDG 使用 PCLK1 的时钟，使用前需先使能时钟） |
| 关键引脚 | 手册未明确（本节未给出 LED1、LED2 的 GPIO 管脚） |
| 时钟使能 | 使能 WWDG 时钟（原文：WWDG 使用的是 PCLK1 的时钟，在使用前需要先使能时钟，未给出具体 RCC 宏名） |
| 关键 API / 寄存器 | 设置窗口值函数、设置分频数函数、开启 WWDG 中断函数、MY_NVIC_Init()、WWDG_IRQn；寄存器 WWDG_CR（控制寄存器）、WWDG_CFR（配置寄存器）、WWDG_SR（状态寄存器）；寄存器位 WDGA、T[6:0]、W[6:0]、EWI、EWIF |
| 初始化步骤 | 1）使能 WWDG 时钟；2）设置 WWDG_CFR 和 WWDG_CR 两个寄存器（使能窗口看门狗、开启中断、设置计数器初值、设置窗口值、设置分频数 WDGTB）；3）开启 WWDG 中断并分组（用 MY_NVIC_Init 配置中断分组及使能，中断函数 WWDG_IRQn）；4）编写中断服务函数喂狗，并在中断里清状态寄存器的 EWIF 位 |
| 常见坑 / 注意 | 当计数器值从 0x40 变为 0x3F（T6 变 0）时产生看门狗复位；在窗口外重装计数器也产生复位；进入中断后必须在不大于 1 个窗口看门狗计数周期（PCLK1 为 36M 且 WDGTB 为 0 时约 113us）内重新写 WWDG_CR，否则产生复位；WDGA 位一旦由软件置 1，只能在硬件复位后才能清零；下窗口值固定为 0x40，上窗口最大可设 7F；原文提示 7F 为窗口上限最大值、40 为窗口下限，STM 系统默认值固定的 3F 将报错 |
| 依赖前置 | LED、串口例程；前文提供的 MY_NVIC_Init 函数 |

**一句话作用**：使用挂在 PCLK1 上的窗口看门狗，在限定的时间窗口内喂狗（计数器递减到 0x40 产生中断时喂狗），超出窗口或漏喂即复位，每次中断翻转 LED2 并串口打印。

### 7.24 输入捕获实验（p.471-479）

| 字段 | 内容 |
|------|------|
| 外设/模块 | 通用定时器 TIM5 通道1（输入捕获）、KEY4 按键、串口2 |
| 接口/总线 | APB1（TIM5 时钟，通过 APB1ENR 使能） |
| 关键引脚 | PA0（TIM5_CH1，连接 KEY4 按键，配置为上拉输入） |
| 时钟使能 | 开启 TIM5 时钟、开启 GPIOA 时钟 |
| 关键 API / 寄存器 | TIM5_Init()，TIM5_Cap_Init()，TIM5_IRQHandler()，TIM5CH1_CAPTURE_STA；寄存器 TIMx_ARR、TIMx_PSC、TIMx_CCMR1、TIMx_CCER、TIMx_DIER、TIMx_CR1、TIMx_CCR1、TIMx_CNT、APB1ENR；寄存器位 CC1S[1:0]（置 01 即 IC1 映射到 TI1）、IC1PSC[1:0]、IC1F[3:0]、CC1E、CC1P、CC1IE |
| 初始化步骤 | 1）开启 TIM5 时钟，配置 PA0 为上拉输入；2）设置 TIM5 的 ARR 和 PSC（自动重装载值和计数频率）；3）设置 TIM5 的 CCMR1（通道1 输入模式、IC1 映射到 TI1、不使用滤波器）；4）设置 TIM5 的 CCER，开启输入捕获并设置为上升沿捕获；5）设置 TIM5 的 DIER，使能捕获和更新中断，并编写中断服务函数；6）设置 TIM5 的 CR1（最低位置 1）使能定时器 |
| 常见坑 / 注意 | 脉宽比较长时定时器会溢出，必须对溢出做处理否则结果不准；设置了中断必须编写中断函数，否则可能导致死机；按键按下时有抖动（实验现象中捕获到 2 个低于 10us 的数据），按键输入一般需要做防抖处理；原文对上升/下降沿的描述不一致（7.24.3 讲捕获高电平脉宽、第一次上升沿第二次下降沿；7.24.5 第5步也说第一次上升沿第二次下降沿；而 7.24.7 又说第一次下降沿第二次上升沿） |
| 依赖前置 | KEY4 按键（接 PA0）；串口2，波特率 115200；需先跑通串口例程 |

**一句话作用**：用 TIM5 通道1（PA0）做输入捕获，捕获 PA0 上（KEY4 按键按下产生的）低电平脉宽，并通过串口打印低电平脉宽时间（捕获时间精度 1us）。

### 7.25 315M无线模块扫描实验（p.480-492）

| 字段 | 内容 |
|------|------|
| 外设/模块 | 315M 无线模块、蜂鸣器、LED1~LED4、板载按键 KEY1~KEY4（KEY3/TAMPER、KEY4/WAKEUP）、串口2 |
| 接口/总线 | GPIO 输入（按键/无线 GPIO 配置为上拉输入）；软件扫描（轮询） |
| 关键引脚 | 无线模块与 GPIO：D3→PD3、D2→PA8、D1→PC13、D0→PA0、VT→PB10（蜂鸣器）；按键与 GPIO：KEY1→PA8、KEY2→PC3、KEY3/TAMPER→PC13、KEY4/WAKEUP→PA0；LED 与 GPIO：LED1→PD6、LED2→PD7、LED3→PD8、LED4→PD9。原文另称四个按键分别与 PC4、PB10、PC13 和 PA0 四个 GPIO 管脚连接（与上面的按键对应表不一致，以原文为准） |
| 时钟使能 | 手册未明确（原文：使用 GPIO 之前一般需对 GPIO 管脚的时钟、GPIO 管脚模式以及速率进行设定） |
| 关键 API / 寄存器 | LED_config()，Check_315M_Wireless()；GPIO 8 种模式（模拟输入、输入浮空、输入下拉、输入上拉、开漏输出、推挽输出、复用功能开漏输出、复用功能推挽输出）；输出最大时钟速率 10MHz/2MHz/50MHz |
| 初始化步骤 | 1）初始化串口，打印提示信息；2）初始化 LED（LED_config）并让 LED 熄灭；3）初始化按键和 315 无线模块对应 GPIO（配置为上拉输入，无有效信号保持高电平）；4）进入 while 死循环，不停检测来自无线模块的控制信号和按键的状态信息 |
| 常见坑 / 注意 | 无线模块输出为高电平有效，经过三极管放大取反后为低电平有效再送给 STM32；VT 脚低电平有效表示无线模块接收到遥控的按键信号，VT 高电平表示未接收到；神舟III号通过三极管将 315M 无线模块与板上的按键进行了资源复用（按键和 315M 无线模块使用相同 GPIO 管脚）；由于蜂鸣器和 315M 无线接收模块的 VT 使用了相同的 GPIO 管脚，用蜂鸣器是否发声来判断是板载按键还是无线遥控信号；按键按下时对应 GPIO 为低电平，无按键按下时为高电平 |
| 依赖前置 | LED 跑马灯实验、按键检测实验、串口 Printf 输出例程；315M 无线模块与 315M 无线遥控器 |

**一句话作用**：通过扫描神舟III号板载 315M 无线模块（及复用按键）的 4 个输出，控制蜂鸣器和 4 个 LED（按下任一键对应 LED1~4 点亮、蜂鸣器鸣响），并通过串口打印按键或无线控制提示信息。

### 7.26 EXTI无线315M模块外部中断实验（p.493-500）

| 字段 | 内容 |
|------|------|
| 外设/模块 | 315M 无线模块、EXTI 外部中断、蜂鸣器、LED、串口1 |
| 接口/总线 | EXTI 外部中断（中断线 EXTI_Line10）；NVIC 中断控制器 |
| 关键引脚 | 无线模块与 GPIO：D3→PD3、D2→PA8、D1→PC13、D0→PA0、VT→PB10（蜂鸣器）；LED 与 GPIO：LED1→PD6、LED2→PD7、LED3→PD8、LED4→PD9；中断线 EXTI_Line10（对应 PB10/VT） |
| 时钟使能 | 手册未明确（本节未给出具体 RCC 宏名） |
| 关键 API / 寄存器 | SZ_STM32_LEDInit()，SZ_STM32_KEYInit()，W315M_VTConfig()，W315M_EXIT_VTConfig()，GPIO_EXTILineConfig()，EXTI_Init()，NVIC_Init()；结构体成员 EXTI_Line（EXTI_Line10）、EXTI_Trigger（EXTI_Trigger_Falling）；NVIC_InitStructure |
| 初始化步骤 | 1）通过 SZ_STM32_LEDInit() 初始化 LED 灯；2）通过 SZ_STM32_KEYInit() 初始化按键；3）W315M_VTConfig() 初始化 315M 的 VT 引脚（配置为输入模式，具体引脚通过原理图确认）；4）W315M_EXIT_VTConfig() 配置 315M 中断：用 GPIO_EXTILineConfig() 连接中断线，EXTI_InitStructure.EXTI_Line 确认中断线为 EXTI_Line10，EXTI_Trigger 赋值为 EXTI_Trigger_Falling（下降沿触发中断），再用 NVIC_InitStructure 配置中断优先级 |
| 常见坑 / 注意 | 硬件设计时不要将外部中断连接到 PA[1]、PB[1]、PC[1] 和 PE[1]，因为 PA[x]~PE[x] 共享一个 GPIO 中断（来自 GPIO 的一共 16 个中断 Px[15:0]），处理器只能选择一个作为中断源，其它中断将无法到达处理器；无线模块输出为高电平有效，经过三极管放大取反后为低电平有效；VT 脚低电平有效表示无线模块接收到遥控的按键信号 |
| 依赖前置 | 流水灯实验、按键输入扫描实验；315M 无线模块与 315M 无线遥控器 |

**一句话作用**：用 315M 无线模块的 VT 信号触发 EXTI 外部中断（下降沿触发，中断线 EXTI_Line10），在中断中控制串口打印信息并驱动蜂鸣器鸣响。

### 7.27 CAN总线实验（p.501-518）

| 字段 | 内容 |
|------|------|
| 外设/模块 | CAN1（bxCAN）、CAN 总线收发器 VP230、LED（DS1~DS4）、串口 |
| 接口/总线 | CAN 总线（CAN1 接口）；环回模式 CAN_Mode_LoopBack；实验分别用轮询和中断两种方式测试 |
| 关键引脚 | 手册未明确（本节未给出 CAN1 的 GPIO 管脚）；LED 由 GPIOF6~9 四个 GPIO 管脚控制（GPIO 输出低电平时 LED 亮） |
| 时钟使能 | 使能 LED 灯、CAN1 总线接口资源模块的时钟（原文未给出具体 RCC 宏名） |
| 关键 API / 寄存器 | CAN_Init()，CAN_FilterInit()，CAN_Transmit()，CAN_TransmitStatus()，CAN_MessagePending()，CAN_Receive()，CAN_Polling()，CAN_Interrupt()，TestStatus CAN_Polling(void)；寄存器 CAN_MCR、CAN_MSR、CAN_TSR、CAN_RFxR、CAN_IER、CAN_ESR、CAN_TIxR、CAN_FxR0/CAN_FxR1；结构体 CAN_InitStruct 成员（CAN_TTCM、CAN_ABOM、CAN_AWUM、CAN_NART、CAN_RFLM、CAN_TXFP、CAN_MODE、CAN_SJW、CAN_BS1、CAN_BS2、CAN_Prescaler）；滤波器结构体成员（CAN_FilterNumber、CAN_FilterMode、CAN_FilterScale、CAN_FilterIDHigh、CAN_FilterIDLow、CAN_FilterMaskIDHigh、CAN_FilterMaskIDLow、CAN_FilterFIFOAssignment、CAN_FilterActivation）；发送/接收结构体成员（StdId、ExtId、IDE、RTR、DLC、Data[8]、FMI） |
| 初始化步骤 | 1）使能 LED 和 CAN1 总线接口的时钟；2）配置 CAN1 总线中断及优先级（本例程设置中断组 1 个、CAN1 的子优先级为 0）；3）配置 LED 灯（GPIOF6~9 初始化为推挽输出模式）；4）用 CAN_Init() 按 CAN_InitStruct 初始化 CAN 寄存器、用 CAN_FilterInit() 配置滤波器；5）轮询方式：CAN_Transmit 发送→CAN_TransmitStatus 检查是否正常传输→CAN_MessagePending 检查挂号信息（≥1 表示收到新数据）→CAN_Receive 接收；6）中断方式：使用 CAN FIFO 中断，编写中断服务程序（位于 stm32f10x_it.c） |
| 常见坑 / 注意 | 中断函数执行完后要清除 FMPIE 标志位（一旦往 FIFO 存入 1 个报文，硬件就会更新 FMP[1:0]，若 CAN_IER 的 FMPIE 位为 1 则产生中断请求）；CAN_Interrupt(void) 的最后要关中断；波特率设定是 CAN 总线通信的难点（近距离几十米可忽略，几公里时波特率、位序、时序都需好好计算）；由标识符决定发送优先级时，标识符数值最低的报文优先级最高；进入初始化模式时报文的接收和发送都被禁止且 CANTX 引脚输出隐性位（高电平）；不同协议节点不能在初始化模式下通信 |
| 依赖前置 | LED 例程、串口例程 |

**一句话作用**：初始化 CAN1 总线，分别用轮询模式和中断模式测试 CAN 总线环回（LoopBack）收发，并用神舟III号的 LED 指示 CAN 环回的数据传送结果（DS1/DS2 亮表示轮询/中断接收成功，DS3/DS4 亮表示轮询/中断接收失败）。

### 7.28 双CAN收发测试实验（p.519-526）

| 字段 | 内容 |
|------|------|
| 外设/模块 | CAN1（bxCAN）、CAN 总线收发器 VP230、串口 |
| 接口/总线 | CAN 总线（两块开发板主机/从机之间的真实物理连接） |
| 关键引脚 | CANH、CANL（用 1 根杜邦线将开发板1 的 CANH 接开发板2 的 CANH，另一根将开发板1 的 CANL 接开发板2 的 CANL）；本节未给出 GPIO 具体管脚 |
| 时钟使能 | 使能对应管脚和功能硬件的时钟（原文未给出具体 RCC 宏名） |
| 关键 API / 寄存器 | CAN_Config()，CAN_Mode_Config()，CAN_Filter_Config()，CAN_SetMsg()，CAN_Transmit(CAN1, &TxMessage)；结构体 CAN_InitStruct 成员（CAN_TTCM、CAN_ABOM、CAN_AWUM、CAN_NART、CAN_RFLM、CAN_TXFP、CAN_MODE、CAN_SJW、CAN_BS1、CAN_BS2、CAN_Prescaler）；滤波器结构体成员（同 7.27）；CAN1 中断接收服务程序 |
| 初始化步骤 | 1）配置串口；2）调用 CAN_Config() 配置 CAN 模块（配置初始化对应的硬件管脚、管脚和功能硬件时钟使能，CAN_Mode_Config() 按 CAN_InitStruct 初始化 CAN 寄存器，CAN_Filter_Config() 配置滤波器参数）；3）配置 CAN 总线中断（中断方式进行数据收发）；4）通过 CAN_SetMsg() 设置要发送的信息；5）通过 CAN_Transmit(CAN1, &TxMessage) 向从机发送数据；6）中断接收：CAN 收到数据时自动调用中断服务程序，接收完成将 flag 赋值为 0，接收失败将 flag 赋值为 0xff |
| 常见坑 / 注意 | 本实验需要 2 块开发板（分别为主机、从机），有 2 个工程源码；用杜邦线连接 CANH-CANH、CANL-CANL；与 7.27 环回实验不同，本实验是两个物理的 CAN 接口之间真实的通信；分别将 2 个程序下载到 2 个开发板上，串口接到从机上；硬件设计、实验原理、意义与作用三节原文均以“请参阅……章节”引用 7.27，未展开 |
| 依赖前置 | CAN 总线实验（7.27）；2 块神舟III号开发板、杜邦线；串口接从机 |

**一句话作用**：用两块神舟III号开发板（主机/从机）通过 CANH/CANL 真实连线进行 CAN 总线数据收发测试，主机发送、从机中断接收，并通过串口显示收发结果。
