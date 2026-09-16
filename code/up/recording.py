# -*- coding: utf-8 -*-
"""
会话录制：原始数据流 + 事件标注 + 病例元信息落盘。

临床数据采集的基础设施——上位机原来只能看、不能存，无法构建带金标准标注的数据集。
每次录制生成一个会话目录：

    recordings/<YYYYmmdd_HHMMSS>_<病例号>/
        data.csv      t_epoch_ms,frame,ch1,ch2
        events.csv    t_epoch_ms,frame,label,note
        meta.json     病例元信息 + 会话参数与计数

时间基准：固件帧不含时间戳，故唯一可用的时间参考是上位机接收时刻（t_epoch_ms），
其中含 USB/串口传输延迟。做刺激-响应潜伏期分析时需知悉此限制。
事件同时记录当时的帧序号，即使时间戳有漂移也能与数据流对齐。

写盘在 GUI 线程内进行：50Hz × 2 通道数据量极小，且按 flush_interval 定期 flush
（而非逐帧），既不会卡界面，崩溃时最多丢一个 flush 周期的数据。
"""
import csv
import json
import os
import re
import time

DATA_HEADER = ["t_epoch_ms", "frame", "ch1", "ch2"]
EVENT_HEADER = ["t_epoch_ms", "frame", "label", "note"]


def now_ms():
    """当前挂钟时间（毫秒）"""
    return int(time.time() * 1000)


def safe_name(text, fallback="case"):
    """把病例号清洗成可作目录名的字符串"""
    cleaned = re.sub(r'[\\/:*?"<>|\s]+', "_", str(text or "")).strip("_")
    return cleaned[:40] or fallback


class SessionRecorder:
    """一次采集会话的落盘记录器。非录制状态下所有写入调用均为安全空操作。"""

    def __init__(self, root_dir, flush_interval=1.0):
        self.root_dir = root_dir
        self.flush_interval = flush_interval
        self.session_dir = None
        self.frame_count = 0
        self.event_count = 0
        self.error = None          # 落盘异常信息（不抛出，避免打断采集）
        self._meta = {}
        self._data_f = None
        self._data_w = None
        self._event_f = None
        self._event_w = None
        self._last_flush = 0.0
        self._started_ms = None

    # ------------------------------------------------------------------ 状态
    @property
    def is_recording(self):
        return self._data_f is not None

    def set_meta(self, meta):
        """更新病例元信息；录制中也会即刻刷新 meta.json"""
        self._meta.update(meta or {})
        if self.session_dir:
            self._write_meta()

    # ------------------------------------------------------------------ 开始
    def start(self, meta=None):
        """创建会话目录并开始录制。成功返回会话目录路径，已在录制则返回 None。"""
        if self.is_recording:
            return None
        self.set_meta(meta)
        self.error = None
        self.frame_count = 0
        self.event_count = 0
        self._started_ms = now_ms()

        stamp = time.strftime("%Y%m%d_%H%M%S")
        case = safe_name(self._meta.get("patient_id"))
        base = os.path.join(self.root_dir, f"{stamp}_{case}")
        # 同一秒内重开录制时不覆盖已有会话
        self.session_dir = base
        suffix = 1
        while os.path.exists(self.session_dir):
            suffix += 1
            self.session_dir = f"{base}_{suffix}"
        try:
            os.makedirs(self.session_dir, exist_ok=True)
            # utf-8-sig：便于 Excel/WPS 直接双击打开中文备注
            self._data_f = open(os.path.join(self.session_dir, "data.csv"),
                                "w", newline="", encoding="utf-8-sig")
            self._data_w = csv.writer(self._data_f)
            self._data_w.writerow(DATA_HEADER)
            self._event_f = open(os.path.join(self.session_dir, "events.csv"),
                                 "w", newline="", encoding="utf-8-sig")
            self._event_w = csv.writer(self._event_f)
            self._event_w.writerow(EVENT_HEADER)
        except OSError as e:
            self.error = f"创建会话文件失败: {e}"
            self._close_files()
            return None

        self._write_meta()
        self._last_flush = time.time()
        return self.session_dir

    # ------------------------------------------------------------------ 写入
    def write_frame(self, g1, g2=None):
        """记录一帧。g2 为 None 表示单通道来源，写为 nan。"""
        if not self.is_recording:
            return
        self.frame_count += 1
        row = [now_ms(), self.frame_count, g1,
               "nan" if g2 is None else g2]
        try:
            self._data_w.writerow(row)
        except OSError as e:
            self._fail(f"数据写入失败: {e}")
            return
        self._maybe_flush()

    def mark(self, label, note=""):
        """打一个事件标注。未在录制时返回 False（界面据此提示）。"""
        if not self.is_recording:
            return False
        self.event_count += 1
        try:
            self._event_w.writerow([now_ms(), self.frame_count, label, note])
        except OSError as e:
            self._fail(f"事件写入失败: {e}")
            return False
        self._flush()
        return True

    def _maybe_flush(self):
        if time.time() - self._last_flush >= self.flush_interval:
            self._flush()

    def _flush(self):
        self._last_flush = time.time()
        for f in (self._data_f, self._event_f):
            if f is not None:
                try:
                    f.flush()
                except OSError:
                    pass

    # ------------------------------------------------------------------ 结束
    def stop(self):
        """结束录制并返回会话目录路径（未在录制则返回 None）。"""
        if not self.is_recording:
            return None
        session = self.session_dir
        self._write_meta(ended=True)
        self._close_files()
        return session

    def _close_files(self):
        for f in (self._data_f, self._event_f):
            if f is not None:
                try:
                    f.close()
                except OSError:
                    pass
        self._data_f = self._data_w = None
        self._event_f = self._event_w = None

    def _fail(self, message):
        """落盘出错：留痕并止损，绝不向上抛异常打断采集与界面"""
        self.error = message
        self._close_files()

    # ------------------------------------------------------------------ 元信息
    def _write_meta(self, ended=False):
        payload = dict(self._meta)
        payload.update({
            "session_dir": self.session_dir,
            "started_at_ms": self._started_ms,
            "ended_at_ms": now_ms() if ended else None,
            "frame_count": self.frame_count,
            "event_count": self.event_count,
            "unit_scale": 30.0,        # 固件单位 = µS × 30
            "channel_names": ["电导1", "电导2"],
            "note": "时间为上位机接收时刻，含串口传输延迟；固件帧无时间戳",
        })
        try:
            with open(os.path.join(self.session_dir, "meta.json"),
                      "w", encoding="utf-8") as f:
                json.dump(payload, f, ensure_ascii=False, indent=2)
        except OSError as e:
            self.error = f"元信息写入失败: {e}"
