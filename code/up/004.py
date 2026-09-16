import sys
import os
import math
import statistics
import pathlib
from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout,
                                  QHBoxLayout, QPushButton, QFileDialog, QMessageBox,
                                  QListWidget, QListWidgetItem, QSplitter,
                                  QFrame, QTextEdit, QGroupBox, QLabel,
                                  QComboBox, QSpinBox, QDialog, QFormLayout,
                                  QLineEdit, QDialogButtonBox)
from PySide6.QtCore import Qt, QPointF, Signal, QTimer, QThread
from PySide6.QtCharts import QChart, QChartView, QLineSeries, QValueAxis
from PySide6.QtGui import QPainter, QPen, QWheelEvent

from serial_acquisition import SerialWorker, list_serial_ports, _SERIAL_AVAILABLE
from recording import SessionRecorder

# 固件 adc_to_g() 输出 = (G_µS - 0.7) * 30（见 code/main.c），即固件单位 = µS × 30。
# 串口实时数据的 SCR 幅度阈值必须按此换算：否则 0.1~5.0 µS 的判据会把固件尺度下
# 所有真实 SCR（约 3~150）全部拒掉，SCR_Freq 恒为 0。
FIRMWARE_UNIT_SCALE = 30.0
# SCR 幅度生理范围（µS，文献经验值）：文件数据量纲未知，按 µS 处理
SCR_AMP_MIN_UV, SCR_AMP_MAX_UV = 0.1, 5.0

# 录制会话落盘目录（相对本文件）。已在 .gitignore 排除：含患者信息，不入库
RECORDING_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "recordings")

# 病例元信息字段（申报书第三阶段临床采集所需的 CRF 要素）
CASE_FIELDS = [
    ("patient_id", "患者编号"),
    ("procedure", "术式"),
    ("block_type", "阻滞方式"),
    ("block_plane", "阻滞平面"),
    ("drugs", "局麻药/用药"),
    ("operator", "操作者"),
]

# 术中常用标注时间点：申报书要求在阻滞前基线、阻滞后、手术切皮等关键点同步采集
EVENT_LABELS = ["基线", "阻滞", "切皮", "拔管"]


class ZoomableChartView(QChartView):
      mouseMovedToDataPoint = Signal(float, float)
      mouseLeft = Signal()

      def __init__(self, parent=None):
          super().__init__(parent)
          self.setRubberBand(QChartView.NoRubberBand)
          self.setDragMode(QChartView.NoDrag)
          self._is_panning = False
          self._last_pos = None
          self.setMouseTracking(True)

      def wheelEvent(self, event: QWheelEvent):
          pos = event.position()
          factor = 1.2
          if event.angleDelta().y() < 0:
              factor = 1.0 / factor

          chart = self.chart()
          if not chart:
              return

          value_pos = chart.mapToValue(pos)
          axis_x = chart.axisX()
          axis_y = chart.axisY()
          if not axis_x or not axis_y:
              return

          x_min, x_max = axis_x.min(), axis_x.max()
          y_min, y_max = axis_y.min(), axis_y.max()

          new_x_min = value_pos.x() - (value_pos.x() - x_min) / factor
          new_x_max = value_pos.x() + (x_max - value_pos.x()) / factor
          new_y_min = value_pos.y() - (value_pos.y() - y_min) / factor
          new_y_max = value_pos.y() + (y_max - value_pos.y()) / factor

          axis_x.setRange(new_x_min, new_x_max)
          axis_y.setRange(new_y_min, new_y_max)
          self.update()
          event.accept()

      def mousePressEvent(self, event):
          if event.button() == Qt.RightButton:
              self._is_panning = True
              self._last_pos = event.position()
              self.setCursor(Qt.ClosedHandCursor)
              event.accept()
          else:
              super().mousePressEvent(event)

      def mouseMoveEvent(self, event):
          pos = event.position()
          chart = self.chart()
          if chart:
              plot_area = chart.plotArea()
              if plot_area.contains(pos):
                  value = chart.mapToValue(pos)
                  self.mouseMovedToDataPoint.emit(value.x(), value.y())
              else:
                  self.mouseLeft.emit()

          if self._is_panning and self._last_pos:
              delta = pos - self._last_pos
              chart = self.chart()
              if chart:
                  axis_x = chart.axisX()
                  axis_y = chart.axisY()
                  if axis_x and axis_y:
                      dx = (axis_x.max() - axis_x.min()) * delta.x() / self.width()
                      dy = (axis_y.max() - axis_y.min()) * delta.y() / self.height()
                      axis_x.setRange(axis_x.min() - dx, axis_x.max() - dx)
                      axis_y.setRange(axis_y.min() - dy, axis_y.max() - dy)
              self._last_pos = pos
              event.accept()
          else:
              super().mouseMoveEvent(event)

      def mouseReleaseEvent(self, event):
          if event.button() == Qt.RightButton and self._is_panning:
              self._is_panning = False
              self.setCursor(Qt.ArrowCursor)
              event.accept()
          else:
              super().mouseReleaseEvent(event)

      def leaveEvent(self, event):
          self.mouseLeft.emit()
          super().leaveEvent(event)


class CaseInfoDialog(QDialog):
      """病例元信息录入。术中随时可修改，取值后由调用方写入录制会话的 meta.json。"""

      def __init__(self, parent=None, values=None):
          super().__init__(parent)
          self.setWindowTitle("病例信息")
          self.resize(430, 280)
          form = QFormLayout(self)
          self.edits = {}
          for key, label in CASE_FIELDS:
              edit = QLineEdit()
              edit.setText((values or {}).get(key, ""))
              self.edits[key] = edit
              form.addRow(label + "：", edit)
          buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
          buttons.accepted.connect(self.accept)
          buttons.rejected.connect(self.reject)
          form.addRow(buttons)

      def values(self):
          return {k: e.text().strip() for k, e in self.edits.items()}


class EDASignalItem(QListWidgetItem):
      def __init__(self, name, series, color, data):
          super().__init__(name)
          self.series = series
          self.color = color
          self.signal_data = data
          self.setForeground(color)


class EDAViewer(QMainWindow):
      def __init__(self):
          super().__init__()
          self.setWindowTitle("皮肤电信号对比查看器")
          self.resize(1000, 700)

          self.colors = [
              Qt.red, Qt.green, Qt.blue, Qt.cyan, Qt.magenta,
              Qt.yellow, Qt.darkRed, Qt.darkGreen, Qt.darkBlue,
              Qt.darkCyan, Qt.darkMagenta, Qt.darkYellow
          ]
          self.color_index = 0
          self.used_colors = set()

          self._sample_rate = 4.0  # 默认采样率 Hz

          central_widget = QWidget()
          self.setCentralWidget(central_widget)
          main_layout = QHBoxLayout(central_widget)

          outer_splitter = QSplitter(Qt.Vertical)
          main_layout.addWidget(outer_splitter)

          # ---- 上半：文件对比区（原布局，仅改容器）----
          file_splitter = QSplitter(Qt.Horizontal)
          outer_splitter.addWidget(file_splitter)

          self.chart_view = ZoomableChartView()
          self.chart_view.setRenderHint(QPainter.Antialiasing)
          file_splitter.addWidget(self.chart_view)

          right_panel = QFrame()
          right_panel.setFrameShape(QFrame.StyledPanel)
          right_layout = QVBoxLayout(right_panel)
          file_splitter.addWidget(right_panel)
          file_splitter.setSizes([700, 300])

          # ---- 下半：串口实时采集面板 ----
          serial_panel = QFrame()
          serial_panel.setFrameShape(QFrame.StyledPanel)
          serial_layout = QVBoxLayout(serial_panel)
          outer_splitter.addWidget(serial_panel)
          outer_splitter.setSizes([600, 350])

          # 按钮区域（第一行）
          btn_layout1 = QHBoxLayout()
          self.add_btn = QPushButton("添加信号")
          self.clear_btn = QPushButton("清除所有")
          btn_layout1.addWidget(self.add_btn)
          btn_layout1.addWidget(self.clear_btn)
          right_layout.addLayout(btn_layout1)

          # 按钮区域（第二行）
          btn_layout2 = QHBoxLayout()
          self.reset_view_btn = QPushButton("重置视图")
          btn_layout2.addWidget(self.reset_view_btn)
          right_layout.addLayout(btn_layout2)

          # 按钮区域（第三行）
          btn_layout3 = QHBoxLayout()
          self.remove_btn = QPushButton("移除选中信号")
          btn_layout3.addWidget(self.remove_btn)
          right_layout.addLayout(btn_layout3)

          # 信号列表
          list_group = QGroupBox("已加载信号")
          list_layout = QVBoxLayout()
          self.signal_list = QListWidget()
          self.signal_list.setSelectionMode(QListWidget.SingleSelection)
          list_layout.addWidget(self.signal_list)
          list_group.setLayout(list_layout)
          right_layout.addWidget(list_group)

          # 信号统计与特征
          combined_group = QGroupBox("信号统计与特征")
          combined_layout = QVBoxLayout()

          self.stats_text = QTextEdit()
          self.stats_text.setReadOnly(True)
          self.stats_text.setMaximumHeight(80)
          combined_layout.addWidget(self.stats_text)

          self.mean_scl_label = QLabel("Mean_SCL: --")
          self.scr_freq_label = QLabel("SCR_Freq: --")
          self.scr_amp_label = QLabel("SCR_Amp: --")
          self.avg_rise_time_label = QLabel("Avg_RiseTime: --")
          self.avg_half_recovery_label = QLabel("Avg_HalfRecovery: --")
          combined_layout.addWidget(self.mean_scl_label)
          combined_layout.addWidget(self.scr_freq_label)
          combined_layout.addWidget(self.scr_amp_label)
          combined_layout.addWidget(self.avg_rise_time_label)
          combined_layout.addWidget(self.avg_half_recovery_label)

          combined_group.setLayout(combined_layout)
          right_layout.addWidget(combined_group)

          # 鼠标位置显示
          mouse_group = QGroupBox("鼠标位置")
          mouse_layout = QVBoxLayout()
          self.mouse_pos_label = QLabel("索引: --, 数值: --")
          mouse_layout.addWidget(self.mouse_pos_label)
          mouse_group.setLayout(mouse_layout)
          right_layout.addWidget(mouse_group)

          # 保存按钮
          self.save_btn = QPushButton("保存选中信号")
          right_layout.addWidget(self.save_btn)

          right_layout.addStretch()

          # 初始化图表
          self.chart = QChart()
          self.chart.setTitle("皮肤电信号波形")
          self.chart.setAnimationOptions(QChart.SeriesAnimations)
          self.chart_view.setChart(self.chart)

          self.axis_x = QValueAxis()
          self.axis_x.setTitleText("采样点")
          self.axis_y = QValueAxis()
          self.axis_y.setTitleText("信号值")
          self.chart.addAxis(self.axis_x, Qt.AlignBottom)
          self.chart.addAxis(self.axis_y, Qt.AlignLeft)

          # 连接信号
          self.add_btn.clicked.connect(self.add_signal)
          self.clear_btn.clicked.connect(self.clear_all)
          self.save_btn.clicked.connect(self.save_selected)
          self.reset_view_btn.clicked.connect(self.reset_view)
          self.signal_list.itemSelectionChanged.connect(self.on_selection_changed)
          self.remove_btn.clicked.connect(self.remove_selected_signal)

          self.chart_view.mouseMovedToDataPoint.connect(self.on_mouse_moved)
          self.chart_view.mouseLeft.connect(self.on_mouse_left)

          # ============ 串口实时采集面板 ============
          # 实时图表（独立，避免与文件图表的缩放/平移冲突）
          self.rt_chart = QChart()
          self.rt_chart.setTitle("实时串口电导波形（电导1 / 电导2）")
          self.rt_chart.setAnimationOptions(QChart.NoAnimation)
          self.rt_chart_view = QChartView(self.rt_chart)
          self.rt_chart_view.setRenderHint(QPainter.Antialiasing)
          serial_layout.addWidget(self.rt_chart_view)

          self.rt_axis_x = QValueAxis()
          self.rt_axis_x.setTitleText("采样点")
          self.rt_axis_x.setRange(0, 500)
          self.rt_axis_y = QValueAxis()
          self.rt_axis_y.setTitleText("EDA/GSR")
          self.rt_axis_y.setRange(0, 1000)
          self.rt_chart.addAxis(self.rt_axis_x, Qt.AlignBottom)
          self.rt_chart.addAxis(self.rt_axis_y, Qt.AlignLeft)

          # 控制行：端口 / 波特率 / 开始停止 / 滚动窗口
          ctrl = QHBoxLayout()
          ctrl.addWidget(QLabel("端口:"))
          self.port_combo = QComboBox()
          self.refresh_ports()
          self.refresh_ports_btn = QPushButton("刷新端口")
          ctrl.addWidget(self.port_combo)
          ctrl.addWidget(self.refresh_ports_btn)
          ctrl.addWidget(QLabel("波特率:"))
          self.baud_combo = QComboBox()
          self.baud_combo.addItems(
              ["9600", "19200", "38400", "57600", "115200",
               "230400", "460800", "921600"]
          )
          self.baud_combo.setCurrentText("115200")
          ctrl.addWidget(self.baud_combo)
          self.start_btn = QPushButton("开始采集")
          self.window_spin = QSpinBox()
          self.window_spin.setRange(50, 100000)
          self.window_spin.setValue(500)
          self.window_spin.setSuffix(" 点")
          ctrl.addWidget(self.start_btn)
          ctrl.addWidget(QLabel("滚动窗口:"))
          ctrl.addWidget(self.window_spin)
          serial_layout.addLayout(ctrl)

          # 录制行：录制开关 / 病例信息 / 会话状态
          rec_row = QHBoxLayout()
          self.rec_btn = QPushButton("开始录制")
          self.case_btn = QPushButton("病例信息")
          self.rec_label = QLabel("未录制")
          self.rec_label.setStyleSheet("color: gray;")
          rec_row.addWidget(self.rec_btn)
          rec_row.addWidget(self.case_btn)
          rec_row.addWidget(self.rec_label)
          rec_row.addStretch()
          serial_layout.addLayout(rec_row)

          # 事件标注行：术中一键打点
          evt_row = QHBoxLayout()
          evt_row.addWidget(QLabel("事件标注:"))
          self.event_btns = {}
          for label in EVENT_LABELS:
              btn = QPushButton(label)
              self.event_btns[label] = btn
              evt_row.addWidget(btn)
          evt_row.addStretch()
          serial_layout.addLayout(evt_row)

          self.status_label = QLabel("未连接")
          self.status_label.setStyleSheet("color: gray;")
          serial_layout.addWidget(self.status_label)

          # 串口状态
          self._serial_thread = None
          self._serial_worker = None
          self._live_item = None          # 电导1（主通道，始终存在）
          self._live_item2 = None         # 电导2（首次收到第二通道数据时才创建）
          self._live_data = []            # 普通 list（compute_scr_features 需切片/索引）
          self._live_data2 = []
          self._live_x = 0
          self._live_window = 500
          self._live_sample_rate = 4.0
          self._samples_since_stats = 0

          # 录制状态
          self._recorder = SessionRecorder(RECORDING_DIR)
          self._case_info = {}

          # 定时器：200ms 刷曲线 / 1s 刷实时统计
          self.chart_timer = QTimer(self)
          self.chart_timer.setInterval(200)
          self.chart_timer.timeout.connect(self.refresh_live_chart)
          self.stats_timer = QTimer(self)
          self.stats_timer.setInterval(1000)
          self.stats_timer.timeout.connect(self.on_live_stats_timer)

          # 串口控件信号
          self.refresh_ports_btn.clicked.connect(self.refresh_ports)
          self.start_btn.clicked.connect(self.toggle_acquisition)
          self.window_spin.valueChanged.connect(self._on_window_changed)

          # 录制控件信号
          self.rec_btn.clicked.connect(self.toggle_recording)
          self.case_btn.clicked.connect(self.edit_case_info)
          for label, btn in self.event_btns.items():
              btn.clicked.connect(lambda _=False, lb=label: self.mark_event(lb))
          self._set_recording_ui(False)
          self._set_acq_ui(False)   # 初始未采集：录制按钮须为禁用态

      def get_next_color(self):
          """获取下一个未被占用的颜色；12 色全部占用时顺延复用（不允许无界循环）"""
          n = len(self.colors)
          for _ in range(n):
              color = self.colors[self.color_index % n]
              self.color_index += 1
              if color not in self.used_colors:
                  self.used_colors.add(color)
                  return color
          color = self.colors[self.color_index % n]
          self.color_index += 1
          return color

      def release_color(self, color):
          """释放已用颜色"""
          self.used_colors.discard(color)

      def add_signal(self):
          file_path, _ = QFileDialog.getOpenFileName(
              self, "选择数据文件", "", "文本文件 (*.txt);;CSV文件 (*.csv);;所有文件 (*)"
          )
          if not file_path:
              return

          try:
              data = []
              with open(file_path, 'r', encoding='utf-8') as f:
                  for line in f:
                      line = line.strip()
                      if line:
                          parts = line.split(',')
                          for p in parts:
                              if p.strip():
                                  data.append(float(p.strip()))
              if not data:
                  QMessageBox.warning(self, "警告", "文件中没有有效数值")
                  return

              series = QLineSeries()
              file_name = pathlib.Path(file_path).name
              series.setName(f"信号 {self.signal_list.count() + 1} ({file_name})")
              points = [QPointF(i, val) for i, val in enumerate(data)]
              series.append(points)

              series.setPointsVisible(True)
              series.setMarkerSize(3)
              color = self.get_next_color()
              pen = QPen(color)
              pen.setWidth(2)
              series.setPen(pen)

              self.chart.addSeries(series)
              series.attachAxis(self.axis_x)
              series.attachAxis(self.axis_y)

              item = EDASignalItem(series.name(), series, color, data)
              self.signal_list.addItem(item)

              self.adjust_axes()
              self.signal_list.setCurrentItem(item)

          except Exception as e:
              QMessageBox.critical(self, "错误", f"读取文件失败：{str(e)}")

      def adjust_axes(self):
          if self.chart.series():
              all_x = []
              all_y = []
              for series in self.chart.series():
                  if isinstance(series, QLineSeries):
                      points = series.pointsVector()
                      if points:
                          x_vals = [p.x() for p in points]
                          y_vals = [p.y() for p in points]
                          all_x.extend(x_vals)
                          all_y.extend(y_vals)
              if all_x and all_y:
                  min_x, max_x = min(all_x), max(all_x)
                  min_y, max_y = min(all_y), max(all_y)
                  margin_x = (max_x - min_x) * 0.02 if max_x != min_x else 1
                  margin_y = (max_y - min_y) * 0.05 if max_y != min_y else 1
                  self.axis_x.setRange(min_x - margin_x, max_x + margin_x)
                  self.axis_y.setRange(min_y - margin_y, max_y + margin_y)
          else:
              self.axis_x.setRange(0, 10)
              self.axis_y.setRange(0, 10)

      def reset_view(self):
          self.adjust_axes()

      def compute_stats(self, data):
          """计算基础统计信息"""
          n = len(data)
          if n == 0:
              return {}
          min_val = min(data)
          max_val = max(data)
          mean_val = statistics.mean(data)
          std_val = statistics.stdev(data) if n > 1 else 0
          return {
              "点数": n,
              "最小值": f"{min_val:.4f}",
              "最大值": f"{max_val:.4f}",
              "均值": f"{mean_val:.4f}",
              "标准差": f"{std_val:.4f}"
          }

      def on_selection_changed(self):
          selected = self.signal_list.currentItem()
          self.update_stats_display(selected)

      def save_selected(self):
          selected = self.signal_list.currentItem()
          if not selected:
              QMessageBox.warning(self, "警告", "请先选中一个信号")
              return

          file_path, _ = QFileDialog.getSaveFileName(
              self, "保存数据文件", "", "文本文件 (*.txt);;CSV文件 (*.csv);;所有文件 (*)"
          )
          if not file_path:
              return

          try:
              data = selected.signal_data
              with open(file_path, 'w', encoding='utf-8') as f:
                  for val in data:
                      f.write(f"{val}\n")
              QMessageBox.information(self, "成功", f"数据已保存到 {file_path}")
          except Exception as e:
              QMessageBox.critical(self, "错误", f"保存文件失败：{str(e)}")

      def compute_scr_features(self, data, sample_rate=4.0,
                               min_amp=0.1, max_amp=5.0,
                               smoothing_sec=0.75):
          """
          优化版 SCR 特征提取算法

          参数:
              data: 原始 EDA 信号
              sample_rate: 采样率 (Hz), 默认 4.0 Hz (250ms/点)
              min_amp: 最小 SCR 幅度 (µS), 默认 0.1
              max_amp: 最大 SCR 幅度 (µS), 默认 5.0
              smoothing_sec: 平滑窗口时长 (秒), 默认 0.75s
                             （按时长而非点数：原值"3 点"是 4Hz 时代的硬编码，
                             50Hz 实时数据下仅 60ms 等于未滤波，会因噪声大量虚检）

          返回:
              (mean_scl, scr_freq, scr_amp, avg_rise_time, avg_half_recovery)
          """
          self._sample_rate = sample_rate

          if not data or len(data) < 10:
              return 0.0, 0, 0.0, 0.0, 0.0

          # ========== 1. 平滑预处理 ==========
          smoothed = self._moving_average(data, max(1, int(round(smoothing_sec * sample_rate))))

          # ========== 2. 基线估计与去趋势 ==========
          mean_scl = statistics.mean(smoothed)
          detrended = [v - mean_scl for v in smoothed]

          # ========== 3. 一阶差分 + 阈值检测极值 ==========
          n = len(detrended)
          diff = [detrended[i+1] - detrended[i] for i in range(n-1)]

          # 使用自适应阈值：信号标准差的 0.5 倍
          signal_std = statistics.stdev(detrended) if len(detrended) > 1 else 0
          adaptive_threshold = max(0.5 * signal_std, 0.02)

          minima, maxima = self._find_extrema_with_prominence(
              detrended, diff, adaptive_threshold
          )

          if not minima or not maxima:
              return mean_scl, 0, 0.0, 0.0, 0.0

          # ========== 4. SCR 配对 (丘壑算法) ==========
          scr_list = self._optimal_scr_pairing(
              minima, maxima, detrended, min_amp, max_amp
          )

          if not scr_list:
              return mean_scl, 0, 0.0, 0.0, 0.0

          # ========== 5. 计算统计特征 ==========
          amplitudes = [scr['amplitude'] for scr in scr_list]
          rise_times = [scr['rise_time'] for scr in scr_list if scr['rise_time'] > 0]
          half_recoveries = [scr['half_recovery'] for scr in scr_list if scr['half_recovery'] > 0]

          scr_freq = len(scr_list)
          scr_amp = statistics.mean(amplitudes) if amplitudes else 0.0
          avg_rise_time = statistics.mean(rise_times) if rise_times else 0.0
          avg_half_recovery = statistics.mean(half_recoveries) if half_recoveries else 0.0

          return mean_scl, scr_freq, scr_amp, avg_rise_time, avg_half_recovery

      def _moving_average(self, data, window):
          """滑动平均平滑"""
          if window < 2:
              return data
          result = []
          half = window // 2
          for i in range(len(data)):
              start = max(0, i - half)
              end = min(len(data), i + half + 1)
              result.append(statistics.mean(data[start:end]))
          return result

      def _find_extrema_with_prominence(self, data, diff, threshold):
          """
          带显著性的极值检测

          显著性 prominence: 极值点到其两侧山谷的落差
          """
          n = len(data)
          minima = []  # (index, value, prominence)
          maxima = []  # (index, value, prominence)

          # 显著性搜索窗 12.5s：4Hz 下即 50 点（与旧硬编码等价），50Hz 实时数据按采样率放大
          search_window = min(int(12.5 * self._sample_rate), n // 4)

          # ---- 检测极大值 (峰) ----
          i = 1
          while i < n - 1:
              if diff[i-1] > 0 and diff[i] <= 0:
                  if data[i] >= data[i-1] and data[i] >= data[i+1]:
                      left_min = min(data[max(0, i-search_window):i+1]) if i > 0 else data[0]
                      right_min = min(data[i:min(n, i+search_window)]) if i < n-1 else data[-1]
                      prom = data[i] - min(left_min, right_min)
                      if prom >= threshold:
                          maxima.append((i, data[i], prom))
              i += 1

          # ---- 检测极小值 (谷) ----
          i = 1
          while i < n - 1:
              if diff[i-1] < 0 and diff[i] >= 0:
                  if data[i] <= data[i-1] and data[i] <= data[i+1]:
                      left_max = max(data[max(0, i-search_window):i+1]) if i > 0 else data[0]
                      right_max = max(data[i:min(n, i+search_window)]) if i < n-1 else data[-1]
                      prom = max(left_max, right_max) - data[i]
                      if prom >= threshold:
                          minima.append((i, data[i], prom))
              i += 1

          return minima, maxima

      def _optimal_scr_pairing(self, minima, maxima, data, min_amp, max_amp):
          """
          优化 SCR 配对算法

          使用"丘壑填水"策略:
          1. 每个谷后第一个满足条件的峰配对
          2. 检查幅度是否在生理范围内
          3. 去重确保一个谷只配一个峰
          """
          scr_list = []

          for min_idx, min_val, min_prom in minima:
              # 按本节文档"每个谷后第一个满足条件的峰配对"。原实现取的是全信号中幅度
              # 最大的峰，会把早先的谷配到远处的高峰上，实测把 1s 的上升算成 9.5s；
              # 多个谷共用同一个峰还会被下面的去重塌缩成一个 SCR。maxima 按索引升序，
              # 故首个满足幅度条件的峰即最近的合格峰。
              for max_idx, max_val, max_prom in maxima:
                  if max_idx <= min_idx:
                      continue
                  amp = max_val - min_val
                  if not (min_amp <= amp <= max_amp):
                      continue

                  rise_time = (max_idx - min_idx) / self._sample_rate

                  half_level = min_val + amp / 2.0
                  half_recovery = 0.0

                  # 半恢复搜索窗 25s：4Hz 下即 100 点（与旧硬编码等价），50Hz 下不再只搜 2s
                  lookahead = int(25 * self._sample_rate)
                  for j in range(max_idx, min(len(data), max_idx + lookahead)):
                      if data[j] <= half_level:
                          half_recovery = (j - max_idx) / self._sample_rate
                          break

                  scr_list.append({
                      'min_idx': min_idx,
                      'max_idx': max_idx,
                      'amplitude': amp,
                      'rise_time': rise_time,
                      'half_recovery': half_recovery,
                      'prominence': max_prom
                  })
                  break

          scr_list.sort(key=lambda x: x['min_idx'])

          used_mins = set()
          used_maxs = set()
          filtered = []
          for scr in scr_list:
              if scr['min_idx'] not in used_mins and scr['max_idx'] not in used_maxs:
                  filtered.append(scr)
                  used_mins.add(scr['min_idx'])
                  used_maxs.add(scr['max_idx'])

          return filtered

      def update_stats_display(self, item, sample_rate=None):
          if item and isinstance(item, EDASignalItem):
              data = item.signal_data
              stats = self.compute_stats(data)
              self.stats_text.setText("\n".join([f"{k}: {v}" for k, v in stats.items()]))
              sr = sample_rate if sample_rate is not None else self._sample_rate
              # 串口数据是固件单位（µS×30），文件数据量纲未知按 µS 处理
              scale = FIRMWARE_UNIT_SCALE if getattr(item, 'is_live', False) else 1.0
              mean_scl, scr_freq, scr_amp, avg_rise, avg_half = self.compute_scr_features(
                  data, sr, SCR_AMP_MIN_UV * scale, SCR_AMP_MAX_UV * scale)
              self.mean_scl_label.setText(f"Mean_SCL: {mean_scl:.4f}")
              self.scr_freq_label.setText(f"SCR_Freq: {scr_freq}")
              self.scr_amp_label.setText(f"SCR_Amp: {scr_amp:.4f}")
              self.avg_rise_time_label.setText(
                  f"Avg_RiseTime: {avg_rise:.2f}s" if avg_rise > 0 else "Avg_RiseTime: --"
              )
              self.avg_half_recovery_label.setText(
                  f"Avg_HalfRecovery: {avg_half:.2f}s" if avg_half > 0 else "Avg_HalfRecovery: --"
              )
          else:
              self.stats_text.clear()
              self.mean_scl_label.setText("Mean_SCL: --")
              self.scr_freq_label.setText("SCR_Freq: --")
              self.scr_amp_label.setText("SCR_Amp: --")
              self.avg_rise_time_label.setText("Avg_RiseTime: --")
              self.avg_half_recovery_label.setText("Avg_HalfRecovery: --")

      def clear_all(self):
          self.stop_acquisition()              # 停止采集（未运行时安全空操作）
          self.rt_chart.removeAllSeries()      # 清空实时图表
          self._live_item = None
          self._live_item2 = None
          self._live_data = []
          self._live_data2 = []
          self._live_x = 0
          self.chart.removeAllSeries()
          self.signal_list.clear()
          self.color_index = 0
          self.used_colors.clear()
          self.stats_text.clear()
          self.mean_scl_label.setText("Mean_SCL: --")
          self.scr_freq_label.setText("SCR_Freq: --")
          self.scr_amp_label.setText("SCR_Amp: --")
          self.avg_rise_time_label.setText("Avg_RiseTime: --")
          self.avg_half_recovery_label.setText("Avg_HalfRecovery: --")
          self.mouse_pos_label.setText("索引: --, 数值: --")
          self.axis_x.setRange(0, 10)
          self.axis_y.setRange(0, 10)

      def on_mouse_moved(self, x, y):
          selected = self.signal_list.currentItem()
          if not selected:
              self.mouse_pos_label.setText("索引: --, 数值: --")
              return
          series = selected.series
          points = series.pointsVector()
          if not points:
              return
          idx = int(round(x))
          if 0 <= idx < len(points):
              y_val = points[idx].y()
              self.mouse_pos_label.setText(f"索引: {idx}, 数值: {y_val:.4f}")
          else:
              self.mouse_pos_label.setText("索引: --, 数值: --")

      def on_mouse_left(self):
          self.mouse_pos_label.setText("索引: --, 数值: --")

      def remove_selected_signal(self):
          current_item = self.signal_list.currentItem()
          if not current_item:
              QMessageBox.warning(self, "警告", "请先选中一个信号")
              return

          if getattr(current_item, 'is_live', False):
              if self._serial_thread is not None:
                  self.stop_acquisition()      # 删除实时项前先停止采集
              # 电导1/电导2 属同一次采集，一并移除，避免留下孤立曲线
              for item in (self._live_item, self._live_item2):
                  if item is not None:
                      self._drop_item(item)
              self._live_item = None
              self._live_item2 = None
              self._live_data = []
              self._live_data2 = []
          else:
              self._drop_item(current_item)

          self.adjust_axes()
          self.update_stats_display(None)

      def _drop_item(self, item):
          """从所属图表与信号列表中移除一个信号项"""
          series = item.series
          if series in self.chart.series():
              self.chart.removeSeries(series)
          if series in self.rt_chart.series():
              self.rt_chart.removeSeries(series)
          self.release_color(item.color)
          self.signal_list.takeItem(self.signal_list.row(item))

      # ============ 串口实时采集 ============

      def refresh_ports(self):
          """刷新可用串口列表"""
          current = self.port_combo.currentText()
          ports = list_serial_ports()
          self.port_combo.clear()
          self.port_combo.addItems(ports)
          if current in ports:
              self.port_combo.setCurrentText(current)

      def toggle_acquisition(self):
          if self._serial_thread is not None:
              self.stop_acquisition()
          else:
              self.start_acquisition()

      def start_acquisition(self):
          if not _SERIAL_AVAILABLE:
              QMessageBox.critical(
                  self, "错误", "未安装 pyserial，请在 conda 环境 EDA 中安装：pip install pyserial"
              )
              return
          port = self.port_combo.currentText()
          if not port:
              QMessageBox.warning(self, "警告", "请先选择串口")
              return
          baud = int(self.baud_combo.currentText())

          self._live_data = []
          self._live_data2 = []
          self._live_x = 0
          self._samples_since_stats = 0
          self._live_window = self.window_spin.value()
          self._live_item = self._make_live_item("电导1", self._live_data)
          self._live_item2 = None
          # 只在开采集时选中电导1；电导2 首帧到达时不应抢走用户的选中项
          self.signal_list.setCurrentItem(self._live_item)

          self._serial_thread = QThread(self)
          self._serial_worker = SerialWorker(port, baud)
          self._serial_worker.moveToThread(self._serial_thread)
          self._serial_thread.started.connect(self._serial_worker.run)
          self._serial_worker.data_received.connect(self.on_serial_data)
          self._serial_worker.status_changed.connect(self.on_serial_status)
          self._serial_worker.error_occurred.connect(self.on_serial_error)
          self._serial_worker.finished.connect(self._serial_thread.quit)
          self._serial_thread.finished.connect(self._serial_worker.deleteLater)
          self._serial_thread.finished.connect(self._serial_thread.deleteLater)
          self._serial_thread.finished.connect(self.on_serial_finished)

          self._set_acq_ui(True)
          self.chart_timer.start()
          self.stats_timer.start()
          self._serial_thread.start()

      def stop_acquisition(self):
          """停止采集：置标志并关串口，读取循环 100ms 内退出。"""
          if self._serial_worker is not None:
              self._serial_worker.stop()
          # thread.quit() 由 worker.finished 触发；on_serial_finished 复位 UI

      def on_serial_finished(self):
          """线程结束后的 UI 复位（在主线程执行）。保留实时项供查看/保存。"""
          # 采集停了就没有数据来源，录制必须一并结束并收尾落盘
          if self._recorder.is_recording:
              self._recorder.stop()
              self._set_recording_ui(False)
          self.chart_timer.stop()
          self.stats_timer.stop()
          self._set_acq_ui(False)
          self._serial_thread = None
          self._serial_worker = None

      def _set_acq_ui(self, running):
          self.start_btn.setText("停止采集" if running else "开始采集")
          self.port_combo.setEnabled(not running)
          self.baud_combo.setEnabled(not running)
          self.refresh_ports_btn.setEnabled(not running)
          # 没有采集就没有数据可录；病例信息任何时候都能填
          self.rec_btn.setEnabled(running)

      def _make_live_item(self, name, data):
          """创建实时信号项（加入信号列表，复用现有统计/SCR/保存逻辑）"""
          series = QLineSeries()
          series.setName(f"实时 {name} ({self.port_combo.currentText()})")
          series.setPointsVisible(False)
          color = self.get_next_color()
          pen = QPen(color)
          pen.setWidth(2)
          series.setPen(pen)
          self.rt_chart.addSeries(series)
          series.attachAxis(self.rt_axis_x)
          series.attachAxis(self.rt_axis_y)

          item = EDASignalItem(series.name(), series, color, data)
          item.is_live = True          # signal_data 直接指向对应缓冲（原地变更）
          self.signal_list.addItem(item)
          return item

      def _ensure_live_item2(self):
          """电导2 项延迟创建：单通道来源（旧版时间戳帧、裸数值）不会留下空的第二通道条目"""
          if self._live_item2 is None:
              self._live_item2 = self._make_live_item("电导2", self._live_data2)
          return self._live_item2

      def on_serial_data(self, g1, g2):
          """串口收到一帧（主线程）：g1 电导1，g2 电导2（单通道来源为 nan）"""
          if self._live_item is None:
              return
          # 先落盘再入缓冲：录制路径独立于绘图，界面卡顿不影响数据留存
          self._recorder.write_frame(g1, None if math.isnan(g2) else g2)
          self._live_data.append(g1)
          self._live_x += 1
          self._samples_since_stats += 1
          if not math.isnan(g2):
              self._ensure_live_item2()
              self._live_data2.append(g2)
          # 两通道同步裁剪到滚动窗口
          for buf in (self._live_data, self._live_data2):
              if len(buf) > self._live_window:
                  del buf[: len(buf) - self._live_window]

      def refresh_live_chart(self):
          """200ms 定时：批量重建实时曲线并自动滚动 X 轴"""
          if self._live_item is None or not self._live_data:
              return
          for item, buf in ((self._live_item, self._live_data),
                            (self._live_item2, self._live_data2)):
              if item is None or not buf:
                  continue
              n = len(buf)
              pts = [QPointF(self._live_x - n + i, v) for i, v in enumerate(buf)]
              item.series.replace(pts)      # 批量替换，不用逐点 append
          self.rt_axis_x.setRange(self._live_x - len(self._live_data), self._live_x)

      def on_live_stats_timer(self):
          """1s 定时：用真实采样率刷新实时项统计（仅当列表选中实时项）"""
          self._refresh_recording_status()
          if self._live_item is None or not self._live_data:
              return
          measured = self._samples_since_stats
          if measured > 0:
              self._live_sample_rate = float(measured)   # 实际 Hz，替代硬编码 4.0
          self._samples_since_stats = 0
          selected = self.signal_list.currentItem()
          for item, buf in ((self._live_item, self._live_data),
                            (self._live_item2, self._live_data2)):
              if item is not None and selected is item and buf:
                  self.update_stats_display(item, sample_rate=self._live_sample_rate)

      # ============ 录制与事件标注 ============

      def toggle_recording(self):
          if self._recorder.is_recording:
              session = self._recorder.stop()
              self._set_recording_ui(False)
              if session:
                  QMessageBox.information(
                      self, "录制结束",
                      f"本次会话已保存到：\n{session}\n\n"
                      f"数据 {self._recorder.frame_count} 帧，"
                      f"事件 {self._recorder.event_count} 个")
          else:
              if self._live_item is None:
                  QMessageBox.warning(self, "警告", "请先开始采集再录制")
                  return
              if not self._case_info.get("patient_id"):
                  # 患者编号是会话目录名与后续检索的主键，缺了会退化成 case 目录
                  if not self.edit_case_info():
                      return
              session = self._recorder.start(self._case_info)
              if session is None:
                  QMessageBox.critical(self, "错误",
                                       self._recorder.error or "创建会话失败")
                  return
              self._set_recording_ui(True)

      def edit_case_info(self):
          """录入/修改病例元信息。录制中修改会即时刷新 meta.json。返回是否已确认。"""
          dlg = CaseInfoDialog(self, self._case_info)
          if dlg.exec() != QDialog.Accepted:
              return False
          self._case_info = dlg.values()
          self._recorder.set_meta(self._case_info)
          return True

      def mark_event(self, label):
          """术中一键打点；事件记录当时的帧序号与主机时间"""
          if not self._recorder.is_recording:
              QMessageBox.warning(self, "警告", "当前未在录制，无法标注事件")
              return
          self._recorder.mark(label)
          self._refresh_recording_status()

      def _set_recording_ui(self, recording):
          self.rec_btn.setText("停止录制" if recording else "开始录制")
          for btn in self.event_btns.values():
              btn.setEnabled(recording)
          if not recording:
              self.rec_label.setText("未录制")
              self.rec_label.setStyleSheet("color: gray;")

      def _refresh_recording_status(self):
          """显示录制状态：会话名 + 已录帧数 + 事件数"""
          if not self._recorder.is_recording:
              return
          name = os.path.basename(self._recorder.session_dir or "")
          case = self._case_info.get("patient_id") or "未填病例号"
          self.rec_label.setText(
              f"● 录制中 | {case} | {name} | {self._recorder.frame_count} 帧 | "
              f"{self._recorder.event_count} 事件")
          self.rec_label.setStyleSheet("color: red;")
          if self._recorder.error:
              self.rec_label.setText(f"录制出错: {self._recorder.error}")
              self.rec_label.setStyleSheet("color: red;")

      def on_serial_status(self, text):
          self.status_label.setText(text)
          self.status_label.setStyleSheet(
              "color: green;" if "已连接" in text else "color: gray;"
          )

      def on_serial_error(self, text):
          self.status_label.setText(text)
          self.status_label.setStyleSheet("color: red;")

      def _on_window_changed(self, value):
          self._live_window = value
          if len(self._live_data) > value:
              del self._live_data[:len(self._live_data) - value]

      def closeEvent(self, event):
          """窗口关闭时干净地停掉串口线程，避免 QThread destroyed 崩溃"""
          self.stop_acquisition()
          if self._serial_thread is not None and self._serial_thread.isRunning():
              self._serial_thread.wait(3000)
          # 关窗即结束录制，确保 csv 关闭、meta.json 回写计数（否则数据不完整）
          if self._recorder.is_recording:
              self._recorder.stop()
          super().closeEvent(event)


def main():
      app = QApplication(sys.argv)
      window = EDAViewer()
      window.show()
      sys.exit(app.exec())


if __name__ == "__main__":
      main()