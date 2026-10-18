# 第 5 章 寄存器版本实战（p.193-258）

> 手册第七章是"库函数版"，本章是**寄存器版**——不依赖 ST 标准库，直接操作寄存器。
> 当你想搞清"库函数背后到底做了什么"，或需要在极简/受限环境（Bootloader、时序敏感代码）里裸写时查这里。
> 全文：[`../_text/ch05-寄存器实战篇.md`](_text/ch05-寄存器实战篇.md)

## 5.1 通用输入/输出 GPIO（p.193-208）

**要点**
- STM32 每个 GPIO 端口含 16 个引脚（PA0~PA15）。
- 输入/输出引脚**最大可承受 5V**；驱动能力 2mA/4mA/8mA 可选，单引脚最大 25mA。
- 8 种工作模式：输入（浮空/上拉/下拉/模拟）、输出（推挽/开漏 × 通用/复用）。
- 关键寄存器：
  - `GPIOx_CRL`（配置 Px0~Px7）、`GPIOx_CRH`（配置 Px8~Px15）— 每引脚 4 bit（CNF[1:0]+MODE[1:0]）
  - `GPIOx_IDR` 输入数据、`GPIOx_ODR` 输出数据
  - `GPIOx_BSRR` 置位/复位（写 1 有效）、`GPIOx_BRR` 复位
- 配置 3 步：① 开 GPIO 端口时钟（RCC_APB2ENR）② 配 CRL/CRH 定模式 ③ 读写 ODR/IDR。

**例程**（LED 点灯系列，均为寄存器版）
| 例程 | 页码 | 内容 |
|------|------|------|
| 例程01 | p.196-207 | 单个 LED 点灯闪烁 |
| 例程02 | p.207-208 | LED 双灯闪烁 |
| 例程03 | p.208-209 | LED 三灯同时亮灭 |
| 例程04 | p.209-208 | LED 流水灯 |

> LED 引脚 PF6~PF9，低电平点亮（详见 [pin-map](hardware/pin-map.md)）。

## 5.2 时钟（p.209-220）

**要点**
- 4 个时钟源：**HSE**（外部高速，4~16MHz，板上 8MHz 晶振）、**HSI**（内部 RC，8MHz，不稳）、**LSE**（外部低速 32.768kHz，给 RTC）、**LSI**（内部低速 ~40kHz，给 RTC/IWDG）。
- 上电默认用 HSI，软件配置后切到 HSE+PLL。
- 72MHz 生成路径：HSE 8MHz → PLLXTPRE 不分频 → PLLSRC 选 HSE → **PLLMUL×9 = 72MHz** → SW 选 PLLCLK 作 SYSCLK → AHB 不分频 → APB2 不分频 → GPIO 得 72MHz。
- 关键寄存器：`RCC_CR`（HSEON/HSERDY、PLLON/PLLRDY）、`RCC_CFGR`（SW/SWS、HPRE、PPRE1/2、PLLMUL、PLLSRC、PLLXTPRE）。
- 时钟频率约束：**APB2 最高 72MHz，APB1 最高 36MHz**。
- 常用时钟名：SYSCLK（系统时钟）、HCLK（AHB/内核）、FCLK（自由运行）、PCLK1（APB1 ≤36M）、PCLK2（APB2 ≤72M）。
- ST 启动文件 `SystemInit()` → `SetSysClock()` → `SetSysClockTo72()` 已配好；改主频靠宏 `SYSCLK_FREQ_xxMHz`。

**例程**：32MHz（p.215）、40MHz（p.216）、72MHz（p.217）三个点灯程序，演示手写 RCC 配置。

## 5.3 独立按键（p.221-232）

**要点**
- 按键机械抖动需软件防抖（延时二次确认）。
- **位带操作（BitBand）**：把外设/ SRAM 的某一位映射到一个独立地址，可像普通变量一样原子读写单个位。
  - 外设位带区 `0x4000_0000~0x400F_FFFF` → 别名区 `0x4200_0000~0x43FF_FFFF`
  - SRAM 位带区 `0x2000_0000~0x200F_FFFF` → 别名区 `0x2200_0000~0x23FF_FFFF`
  - 公式：`别名地址 = 别名基址 + (字节偏移×32) + (位号×4)`
- 例程01（p.225）按键点灯无防抖；例程02（p.230）加了防抖。

## 5.4 串口通信的收与发（p.233-258）

**要点**
- 异步串口参数：字长、波特率、奇偶校验、停止位。
- **波特率**：`USARTDIV = fPCLK / (16 × 波特率)`，写入 `USART_BRR`。
- 寄存器：`USART_SR`（TXE/TC/RXNE）、`USART_DR`（数据）、`USART_BRR`（波特率）、`USART_CR1/2/3`（使能/字长/停止位）。
- 神舟III号板载 **USB 转串口 TTL 电平模块**设计（p.246），方便调试。

**例程**
| 例程 | 页码 | 内容 |
|------|------|------|
| 例程01 | p.247 | 最简单串口打印 `$` 字符 |
| 例程02 | p.252 | 单串口打印 `www.armjishu.com`—初级 |
| 例程03 | p.255 | 同上—中级 |
| 例程04 | p.256 | 同上—高级 |
| 例程05 | p.257 | COM1 收发—初级 |
| 例程06 | p.258 | COM1 收发—中级 |
| 例程07 | p.258 | COM1 收发—高级 |

## 相关

- 库函数版对应章节 → [`../examples/`](examples/)（第 7 章）
- 时钟总线归属 → [pin-map.md](hardware/pin-map.md#时钟总线归属配-rcc-时用)
