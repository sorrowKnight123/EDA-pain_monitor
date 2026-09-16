# -*- coding: utf-8 -*-
"""
解析器自测（无需硬件、无需 pyserial）：python test_parser.py

数据源统一为 STM32 固件，覆盖：双通道帧（半角/全角分隔符、CRLF、整数电导）、
旧版时间戳帧、裸数值、空行/垃圾行/乱码，以及已摈弃的 Arduino 标签行必须被拒。
断言失败即解析逻辑被改坏。
"""
from serial_acquisition import parse_eda_frame

CASES = [
    # (说明, 输入行, 期望结果)
    ("固件双通道 半角逗号",     "12.34,56.78",            (12.34, 56.78)),
    ("固件双通道 全角逗号",     "12.34，56.78",            (12.34, 56.78)),
    ("固件双通道 行尾CRLF",     "12.34,56.78\r\n",         (12.34, 56.78)),
    ("固件双通道 空格容错",     "  12.34, 56.78  ",        (12.34, 56.78)),
    ("限幅边界 999.99",         "999.99,999.99",          (999.99, 999.99)),
    ("电导为负(零点以下)",      "-1.50,0.00",             (-1.5, 0.0)),
    ("整数电导仍算双通道",      "12,56.78",               (12.0, 56.78)),
    ("旧版 时间戳,EDA(8位)",    "12345678,2048.0000",     (2048.0, None)),
    ("旧版 时间戳,EDA(4位)",    "1234,2048.0",            (2048.0, None)),
    ("裸数值",                  "42.5",                   (42.5, None)),
    ("空行",                    "",                       None),
    ("纯空白",                  "   ",                     None),
    ("垃圾行",                  "hello world",            None),
    ("乱码字节残留",            "12.3\ufffd4,56.78",       None),
    # 已摈弃：Arduino 的 GSR 原始计数与固件量纲不同，必须整行丢弃
    ("Arduino 标签行被拒",
     "GSR:523,Ax:120,Ay:200,Az:50,Gy:10,Gy:20,Gz：30,HR:72,SPO2:98", None),
    ("Arduino 单字段标签被拒",  "GSR:523",                 None),
]


def main():
    failed = 0
    for note, line, expect in CASES:
        got = parse_eda_frame(line)
        ok = got == expect
        failed += 0 if ok else 1
        print(f"{'OK  ' if ok else 'FAIL'} {note:<20} {line!r:<28} -> {got}")
        if not ok:
            print(f"     期望: {expect}")
    print(f"\n{len(CASES) - failed}/{len(CASES)} 通过")
    return failed


if __name__ == "__main__":
    raise SystemExit(1 if main() else 0)
