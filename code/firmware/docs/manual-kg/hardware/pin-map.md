# 神舟III号 引脚分配与外设映射

> 手册：第 1 章 1.2 硬件详解（p.29-45）+ 第 7 章各例程的硬件设计小节
> 用途：**扩展功能前必看**——确认目标引脚是否已被占用、走哪条总线、要不要改跳线。
> 引脚以第一章权威描述为准；例程中的表格若与之冲突，已在备注中标明。

## 板载外设引脚总表

| 外设 / 模块 | 接口总线 | 引脚 | 备注 |
|------------|---------|------|------|
| LED DS1~DS4 | GPIO 输出 | **PF6, PF7, PF8, PF9** | 低电平点亮；DS1=PF6 |
| 蜂鸣器 | GPIO 输出 | **PB10** | 低电平鸣响，R14 限流 |
| 按键 KEY1~4 | GPIO 输入 | **PA0, PC13, PA8, PD3** | 按下为低；PA0=WAKEUP，PC13=TAMPER |
| 电位器（ADC） | ADC1_IN13 | **PC3** | 对 3.3V 分压，AD 实验用 |
| DAC 输出 | DAC1/DAC2 | **PA4, PA5** | 输出通道1=PA4，通道2=PA5；引脚需配成模拟输入模式 |
| 串口1 | USART1 | **PA9 (TX), PA10 (RX)** | 经 MAX3232 转 RS-232，DB9 公头，3 线 |
| 串口2 | USART2 | **PD5 (TX), PD6 (RX)** | 可跳线切换为 RS-485 |
| RS-485 方向控制 | GPIO | **PF11** | 高=发送，低=接收；控制 SP3485 的 /RE、DE |
| TFT LCD（2.8/3.2寸） | FSMC Bank1 | **RS→FSMC_A0, CS→FSMC_NE4** | 16 位数据总线；基址 `0x6C000000`；数据线 FSMC_D[0:15] |
| LCD 1602（模块） | GPIO | 数据 **PE0~PE7**，控制 **PC8(en), PC9(rw), PC10(rs)** | 外接模块用，非板载屏 |
| Nor Flash | FSMC Bank1 | **CE→FSMC_NE2** | 基址 `0x64000000` |
| SRAM | FSMC Bank1 | **CE→FSMC_NE3** | 基址 `0x68000000`，IS62WV25616 |
| Nand Flash | FSMC | **ALE=PD11, CLE=PD12, NCE2=PD7, RE=PD4, WE=PD5, RB=PD6/PG6** | HY27UF081G2A，1G bit |
| EEPROM 24C02 | I2C1（硬件） | **PB6 (SCL), PB7 (SDA)** | 手册 7.19 用处理器自带硬件 I2C1；A0~A2 接地，器件地址 0xA0 |
| 收音机 TEA5767 | I2C（软件模拟） | **PB6, PB7** | 手册 7.35 用 PB6/PB7 软件模式 I2C，与 EEPROM 同一对引脚 |
| 音频 PCM1770 | I2S3 + SPI2 | I2S3 送音频数据，SPI2 做配置 | 输出到 J2 音频座 |
| SPI Flash W25X16 | SPI1 | 见下（与网口共用 SPI1） | 16M bit = 2M 字节 |
| 以太网 ENC28J60 | SPI1 | **/CS=PA4, SCK=PA5, MISO=PA6, MOSI=PA7, INT=PC2** | 10M 以太网，HR911105A RJ45 |
| SD 卡 | SDIO（4位） | **D0=PC8, D1=PC9, D2=PC10, D3=PC11, CK=PC12, CMD=PD2, 在位检测=PC6** | **容量上限 2GB（手册 p.683/692/705 三处明确）；用 SDSC 卡，不能用 SDHC**；FAT16；初始化阶段 CK<400kHz。**本项目已启用（bsp_sd.c），时钟 9MHz（CLKDIV=2）** |
| CAN | CAN（VP230 收发器） | 由 F103 内部 CAN 引出 | 3.3V 收发器 |
| 触摸屏（XPT2046/ADS7843） | SPI2（GPIO 模拟） | 片选与中断见 7.47；**中断=PG7** | 屏模块上的触摸芯片 |
| nRF24L01+ 2.4G | SPI2 | 见 7.34 | IRQ 与 3.2 寸屏模块 SD 卡 CS 复用 |
| 315M 无线 | GPIO（与按键复用） | D0~D3 与 4 个按键资源复用（三极管切换） | VT 低电平有效=收到遥控 |
| 三轴加速度 ADXL345 | I2C（软件模拟） | **SCL=PB6, SDA=PB7** | 外接模块，地址 SDO 接地=0x53 |
| DHT11 温湿度 | 单总线 | **PA11 (DATA)** | 外接模块 |
| DS18B20 温度 | 单总线 | 单根 DQ（外接模块接线） | 外接模块 |

## 总线/片选复用冲突（扩展时必须避让）

| 总线 | 共享设备 | 冲突说明 |
|------|---------|---------|
| **SPI1** | W25X16 SPI Flash、ENC28J60 以太网 | 共用 SPI1，靠不同 CS 区分。**不可同时拉低两者 CS**，否则读写失败 |
| **SPI2** | PCM1770 音频配置、nRF24L01+ 2.4G、触摸屏 XPT2046 | nRF24L01+ 的 IRQ 与 3.2 寸屏模块上 SD 卡 CS 复用；用屏上 SD 卡时不能用 nRF24L01+ |
| **I2C（PB6/PB7）** | EEPROM 24C02（硬件 I2C1）、TEA5767 收音机（软件模拟） | 共用同一对引脚；两例程的驱动方式不同（一个硬件外设、一个软件翻转 GPIO） |
| **FSMC Bank1** | LCD(NE4)、Nor Flash(NE2)、SRAM(NE3) | 三个片选独立，互不冲突；Nand 走 FSMC 控制器（非 Bank1） |
| **按键** | 4 个功能按键、315M 无线模块 D0~D3 | 通过三极管做资源复用，315M 使用时按键行为受影响 |

## FSMC Bank1 地址映像（扩展外部存储器/屏必查）

FSMC 管理 1GB 空间，分 4 个 256MB 存储块（Bank）。Bank1 分 4 个区，每区 64MB：

| 区 | 片选 | 地址范围 | 本项目用途 |
|----|------|---------|-----------|
| 第1区 | FSMC_NE1 | `0x6000_0000 ~ 0x63FF_FFFF` | — |
| 第2区 | FSMC_NE2 | `0x6400_0000 ~ 0x67FF_FFFF` | Nor Flash（手册例程） |
| 第3区 | FSMC_NE3 | `0x6800_0000 ~ 0x6BFF_FFFF` | SRAM（手册例程） |
| 第4区 | FSMC_NE4 | `0x6C00_0000 ~ 0x6FFF_FFFF` | **本项目 TFT LCD（SSD1289）** |

> 手册 7.36 的 TFT 例程用的正是 Bank1 第4区（NE4, `0x6C000000`），与本项目 `fsmc.c` 的 `FSMC_NORSRAM_BANK4` 一致。手册 7.30 SRAM 例程用 NE3。

## 时钟总线归属（配 RCC 时用）

| 总线 | 挂载外设 |
|------|---------|
| APB2（高速） | GPIO(A~G)、USART1、ADC1/2、TIM1、TIM8、AFIO、SPI1 |
| APB1（低速） | USART2/3、TIM2~7、SPI2/3、I2C1/2、CAN、DAC、PWR、BKP、WWDG |
| AHB | DMA、FSMC、SDIO、CRC、FLASH、SRAM |

## 相关

- 板载资源总览 → [`board-resources.md`](board-resources.md)
- 各外设官方例程 → [`../examples/README.md`](../examples/README.md)
- 本项目引脚占用 → [`../firmware-map.md`](../firmware-map.md)
