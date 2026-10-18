# 本项目固件 ↔ 手册条目 双向映射

> 用途：扩展功能时一眼看出——目标能力**已由哪个模块实现**（复用），**哪些要新写**（对照手册例程）。
> 本项目基于 STM32 HAL 库（非手册的标准库），芯片同款 STM32F103ZET6。
> 主循环结构见 `Core/Src/main.c`，中断见 `Core/Src/stm32f1xx_it.c`。

## 项目功能概述

一个**皮肤电导（GSR）双通道采集仪**：ADC 双通道采电导 → 零点标定 → 滤波 → 麻醉状态判定 → SSD1289 彩屏显示（三行数值 + 双波形 + 状态）→ 串口1 上报上位机；带采集/上报链路停摆监测与看门狗兜底。

## 模块 ↔ 手册条目

| 项目模块 | 职责 | 对应手册条目 | 复用/新写 |
|---------|------|-------------|----------|
| `Core/Src/adc.c` `Core/Inc/adc.h` | ADC1 双通道（IN11=PC1, IN10=PC0），TIM3 TRGO 触发，扫描+DMA | [7.14 ADC 模数转换](../manual-kg/examples/03-id-adc-dac-rtc-mem.md) p.392-400；[7.15 ADC 多路采集](../manual-kg/examples/03-id-adc-dac-rtc-mem.md) p.401 | 参考例程，HAL 重写 |
| `Core/Src/tim.c` | TIM3 定时 500Hz（PSC=71, ARR=1999）作 ADC 触发源 | [7.21 TIMER 定时器中断](../manual-kg/examples/04-timer-wdg-wireless-can.md) p.452-456 | 参考例程 |
| `Core/Src/dma.c` | DMA1_Ch1=ADC1 循环搬运；DMA1_Ch4=USART1_TX | [7.63 DMA 传输](../manual-kg/examples/07-sd-audio-crc-pvd-lp-dma-sensor.md) p.743-748；[7.14](../manual-kg/examples/03-id-adc-dac-rtc-mem.md) | 参考例程 |
| `Core/Src/usart.c` | USART1 115200 8N1（PA9/PA10），DMA 发送 | [7.7 USART 串口1](../manual-kg/examples/02-uart-rs485.md) p.333-347 | 参考例程 |
| `Core/Src/fsmc.c` | FSMC Bank1 第4区（NE4）16 位，驱动 SSD1289 屏 | [7.36 TFT 只显示红色](../manual-kg/examples/06-tft-touch-gui.md) p.602-619（FSMC NE4/RS=A0/0x6C000000） | 参考例程，HAL 重写 |
| `Core/Src/gpio.c` | GPIO 时钟与引脚初始化 | [7.1 LED 流水灯](../manual-kg/examples/01-gpio-key-systick.md) p.273-288 | 参考例程 |
| `Core/Src/BSP/ssd1289_fsmc.c` | SSD1289 屏驱动：初始化、布局、数值/波形/状态渲染 | [7.36–7.54 TFT 显示与触摸](../manual-kg/examples/06-tft-touch-gui.md) p.602-672 | 本项目自研（例程是 ILI9320，屏型号不同） |
| `Core/Src/BSP/bsp_adc_acquire.c` | TIM3+DMA 半区抽取滤波（50Hz 有效样本）、突发干扰剔除、采集停摆/劣化监测与恢复 | [7.14 ADC](../manual-kg/examples/03-id-adc-dac-rtc-mem.md)、[7.63 DMA](../manual-kg/examples/07-sd-audio-crc-pvd-lp-dma-sensor.md) | 本项目自研（例程无此健壮性设计） |
| `Core/Src/BSP/bsp_adc_cal.c` | 上电零点标定（每通道独立）+ 电导换算 G=5·(4095-adc)/adc | [7.14 ADC](../manual-kg/examples/03-id-adc-dac-rtc-mem.md) | 本项目自研 |
| `Core/Src/BSP/bsp_anes.c` | 麻醉状态判定（相对差 + 迟滞） | —（应用层逻辑，手册无对应） | 本项目自研 |
| `Core/Src/BSP/bsp_fault.c` | 故障/劣化时间窗标记（告警红块判据） | —（应用层逻辑） | 本项目自研 |
| `Core/Src/BSP/bsp_iwdg.c` | 独立看门狗，LSI/64/625 ≈1s（**寄存器级**，不用 HAL IWDG） | [7.22 IWDG 独立看门狗](../manual-kg/examples/04-timer-wdg-wireless-can.md) p.457-462 | 参考例程（寄存器写法） |
| `Core/Src/BSP/bsp_uart_report.c` | 串口1 上报：帧组装、DMA 发送、停摆恢复 | [7.7 USART1](../manual-kg/examples/02-uart-rs485.md)、[7.11 Printf 中断收发](../manual-kg/examples/02-uart-rs485.md) p.362-369 | 本项目自研 |
| `Core/Src/BSP/cn_font_16.h` | 16×16 中文点阵字模 | [7.53 TFT 显示中文字](../manual-kg/examples/06-tft-touch-gui.md) p.668-671 | 参考例程（字模概念） |
| `Core/Src/BSP/XGA_8x16.h` | 8×16 ASCII 点阵字模 | [7.52 TFT 显示英文字](../manual-kg/examples/06-tft-touch-gui.md) p.661-667 | 参考例程 |
| `Core/Inc/app_config.h` | 应用参数集中定义（采集/滤波/阈值/量程） | — | 本项目自研 |

## 本项目用到的引脚（对照 [pin-map](../manual-kg/hardware/pin-map.md)）

| 引脚 | 用途 | 手册依据 |
|------|------|---------|
| PC0 | ADC1_IN10，电导2 | 板载 ADC 通道（例程用 PC3，本项目自定义） |
| PC1 | ADC1_IN11，电导1 | 同上 |
| PA9 / PA10 | USART1 TX/RX | 串口1（pin-map） |
| PG12 | FSMC_NE4，LCD 片选 | TFT 例程 NE4 |
| FSMC_A0 | LCD RS（命令/数据选择） | TFT 例程 RS=A0 |
| FSMC_D[0:15] | LCD 16 位数据 | TFT 例程 |

## 本项目**未使用**、但板子具备的能力（扩展候选）

按手册例程可直接参照：

| 能力 | 手册条目 | 硬件 |
|------|---------|------|
| SD 卡存储 / FATFS | [7.55–7.57](../manual-kg/examples/07-sd-audio-crc-pvd-lp-dma-sensor.md) p.673-705 | SDIO PC8~PC12/PD2 |
| 触摸屏输入 | [7.47–7.51](../manual-kg/examples/06-tft-touch-gui.md) p.648-660 | XPT2046, SPI2, PG7 |
| RTC 实时时钟 | [7.17–7.18](../manual-kg/examples/03-id-adc-dac-rtc-mem.md) p.408-431 | 32.768kHz + CR1220 |
| 片外 EEPROM 存配置 | [7.19](../manual-kg/examples/03-id-adc-dac-rtc-mem.md) p.432-447 | 24C02, I2C1 PB6/PB7 |
| 片内 FLASH 存参数 | [7.20](../manual-kg/examples/03-id-adc-dac-rtc-mem.md) p.448-451 | 内部 FPEC |
| 低功耗待机 | [7.61–7.62](../manual-kg/examples/07-sd-audio-crc-pvd-lp-dma-sensor.md) p.724-742 | PWR + WKUP |
| 无线 2.4G | [7.34](../manual-kg/examples/05-spi-sram-nor-nand-24g-radio.md) p.579-586 | nRF24L01, SPI2 |
| 以太网上报 | [7.71](../manual-kg/examples/08-iap-mp3-ir-eth-ucos-lcd.md) p.822-831 | ENC28J60, SPI1 |
| CAN 总线 | [7.27–7.28](../manual-kg/examples/04-timer-wdg-wireless-can.md) p.501-526 | VP230 |
| RS-485 长线 | [7.12](../manual-kg/examples/02-uart-rs485.md) p.370-387 | PF11 + SP3485 |
| 串口 IAP 升级 | [7.68](../manual-kg/examples/08-iap-mp3-ir-eth-ucos-lcd.md) p.786-798 | Flash 分区 |
| uCOS-II 多任务 | [7.72–7.74](../manual-kg/examples/08-iap-mp3-ir-eth-ucos-lcd.md) p.832-839 | 软件 |
| 蜂鸣器告警 | [7.2](../manual-kg/examples/01-gpio-key-systick.md) p.289-294 | PB10 |
| 按键输入 | [7.3–7.5](../manual-kg/examples/01-gpio-key-systick.md) p.295-324 | PA0/PC13/PA8/PD3 |
| 内部温度传感器 | [7.64](../manual-kg/examples/07-sd-audio-crc-pvd-lp-dma-sensor.md) p.749-753 | ADC1_IN16 |
| 唯一 ID 防克隆 | [7.13](../manual-kg/examples/03-id-adc-dac-rtc-mem.md) p.388-391 | 内部 UID |
| PVD 掉电检测 | [7.60](../manual-kg/examples/07-sd-audio-crc-pvd-lp-dma-sensor.md) p.719-723 | PWR |
| 硬件 CRC | [7.59](../manual-kg/examples/07-sd-audio-crc-pvd-lp-dma-sensor.md) p.714-718 | CRC 外设 |

## 注意：本项目与手册的关键差异

1. **库不同**：手册第 7 章用 **ST 标准库**（`SZ_STM32_*`、`GPIO_Init`…），本项目用 **HAL 库**（`HAL_ADC_*`、`HAL_UART_*`…）。照搬例程代码需做 HAL 适配。
2. **屏型号不同**：手册例程驱动 **ILI9320**，本项目驱动 **SSD1289**（入口模式 R0x11=0x6838 打开 BGR 位，故颜色字 R/B 看似交换，见 `ssd1289_fsmc.c`）。
3. **ADC 通道不同**：例程用 PC3(IN13) 单通道电位器；本项目用 PC0(IN10)+PC1(IN11) 双通道电导。
4. **I2C 用法**：手册 7.19 EEPROM 用硬件 I2C1，7.35 收音机用软件模拟 I2C——本项目均未使用。
5. 手册 7.15（ADC 多路采集）、7.74（UCOS+UCGUI）仅有标题无正文。

## 相关

- 总索引 → [`INDEX.md`](INDEX.md)
- 引脚与总线 → [`hardware/pin-map.md`](hardware/pin-map.md)
- 例程速查 → [`examples/README.md`](examples/README.md)
