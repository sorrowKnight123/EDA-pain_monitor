# -*- coding: utf-8 -*-
# 仿真验证 lcd 工程 ADC/滤波/波形绘制的 C 逻辑（逐条复刻 C 实现）

# ---------- 参数（与 main.c / tim.c 一致） ----------
PSC, ARR = 71, 1999
SYSCLK = 72_000_000
TRIG_HZ = SYSCLK / (PSC + 1) / (ARR + 1)          # 期望 500Hz
ADC_BLOCK_TRIG = 10
ADC_DMA_BUF_LEN = 4 * ADC_BLOCK_TRIG              # 40 半字
FILT_N = 8
WF_W, WF_H = 140, 180

def approx(a, b, tol=1e-9): return abs(a - b) < tol
ok = True
def check(name, cond):
    global ok
    print(("PASS " if cond else "FAIL ") + name)
    if not cond: ok = False

# ---------- 1. TIM3 触发率 ----------
check("TIM3 trigger = 500Hz", approx(TRIG_HZ, 500.0))
check("有效输出率 = 50Hz（每块10次触发=20ms）", approx(1.0 / (ADC_BLOCK_TRIG / TRIG_HZ), 50.0))

# ---------- 2. gfilter（复刻 main.c 整数滑动平均） ----------
class GFilter:
    def __init__(self):
        self.buf = [0] * FILT_N
        self.sum = 0
        self.idx = 0
        self.cnt = 0
def gfilter(f, raw):
    f.sum -= f.buf[f.idx]
    f.buf[f.idx] = raw
    f.sum += raw
    f.idx = (f.idx + 1) % FILT_N
    if f.cnt < FILT_N: f.cnt += 1
    return f.sum // f.cnt

# 对照：朴素的 cnt 窗口平均
class Naive:
    def __init__(self): self.hist = []
def naive(n_obj, raw):
    n_obj.hist.append(raw)
    if len(n_obj.hist) > FILT_N: n_obj.hist.pop(0)
    return sum(n_obj.hist) // len(n_obj.hist)

f, n = GFilter(), Naive()
seq = [100, 120, 90, 4000, 800, 1200, 1000, 1100, 1050, 1000, 1000, 1000]
all_match = all(gfilter(f, x) == naive(n, x) for x in seq)
check("gfilter 与朴素滑动平均逐点一致", all_match)
# 窗口满后 O(1) 版本应正确遗忘最老样本
check("gfilter 满窗后遗忘最老样本", f.buf[f.idx % FILT_N] == seq[len(seq) - FILT_N])

# ---------- 3. adc_block_reduce（复刻去极值均值） ----------
def adc_block_reduce(p):        # p: 长度 2*ADC_BLOCK_TRIG 的半区，偶=ch1, 奇=ch2
    vals = [p[i * 2] for i in range(ADC_BLOCK_TRIG)]
    s = sum(vals); mn = min(vals); mx = max(vals)
    return (s - mn - mx) // (ADC_BLOCK_TRIG - 2)
def adc_block_reduce_ch2(p):
    vals = [p[i * 2 + 1] for i in range(ADC_BLOCK_TRIG)]
    s = sum(vals); mn = min(vals); mx = max(vals)
    return (s - mn - mx) // (ADC_BLOCK_TRIG - 2)

block = [1100, 2200, 1105, 2210, 1090, 2190, 1110, 2205, 1102, 2201,
         4000, 2199, 1098, 2203, 1101, 2197, 1099, 2208, 1103, 2200]
# ch1: 去掉野值4000和最小1090 -> (sum-4000-1090)/8
v1 = [b for i, b in enumerate(block) if i % 2 == 0]
exp1 = (sum(v1) - max(v1) - min(v1)) // 8
check("block_reduce 取到的是 ch1（偶下标）且去除野值4000",
      adc_block_reduce(block) == exp1 and 4000 not in [adc_block_reduce(block)])
v2 = [b for i, b in enumerate(block) if i % 2 == 1]
exp2 = (sum(v2) - max(v2) - min(v2)) // 8
check("block_reduce ch2（奇下标）正确", adc_block_reduce_ch2(block) == exp2)

# ---------- 4. DMA 半区索引/交替（复刻 HT/TC 回调） ----------
def dma_sim(trig_seq):
    """trig_seq: [(ch1,ch2)]*N，500Hz 触发序列；DMA 40 半字循环，返回 (half_no, start, 20项) 流"""
    buf = [0] * ADC_DMA_BUF_LEN
    halves = []
    item = 0
    for (c1, c2) in trig_seq:
        buf[item % ADC_DMA_BUF_LEN] = c1
        buf[(item + 1) % ADC_DMA_BUF_LEN] = c2
        item += 2
        if item % (2 * ADC_BLOCK_TRIG) == 0:      # 半区满（20 项）
            start = (item - 2 * ADC_BLOCK_TRIG) % ADC_DMA_BUF_LEN
            half = [buf[(start + k) % ADC_DMA_BUF_LEN] for k in range(2 * ADC_BLOCK_TRIG)]
            halves.append((start, half))
    return halves

trigs = [((1000 + i), (2000 + i * 2)) for i in range(60)]   # 两路各自缓变
halves = dma_sim(trigs)
check("DMA 半区数量 = 触发数/10", len(halves) == 6)
h0_start, h0 = halves[0]
check("半区0起始下标=0", h0_start == 0)
# 半区0 = 触发0..9：偶=ch1(1000..1009), 奇=ch2(2000,2004..)
ch1_ok = all(h0[2 * i] == 1000 + i for i in range(10))
ch2_ok = all(h0[2 * i + 1] == 2000 + 2 * i for i in range(10))
check("半区0偶下标=ch1样本、奇下标=ch2样本", ch1_ok and ch2_ok)
h2_start, h2 = halves[2]
check("半区2回绕到缓冲起始(下标0)", h2_start == 0)
h1_start, h1 = halves[1]
check("半区1起始下标=20（TC半区）", h1_start == 20)
check("半区1内容=触发10..19", all(h1[2 * i] == 1010 + i for i in range(10)))

# ---------- 5. 端到端：有效样本序列 + adc_seq 消费（含回绕） ----------
s = 65535
last = 65534            # 已消费到 65534，下一帧 65535 应消费
consumed = 0
for _ in range(5):      # 跨回绕：65535 -> 0 -> 1 -> 2 -> 3
    s = (s + 1) & 0xFFFF
    if s != last:       # 复刻 main.c: if (adc_seq != last_seq)
        last = s; consumed += 1
check("adc_seq uint16 回绕时消费不卡死", consumed == 5)

# 端到端延迟估算：10ms 块均 + 8点MA群延迟 (8-1)/2*20ms
lat = 20 / 2 + (FILT_N - 1) / 2 * 20
print(f"INFO 端到端滤波群延迟 ≈ {lat:.0f}ms（原 16 点浮点滤波为 {(16-1)/2*20:.0f}ms）")

# ---------- 6. 波形绘制像素一致性：旧3次Rect_Fill vs 新 wave_draw_col ----------
GRAY, GREEN, BLUE = "GRAY", "GREEN", "BLUE"
def old_render_col(lo, hi, a1, b1, a2, b2):
    col = {}
    for y in range(lo, hi + 1): col[y] = GRAY
    if a1 != 0xFF:
        for y in range(a1, b1 + 1): col[y] = GREEN
    if a2 != 0xFF:
        for y in range(a2, b2 + 1): col[y] = BLUE
    return col
def new_render_col(lo, hi, a1, b1, a2, b2):
    col = {}
    for y in range(lo, hi + 1):
        c = GRAY
        if a1 != 0xFF and a1 <= y <= b1: c = GREEN
        if a2 != 0xFF and a2 <= y <= b2: c = BLUE
        col[y] = c
    return col
import random
random.seed(42)
same = True
for _ in range(2000):
    a1 = random.choice([0xFF] + list(range(0, WF_H - 1)))
    b1 = WF_H - 1 if a1 == 0xFF else min(a1 + random.randint(0, 20), WF_H - 1)
    a2 = random.choice([0xFF] + list(range(0, WF_H - 1)))
    b2 = WF_H - 1 if a2 == 0xFF else min(a2 + random.randint(0, 20), WF_H - 1)
    lo = min([v for v in (a1, a2) if v != 0xFF] + [WF_H])
    hi = max([v for v in (b1, b2) if v != 0xFF] + [0])
    if lo > hi: lo, hi = 0, 0
    if old_render_col(lo, hi, a1, b1, a2, b2) != new_render_col(lo, hi, a1, b1, a2, b2):
        same = False; break
check("wave_draw_col 与原 3 次 Rect_Fill 渲染逐像素一致（2000 组随机区间）", same)

# ---------- 7. 满量程映射与象限 ----------
def map_y(G): return int(WF_H - 1 - min(G, 50.0) * (WF_H - 1) / 50.0)
check("G=0 -> y=179（底部）", map_y(0) == 179)
check("G=50 -> y=0（顶部）", map_y(50) == 0)
check("G>50 钳位到 y=0", map_y(80) == 0)

# ---------- 8. adc_to_g 公式 ----------
def adc_to_g(adc):
    a = float(adc)
    return 5.0 * (4095.0 - a) / a if a > 0 else 0.0
check("adc=4095 -> 0μS", approx(adc_to_g(4095), 0.0))
check("adc=2047.5 -> 5μS", approx(adc_to_g(2047.5), 5.0))
check("adc=682.5 -> 25μS", approx(adc_to_g(682.5), 25.0))

print("\n" + ("ALL PASS" if ok else "SOME FAILED"))
