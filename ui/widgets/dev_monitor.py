# Retune — desktop audio player with real-time pitch and tempo control.
# Copyright (c) 2026 Christopher_VN
# Licensed under the MIT License.
#
# ui/widgets/dev_monitor.py
# -------------------------
# Developer-mode memory and CPU monitor widget.
# Only shown when the application is launched with the --dev flag.
# Reads process metrics via psutil every second and displays:
#   RAM  — virtual memory size (total reserved address space)
#   RSS  — resident set size (actual physical memory in use)
#   CPU  — CPU usage of the current process

import os
import psutil
from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QLabel
from PySide6.QtCore import QTimer, Qt
from ui.widgets.card import Card


class DevMonitor(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._process = psutil.Process(os.getpid())

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        card = Card(radius=12)
        card_layout = QVBoxLayout(card)
        card_layout.setContentsMargins(14, 10, 14, 10)
        card_layout.setSpacing(6)

        header = QLabel("⚙  dev monitor")
        header.setObjectName("devHeader")
        card_layout.addWidget(header)

        row = QHBoxLayout()
        row.setSpacing(20)

        self._ram_label = self._make_metric("RAM", "— MB")
        self._rss_label = self._make_metric("RSS", "— MB")
        self._cpu_label = self._make_metric("CPU", "—%")

        for w in [self._ram_label, self._rss_label, self._cpu_label]:
            row.addWidget(w)
        row.addStretch()

        card_layout.addLayout(row)
        layout.addWidget(card)

        # Update metrics every second.
        self._timer = QTimer()
        self._timer.setInterval(1000)
        self._timer.timeout.connect(self._update)
        self._timer.start()
        self._update()

    def _make_metric(self, title: str, value: str) -> QWidget:
        """Create a small label pair (title above, value below)."""
        w = QWidget()
        w.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
        vl = QVBoxLayout(w)
        vl.setContentsMargins(0, 0, 0, 0)
        vl.setSpacing(2)

        lbl_title = QLabel(title)
        lbl_title.setObjectName("devMetricTitle")

        lbl_value = QLabel(value)
        lbl_value.setObjectName("devMetricValue")

        vl.addWidget(lbl_title)
        vl.addWidget(lbl_value)

        # Store reference so _update() can reach the value label.
        w._value_label = lbl_value
        return w

    def _update(self):
        """Read current process metrics and refresh the displayed values."""
        try:
            mem = self._process.memory_info()
            rss = mem.rss / 1024 / 1024
            vms = mem.vms / 1024 / 1024
            cpu = self._process.cpu_percent(interval=None)

            self._ram_label._value_label.setText(f"{vms:.1f} MB")
            self._rss_label._value_label.setText(f"{rss:.1f} MB")
            self._cpu_label._value_label.setText(f"{cpu:.1f}%")
        except Exception:
            pass

    def stop(self):
        """Stop the update timer. Call before the widget is destroyed."""
        self._timer.stop()
