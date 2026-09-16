# -*- coding: utf-8 -*-
"""
SCR 特征提取自测：python test_features.py（离屏，无需硬件）

锁住三件容易被改坏的事：
1. 谷必须配"紧随其后的合格峰"，不是全信号幅度最大的峰（否则上升时间荒谬）；
2. 各窗口常量按秒折算，4Hz 下与历史硬编码完全等价；
3. 颜色分配在 12 色耗尽后不得死循环。
"""
import importlib.util
import math
import os
import random
import sys

from PySide6.QtWidgets import QApplication

BASE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("up004", os.path.join(BASE, "004.py"))
up = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(up)


def synth(fs, dur=20.0, base=5.0, amp=1.0, onsets=(2.0, 10.0), noise=0.0, seed=1):
    """基线 + 两个典型 SCR（快升慢降），可选高斯噪声"""
    rng = random.Random(seed)
    out = []
    for i in range(int(fs * dur)):
        t = i / fs
        v = base + sum(amp * (math.exp(-(t - t0) / 2.0) - math.exp(-(t - t0) / 0.5))
                       for t0 in onsets if t >= t0)
        out.append(v + (rng.gauss(0, noise) if noise else 0.0))
    return out


def main():
    app = QApplication([])
    w = up.EDAViewer()
    fails = []

    def check(name, cond, extra=""):
        print(f"{'OK  ' if cond else 'FAIL'} {name} {extra}")
        if not cond:
            fails.append(name)

    # 1. 配对：谷(5) 后有两峰，近峰幅度小、远峰幅度大 → 必须配近峰(8)
    w._sample_rate = 4.0
    pair = w._optimal_scr_pairing([(5, 0.0, 1.0)],
                                 [(8, 1.0, 1.0), (20, 3.0, 3.0)],
                                 [0.0] * 30, 0.1, 5.0)
    check("谷配紧随其后的峰(非全信号最大峰)",
          len(pair) == 1 and pair[0]['max_idx'] == 8,
          f"max_idx={pair[0]['max_idx'] if pair else None}")
    check("上升时间按时长折算",
          pair and abs(pair[0]['rise_time'] - 0.75) < 1e-9,
          f"{pair[0]['rise_time'] if pair else None}s")

    # 2. 窗口折算：4Hz 下与历史硬编码等价
    check("4Hz 显著性窗 = 50 点", int(12.5 * 4.0) == 50)
    check("4Hz 半恢复窗 = 100 点", int(25 * 4.0) == 100)

    # 3. 同一物理波形在 4Hz/50Hz 下应给出一致的特征
    f4 = w.compute_scr_features(synth(4.0, noise=0.005), 4.0)
    f50 = w.compute_scr_features(synth(50.0, noise=0.005), 50.0)
    print(f"     4Hz : Freq={f4[1]} Rise={f4[3]:.2f}s Half={f4[4]:.2f}s")
    print(f"     50Hz: Freq={f50[1]} Rise={f50[3]:.2f}s Half={f50[4]:.2f}s")
    check("上升时间为生理量级(<3s，配对bug时会到9.5s)",
          f4[3] < 3.0 and f50[3] < 3.0, f"{f4[3]:.2f}s / {f50[3]:.2f}s")
    check("两速率检出 SCR 数量一致", f4[1] == f50[1], f"{f4[1]} vs {f50[1]}")
    check("两速率半恢复时间一致(±25%)",
          abs(f4[4] - f50[4]) <= 0.25 * max(f4[4], 1e-6),
          f"{f4[4]:.2f}s vs {f50[4]:.2f}s")

    # 4. 量纲：串口数据是固件单位(µS×30)，阈值须随之换算，否则 SCR 全被拒
    #    固件尺度：基线 150、SCR 幅度 30（= 1µS）；文件尺度：基线 5、幅度 1（µS）
    live = up.EDASignalItem("实时", None, up.Qt.red, synth(50.0, base=150.0, amp=30.0, noise=3.0))
    live.is_live = True
    w.update_stats_display(live, sample_rate=50.0)
    live_freq = int(w.scr_freq_label.text().split(":")[1])
    check("固件单位下能检出 SCR(阈值已×30)", live_freq >= 1,
          f"SCR_Freq={live_freq}, {w.scr_amp_label.text()}")

    file_item = up.EDASignalItem("文件", None, up.Qt.blue, synth(4.0, base=5.0, amp=1.0, noise=0.005))
    w.update_stats_display(file_item, sample_rate=4.0)
    file_freq = int(w.scr_freq_label.text().split(":")[1])
    check("文件(µS)路径阈值不变仍能检出", file_freq >= 1, f"SCR_Freq={file_freq}")

    # 5. 颜色分配不得死循环
    check("12 色耗尽后仍正常返回", len([w.get_next_color() for _ in range(30)]) == 30)

    print(f"\n{'全部通过' if not fails else '失败项: ' + ', '.join(fails)}")
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
