# 第7章 例程：USART 串口 / RS-485

> 手册 p.333-387

### 7.7 USART串口1通信实验（p.333-347）

| 字段 | 内容 |
|------|------|
| 外设/模块 | USART1（串口1） |
| 接口/总线 | APB2 总线（USART1 时钟源 fPCLK2）；经 MAX3232 电平转换接 DB9 |
| 关键引脚 | PA9（USART1_Tx，复用推挽输出 GPIO_Mode_AF_PP）、PA10（USART1_Rx，浮空输入 GPIO_Mode_IN_FLOATING）；MAX3232 把 PA10→DB9 第 2 脚（接收）、PA9→DB9 第 3 脚（发送） |
| 时钟使能 | 手册未明确（由库函数 SZ_STM32_COMInit 内部完成） |
| 关键 API / 寄存器 | SZ_STM32_COMInit(COM1,115200)、__SZ_STM32_COMInit(COM,&USART_InitStructure)、USART_Init()、USART_Cmd()、USART_SendData()、USART_GetFlagStatus(USART1,USART_FLAG_TC)、fputc()；寄存器 USART_BRR、USARTDIV(DIV_Mantissa/DIVFraction)、USART_CR1/CR2/CR3、USART_SR、TDR、RDR |
| 初始化步骤 | ① GPIO 初始化：PA9 配复用推挽输出、PA10 配浮空输入；② 填充 USART 初始化结构体：BaudRate=115200、WordLength_8b、StopBits_1、Parity_No、HardwareFlowControl_None、Mode_Rx\|Tx；③ 调 USART_Init() 写寄存器；④ 调 USART_Cmd() 使能 USART1；⑤ 重写 fputc() 把 printf 重定向到串口1（内部用 USART_SendData + 等待 TC 标志） |
| 常见坑 / 注意 | 收发双方波特率、字长、停止位、奇偶校验位必须一致（PC 端同设 115200）；printf 需重写 fputc() 才能从串口输出；若重定向无效可勾选 USR MicroLIB 库（一般不需要）；使用外设除使能时钟外还须 USART_Cmd() 使能外设；GPIO 用复用功能时模式须查参考手册 |
| 依赖前置 | 用两头母的交叉串口线把开发板串口1（CN5）连 PC；串口1 与 USB 的 ID/VBUS 信号复用，使用时需改跳线（手册推荐改用串口2） |

**子例程**：例程01 = UART串口1 Printf 打印输出字符（p.336-344）；例程02 = UART串口1 带 SYSTICK 中断 Printf（打印同时点亮/闪烁 LED，p.344）；例程03 = UART串口1 Printf 输出和 scanf 输入（p.345-347，scanf 阻塞等回车，完成一次输入后 LED1 取反）。

**一句话作用**：用 ST 库函数初始化 USART1（PA9/PA10，115200），并重定向 printf，实现串口1向 PC 超级终端打印字符串和日期（例程03 增加 scanf 键盘输入）。

### 7.8 USART串口2通信实验（p.348-354）

| 字段 | 内容 |
|------|------|
| 外设/模块 | USART2（串口2） |
| 接口/总线 | APB1 总线（USART2~USART5 时钟源 fPCLK1，APB1 为 35MHz，比 APB2 慢一半）；经 MAX3232 电平转换接 DB9 |
| 关键引脚 | PD5（发送）、PD6（接收）——见 7.8.7 软件设计；经 MAX3232 转换。注：本节"串口2（CN6）信号定义"表却写作 USART2_PA2/PA3，手册前后不一致 |
| 时钟使能 | 手册未明确（由库函数 SZ_STM32_COMInit 内部完成）；明确 USART2 时钟总线为 APB1 |
| 关键 API / 寄存器 | SZ_STM32_COMInit()、__SZ_STM32_COMInit()、USART_Init()、USART_Cmd()、USART_SendData()、USART_GetFlagStatus()、fputc()；接收采用中断方式；寄存器 RDR（接收数据寄存器）、Receive Shift Register |
| 初始化步骤 | ① 在 __SZ_STM32_COMInit() 中初始化串口接收/发送管脚为 PD5、PD6；② 串口端口由 USART1 改为 USART2；③ 串口时钟总线由 APB2 改为 APB1；④ 其余配置与串口1 相同（用中断方式收发） |
| 常见坑 / 注意 | 串口1 与 USB 的 ID/VBUS 信号复用，使用时需改跳线，故手册推荐使用串口2；连串口2 需把 JP14 的 2→3 用跳帽短接、JP12 的 2→3 用跳帽短接（默认出厂已连好串口2）；两串口同时来数据靠中断即可不丢数据，但接收中断须在 1 字节传输时间内读出 RDR（38400 下每字节≥260us，两串口则各须≤130us）；移位寄存器与数据寄存器分开，下一字节到来前读出 RDR 即不丢失 |
| 依赖前置 | 跳线 JP14/J12 置 2-3（选 RS-232）；用交叉母对母串口线把串口2（CN6）连 PC 或 USB 转串口线 |

**子例程**：例程01 = UART串口2 Printf() 打印（原理与串口1相同，仅上述 3 点不同，现象相同）；例程02 = UART串口2 带 SYSTICK 中断 Printf（同串口1 对应章节）；例程03 = UART串口2 Printf 输出和 scanf 输入（同串口1 对应章节）。

**一句话作用**：把串口1 的例程移植到 USART2（PD5/PD6，APB1 总线），实现串口2的 Printf 打印与中断收发。

### 7.9 UART串口1和串口2同时格式化输出输入（p.355-358）

| 字段 | 内容 |
|------|------|
| 外设/模块 | USART1（串口1）+ USART2（串口2） |
| 接口/总线 | APB2（USART1）+ APB1（USART2） |
| 关键引脚 | 串口1：PA9/PA10；串口2：PD5/PD6（与串口1/串口2 例程一致） |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | printf/scanf 重定向（fputc）、SZ_STM32_COMInit 等；本节未列具体代码，手册未明确 |
| 初始化步骤 | 手册未明确（本节仅描述下载验证与实验现象，未给出软件设计代码，代码同串口1/串口2 对应章节） |
| 常见坑 / 注意 | 需注意开发板跳线短接（J14、J12 跳到 2↔3 侧）；需先连串口2、后切到串口1 分别操作；提示语中明确"当前窗口为串口2/COM2/USART2" |
| 依赖前置 | 需先跑通串口1、串口2 例程；跳线 J14/J12 置 2-3；用串口线分别连接串口2、串口1 与 PC |

**一句话作用**：同一程序先在串口2、再切到串口1 进行格式化输入输出（printf/scanf），演示两个串口同时工作。

### 7.10 串口高级例程之Printf中断接收实验（p.359-361）

| 字段 | 内容 |
|------|------|
| 外设/模块 | USART1（串口1）；也可按宏定义切换串口2 |
| 接口/总线 | APB2（USART1） |
| 关键引脚 | 手册未明确（7.10.3 硬件设计指向"错误！未找到引用源"章节，引脚参见串口基础章节） |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | USART_GetITStatus(USART1, USART_IT_RXNE)、USART1->DR（读数据）、USART_SendData()、fputc() 重定向；中断向量表配置；状态标志 USART_IT_RXNE |
| 初始化步骤 | ① 配置中断向量表（按宏定义使能串口1 或串口2 中断）；② 初始化串口1 参数并使能串口1 接收中断；③ printf 重定向（改写 fputc，内部 USART_SendData 发送、按标志位判断发送完成）；④ 主程序打印提示后进入 while 循环，等待接收完成或缓冲区溢出再打印；⑤ 中断服务函数用 USART_GetITStatus 判 RXNE，读 USART1->DR 取数据，最终经 Printf 输出 |
| 常见坑 / 注意 | 中断中长时间等待是致命的（printf 串口打印需等待），故研究 Printf 0 等待与缩短接收中断处理时间；中断服务函数只做判断+取数据 |
| 依赖前置 | 需先看实验4、实验5（串口基础知识）；硬件参见前章节；实验现象中串口线连接的是串口2 |

**一句话作用**：用串口接收中断（RXNE）接收数据并经 printf 打印，实现 Printf 0 等待与更快的串口接收中断处理。

### 7.11 串口高级例程之Printf中断收发实验（p.362-369）

| 字段 | 内容 |
|------|------|
| 外设/模块 | USART1 或 USART2（由 Printf.h 宏定义选择，默认串口2） |
| 接口/总线 | APB2（USART1）/ APB1（USART2） |
| 关键引脚 | 手册未明确（7.11.3 硬件设计指向"错误！未找到引用源"章节） |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | USART_StoreBufferData()、USART_SendBufferData()、USART_GetInputString()、USART_SendData()、USART_GetITStatus()、USART_Rx_Done（全局变量）；FIFO 缓冲 USART_Tx_Buffer（256 字节）、USART_Rx_Buffer（20 字节）；发送中断 + 接收中断；PUTCHAR_PROTOTYPE 宏 |
| 初始化步骤 | ① 配置中断向量表（按宏定义使能串口1 或串口2 中断）；② 初始化串口参数并使能接收和发送中断；③ 发送：单个字节发送函数把数据写入 FIFO（USART_StoreBufferData）并开发送中断后即退出，真正发送在发送中断里由 USART_SendBufferData 完成；④ 接收：接收中断调用 USART_GetInputString 缓存数据（支持退格键），收到回车或缓冲填满时置 USART_Rx_Done 通知前台；⑤ 主程序 while 等 USART_Rx_Done 后打印 |
| 常见坑 / 注意 | 频繁在中断中用 Printf 会降低效率甚至崩溃（中断中长等待致命），故用 FIFO 实现 0 等待；唯一可能影响速度的是 FIFO 深度，FIFO 满后需等待，可按需改深度；发送缓冲默认 256B、接收缓冲默认 20B 均可改；发送缓冲非空则发数据，空则禁止发送中断（下次 StoreBufferData 再打开） |
| 依赖前置 | 需先看串口基础知识；跳线 J12/J14 置 2-3（选串口2/RS-232）；改 Printf.h 宏可在串口1/串口2 间切换 |

**一句话作用**：用接收中断 + 发送中断 + FIFO 缓冲实现高效率串口收发，使 Printf 输出 0 等待、接收中断快速返回。

### 7.12 RS-485总线收发实验（p.370-387）

| 字段 | 内容 |
|------|------|
| 外设/模块 | USART2（与 RS-485 复用）、USART1（printf 打印）、按键、LED、RS-485 收发器 |
| 接口/总线 | RS-485 半双工差分总线（两线、多点）；收发器 SP3485（3.3V 供电，最大 10Mbps，支持 32 节点，带输出短路保护）；RS-485 与串口2 复用，跳线选择 |
| 关键引脚 | PF11 控制收发方向（与 SP3485 的 /RE 第 2 脚、DE 第 3 脚相连：高电平=发送模式，低电平=接收模式）；SP3485：A、B（485 总线接口）、RO（接收输出）、DI（发送输入）、RE（接收使能，低有效）、DE（发送使能，高有效）、VCC/GND；RS-485 接口座 J1；串口2 复用 PD5/PD6 |
| 时钟使能 | 手册未明确 |
| 关键 API / 寄存器 | RS485_MODE_SET()、SZ_STM32_KEYInit()、SZ_STM32_COMInit()、printf；GPIO 输出 PF11 高低电平；变量 RS485_Mode（TX_MODE / RX_MODE / IDLE）；接收数据变量 RcvCh、发送数组 TxBuffer |
| 初始化步骤 | ① 分别初始化 LED、RS-485（含方向脚 PF11）、系统定时器、串口；② 串口1 打印实验内容与提示信息；③ 等待按键设置工作模式：按 USER1=发送端（RS485_Mode=TX_MODE），按 USER2=接收端（RS485_Mode=RX_MODE），按按键3=IDLE 退出重设；④ 接收端：PF11 输出低电平使 SP3485 处于接收模式，收完整一串数据后经串口1 打印；⑤ 发送端：PF11 输出高电平使 SP3485 处于发送模式，周期性把 TxBuffer（"www.armjishu.com STM32 神舟系列开发板 RS485 总线收发实验\r\n"）发送到 485 网络并串口1 打印，延时后重复发送 |
| 常见坑 / 注意 | 需两块神舟III号开发板（一发一收）；485 总线座 J1 用两根杜邦线相连，A 接 A、B 接 B（第 1 脚接第 1 脚、第 2 脚接第 2 脚）；因 485 与 USART2 共用 GPIO，须把 J14/J12 跳至 1-2（下侧）选 RS-485 接口；默认已安装 120Ω 终端匹配电阻（R82），须按实际选择装/拆，终端匹配电阻一般只在总线最远端节点并一个；信号线须对绞以降低共模干扰；须共地，否则接口损坏率高、系统不稳；阻抗不连续/不匹配会产生信号反射，须在电缆末端跨接特性阻抗匹配的终端电阻 |
| 依赖前置 | 需两块神舟III号开发板；跳线 J14/J12 置 1-2；两块板 RS-485 接口（J1）用杜邦线 A-A、B-B 相连；两块板串口1 各用串口线连 PC 并打开超级终端；需先跑通串口、按键、LED 例程 |

**一句话作用**：通过按键设定开发板为 RS-485 发送端或接收端，用串口2 + SP3485 收发器在 RS-485 总线上周期性收发数据，并用串口1 打印收发内容（USER1 发送、USER2 接收）。
