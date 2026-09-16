# -*- coding: utf-8 -*-
"""
串口实时采集模块：容错解析器 + 端口枚举 + 串口工作线程

数据源统一为 STM32 固件，量纲统一为固件电导单位（见 004.py 的 FIRMWARE_UNIT_SCALE）。
解析器自动识别下列格式（优先级从上到下）：

1. STM32 固件 main.c 双通道电导帧（当前在用，115200 波特，50Hz）：
       <电导1><UART_FRAME_SEP><电导2>      例如  12.34,56.78
   两个数值同源于屏显格式化（限幅 999.99），无时间戳。
   UART_FRAME_SEP 定义在固件 app_config.h，半角/全角逗号都兼容。

2. STM32 旧版 data_transmit.c 单通道帧（4Hz，可编辑历史代码）：
       <时间戳>,<EDA值>                    例如  12345678,2048.0000
   时间戳为 HAL_GetTick() 毫秒值。与格式 1 同为"数值,数值"，靠整数位数区分（见 _TS_RE）。

兜底：整行仅一个数值时按单通道处理。

已明确不支持 Arduino 标签行（旧 try.ino 的 GSR:xxx 是 0~65535 原始计数，量纲与固件不同），
此类行会被判为垃圾行丢弃。

pyserial 缺失时模块仍可导入（_SERIAL_AVAILABLE=False），004.py 的文件加载功能照常工作。
"""
import re

try:
    import serial
    from serial.tools import list_ports
    _SERIAL_AVAILABLE = True
except ImportError:
    serial = None
    list_ports = None
    _SERIAL_AVAILABLE = False

from PySide6.QtCore import QObject, Signal

_NUM = r'[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?'

# <数值><半角/全角逗号><数值>：既覆盖固件双通道帧，也覆盖旧版 时间戳,EDA
_PAIR_RE = re.compile(r'^\s*(?P<a>' + _NUM + r')\s*[,，]\s*(?P<b>' + _NUM + r')\s*$')
_BARE_RE = re.compile(r'^\s*(?P<val>' + _NUM + r')\s*$')
# 旧版时间戳判定：4 位以上纯整数。固件电导值经 LCD_ValStr 限幅 999.99（整数部分最多
# 3 位），因此 4 位以上的纯整数不可能是电导值，必然是 HAL_GetTick() 毫秒时间戳。
# 唯一残留歧义是旧固件开机后 1 秒内（时间戳 < 1000）——该固件已不在用，按双通道处理。
_TS_RE = re.compile(r'^\d{4,}$')

# 连续无法解析多少行后上报一次诊断（避免开串口瞬间的噪声误报，又能迅速暴露格式不匹配）
_PARSE_FAIL_ALERT = 20


def parse_eda_frame(line):
    """
    从一行串口文本中解析电导值（固件单位）。

    返回 (电导1, 电导2)：
    - 固件双通道帧 → 两个数值；
    - 单通道来源（旧版时间戳帧 / 裸数值）→ (值, None)；
    - 空行、垃圾行、无法识别 → None（调用方丢弃该行）。
    """
    if not line:
        return None
    text = line.strip()
    if not text:
        return None

    m = _PAIR_RE.match(text)
    if m:
        first, second = m.group('a'), m.group('b')
        if _TS_RE.match(first):
            return (float(second), None)      # 旧版 时间戳,EDA值
        return (float(first), float(second))  # 固件 电导1,电导2

    m = _BARE_RE.match(text)
    if m:
        return (float(m.group('val')), None)

    return None


def list_serial_ports():
    """枚举可用串口，返回设备名列表（如 ['COM3', 'COM5']）。"""
    if not _SERIAL_AVAILABLE:
        return []
    try:
        return [p.device for p in list_ports.comports()]
    except Exception:
        return []


class SerialWorker(QObject):
    """
    串口读取工作线程（QObject，moveToThread 到子线程）。

    线程安全模型：
    - worker 在子线程运行，不触碰任何 GUI 对象；
    - 所有数据经 queued 信号交给 GUI 线程；
    - 跨线程唯一可调用方法是 stop()（置标志 + 关串口）。
    """
    data_received = Signal(float, float)   # (电导1, 电导2)；单通道来源时 电导2 为 nan
    status_changed = Signal(str)           # 连接/停止状态文本
    error_occurred = Signal(str)           # 打开失败 / 读取异常 / 持续解析失败
    finished = Signal()                    # 循环因任何原因退出后发出

    def __init__(self, port, baud, parent=None):
        super().__init__(parent)
        self._port = port
        self._baud = baud
        self._ser = None
        self._running = False

    def run(self):
        """槽函数，由 QThread.started 触发，在子线程执行读取循环。"""
        self._running = True
        self.status_changed.emit(f"正在打开 {self._port} @ {self._baud}")
        try:
            self._ser = serial.Serial(self._port, self._baud, timeout=0.1)
        except (serial.SerialException, OSError, ValueError) as e:
            self.error_occurred.emit(f"打开串口失败: {e}")
            self.status_changed.emit("串口打开失败")
            self.finished.emit()
            return

        self.status_changed.emit(f"已连接 {self._port} @ {self._baud} baud")
        fail_streak = 0
        alerted = False
        try:
            while self._running:
                try:
                    raw = self._ser.readline()   # timeout=0.1，不会阻塞超 100ms
                except (serial.SerialException, OSError) as e:
                    if self._running:
                        self.error_occurred.emit(f"串口读取错误(可能已断开): {e}")
                    break
                if not raw:
                    continue                     # readline 超时 tick
                # 用 utf-8 而非 ascii：固件分隔符若为全角逗号，ascii+ignore 会
                # 直接删字节把两个数值粘连成一串，这里必须保留全角字符才能识别
                text = raw.decode('utf-8', errors='replace').strip()
                frame = parse_eda_frame(text)
                if frame is None:
                    # 连续失败到阈值就报一次，避免"连上了但一条数据都没有"这种静默失效
                    fail_streak += 1
                    if fail_streak >= _PARSE_FAIL_ALERT and not alerted:
                        alerted = True
                        self.error_occurred.emit(
                            f"连续 {fail_streak} 行无法解析，请核对下位机输出格式。"
                            f"首例原文: {text[:60]!r}"
                        )
                    continue
                fail_streak = 0
                g1, g2 = frame
                self.data_received.emit(g1, float('nan') if g2 is None else g2)
        finally:
            self._close()
        self.status_changed.emit("已停止")
        self.finished.emit()

    def stop(self):
        """线程安全停止：置标志并关闭串口，循环在 100ms 内退出。"""
        self._running = False
        self._close()

    def _close(self):
        if self._ser is not None:
            try:
                self._ser.close()
            except Exception:
                pass
            self._ser = None
