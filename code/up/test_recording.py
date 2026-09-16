# -*- coding: utf-8 -*-
"""
录制模块自测：python test_recording.py（纯文件读写，无需硬件与 GUI）

锁住：会话目录与三份文件的结构、数据/事件逐条落盘且不丢、时间与帧号对齐、
单通道写 nan、未录制时的安全空操作、病例号清洗、同名会话不覆盖。
"""
import csv
import json
import os
import shutil
import sys
import tempfile

from recording import SessionRecorder, safe_name


def read_csv(path):
    with open(path, encoding="utf-8-sig") as f:
        return list(csv.reader(f))


def main():
    root = tempfile.mkdtemp(prefix="zc_rec_")
    fails = []

    def check(name, cond, extra=""):
        print(f"{'OK  ' if cond else 'FAIL'} {name} {extra}")
        if not cond:
            fails.append(name)

    try:
        rec = SessionRecorder(root, flush_interval=0.0)

        # ---- 未开始录制时的空操作 ----
        rec.write_frame(1.0, 2.0)
        check("未录制时写入被忽略", rec.frame_count == 0)
        check("未录制时打点返回 False", rec.mark("基线") is False)

        # ---- 开始录制 ----
        session = rec.start({"patient_id": "P-001", "procedure": "膝关节置换"})
        check("会话目录已创建", session and os.path.isdir(session), session)
        check("目录名含病例号且已清洗", os.path.basename(session).endswith("_P-001"),
              os.path.basename(session))
        for fname in ("data.csv", "events.csv", "meta.json"):
            check(f"{fname} 已生成", os.path.exists(os.path.join(session, fname)))

        # ---- 数据落盘 ----
        for i in range(5):
            rec.write_frame(10.0 + i, 20.0 + i)
        rec.write_frame(99.0, None)          # 单通道来源
        check("帧计数正确", rec.frame_count == 6, rec.frame_count)

        rows = read_csv(os.path.join(session, "data.csv"))
        check("表头正确", rows[0] == ["t_epoch_ms", "frame", "ch1", "ch2"], rows[0])
        check("数据行数正确", len(rows) == 7, len(rows))
        check("帧序号连续递增", [r[1] for r in rows[1:]] == ["1", "2", "3", "4", "5", "6"])
        check("通道值逐条无损", [r[2] for r in rows[1:6]] == ["10.0", "11.0", "12.0", "13.0", "14.0"])
        check("单通道缺第二通道写 nan", rows[6][3] == "nan", rows[6])
        check("时间戳为毫秒整数", all(r[0].isdigit() and len(r[0]) == 13 for r in rows[1:]))

        # ---- 事件标注 ----
        rec.mark("基线")
        rec.write_frame(50.0, 60.0)
        rec.mark("阻滞", "左侧肌间沟")
        events = read_csv(os.path.join(session, "events.csv"))
        check("事件表头正确", events[0] == ["t_epoch_ms", "frame", "label", "note"], events[0])
        check("事件条数正确", len(events) == 3, len(events))
        check("事件带帧号且落在数据范围内",
              events[1][1] == "6" and events[2][1] == "7",
              [e[1] for e in events[1:]])
        check("事件备注保留中文", events[2][3] == "左侧肌间沟", events[2])

        # ---- 元信息 ----
        rec.set_meta({"block_type": "肌间沟臂丛阻滞", "operator": "示例操作者"})
        meta = json.load(open(os.path.join(session, "meta.json"), encoding="utf-8"))
        check("元信息含病例字段", meta["patient_id"] == "P-001"
              and meta["block_type"] == "肌间沟臂丛阻滞")
        check("元信息标出量纲系数", meta["unit_scale"] == 30.0)
        check("元信息记录双通道名", meta["channel_names"] == ["电导1", "电导2"])

        # ---- 结束录制 ----
        ended = rec.stop()
        check("stop 返回会话目录", ended == session)
        check("结束后停止计数", rec.is_recording is False)
        rec.write_frame(1.0, 1.0)
        check("结束后写入被忽略", rec.frame_count == 7, rec.frame_count)
        meta2 = json.load(open(os.path.join(session, "meta.json"), encoding="utf-8"))
        check("结束时回写计数与结束时间",
              meta2["frame_count"] == 7 and meta2["event_count"] == 2
              and meta2["ended_at_ms"] is not None)

        # ---- 同名会话不覆盖 ----
        rec2 = SessionRecorder(root, flush_interval=0.0)
        s1 = rec2.start({"patient_id": "同一病例"})
        rec2.write_frame(1.0, 2.0)
        rec2.stop()
        rec3 = SessionRecorder(root, flush_interval=0.0)
        s2 = rec3.start({"patient_id": "同一病例"})
        rec3.write_frame(3.0, 4.0)
        rec3.stop()
        check("同名会话另建目录不覆盖", s1 != s2 and os.path.exists(s1) and os.path.exists(s2),
              f"{os.path.basename(s1)} vs {os.path.basename(s2)}")
        check("旧会话数据未被清空", len(read_csv(os.path.join(s1, "data.csv"))) == 2)

        # ---- 路径穿越与非法字符 ----
        check("病例号清洗掉路径分隔符", safe_name("../../etc/passwd") == ".._.._etc_passwd",
              safe_name("../../etc/passwd"))
        check("空病例号有兜底", safe_name("") == "case" and safe_name(None) == "case")

        # ---- 会话总数 ----
        sessions = [d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d))]
        check("共生成 3 个会话目录", len(sessions) == 3, sessions)

    finally:
        shutil.rmtree(root, ignore_errors=True)

    print(f"\n{'全部通过' if not fails else '失败项: ' + ', '.join(fails)}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
