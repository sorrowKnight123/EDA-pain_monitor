# -*- coding: utf-8 -*-
# 仿真验证 lcd 工程 ADC/滤波/波形绘制的 C 逻辑（逐条复刻 C 实现）

import os, re

# ---------- 参数：直接从源码读取，避免测试与实现脱节 ----------
# （曾经把 G_RANGE_MAX 写死成 200.0、限幅上限写死成 999.99，实现改了测试却照样 PASS）
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def read_src(rel):
    with open(os.path.join(ROOT, rel), encoding='utf-8') as fh:
        return fh.read()

CFG    = read_src(os.path.join('Core', 'Inc', 'app_config.h'))
LCD_H  = read_src(os.path.join('Core', 'Inc', 'BSP', 'ssd1289_fsmc.h'))
LCD_C  = read_src(os.path.join('Core', 'Src', 'BSP', 'ssd1289_fsmc.c'))
MAIN_C = read_src(os.path.join('Core', 'Src', 'main.c'))
CN_H   = read_src(os.path.join('Core', 'Inc', 'BSP', 'cn_font_16.h'))
HAL_ADC_C = read_src(os.path.join('Drivers', 'STM32F1xx_HAL_Driver', 'Src', 'stm32f1xx_hal_adc.c'))
HAL_DMA_C = read_src(os.path.join('Drivers', 'STM32F1xx_HAL_Driver', 'Src', 'stm32f1xx_hal_dma.c'))

def macro(text, name, env=None):
    """取 #define <name> 的值并在已解析的宏环境里求值（支持 WF_W 这类宏间引用）。
    看到 '/'（注释）即止；去掉 C 的 f/u/l 后缀"""
    m = re.search(r'^\s*#define\s+%s\s+([^\r\n/]+)' % re.escape(name), text, re.M)
    if not m:
        raise KeyError('未在源码中找到宏 ' + name)
    expr = re.sub(r'\b(\d+(?:\.\d*)?)[fFuUlL]+\b', r'\1', m.group(1).strip())
    val = eval(expr, {'__builtins__': {}}, env if env is not None else {})
    return val

# 按依赖顺序解析布局宏：布局参数 -> 派生量，全部取自源码
_M = {}
def layout(name, src=LCD_H, cast=None):
    v = macro(src, name, _M)
    v = cast(v) if cast else v
    _M[name] = v
    return v

SCREEN_W = layout('SCREEN_W', cast=int)
SCREEN_H = layout('SCREEN_H', cast=int)
SPLIT_X  = layout('LAYOUT_SPLIT_X', cast=int)
SPLIT_Y  = layout('LAYOUT_SPLIT_Y', cast=int)
VAL_Y1   = layout('LAYOUT_VAL_Y1', cast=int)
VAL_PITCH = layout('LAYOUT_VAL_PITCH', cast=int)
STATUS_Y = layout('LAYOUT_STATUS_Y', cast=int)
DEBUG_Y  = layout('LAYOUT_DEBUG_Y', cast=int)
VAL_W_REF = layout('VAL_W_REF', cast=int)
SCALE_X_TICK = layout('SCALE_X_TICK', cast=int)
WF_X0 = layout('WF_X0', cast=int)
WF_W  = layout('WF_W', cast=int)
WF_H  = layout('WF_H', cast=int)

SYSCLK = 72_000_000
PSC, ARR = 71, 1999
TRIG_HZ = SYSCLK / (PSC + 1) / (ARR + 1)          # 期望 500Hz
ADC_BLOCK_TRIG = int(macro(CFG, 'ADC_BLOCK_TRIG'))
ADC_DMA_BUF_LEN = 4 * ADC_BLOCK_TRIG              # 40 半字
FILT_N = int(macro(CFG, 'FILT_N'))
G_RANGE_MAX = macro(CFG, 'G_RANGE_MAX')
ON_PCT  = int(macro(CFG, 'G_STATE_ON_PCT'))
OFF_PCT = int(macro(CFG, 'G_STATE_OFF_PCT'))
CAL_ADC_MIN = int(macro(CFG, 'CAL_ADC_MIN'))
CAL_ADC_MAX = int(macro(CFG, 'CAL_ADC_MAX'))
CAL_SPREAD_MAX = int(macro(CFG, 'CAL_SPREAD_MAX'))
CAL_SAMPLES = int(macro(CFG, 'CAL_SAMPLES'))
SPREAD_ABS = int(macro(CFG, 'ADC_BLOCK_SPREAD_ABS'))
SPREAD_PCT = int(macro(CFG, 'ADC_BLOCK_SPREAD_PCT'))
# 限幅上限：从 LCD_ValStr 实现里抓（写死过一次就是这个值没跟着改）
_m = re.search(r'if\s*\(\s*G\s*>\s*([0-9.]+)f\s*\)\s*G\s*=', LCD_C)
CLAMP_HI = float(_m.group(1))
print("INFO 从源码读到参数：G_RANGE_MAX=%g 限幅上限=%g ON/OFF=%d%%/%d%% "
      "CAL_ADC=%d..%d CAL_SPREAD_MAX=%d" %
      (G_RANGE_MAX, CLAMP_HI, ON_PCT, OFF_PCT, CAL_ADC_MIN, CAL_ADC_MAX, CAL_SPREAD_MAX))
print("INFO 布局：屏 %dx%d  波形区 x=%d..%d (宽 %d) y=0..%d  刻度线原起始列=%d"
      % (SCREEN_W, SCREEN_H, WF_X0, WF_X0 + WF_W - 1, WF_W, WF_H - 1, SCALE_X_TICK))

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

# ---------- 3b. 块级突发干扰剔除（复刻 main.c adc_block_reduce 的判据） ----------
def block_reduce_checked(vals):
    """返回 (是否有效, 均值)。判据与 C 一致：
       用「去极值后剩余样本」的极差 (次大-次小) 判据：
       (mx2-mn2) > mn1*PCT/100 + ABS  → 该块受扰作废"""
    s = sum(vals)
    srt = sorted(vals)
    mn1, mn2 = srt[0], srt[1]
    mx2, mx1 = srt[-2], srt[-1]
    if (mx2 - mn2) > (mn1 * SPREAD_PCT // 100 + SPREAD_ABS):
        return False, None
    return True, (s - mx1 - mn1) // (ADC_BLOCK_TRIG - 2)

# a) 干净块必须通过（阈值不能误杀正常数据）
import random
random.seed(11)
ok_all = True
for _ in range(2000):
    base = random.randint(200, 3900)
    vals = [base + random.randint(-6, 6) for _ in range(ADC_BLOCK_TRIG)]   # 正常噪声本底
    good, _ = block_reduce_checked(vals)
    if not good: ok_all = False; print("   误杀:", base, vals)
check("正常噪声本底（±6 码）不被误判为受扰（2000 组随机块）", ok_all)

# b) 单个野值：去极值本身就能挡，不该判为"受扰"（避免多计 K 计数）
single = [1000, 1002, 1001, 1003, 999, 1001, 5000, 1002, 1000, 1001]
good, val = block_reduce_checked(single)
check("单个野值(5000)由去极值吸收，不触发块作废", good and val == (sum(single) - 5000 - 999) // 8)

# c) 持续突发干扰（跨多个采样点）：这才是本次要拦的场景
burst = [1000, 1002, 3100, 3050, 2980, 3000, 1001, 999, 1003, 1000]   # 4 个连续大点
good, _ = block_reduce_checked(burst)
check("连续 4 点的突发干扰被判为受扰块（去极值无法吸收）", not good)

# d) 阈值随 adc 自适应：同样的码差，在低 adc（高电导）阈值更严
#    注意：判据取"次大-次小"，所以探针值必须出现 ≥2 次才会进入 mx2/mn2
spread = 300
hi_adc = [4000, 4000 + spread, 4000, 4000 + spread] + [4000] * 6   # adc 大 → 阈值宽
lo_adc = [500, 500 + spread, 500, 500 + spread] + [500] * 6        # adc 小 → 阈值严
gh, _ = block_reduce_checked(hi_adc)
gl, _ = block_reduce_checked(lo_adc)
print(f"INFO 同样 {spread} 码差（各出现2次）: adc≈4000 判{'有效' if gh else '受扰'}, "
      f"adc≈500 判{'有效' if gl else '受扰'}")
check("阈值随 adc 自适应（相对项生效，未退化成固定阈值）", gh and not gl)

# e) 绝对下限生效：adc 很小时相对项接近 0，靠 ABS 兜底
#    mn≈10 → 相对项 = 0，阈值 = ABS = 200；令次大-次小 = ABS+50 > 200
tiny = [10, 10 + SPREAD_ABS + 50, 10, 10 + SPREAD_ABS + 50] + [10] * 6
gt, _ = block_reduce_checked(tiny)
check("adc 极小时仍能被绝对下限拦住（相对项失效场景）", not gt)

# f) 边界：次大-次小 恰好等于阈值应放行，超过 1 码应拦下
mn_b = 1000
limit = mn_b * SPREAD_PCT // 100 + SPREAD_ABS      # = 250
eq    = [mn_b, mn_b + limit,     mn_b, mn_b + limit]     + [mn_b] * 6   # 极差 == limit
over  = [mn_b, mn_b + limit + 1, mn_b, mn_b + limit + 1] + [mn_b] * 6   # 极差 == limit+1
gt, _ = block_reduce_checked(eq)
go, _ = block_reduce_checked(over)
check("边界：极差 == 阈值放行、阈值+1 拦下（无差一错误）", gt and not go)

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

# ---------- 7. 满量程映射与象限（G_RANGE_MAX 从 app_config.h 读取） ----------
def map_y_raw(G):
    """复刻 C：未做任何夹取时的映射（用来证明旧实现会画到区域外）"""
    return int((WF_H - 1) - G * (WF_H - 1) / G_RANGE_MAX)
def map_y(G):
    """复刻 C wave_push_pair：先夹取到 [0, G_RANGE_MAX] 再映射"""
    if not (G >= 0.0): G = 0.0          # NaN 也会走进来
    if G > G_RANGE_MAX: G = G_RANGE_MAX
    return int((WF_H - 1) - G * (WF_H - 1) / G_RANGE_MAX)
check("G=0 -> y=%d（底部）" % (WF_H - 1), map_y(0) == WF_H - 1)
check("G=G_RANGE_MAX -> y=0（顶部）", map_y(G_RANGE_MAX) == 0)
check("G=G_RANGE_MAX/2 -> y=%d（中点）" % ((WF_H - 1) // 2), map_y(G_RANGE_MAX / 2) == (WF_H - 1) // 2)
check("G>G_RANGE_MAX 钳位到 y=0", map_y(G_RANGE_MAX * 2) == 0)

# ---------- 7b. 布局：刻度线删除后腾出的列必须并入波形区 ----------
check("波形区右边界顶到屏幕右缘（x=%d = SCREEN_W-1）" % (SCREEN_W - 1),
      WF_X0 + WF_W - 1 == SCREEN_W - 1)
check("波形区左边界紧贴左右分隔线右侧（x=%d）" % (SPLIT_X + 1), WF_X0 == SPLIT_X + 1)
check("波形区已覆盖原刻度短线起始列 %d（该列现在画波形）" % SCALE_X_TICK,
      WF_X0 <= SCALE_X_TICK <= WF_X0 + WF_W - 1)
old_w = SCALE_X_TICK - WF_X0                      # 取消刻度前的宽度
print("INFO 波形区宽度 %d 列（取消刻度前 %d 列，并入 %d 列）" % (WF_W, old_w, WF_W - old_w))
check("宽度确实比取消刻度前更宽（确实用上了腾出的空间）", WF_W > old_w)
check("并入的列数 == 原刻度线区域的列数（%d）" % (SCREEN_W - SCALE_X_TICK),
      WF_W - old_w == SCREEN_W - SCALE_X_TICK)
# 波形区不得越过上下分隔线：最后一行 y=WF_H-1 必须正好在分隔线 SPLIT_Y 之上
check("波形区底边 y=%d 停在分隔线 y=%d 之上" % (WF_H - 1, SPLIT_Y),
      WF_H == SPLIT_Y and WF_H - 1 < SPLIT_Y)
# 数值列：标签 + 参考数值宽度必须放得进左栏
lab_g1 = 4 * 16 + 8 + 8                            # "皮肤电导1:" = 4 汉字 + '1' + ':'
check("左栏放得下\"标签+数值\"（%d+%d=%d <= %d）" % (lab_g1, VAL_W_REF, lab_g1 + VAL_W_REF, SPLIT_X - 1),
      lab_g1 + VAL_W_REF <= SPLIT_X - 1)
# 底部两行不得重叠、不得超出屏高
check("状态行 %d..%d 与调试行 %d..%d 不重叠且都在屏内" % (STATUS_Y, STATUS_Y + 15,
                                                       DEBUG_Y, DEBUG_Y + 15),
      STATUS_Y + 16 <= DEBUG_Y and DEBUG_Y + 16 <= SCREEN_H)
# 数值三行不得压到分隔线
check("数值三行底边 %d 在分隔线 %d 之上" % (VAL_Y1 + 2 * VAL_PITCH + 15, SPLIT_Y),
      VAL_Y1 + 2 * VAL_PITCH + 15 < SPLIT_Y)
# 三行标签与数值不得横向重叠：数值列起点 = 标签起点 + 标签宽
check("数值列宽 %d 放得下最长数值 7 字符（56px）" % VAL_W_REF, 7 * 8 <= VAL_W_REF)

# ---------- 8. adc_to_g 公式（main.c：减本通道零点、乘 30、负值截零） ----------
# 零点现在只有一个来源：上电标定。测试里取一个代表性静息基线当零点
# （adc=2925 -> 正好 2.0μS），等价于"标定窗口取到了 2925 这个均值"
ZERO_ADC = 2925
def g_raw(adc):
    """复刻 main.c adc_g_raw：G = 5*(4095-adc)/adc（前端 0..5μS 量程），adc=0 → 0"""
    a = float(adc)
    return 5.0 * (4095.0 - a) / a if a > 0 else 0.0

ZERO = g_raw(ZERO_ADC)                            # = 2.0 μS
check("测试用零点取自标定式：g_raw(%d) = 2.0μS" % ZERO_ADC, approx(ZERO, 2.0))

def adc_to_g(adc, off=None):
    """复刻 main.c adc_to_g：(g_raw - off) * 30，负值在此统一截零。
    off 省略时用上面的代表性零点。截零放在这里（而不是只放在格式化函数里）
    是现场故障的关键修复点：屏上三行、差值、状态判定、波形、串口全部取自
    本函数，各处必然自洽"""
    g = (g_raw(adc) - (ZERO if off is None else off)) * 30.0
    return g if g > 0.0 else 0.0

check("adc=1365 -> g_raw=10，(10-2)*30 = 240", approx(adc_to_g(1365), 240.0))
check("adc=2730 -> g_raw=2.5，(2.5-2)*30 = 15", approx(adc_to_g(2730), 15.0))
# 低于本通道零点时公式为负（adc=4095 -> (0-2)*30 = -60），现在在源头就截零
check("adc=4095 -> 0（负电导在 adc_to_g 内截零，不再靠格式化兜底）", adc_to_g(4095) == 0.0)
check("adc=%d（恰在零点上）-> 0" % ZERO_ADC, adc_to_g(ZERO_ADC) == 0.0)
check("adc=0 -> 0（g_raw 对 adc=0 的保护）", adc_to_g(0) == 0.0)
check("全量程 0..4095 输出恒非负", all(adc_to_g(a) >= 0.0 for a in range(4096)))

# ---------- 9. 采集停摆/劣化监测状态机（复刻 main.c adc_stall_monitor + adc_rate_low） ----------
R_MS   = int(macro(CFG, 'ADC_STALL_RECOVER_MS'))
RATE_WIN = int(macro(CFG, 'ADC_STALL_RATE_WIN_MS'))
MIN_N  = int(macro(CFG, 'ADC_STALL_MIN_SAMPLES'))
B_MS   = int(macro(CFG, 'ADC_STALL_BURST_MS'))
LIMIT  = int(macro(CFG, 'ADC_STALL_RESET_LIMIT'))
NOMINAL_HZ = TRIG_HZ / ADC_BLOCK_TRIG          # 额定有效样本率 50Hz

def stall_sim(freeze_ranges, sim_ms=30000, rate_hz=None):
    """freeze_ranges: [(起,止)] seq 停止增长的时间段(ms)。
       rate_hz: 非停摆期间的实际样本率（Hz，默认额定）。用于模拟"还活着但变慢"。
       返回 (恢复次数, 是否复位)"""
    seen_seq, seen_tick, last_stall_tick, burst = 0, 0, 0, 0
    rate_mark, rate_tick, primed = 0, 0, False
    seq_f, recoveries, reset = 0.0, 0, False
    frozen = set()
    for a, b in freeze_ranges: frozen.update(range(a, b))
    hz = NOMINAL_HZ if rate_hz is None else rate_hz
    for now in range(sim_ms):
        if now not in frozen: seq_f += hz / 1000.0
        seq = int(seq_f)
        if seq != seen_seq:
            seen_seq, seen_tick = seq, now

        # 判据②：统计窗口内样本数不足
        low = 0
        if not primed:
            primed, rate_tick, rate_mark = True, now, seq
        elif now - rate_tick >= RATE_WIN:
            low = 1 if (seq - rate_mark) < MIN_N else 0
            rate_tick, rate_mark = now, seq

        if (now - seen_tick < R_MS) and not low:
            continue
        recoveries += 1
        burst = burst + 1 if (now - last_stall_tick <= B_MS) else 1
        # 恢复后两个窗口都重新起算（否则停摆还留在统计窗里，会被重复计一次）
        last_stall_tick = seen_tick = rate_tick = now
        rate_mark = seq
        if burst >= LIMIT:
            reset = True; break
    return recoveries, reset

r, rst = stall_sim([])
check("无停摆 → 不恢复不复位（样本率判据不误报）", r == 0 and not rst)
r, rst = stall_sim([(1000, 1400)])
check("单次400ms停摆 → 恢复1次、300ms时触发、不复位", r == 1 and not rst)
r, rst = stall_sim([(1000, 29999)])
check("永久停摆 → 连续3次后复位", r == 3 and rst)
r, rst = stall_sim([(1000, 1400), (25000, 25400)])
check("两次相隔远的停摆 → 恢复2次不复位（burst窗口外重新计数）", r == 2 and not rst)
r, rst = stall_sim([(1000, 1400), (3000, 3400)])
check("两次2s内连发停摆 → 恢复2次不复位（未到上限）", r == 2 and not rst)
r, rst = stall_sim([(1000, 1400), (3000, 3400), (5000, 5400)])
check("三次2s间隔连发停摆 → 第3次后复位", r == 3 and rst)

# 判据②：DMA TE 后 ADC ERROR_DMA 位没清 → 只剩半区回调 → 额定 50Hz 掉到 25Hz。
# 这种"还活着但腰斩"必须能被抓到，否则只靠"seq 是否增长"永远发现不了。
r, rst = stall_sim([], rate_hz=NOMINAL_HZ / 2)
print("INFO 半速静默劣化（%.0fHz）：恢复 %d 次、复位=%s" % (NOMINAL_HZ / 2, r, rst))
check("判据②能抓到半速静默劣化（seq 一直在涨，判据①抓不到）", r > 0)
r, rst = stall_sim([], rate_hz=NOMINAL_HZ * 0.9)   # 45Hz：正常波动范围，不该误报
check("判据②对轻微掉速（90%%）不误报", r == 0 and not rst)
check("判据②阈值留有余量：%d < 额定 %.0f 且 > 半速 %.0f" % (MIN_N, NOMINAL_HZ, NOMINAL_HZ / 2),
      NOMINAL_HZ / 2 < MIN_N < NOMINAL_HZ)

# ---------- 10. 串口1 上报帧格式（复刻 main.c adc_to_g100 + uart_send_g100） ----------
# 参数与 app_config.h 一致：分隔符为源码里的 UART_FRAME_SEP 字符串（默认 UTF-8 全角逗号）
UART_FRAME_SEP = u"，"
UART_FRAME_BUF = 32
UART_RATE_HZ = TRIG_HZ / ADC_BLOCK_TRIG      # 与有效样本同频 50Hz
UART_BAUD = 115200

def lcd_val_str(G):
    """复刻 BSP 的 LCD_ValStr（屏上数值与串口共用）：
       G<0 → 0；G>限幅上限（从源码读取）→ 限幅；v = (uint32_t)(G*100 + 0.5)
       小数位随量级自适应：v<1000 → 2位；v<10000 → 1位（对 v 四舍五入到 0.1）；否则取整"""
    if G < 0.0: G = 0.0
    if G > CLAMP_HI: G = CLAMP_HI
    v = int(G * 100.0 + 0.5)
    if v < 1000:
        return "%d.%02d" % (v // 100, v % 100)
    if v < 10000:
        r = (v + 5) // 10                     # v 为正数，// 等价于 C 的整数除法
        return "%d.%01d" % (r // 10, r % 10)
    return "%d" % ((v + 50) // 100)

def uart_frame(g1, g2):
    """复刻 main.c：两个值各自经 adc_to_g + LCD_ValStr 得到字符串，再拼成帧"""
    return (lcd_val_str(adc_to_g(g1)) + UART_FRAME_SEP +
            lcd_val_str(adc_to_g(g2)) + "\n").encode('utf-8')

# 核心要求：串口字面 == 屏上字面（两处都走 adc_to_g + LCD_ValStr）
# 对同一 adc，串口帧里拆出的字符串必须逐字符等于屏上数值字符串
mismatch = []
for a in range(0, 4096):
    want = lcd_val_str(adc_to_g(a))
    got = uart_frame(a, a).decode('utf-8').strip().split(UART_FRAME_SEP)[0]
    if got != want:
        mismatch.append((a, got, want))
check("串口帧解析出的数值 == 屏上数值（全量程 0..4095）", not mismatch)
if mismatch:
    for a, g, w in mismatch[:8]:
        print("    adc=%4d 串口=%s 屏上=%s" % (a, g, w))

# 公式验算点（×30 缩放，小数位自适应）
check("adc=1365 -> \"240\"（>=100 取整）", lcd_val_str(adc_to_g(1365)) == "240")
check("adc=2730 -> \"15.0\"（10~100 一位小数）", lcd_val_str(adc_to_g(2730)) == "15.0")
check("adc=4095 -> \"0.00\"（负值兜底，<10 两位小数）", lcd_val_str(adc_to_g(4095)) == "0.00")
check("adc=1 -> \"2000\"（超上限：先限幅再取整）", lcd_val_str(adc_to_g(1)) == "2000")

# 自适应分档边界（不能出现"末位无意义"或"跳位"）
check("分档边界: 9.99 → \"9.99\"（两位）", lcd_val_str(9.99) == "9.99")
check("分档边界: 10.00 → \"10.0\"（一位）", lcd_val_str(10.00) == "10.0")
check("分档边界: 99.99 → \"100.0\"（一位四舍五入）", lcd_val_str(99.99) == "100.0")
check("分档边界: 100.0 → \"100\"（整数）", lcd_val_str(100.0) == "100")
check("分档边界: 999.99 → \"1000\"（>=100 取整）", lcd_val_str(999.99) == "1000")
# 限幅上限：数值取自 LCD_ValStr 实现本身（这一项以前漏测，导致"上限改 2000"未生效也没被发现）
check("限幅上限 = %g（从 ssd1289_fsmc.c 读到）" % CLAMP_HI, approx(CLAMP_HI, 1999.99, 0.01))
check("上限: CLAMP_HI → \"2000\"（限幅后取整）", lcd_val_str(CLAMP_HI) == "2000")
check("上限: 5000.0 被限幅到 CLAMP_HI → \"2000\"（不是 999.99 的旧上限）",
      lcd_val_str(5000.0) == "2000")
check("限幅常量确为 %g（旧值 999.99 会让 1500 显示成 1000）" % CLAMP_HI,
      lcd_val_str(1500.0) == "1500")

# 两种精度的四舍五入（避开正好落在 .x5 的平局点：那里会有一致但无意义的 ±1 偏差）
check("四舍五入(两位): 9.344→\"9.34\"，9.346→\"9.35\"",
      lcd_val_str(9.344) == "9.34" and lcd_val_str(9.346) == "9.35")
check("四舍五入(一位): 54.04→\"54.0\"，54.05→\"54.1\"",
      lcd_val_str(54.04) == "54.0" and lcd_val_str(54.05) == "54.1")
check("四舍五入(整数): 149.4→\"149\"，149.6→\"150\"",
      lcd_val_str(149.4) == "149" and lcd_val_str(149.6) == "150")

# 分档规则：显示值越大幅度越大时，有效位数恒为 3 位左右（信息量不虚报）
print("INFO 自适应分档示例：")
for g in (0.0, 5.0, 9.99, 10.0, 54.05, 99.99, 100.0, 279.0, 999.99):
    print("     %8.2f → \"%s\"" % (g, lcd_val_str(g)))

# 帧内容：需求格式 "<电导1>，<电导2>"
f = uart_frame(1365, 2730)
check("帧按 UTF-8 解码为 \"240，15.0\\n\"", f.decode('utf-8') == u"240，15.0\n")

# 解析回路：上位机按分隔符 split 能还原两个值
a, b = f.decode('utf-8').strip().split(UART_FRAME_SEP)
check("按分隔符可解析回两个数值", a == "240" and b == "15.0")

# 帧长上界 ≤ 缓冲（uart_send_g100 用 n < sizeof uart_buf 做截断保护，需保证不截断）
max_frame = uart_frame(1, 1)          # 触发上限（CLAMP_HI → "2000"）
check("上限值帧 = \"2000，2000\\n\"（%d B）" % len(max_frame), max_frame.decode('utf-8') == u"2000，2000\n")
check(f"最大帧长 {len(max_frame)}B ≤ 缓冲 {UART_FRAME_BUF}B", len(max_frame) <= UART_FRAME_BUF)
check("单值缓冲 8B 足够（最长 \"1999.99\" 7 字符 + '\\0'）", len("1999.99") + 1 <= 8)
check("显示列宽：最长 \"1999.99\" 7 字符 = 56px ≤ 列宽 %dpx" % VAL_W_REF, len("1999.99") * 8 <= VAL_W_REF)

# 带宽/占用率：保证 50Hz 持续发送不会积压（>1 帧周期即会触发丢帧）
bits_per_frame = len(max_frame) * 10          # 8N1 = 10 位/字节
occupancy = bits_per_frame / UART_BAUD * UART_RATE_HZ
print(f"INFO 串口占用率 ≈ {occupancy*100:.1f}%（{len(max_frame)}B/帧 × {UART_RATE_HZ:.0f}Hz @ {UART_BAUD}bps）")
check("串口占用率 < 30%（丢帧仅可能来自瞬时排空延迟，不会持续积压）", occupancy < 0.30)

# ---------- 11. 麻醉状态判定：相对判据 + 迟滞（复刻 LCD_DrawSkinText 状态机） ----------
def decide(g1, g2, last):
    """复刻 C：相对差 = |G1-G2|/max(G1,G2)*100；已是麻醉完全时用退出阈值构成迟滞。
    mx<=0 时直接判未麻醉完全（两路都在零点上时 0>=0 会误判）"""
    diff = abs(g1 - g2)
    mx = max(g1, g2)
    d100 = diff * 100.0
    if mx <= 0.0:
        return 0
    if last == 1:
        return 1 if d100 > mx * OFF_PCT else 0
    return 1 if d100 >= mx * ON_PCT else 0

# 测试点全部按实际阈值推导，不写死（阈值被改过：曾经写死 23%/30%/15% 就全部失效）
def pair_for(rel_pct):
    """构造一对 G，使其相对差恰为 rel_pct%"""
    a = 100.0
    return a, a * (1 - rel_pct / 100.0)
BAND_MID  = (ON_PCT + OFF_PCT) / 2.0        # 迟滞带中点
ABOVE_ON  = ON_PCT + (100.0 - ON_PCT) / 2.0 # 明确高于 ON
BELOW_OFF = OFF_PCT / 2.0                   # 明确低于 OFF
check("阈值配置合法：0 < OFF < ON <= 100（迟滞方向正确）", 0 < OFF_PCT < ON_PCT <= 100)

# a) 迟滞的实质：ON 与 OFF 之间必须"保持原状态"，否则单阈值会在边界抖动
g1, g2 = pair_for(BAND_MID)
rel = abs(g1 - g2) / max(g1, g2) * 100
print(f"INFO 迟滞带测试：G1={g1} G2={g2:.2f} 相对差={rel:.1f}%（介于 {OFF_PCT}% 与 {ON_PCT}% 之间）")
check("迟滞带内：原状态=未麻醉完全 → 保持未麻醉完全", decide(g1, g2, 0) == 0)
check("迟滞带内：原状态=麻醉完全 → 保持麻醉完全", decide(g1, g2, 1) == 1)

# b) 迟滞带外应能正常切换
check("相对差 %.1f%% > ON(%d%%) → 切到麻醉完全" % (ABOVE_ON, ON_PCT), decide(*pair_for(ABOVE_ON), 0) == 1)
check("相对差 %.1f%% < OFF(%d%%) → 切到未麻醉完全" % (BELOW_OFF, OFF_PCT), decide(*pair_for(BELOW_OFF), 1) == 0)

# c) 迟滞必须真的消除抖动：让相对差在两个阈值附近来回漂，统计状态翻转次数
seq = []
for thr in (ON_PCT, OFF_PCT):
    for d in (-3, -1, +1, +3, +1, -1, -3, -1, +2, +3):
        v = thr + d
        if 0.0 < v < 100.0:
            seq.append(v)
def flips(with_hyst):
    last = 0; n = 0
    for r in seq:
        a, b = pair_for(r)
        now = decide(a, b, last) if with_hyst else (1 if r >= ON_PCT else 0)
        if now != last: n += 1
        last = now
    return n
n_h = flips(True); n_n = flips(False)
print(f"INFO 在阈值 {ON_PCT}%/{OFF_PCT}% 附近漂动的 {len(seq)} 帧里："
      f"无迟滞翻转 {n_n} 次，有迟滞翻转 {n_h} 次")
check("迟滞显著减少状态翻转次数", n_h < n_n)

# d) 相对判据 vs 绝对判据：基线不同时相对判据保持同一含义
def abs_decide(g1, g2, thr_abs): return 1 if abs(g1 - g2) * 30 > thr_abs else 0
R = 20.0                                            # 固定的相对差
lo = (1.0, 1.0 * (1 - R / 100)); hi = (10.0, 10.0 * (1 - R / 100))
print(f"INFO 低基线{lo} 与 高基线{hi} 的生理含义相同（相对差都是 {R}%）")
check("相对判据对两种基线判定一致（相对绝对判据的核心优势）",
      decide(lo[0], lo[1], 0) == decide(hi[0], hi[1], 0))
check("绝对判据在同一阈值下对两种基线结果不同（说明它会随基线漂移）",
      abs_decide(lo[0], lo[1], 50) != abs_decide(hi[0], hi[1], 50))

# e) 边界与退化情形
check("G1=G2 → 相对差 0 → 未麻醉完全", decide(5.0, 5.0, 0) == 0)
check("两者均 0（标定后完全一致）→ 分母为 0 不崩且判未麻醉完全", decide(0.0, 0.0, 0) == 0)
# f) 恰好落在阈值上：进入用 >=、退出用 >，两侧都不该有歧义
check("相对差恰等于 ON → 判麻醉完全（进入侧取等号）", decide(*pair_for(ON_PCT), 0) == 1)
check("相对差恰等于 OFF → 判未麻醉完全（退出侧取等号）", decide(*pair_for(OFF_PCT), 1) == 0)

# ---------- 12. 三行数值自洽（现场故障：两路显示 0、差值却 70 多） ----------
# 根因：adc_to_g 不截零，格式化时才各自截零 → 差值仍按未截零的负值相减
bad_pair = None
for a in range(0, 4096, 11):
    Ga = adc_to_g(a)
    for b in range(0, 4096, 11):
        Gb = adc_to_g(b)
        if lcd_val_str(Ga) == "0.00" and lcd_val_str(Gb) == "0.00" \
           and lcd_val_str(abs(Ga - Gb)) != "0.00":
            bad_pair = (a, b, Ga, Gb)
            break
    if bad_pair:
        break
check("全量程抽样：两路都显示 0.00 时差值行必然也是 0.00", bad_pair is None)
if bad_pair:
    print("    反例 adc=%d/%d G=%.2f/%.2f" % bad_pair)

# 结构不变量：非负输入下差值不可能超过较大的一路
viol = [(a, b) for a in range(0, 4096, 37) for b in range(0, 4096, 37)
        if abs(adc_to_g(a) - adc_to_g(b)) > max(adc_to_g(a), adc_to_g(b)) + 1e-9]
check("不变量：差值 <= max(G1,G2)（截零后必然成立，旧实现在负值区被破坏）", not viol)

# ---------- 13. 波形区越界防护（现场故障：波形显示超出区域） ----------
probe = [-1e9, -500.0, -70.0, -20.0, -0.001, 0.0, 1.0, G_RANGE_MAX / 2, G_RANGE_MAX,
         G_RANGE_MAX + 1e-3, 1e9, float('inf'), float('-inf'), float('nan')]
oob = [g for g in probe if not (0 <= map_y(g) <= WF_H - 1)]
check("任意 G（含负值/超量程/±inf/NaN）映射出的 y 都落在波形区 0..%d" % (WF_H - 1), not oob)
old20, old70 = map_y_raw(-20.0), map_y_raw(-70.0)
print("INFO 旧实现（不夹取）：G=-20 -> y=%d（>%d，画到分隔线以下）；"
      "G=-70 -> y=%d -> uint8 回绕成 %d（跳到波形区上部）" % (old20, WF_H - 1, old70, old70 & 0xFF))
check("旧实现确实会让负值 y 越界（该故障可复现，不是臆测）", old20 > WF_H - 1)

# ---------- 14. 状态行缓存与 CAL 提示（现场故障：一直显示 CAL） ----------
def status_sim(events, invalidate):
    """复刻 LCD_DrawSkinText 的状态行缓存 + LCD_DrawCalText。
       events: ('draw', G1, G2) / ('cal',)
       invalidate=False 复刻旧实现（函数内 static last_anes，CAL 不置失效）
       invalidate=True  复刻新实现（文件级 status_anes，CAL 置 0xFF）"""
    shown, screen = 0xFF, None
    for ev in events:
        if ev[0] == 'cal':
            screen = 'CAL'
            if invalidate:
                shown = 0xFF
            continue
        g1, g2 = ev[1], ev[2]
        mx = max(g1, g2)
        d100 = abs(g1 - g2) * 100.0
        if mx <= 0.0:
            now = 0
        elif shown == 1:
            now = 1 if d100 > mx * OFF_PCT else 0
        else:
            now = 1 if d100 >= mx * ON_PCT else 0
        if now != shown:
            screen, shown = now, now
    return screen

EV = [('draw', 0.0, 0.0), ('cal',), ('draw', 0.0, 0.0)]   # 上电 → 标定中 → 标定完仍在零点
check("旧实现：标定结束后 CAL 提示留在屏上（复现“一直在校准”）", status_sim(EV, False) == 'CAL')
check("新实现：标定结束后状态行恢复为“未麻醉完全”(0)", status_sim(EV, True) == 0)
_hi = pair_for(ABOVE_ON)                                   # 明确高于 ON 阈值的一对值
check("新实现：标定结束后若判为麻醉完全也能正常显示",
      status_sim([('draw', 0.0, 0.0), ('cal',), ('draw', _hi[0], _hi[1])], True) == 1)
check("新实现：CAL 提示本身也必须先清掉上电时画的旧状态",
      status_sim([('draw', 0.0, 0.0), ('cal',)], True) == 'CAL')

# ---------- 15. 标定有效性（量程 + 稳定性）与现场故障的前后对比 ----------
def cal_accept(mean, spread):
    return CAL_ADC_MIN <= mean <= CAL_ADC_MAX and spread <= CAL_SPREAD_MAX

def cal_loop(windows):
    """复刻 adc_cal_task 的重试循环：windows 为每个 1s 窗口的 (均值1, 均值2, 极差)。
       两路同时合格才锁定零点并返回 (True, 零点1, 零点2)；否则丢弃整窗继续采。
       直到窗口用尽仍未合格 → (False, None, None)：此时 cal_done 保持 0，
       屏上三行数值保持空白、状态行停在 CAL...，不会拿不合格的零点去算读数"""
    for (m1, m2, sp) in windows:
        if cal_accept(m1, sp) and cal_accept(m2, sp):
            return True, g_raw(m1), g_raw(m2)
    return False, None, None

SIG_ADC = 2925          # 之后的真实工作点（2.0μS）

def shown_pair(o1, o2, off_ok=True):
    """给定零点对，返回 (屏上第一行, 第二行, 差值行)；off_ok=False 表示还没标定完（空白）"""
    if not off_ok:
        return (None, None, None)
    G1 = max(0.0, (g_raw(SIG_ADC) - o1) * 30.0)
    G2 = max(0.0, (g_raw(SIG_ADC) - o2) * 30.0)
    return lcd_val_str(G1), lcd_val_str(G2), lcd_val_str(abs(G1 - G2))

# 现场故障：旧实现（无校验）把 123/125 直接当零点
o1, o2 = g_raw(123), g_raw(125)
G1 = (g_raw(SIG_ADC) - o1) * 30.0
G2 = (g_raw(SIG_ADC) - o2) * 30.0
print("INFO 旧实现 标定均值 123/125 -> 零点 %.1f/%.1fμS -> 屏上 %s / %s，差值行 %s（现场现象）"
      % (o1, o2, lcd_val_str(G1), lcd_val_str(G2), lcd_val_str(abs(G1 - G2))))
check("复现现场：旧实现两路显示 \"0.00\"/\"0.00\"、差值行却是 70~79",
      lcd_val_str(G1) == "0.00" and lcd_val_str(G2) == "0.00" and 70 <= abs(G1 - G2) < 80)

# 新实现：同一个窗口被判不合格 → 整窗丢弃重采，绝不锁定该零点
okc, a1, a2 = cal_loop([(123, 125, 3)])
check("新实现：该窗口被拒、零点未锁定（不会显示 0.00/0.00/79.9）", not okc and a1 is None)
check("新实现：未标定期间屏上三行保持空白（不是 0.00）", shown_pair(None, None, False) == (None, None, None))
# 换到合格窗口后应立即锁定
okc, a1, a2 = cal_loop([(123, 125, 3), (SIG_ADC, SIG_ADC + 4, 6)])
check("新实现：丢弃坏窗口后，下一个合格窗口即锁定零点", okc and approx(a1, 2.0))
# 锁定后的三行必须自洽：|第一行 - 第二行| == 差值行（按格式化后的字面复算）
r1, r2, r3 = shown_pair(a1, a2)
check("新实现：锁定后三行自洽 |第1行-第2行| == 差值行（%s / %s / %s）" % (r1, r2, r3),
      approx(abs(float(r1) - float(r2)), float(r3), 0.051))
okc, b1, b2 = cal_loop([(SIG_ADC, SIG_ADC, 6)])
check("新实现：两路零点相同时两路读数完全一致",
      shown_pair(b1, b2) == ("0.00", "0.00", "0.00"))

# 找出所有"能做出 两路 0 + 差值>=70"的标定均值，逐个确认新判据都能拒绝
danger = []
for m in range(1, 4095):
    for d in (1, 2):
        oo1, oo2 = g_raw(m), g_raw(m + d)
        gg1 = (g_raw(SIG_ADC) - oo1) * 30.0
        gg2 = (g_raw(SIG_ADC) - oo2) * 30.0
        if gg1 <= 0.0 and gg2 <= 0.0 and abs(gg1 - gg2) >= 70.0:
            danger.append(m)
            break
print("INFO 旧实现下可做出该故障的标定均值共 %d 个，adc 范围 %d..%d（前端 5μS 满量程对应 adc>=%d）"
      % (len(danger), min(danger), max(danger), CAL_ADC_MIN))
check("危险均值集合非空（说明这条校验并非多余）", len(danger) > 0)
check("危险均值全部被新判据拒绝，永远锁不进零点", all(not cal_accept(m, 0) for m in danger))
check("危险均值组成的窗口重试再多也锁不上（模拟连续 5 个坏窗口）",
      not cal_loop([(m, m + 1, 3) for m in danger[:5]])[0])

check("量程校验：正常静息基线 adc=%d 接受" % SIG_ADC, cal_accept(SIG_ADC, 6))
check("量程校验：现场异常基线 adc=118（≈168μS，出前端量程）拒绝", not cal_accept(118, 3))
check("量程校验：下边界 adc=%d 接受、%d 拒绝" % (CAL_ADC_MIN, CAL_ADC_MIN - 1),
      cal_accept(CAL_ADC_MIN, 0) and not cal_accept(CAL_ADC_MIN - 1, 0))
check("量程校验：上边界 adc=%d 接受、%d 拒绝" % (CAL_ADC_MAX, CAL_ADC_MAX + 1),
      cal_accept(CAL_ADC_MAX, 0) and not cal_accept(CAL_ADC_MAX + 1, 0))
check("稳定性校验：窗口极差 %d 接受、%d 拒绝" % (CAL_SPREAD_MAX, CAL_SPREAD_MAX + 1),
      cal_accept(SIG_ADC, CAL_SPREAD_MAX) and not cal_accept(SIG_ADC, CAL_SPREAD_MAX + 1))
check("只有一路不合格也必须整窗作废（两路零点必须同一时刻）",
      not cal_loop([(SIG_ADC, 118, 3)])[0] and not cal_loop([(118, SIG_ADC, 3)])[0])

# 采样节拍：标定必须按 adc_seq 取"不同"的样本，否则 1s 均值只是一瞬间的一个值
check("标定窗口时长 = %d 个样本 / 50Hz = %.1fs" % (CAL_SAMPLES, CAL_SAMPLES / 50.0),
      approx(CAL_SAMPLES / 50.0, 1.0, 0.01))

# ---------- 16. 固定偏移逻辑已彻底移除（源码级核对） ----------
check("app_config.h 已无 CAL_FIXED_OFFSET / CAL_ENABLE",
      'CAL_FIXED_OFFSET' not in CFG and 'CAL_ENABLE' not in CFG)
check("main.c 已无 -0.7 固定偏移兜底", 'CAL_FIXED_OFFSET' not in MAIN_C)
check("main.c 标定失败只置失败标志，不写零点",
      'cal_fail = 1;' in MAIN_C and 'cal_fail' in MAIN_C)
check("状态行提示已接上\"本轮不合格\"标志（红/黄区分）", 'LCD_DrawCalText(cal_fail)' in MAIN_C)
check("上电不再预画 0.00（已删除 LCD_DrawSkinText(0.0f, 0.0f) 预画）",
      'LCD_DrawSkinText(0.0f, 0.0f)' not in MAIN_C)
check("零点初值不再引用固定偏移（仅标定合格后才有效）",
      'cal_off1 = 0.0f' in MAIN_C and 'cal_off2 = 0.0f' in MAIN_C)
# 状态行提示接口已改为带参数
check("LCD_DrawCalText 接口带 failed 参数",
      'void LCD_DrawCalText(uint8_t failed)' in LCD_C and
      'void LCD_DrawCalText(uint8_t failed);' in LCD_H)

# ---------- 17. DMA TE 后 ADC 错误状态位必须清（半速静默劣化的根因） ----------
# 前提全部对着 HAL 源码核对，HAL 一升级这里就会报出来
check("HAL 前提①：ADC_DMAConvCplt 用 ERROR_INTERNAL|ERROR_DMA 挡住完成回调",
      'HAL_IS_BIT_CLR(hadc->State, HAL_ADC_STATE_ERROR_INTERNAL | HAL_ADC_STATE_ERROR_DMA)'
      in HAL_ADC_C)
check("HAL 前提②：ADC_DMAError 会置 HAL_ADC_STATE_ERROR_DMA",
      'SET_BIT(hadc->State, HAL_ADC_STATE_ERROR_DMA);' in HAL_ADC_C)
check("HAL 前提③：全 HAL 里没有任何 CLEAR_BIT 主动清 ERROR 位（所以不清就永远留着）",
      not re.search(r'CLEAR_BIT\(\s*hadc->State\s*,\s*[^)]*HAL_ADC_STATE_ERROR', HAL_ADC_C))
check("HAL 前提④：ADC_STATE_CLR_SET 的清除掩码里都不含 ERROR 位",
      all('ERROR' not in m for m in re.findall(r'ADC_STATE_CLR_SET\(hadc->State,\s*([^,]+),', HAL_ADC_C)))
check("HAL 前提⑤：半满回调没有这个门（所以只剩 25Hz，seq 仍在增长）",
      'HAL_ADC_ConvHalfCpltCallback(hadc);' in HAL_ADC_C and
      HAL_ADC_C.index('void ADC_DMAHalfConvCplt') < HAL_ADC_C.index('HAL_ADC_ConvHalfCpltCallback(hadc);'))
check("HAL 前提⑥：F1 的 DMA TE 把 hdma->State 置为 READY（→ Stop_DMA 的 BUSY 分支不成立、不清状态）",
      re.search(r'hdma->ErrorCode = HAL_DMA_ERROR_TE;.*?hdma->State = HAL_DMA_STATE_READY;',
                HAL_DMA_C, re.S) is not None)

# 我方的修复必须存在
check("adc_stall_recover 显式清 ADC 错误状态位（修复本体）",
      'CLEAR_BIT(hadc1.State, HAL_ADC_STATE_ERROR_INTERNAL | HAL_ADC_STATE_ERROR_DMA)' in MAIN_C)
check("adc_stall_recover 同时清 ErrorCode 字段", 'ADC_CLEAR_ERRORCODE(&hadc1)' in MAIN_C)
check("历史上那条错误注释（\"由 HAL_ADC_Start_DMA 清错误码\"）已删除",
      '实际由后续 HAL_ADC_Start_DMA 清错误码' not in MAIN_C)

# 行为建模：不清 vs 清，完成后回调是否还活着
ERR_INT, ERR_DMA, READY, REG_BUSY, REG_EOC, REG_OVR, REG_EOSMP = 0x10, 0x40, 0x01, 0x100, 0x200, 0x400, 0x800
def hal_after_recover(state, we_clear):
    st = state
    if we_clear:
        st &= ~(ERR_INT | ERR_DMA)                     # adc_stall_recover 里的 CLEAR_BIT
    # HAL_ADC_Stop_DMA：TE 后 DMA 是 READY 不是 BUSY → 该分支不执行，状态位不变
    st |= READY                                        # Calibration_Start: CLR_SET(BUSY_INTERNAL, READY)
    st &= ~(READY | REG_EOC | REG_OVR | REG_EOSMP)     # Start_DMA: CLR_SET(..., REG_BUSY)
    st |= REG_BUSY
    return st
def cplt_alive(state):
    return (state & (ERR_INT | ERR_DMA)) == 0
check("行为复现：不清状态位 → 恢复后完成回调仍被挡死（只剩半速）",
      not cplt_alive(hal_after_recover(ERR_DMA, False)))
check("行为复现：清了状态位 → 恢复后完成回调恢复可用",
      cplt_alive(hal_after_recover(ERR_DMA, True)))

# ---------- 18. 标定未完成前不得输出无意义数据 ----------
_i0 = MAIN_C.index('if (adc_seq != last_seq)')          # 主循环里消费样本的分支
_i1 = MAIN_C.index('LCD_WaveformPush', _i0)             # 注意从 _i0 之后找调用，
_i2 = MAIN_C.index('uart_send_pair', _i0)               # 否则会先命中函数定义
check("波形推进与串口上报都在 `if (cal_done)` 门控之后",
      'if (cal_done)' in MAIN_C[_i0:_i1] and 'if (cal_done)' in MAIN_C[_i0:_i2])
check("门控理由有注释（零点未标定前 cal_off 还是 0）",
      '零点没标定出来之前不推波形' in MAIN_C)

# ---------- 19. 调参自检与布局/字库一致性 ----------
check("ADC_BLOCK_TRIG=%d 去极值后仍有 %d 个样本可平均（除数非 0）" % (ADC_BLOCK_TRIG, ADC_BLOCK_TRIG - 2),
      ADC_BLOCK_TRIG - 2 >= 1)
check("FILT_N=%d >= 1（取模/除数非 0）" % FILT_N, FILT_N >= 1)
check("app_config.h 内置编译期自检（改坏了直接编译报错，不会留到运行期除零）",
      '#if (ADC_BLOCK_TRIG < 4)' in CFG and '#if (FILT_N < 1)' in CFG)

# 三行标签宽度 + 数值列必须放得进左栏（否则定位算成负数、开窗坐标错乱）
for _nm, _body in re.findall(r'LABEL_(\w+)\[\]\s*=\s*\{([^}]*)\}', LCD_C):
    _tok = [t.strip() for t in _body.split(',') if t.strip() and t.strip() != '0']
    _w = sum(16 if t.startswith('GB_') else 8 for t in _tok)
    check("标签 %-4s 宽 %2dpx + 数值列 %dpx = %dpx ≤ 左栏 %dpx" % (_nm, _w, VAL_W_REF, _w + VAL_W_REF, SPLIT_X),
          _w + VAL_W_REF <= SPLIT_X)

# LCD_ValStr 全范围最长输出（原来只断言了 "1999.99" 这个根本不会出现的串）
_vmax, _smax = 0, ''
for _v in range(0, int(CLAMP_HI * 100) + 1):
    _s = lcd_val_str(_v / 100.0)
    if len(_s) > _vmax:
        _vmax, _smax = len(_s), _s
print("INFO LCD_ValStr 全范围最长输出 = %d 字符（%r）" % (_vmax, _smax))
check("最长输出 %d 字符 × 8px = %dpx ≤ 数值列宽 %dpx" % (_vmax, _vmax * 8, VAL_W_REF), _vmax * 8 <= VAL_W_REF)

# 字库一致性：CN_GB 与 CN_GLYPHS 一一对应，源码用到的码都能查到
_gb_def = dict(re.findall(r'#define\s+(GB_\w+)\s+(0x[0-9A-Fa-f]+)', CN_H))
_cn_gb = re.findall(r'(GB_\w+)', re.search(r'CN_GB\[\]\s*=\s*\{([^}]*)\}', CN_H).group(1))
_nglyph = len(re.findall(r'^\t\{0x', CN_H, re.M))
_used = sorted(set(re.findall(r'\bGB_\w+\b', LCD_C)))
check("CN_GLYPHS 字形数 %d == CN_GB 条目数 %d" % (_nglyph, len(_cn_gb)), _nglyph == len(_cn_gb))
check("源码用到的 %d 个汉字码都在 CN_GB 里（否则静默画成第 0 个字形）" % len(_used),
      all(u in _cn_gb for u in _used))
check("CN_GB 没有多余条目（每个字形都真的用得上，不白占 flash）",
      sorted(_cn_gb) == _used)
check("CN_GB 码值与行内注释一一对应（改码值必须同步改字形顺序）",
      len(set(_gb_def.values())) == len(_gb_def))
# 每字形必须正好 32 字节（LCD_Cn16 按 row*2 / row*2+1 读 16 行）
_bodies = re.findall(r'\{((?:0x[0-9A-Fa-f]{2},?\s*){2,})\}', CN_H)
check("每个汉字字形正好 32 字节（16 行 × 2 字节）",
      len(_bodies) == _nglyph and all(len(re.findall(r'0x[0-9A-Fa-f]{2}', b)) == 32 for b in _bodies))

# ---------- 20. 调色板字面值锁定（防"看着像交换了 R/B"被改回去） ----------
# 入口模式 R0x11=0x6838 开了 BGR，面板按 B5G6R5 解释 16 位字，所以 H24_RGB565
# 把 R 放低位、B 放高位。实测依据：报警块用 RED 显示出来是红的。
# 下面的期望值就是当前实现的输出，改动它们必须先确认屏幕颜色没变。
def h24_word(color24):
    r = (color24 >> 16) & 0xFF
    g = (color24 >> 8) & 0xFF
    b = color24 & 0xFF
    return ((b // 8) << 11) | ((g // 4) << 5) | (r // 8)
_PALETTE = [('BLACK', 0x000000, 0x0000), ('WHITE', 0xFFFFFF, 0xFFFF), ('GRAY', 0x7F7F7F, 0x7BEF),
            ('RED', 0xFF0000, 0x001F), ('GREEN', 0x00FF00, 0x07E0), ('BLUE', 0x0000FF, 0xF800),
            ('YELLOW', 0xFFFF00, 0x07FF)]
for _nm, _c24, _want in _PALETTE:
    check("调色板 %-6s 0x%06X -> 面板字 0x%04X" % (_nm, _c24, _want), h24_word(_c24) == _want)
check("R 与 B 不能互换（互换后 RED 会变成 0xF800，屏上红蓝反色）",
      h24_word(0xFF0000) != h24_word(0x0000FF))
# 调色板宏必须与上面的期望值一致
for _nm, _c24, _w in _PALETTE:
    _def = int(re.search(r'#define\s+%s\s+(0x[0-9A-Fa-f]+)' % _nm, LCD_H).group(1), 16)
    check("ssd1289_fsmc.h 里 %s 的值 = 0x%06X" % (_nm, _c24), _def == _c24)

# ---------- 21. 端到端：500Hz 原始采样 → 块抽取 → 滤波 → 标定 → 显示/串口 ----------
# 把整条链路串起来跑一遍，含 50Hz 工频干扰、噪声、一次持续干扰突发。
# 这是唯一能一次性验证"各处数值自洽"的测试——现场那次"两路 0 / 差值 79.9"
# 就是单看某个函数都正常、串起来才暴露的。
import math
def adc_of_uS(us):
    """前端反函数：G=5*(4095-adc)/adc  =>  adc = 20475/(G+5)"""
    return int(round(20475.0 / (us + 5.0)))

def run_chain(seconds=6.0, mains_uS=0.05, noise_codes=2, burst=None, seed=7):
    """返回 (cal_ok, zero_uS, rows[(G1,G2,diff)...], uart[(s1,s2)...], wave_y[], spikes, seq)
       burst=(起秒, 持续秒) 期间在原始码上加 ±2000 的摆动（模拟跨多个采样点的突发干扰）"""
    rnd = random.Random(seed)
    gf = [GFilter(), GFilter()]
    val = [adc_of_uS(2.0), adc_of_uS(1.9)]
    seq = 0
    rows, uart, wave_y, spikes = [], [], [], 0
    # 标定状态
    acc = [0, 0]; n = 0; done = False; off = [0.0, 0.0]
    blk = [[], []]
    for k in range(int(seconds * TRIG_HZ)):
        t = k / TRIG_HZ
        for ch in (0, 1):
            base_us = (2.0 if ch == 0 else 1.9) + 0.3 * math.sin(2 * math.pi * 0.2 * t)
            us = base_us + mains_uS * math.sin(2 * math.pi * 50.0 * t)
            adc = adc_of_uS(us) + rnd.randint(-noise_codes, noise_codes)
            if burst and burst[0] <= t < burst[0] + burst[1]:
                adc += 2000 if ((k // 3) % 2 == 0) else -2000
            blk[ch].append(max(0, min(4095, adc)))
        if len(blk[0]) < ADC_BLOCK_TRIG:
            continue
        ok_ch = []
        for ch in (0, 1):
            good, v = block_reduce_checked(blk[ch])
            blk[ch] = []
            ok_ch.append(good)
            if good:
                val[ch] = gfilter(gf[ch], v)
            else:
                spikes += 1
        seq += 1
        # 主循环：标定（CAL_START_MS 之后，按 seq 取不同样本）
        if not done and t * 1000 >= int(macro(CFG, 'CAL_START_MS')):
            for ch in (0, 1):
                acc[ch] += val[ch]
            n += 1
            if n >= CAL_SAMPLES:
                m1, m2 = acc[0] // n, acc[1] // n
                if cal_accept(m1, CAL_SPREAD_MAX) and cal_accept(m2, CAL_SPREAD_MAX):
                    off = [g_raw(m1), g_raw(m2)]
                    done = True
        if not done:
            continue
        # 显示 / 串口 / 波形（三者取同一份数据）
        G1 = max(0.0, (g_raw(val[0]) - off[0]) * 30.0)
        G2 = max(0.0, (g_raw(val[1]) - off[1]) * 30.0)
        rows.append((lcd_val_str(G1), lcd_val_str(G2), lcd_val_str(abs(G1 - G2))))
        uart.append((lcd_val_str(G1), lcd_val_str(G2)))
        wave_y.append(map_y(G1)); wave_y.append(map_y(G2))
    return done, off, rows, uart, wave_y, spikes, seq

ok_cal, zero, rows, uart, wy, spikes, seq = run_chain()
print("INFO 端到端：标定=%s 零点=%.3f/%.3fμS 输出 %d 帧 剔除块=%d seq=%d"
      % (ok_cal, zero[0], zero[1], len(rows), spikes, seq))
check("端到端：标定能锁上，零点落在真实基线附近（2.0/1.9μS ±0.3）",
      ok_cal and all(abs(z - b) < 0.3 for z, b in zip(zero, (2.0, 1.9))))
check("端到端：样本率 = 额定 50Hz（seq/时长）", abs(seq / 6.0 - NOMINAL_HZ) < 1.0)
check("端到端：波形 y 全程落在 0..%d" % (WF_H - 1), all(0 <= y <= WF_H - 1 for y in wy))
check("端到端：串口字符串与屏上数值逐字符相同", all(a == b for a, b in zip(rows, uart)) if False
      else all(u[0] == r[0] and u[1] == r[1] for r, u in zip(rows, uart)))
check("端到端：三行数值自洽 |第1行-第2行| == 差值行",
      all(approx(abs(float(r[0]) - float(r[1])), float(r[2]), 0.051) for r in rows))
check("端到端：两路显示值都在合理范围内（0~200，不是被钳死也不是爆表）",
      all(0 <= float(r[0]) <= 200 and 0 <= float(r[1]) <= 200 for r in rows))

# 突发干扰：持续 0.2s 的 ±2000 码摆动应被块判据剔除，显示值不跟着跳
ok2, zero2, rows2, _, _, spikes2, _ = run_chain(burst=(3.0, 0.2))
check("端到端：持续 0.2s 的突发干扰被判为受扰块（剔除计数增加）", spikes2 > spikes)
_r = [float(x[0]) for x in rows2]
check("端到端：突发期间显示值不出现 >50 的跳变（块剔除真的挡住了）",
      max(abs(a - b) for a, b in zip(_r, _r[1:])) < 50)

# 工频抑制：把 50Hz 干扰加到 0.05μS（约占基线 2.5%），输出不应出现 50Hz 残留
ok3, zero3, rows3, _, _, _, _ = run_chain(mains_uS=0.05, noise_codes=0, seed=11)
_v = [float(x[0]) for x in rows3]
# 相邻样本（20ms 一格，正好一个工频周期）的差若含 50Hz 残留会很大
_d = max(abs(a - b) for a, b in zip(_v, _v[1:])) if len(_v) > 1 else 0.0
print("INFO 工频抑制：注入 50Hz(0.05μS) 后相邻样本最大跳变 = %.3f 显示单位" % _d)
check("端到端：50Hz 工频几乎不残留（相邻样本跳变 < 2 个显示单位）", _d < 2.0)

# ---------- 22. 本轮修复的锁定测试 ----------
MSP_C = read_src(os.path.join('Core', 'Src', 'stm32f1xx_hal_msp.c'))
IOC   = read_src('lcd.ioc')

# a) 调试口必须保留：关掉 SWJ 一个引脚都省不出来，却让上板调试变成 connect-under-reset
#    （只认真正生效的语句，注释里提到这个宏名不算）
_swj_active = [ln for ln in MSP_C.splitlines()
               if ln.strip().startswith('__HAL_AFIO_REMAP_SWJ_DISABLE')]
check("HAL_MspInit 不再关 SWD/JTAG（否则上电后无法 attach）", not _swj_active)
_ioc_pins = ','.join(re.findall(r'^Mcu\.Pin\d+=(.+)$', IOC, re.M))
check("lcd.ioc 确实没占用 PA13/PA14/PA15/PB3/PB4（所以保留 SWJ 零代价）",
      not any(p in _ioc_pins for p in ('PA13', 'PA14', 'PA15', 'PB3', 'PB4')))
check("lcd.ioc 的 SYS 调试模式写成 Serial_Wire（重新生成不会再关掉调试口）",
      'VP_SYS_VS_SWD.Mode=Serial_Wire' in IOC and 'No_Debug' not in IOC)

# b) 波形缓存哨兵必须是 0xFF（0 是合法 y，误用会把重写区间扩到顶部）
_w = LCD_C[LCD_C.index('static void wave_push_pair'):LCD_C.index('void LCD_InitLayout')]
check("wave_push_pair 的\"已绘制区间\"初值是 0xFF 而非 0",
      'uint8_t da1 = 0xFF, db1 = 0xFF, da2 = 0xFF, db2 = 0xFF;' in _w and
      'uint8_t da1 = 0, db1 = 0, da2 = 0, db2 = 0;' not in _w)

# c) LCD_ValStr 对 NaN 不得进入 float→uint 的 UB
check("LCD_ValStr 用 !(G >= 0.0f) 同时挡住负值和 NaN（NaN 两个 clamp 都拦不住）",
      'if (!(G >= 0.0f)) G = 0.0f;' in LCD_C)
check("LCD_ValStr 不再用只挡负值的写法", 'if (G < 0.0f) G = 0.0f;' not in LCD_C)
def valstr_nan_safe(G):
    """复刻修复后的 clamp：!(G>=0) 对 NaN 成立 → 归零；随后 u32 转换安全"""
    if not (G >= 0.0): G = 0.0
    if G > CLAMP_HI: G = CLAMP_HI
    return int(G * 100.0 + 0.5)
check("NaN/inf/-inf 经 clamp 后都能安全转成 uint32",
      valstr_nan_safe(float('nan')) == 0 and valstr_nan_safe(float('inf')) == int(CLAMP_HI * 100 + 0.5)
      and valstr_nan_safe(float('-inf')) == 0)

# d) 文档与代码一致：块判据是"相加"不是"取较大者"（曾把注释和测试一起带偏）
check("app_config.h 的阈值公式说明是\"相加\"（与 main.c 的 + 一致）",
      '是**相加**，不是取较大者' in CFG and 'max(绝对下限' not in CFG)
check("main.c 的块判据确实用加法", 'ADC_BLOCK_SPREAD_PCT / 100u + ADC_BLOCK_SPREAD_ABS' in MAIN_C)
check("adc_err_cnt 注释不再宣称能数 overrun（F1 的 ADC 没有 OVR 标志）",
      '唯一可达路径是 DMA 传输错误' in MAIN_C and 'overrun 等' not in MAIN_C)

# e) CubeMX 回退面：历史上回过退的项必须在 .ioc 里钉死
check("lcd.ioc 显式钉住 ADC1.ScanConvMode（历史回退项）", 'ADC1.ScanConvMode=ADC_SCAN_ENABLE' in IOC)
check("lcd.ioc 显式钉住 USART1.BaudRate（否则重新生成可能静默改波特率）",
      'USART1.BaudRate=115200' in IOC)
check("usart.c 实际波特率与 .ioc 一致", 'huart1.Init.BaudRate = 115200;' in
      read_src(os.path.join('Core', 'Src', 'usart.c')))

print("\n" + ("ALL PASS" if ok else "SOME FAILED"))


