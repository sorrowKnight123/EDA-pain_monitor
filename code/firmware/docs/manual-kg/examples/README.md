# 第 7 章 库函数实战例程索引（77 个）

> 手册：第 7 章，p.273-860。基于 ST 标准库 `STM32F10x_StdPeriph_Lib_V3.5.0`，MDK 4.22 调试通过。
> 每个例程条目含：外设/接口/引脚/时钟使能/关键 API/初始化步骤/常见坑/依赖前置。
> 模板见 [`_TEMPLATE.md`](_TEMPLATE.md)。页码 = PDF 物理页 = 手册印刷页。

## 按外设分类

| 分类文件 | 例程 | 页码 |
|---------|------|------|
| [01-gpio-key-systick.md](01-gpio-key-systick.md) | 7.1–7.6 GPIO/LED/蜂鸣器/按键/位带/EXTI/SysTick | p.273-332 |
| [02-uart-rs485.md](02-uart-rs485.md) | 7.7–7.12 USART 串口1/2、Printf 中断收发、RS-485 | p.333-387 |
| [03-id-adc-dac-rtc-mem.md](03-id-adc-dac-rtc-mem.md) | 7.13–7.20 唯一ID、ADC、DAC、RTC、EEPROM、FLASH | p.388-451 |
| [04-timer-wdg-wireless-can.md](04-timer-wdg-wireless-can.md) | 7.21–7.28 定时器、看门狗、输入捕获、315M无线、CAN | p.452-526 |
| [05-spi-sram-nor-nand-24g-radio.md](05-spi-sram-nor-nand-24g-radio.md) | 7.29–7.35 SPI FLASH、SRAM、内存管理、Nor/Nand、2.4G、收音机 | p.527-601 |
| [06-tft-touch-gui.md](06-tft-touch-gui.md) | 7.36–7.54 TFT 显示、字模、图片、触摸屏、GUI 交互 | p.602-672 |
| [07-sd-audio-crc-pvd-lp-dma-sensor.md](07-sd-audio-crc-pvd-lp-dma-sensor.md) | 7.55–7.67 SD卡、音频、CRC、PVD、低功耗、DMA、传感器 | p.673-785 |
| [08-iap-mp3-ir-eth-ucos-lcd.md](08-iap-mp3-ir-eth-ucos-lcd.md) | 7.68–7.77 IAP、MP3、红外、以太网、uCOS-II、1602/4.3/7寸屏 | p.786-860 |

## 全部例程速查表

| 例程 | 页码 | 外设要点 |
|------|------|---------|
| 7.1 LED流水灯 | p.273-288 | GPIO PF6~PF9，库开发流程/时钟/地址映射入门 |
| 7.2 蜂鸣器 | p.289-294 | PB10，低电平鸣响 |
| 7.3 按键检测 | p.295-301 | PA0/PC13/PA8/PD3，软件防抖，GPIO 8 种模式 |
| 7.4 BitBand 按键扫描 | p.302-311 | 位带别名区操作，GPIOF->ODR 单 bit |
| 7.5 EXTI 外部按键中断 | p.312-324 | PD3→EXTI3，NVIC，下降沿 |
| 7.6 SysTick 系统滴答 | p.325-332 | 精确延时 Delay_us/ms |
| 7.7 USART 串口1 | p.333-347 | PA9/PA10，MAX3232，printf 重定向 |
| 7.8 USART 串口2 | p.348-354 | PD5/PD6（原文另有 PA2/PA3 矛盾表述） |
| 7.9 串口1+2 同时格式化 | p.355-358 | 双串口格式化输入输出 |
| 7.10 Printf 中断接收 | p.359-361 | USART1 RXNE 中断 |
| 7.11 Printf 中断收发 | p.362-369 | 收发中断 + FIFO，0 等待 |
| 7.12 RS-485 收发 | p.370-387 | PF11 方向控制，SP3485，需 J12/J14 跳线 |
| 7.13 唯一身份标识 | p.388-391 | 96 位 UID @0x1FFFF7E8 |
| 7.14 ADC 模数转换 | p.392-400 | ADC1_IN13=PC3 电位器，DMA，时钟≤14MHz |
| 7.15 ADC 多路采集 | p.401 | **手册仅有标题无正文** |
| 7.16 DAC 数模转换 | p.401-407 | PA4/PA5，TIM2 触发 + DMA，双路正弦波 |
| 7.17 RTC 实时时钟 | p.408-419 | 32.768kHz + 电池，秒中断 |
| 7.18 Calendar 农历 | p.420-431 | RTC + calendar/date 库，BKP 判首次上电 |
| 7.19 EEPROM 读写 | p.432-447 | 24C02，硬件 I2C1 PB6/PB7，地址 0xA0 |
| 7.20 FLASH 模拟 EEPROM | p.448-451 | FPEC 编程/页擦除，16 位半字写 |
| 7.21 TIMER 定时器中断 | p.452-456 | TIM5，1 秒中断翻转 LED |
| 7.22 IWDG 独立看门狗 | p.457-462 | LSI ~40kHz，KR 三步，启用后不可关 |
| 7.23 窗口看门狗 | p.463-470 | WWDG 挂 PCLK1，窗口外喂狗即复位 |
| 7.24 输入捕获 | p.471-479 | TIM5_CH1=PA0，捕获低电平脉宽 |
| 7.25 315M 无线扫描 | p.480-492 | D0~D3 与按键复用，VT 低有效 |
| 7.26 EXTI 315M 中断 | p.493-500 | VT(PB10)→EXTI10 下降沿 |
| 7.27 CAN 总线 | p.501-518 | VP230，环回模式，轮询/中断 |
| 7.28 双 CAN 收发 | p.519-526 | 两块板 CANH/CANL 互联 |
| 7.29 SPI FLASH 读写 | p.527-536 | W25X16，SPI1，与网口共用 SPI1 |
| 7.30 SRAM 访问 | p.537-548 | IS62WV25616，FSMC_NE3，0x68000000 |
| 7.31 内存管理 | p.549-554 | mymalloc/myfree 分块管理 |
| 7.32 Nor Flash 访问 | p.555-566 | FSMC_NE2，0x64000000 |
| 7.33 Nand Flash 访问 | p.567-578 | HY27UF081G2A，PD11/PD12/PD7/PD4/PD5 |
| 7.34 2.4G 无线 | p.579-586 | nRF24L01，SPI2，IRQ 与屏上 SD CS 复用 |
| 7.35 收音机 | p.587-601 | TEA5767，软件 I2C PB6/PB7 |
| 7.36 TFT 只显示红色 | p.602-619 | FSMC NE4 @0x6C000000，RS=FSMC_A0，16 位 |
| 7.37–7.46 TFT 显示系列 | p.620-647 | 蓝色/点/数字/字母/图片/刷屏/移动 |
| 7.47 TFT 触摸屏 | p.648-654 | XPT2046 走 SPI2，中断 PG7→EXTI9_5 |
| 7.48–7.51 触摸交互 | p.655-660 | 图片跟随/图标外框/触发事件/双击 |
| 7.52–7.54 TFT 文字 | p.661-672 | 英文 8x16 / 中文 / 中英混合 |
| 7.55 SD 卡访问 | p.673-687 | SDIO 4 位，PC8~PC12/PD2，CK<400kHz；**容量上限 2GB（SDSC），不可用 SDHC** |
| 7.56 SD FAT 文件系统 | p.688-692 | FatFs 移植，TFT 列文件 |
| 7.57 SD 读卡器 | p.693-705 | SDIO + USB Mass Storage |
| 7.58 音频播放 | p.706-713 | PCM1770，I2S3 + SPI2，需关 JTAG 用 SWD |
| 7.59 硬件 CRC | p.714-718 | 多项式 0x04C11DB7 |
| 7.60 PVD 电源监测 | p.719-723 | 阈值 2.9V，EXTI16 中断 |
| 7.61 STANDBY 待机 | p.724-733 | PWR_CR PDDS + WFI，~2µA，WKUP 唤醒 |
| 7.62 STOP 停止模式 | p.734-742 | 外部中断/RTC 闹钟唤醒，SRAM 保留 |
| 7.63 DMA 传输 | p.743-748 | DMA1_Ch4 内存→USART1_TX |
| 7.64 内部温度传感器 | p.749-753 | ADC1_IN16 |
| 7.65 DS18B20 | p.754-769 | 单总线，9~12 位，93.75~750ms |
| 7.66 DHT11 | p.770-773 | 单总线 PA11，40bit+校验 |
| 7.67 三轴加速度 | p.774-785 | ADXL345，I2C PB6/PB7，地址 0x53 |
| 7.68 串口 IAP 升级 | p.786-798 | Bootloader + APP 分区，向量表偏移 |
| 7.69 MP3(VS1003) | p.799-812 | SPI1，PE8/PE7/PC13/PE6，DREQ 流控 |
| 7.70 红外遥控 | p.813-821 | TIM4_CH4 捕获，接收头接 PB9 |
| 7.71 以太网 ENC28J60 | p.822-831 | SPI1，PA4~PA7/PC2，HTTP WebServer |
| 7.72 uCOS-II 单任务 | p.832-836 | OSInit/OSTaskCreate/OSStart |
| 7.73 uCOS-II 多任务 | p.837-838 | SysTick 10ms 节拍，3 任务 |
| 7.74 uCOS+UCGUI DEMO | p.839 | 手册仅标题 |
| 7.75 1602 液晶屏 | p.839-854 | 数据 PE0~PE7，控制 PC8/PC9/PC10 |
| 7.76 4.3 寸液晶屏 | p.855-857 | 以实验现象为主 |
| 7.77 7 寸液晶屏 | p.858-860 | 以实验现象为主，7寸34脚 vs 底板32脚 |

## 手册原文中的已知矛盾（条目内已标注）

提炼时发现手册若干处自相矛盾，条目里按"忠实原文 + 标注"处理，**实际开发以实物/数据手册为准**：

- 7.8：正文说串口2 用 PD5/PD6，同节信号定义表却写 PA2/PA3。
- 7.15 / 7.74：仅有小节标题，无正文。
- 7.19：手册称用**硬件 I2C1**；本项目实际为**软件模拟 I2C**（以代码为准）。
- 7.24：捕获边沿先后顺序描述前后不一致。
- 7.25：正文说按键接 PC4/PB10/PC13/PA0，同节按键表却列 PA8/PC3/PC13/PA0。
- 7.30/7.32/7.33：SRAM/Nor 型号、Nand 结构体名等处有笔误。
- 7.45：标题标注【未更新】。
- 7.47：一处称触摸走 SPI1，软件部分为 SPI2。
- 7.55：一处称 SD 用 SPI 模式，实验原理为 SDIO 模式。**另注意容量口径**：7.55.2 的"SDHC 最大 32GB"是 SD 标准通用分类科普，本板实际上限见 p.683/692/705 —— **最大 2GB**。
- 7.58：硬件小节标题误写"SD卡读卡器"。
- 7.63：串口1/2 口径不一致，数据量 5200/5168 两值。

## 相关

- 引脚与总线 → [`../hardware/pin-map.md`](../hardware/pin-map.md)
- 本项目已实现模块 → [`../firmware-map.md`](../firmware-map.md)
