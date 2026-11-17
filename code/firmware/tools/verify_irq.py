# -*- coding: utf-8 -*-
"""中断接线核对：确认每个"在源码里使能了 NVIC"的中断，在最终固件里都接到了真实处理函数，
而不是启动文件的 weak Default_Handler。

背景（本脚本要防的坑）：
  USART1_IRQn 已使能、却漏写 USART1_IRQHandler —— 编译链接全部通过、0 警告，
  但 DMA 发送完成后 HAL 只清 CR3.DMAT 并打开 CR1.TCIE，gState 仍为 BUSY_TX，
  必须靠 TC 中断里的 UART_EndTransmit_IT() 才置回 READY。
  TC 中断无人服务 → gState 永久 BUSY_TX → 串口只发出上电第一帧，之后全部被忙检查丢弃。

判据（不依赖易错的 IRQ 编号推导，直接查固件事实）：
  1) 该处理函数符号是否真的存在于固件中（未被 --gc-sections 剔除）
  2) 启动文件是否为它在向量表里留了槽位（R_ARM_ABS32 重定位）
  3) 固件向量表该槽位的实际值是否等于该处理函数地址

用法：python tools/verify_irq.py   （需先运行 tools/build_check.ps1）
"""
import io, os, re, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TC = r"E:\STM32CubeIDE\STM32CubeIDE\plugins"
ELF = os.path.join(ROOT, "build_check", "lcd.elf")
STARTUP_OBJ = os.path.join(ROOT, "build_check", "startup.o")

# 源码里使能了 NVIC、因而必须存在处理函数的"外设"中断。
# （Cortex-M 系统异常另有 startup 默认实现，不在此列）
EXPECTED = ["ADC1_2_IRQn", "DMA1_Channel1_IRQn", "DMA1_Channel4_IRQn",
            "TIM3_IRQn", "USART1_IRQn"]


def tool(name):
    for d in os.listdir(TC):
        if "gnu-tools-for-stm32" in d:
            p = os.path.join(TC, d, "tools", "bin", name)
            if os.path.exists(p):
                return p
    sys.exit("未找到 %s" % name)


if not (os.path.exists(ELF) and os.path.exists(STARTUP_OBJ)):
    sys.exit("未找到 %s / startup.o，请先运行 tools/build_check.ps1" % ELF)

# 1) 启动文件向量表重定位：偏移 -> 处理函数符号
relocs = {}
out = subprocess.run([tool("arm-none-eabi-objdump.exe"), "-r", "-j", ".isr_vector", STARTUP_OBJ],
                     capture_output=True, text=True).stdout
for line in out.splitlines():
    m = re.match(r"\s*([0-9a-f]+)\s+R_ARM_ABS32\s+(\S+)", line)
    if m:
        relocs[int(m.group(1), 16)] = m.group(2)

# 2) 固件符号表（只收强定义：小写/大写 T 表示代码段强符号；
#    weak 的 'W' 是启动文件 Default_Handler 的别名，不算"本工程实现了处理函数"）
syms, addr2name = {}, {}
out = subprocess.run([tool("arm-none-eabi-nm.exe"), ELF], capture_output=True, text=True).stdout
for line in out.splitlines():
    m = re.match(r"^\s*([0-9a-f]{8})\s+([Tt])\s+(\S+)$", line)
    if m:
        syms[m.group(3)] = int(m.group(1), 16)
        addr2name[int(m.group(1), 16)] = m.group(3)

# 3) 向量表二进制
vec_bin = os.path.join(ROOT, "build_check", "vec.bin")
subprocess.run([tool("arm-none-eabi-objcopy.exe"), "-O", "binary",
                "--only-section=.isr_vector", ELF, vec_bin], check=True)
vec = io.open(vec_bin, "rb").read()

# 4) 源码中实际调用了 HAL_NVIC_EnableIRQ 的中断（扫描 Core 下全部 .c，先剔除注释）
src_enabled = set()
for dirpath, _, files in os.walk(os.path.join(ROOT, "Core")):
    for fn in files:
        if not fn.endswith(".c"):
            continue
        txt = io.open(os.path.join(dirpath, fn), encoding="utf-8", errors="replace").read()
        txt = re.sub(r"/\*.*?\*/", "", txt, flags=re.S)      # 去块注释
        txt = re.sub(r"//[^\n]*", "", txt)                   # 去行注释
        for m in re.finditer(r"HAL_NVIC_EnableIRQ\((\w+)\)", txt):
            src_enabled.add(m.group(1))

names = sorted(set(EXPECTED) | src_enabled)
print("待核对中断：%d 个（源码使能 %d 个）" % (len(names), len(src_enabled)))
print()
fail = 0
for irqn in names:
    handler = irqn.replace("_IRQn", "") + "_IRQHandler"   # USART1_IRQn -> USART1_IRQHandler
    # 双向核对：
    #   A) 源码使能了 NVIC  → 固件里必须有真实处理函数并接上向量
    #   B) 向量接的是本工程自己的处理函数（未被 gc-sections 剔除）→ 源码必须使能 NVIC
    # 只查 A 会漏掉"处理函数写好了、却忘了 HAL_NVIC_EnableIRQ"这一半故障
    in_fw = handler in syms
    off = next((o for o, s in relocs.items() if s == handler), None) if in_fw else None
    wired = (off is not None and
             (struct.unpack_from("<I", vec, off)[0] & 0xFFFFFFFE) == syms[handler])
    enabled = irqn in src_enabled

    if enabled and not wired:
        print("  FAIL %-24s 已使能 NVIC，但固件中未接到 %s（漏实现？被 gc-sections 剔除？）"
              % (irqn, handler))
        fail += 1
    elif wired and not enabled:
        print("  FAIL %-24s 已实现 %s 并接入向量，但源码从未 HAL_NVIC_EnableIRQ（中断永不触发！）"
              % (irqn, handler))
        fail += 1
    elif wired and enabled:
        actual = struct.unpack_from("<I", vec, off)[0] & 0xFFFFFFFE
        print("  OK   %-24s 已使能 → 向量@0x%03X → %s" % (irqn, off, addr2name[actual]))
    else:
        print("  --   %-24s 未使能且未接入（不需要）" % irqn)

print()
if fail == 0:
    print("PASS: 所有已使能中断都正确接到真实处理函数")
else:
    print("FAIL: %d 个中断接线错误" % fail)
    sys.exit(1)

