# Retune — desktop audio player with real-time pitch and tempo control.
# Copyright (c) 2026 Christopher_VN
# Licensed under the MIT License.
#
# ui/widgets/waveform.py
# ----------------------
# Custom widget that renders a waveform from a list of peak amplitudes and
# indicates playback progress with a filled color and a position cursor.
# Emits seek_requested(ratio) when the user clicks or drags on the waveform,
# where ratio is a float in [0, 1] representing the target position.

from PySide6.QtWidgets import QWidget
from PySide6.QtCore import Qt, QRectF, Signal
from PySide6.QtGui import QPainter, QColor, QPainterPath, QCursor


class WaveformWidget(QWidget):
    # Emitted when the user clicks or drags; value is in [0.0, 1.0].
    seek_requested = Signal(float)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(48)
        self.setMaximumHeight(48)
        self.setCursor(QCursor(Qt.CursorShape.PointingHandCursor))

        self._peaks: list[float] = []
        self._progress: float = 0.0
        self._dragging = False

        self._color_played   = QColor("#89b4fa")  # Blue for the played portion
        self._color_unplayed = QColor("#45475a")  # Grey for the remainder
        self._color_bg       = QColor("#181825")  # Background

    def set_waveform(self, peaks: list[float]):
        """Set the amplitude data and reset progress to zero."""
        self._peaks = peaks
        self._progress = 0.0
        self.update()

    def set_progress(self, value: float):
        """Update the playback cursor position (0.0 = start, 1.0 = end)."""
        self._progress = max(0.0, min(1.0, value))
        self.update()

    def clear(self):
        """Remove waveform data and reset display."""
        self._peaks = []
        self._progress = 0.0
        self.update()

    def _pos_to_ratio(self, x: int) -> float:
        """Convert an x pixel coordinate to a [0, 1] position ratio."""
        return max(0.0, min(1.0, x / max(1, self.width())))

    # --- Mouse events for seek ---

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton and self._peaks:
            self._dragging = True
            ratio = self._pos_to_ratio(event.position().x())
            self._progress = ratio
            self.seek_requested.emit(ratio)
            self.update()

    def mouseMoveEvent(self, event):
        if self._dragging and self._peaks:
            ratio = self._pos_to_ratio(event.position().x())
            self._progress = ratio
            self.seek_requested.emit(ratio)
            self.update()

    def mouseReleaseEvent(self, event):
        self._dragging = False

    # --- Painting ---

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        w = self.width()
        h = self.height()

        # Clip all drawing to a rounded rectangle so corners stay clean.
        path = QPainterPath()
        path.addRoundedRect(QRectF(0, 0, w, h), 8, 8)
        painter.setClipPath(path)
        painter.fillRect(0, 0, w, h, self._color_bg)

        if not self._peaks:
            return

        n = len(self._peaks)
        gap   = 1
        bar_w = max(1.0, (w - gap * (n - 1)) / n)
        step  = bar_w + gap
        played_x = w * self._progress

        for i, peak in enumerate(self._peaks):
            x     = i * step
            bar_h = max(2.0, peak * (h - 4))
            y     = (h - bar_h) / 2
            color = self._color_played if x < played_x else self._color_unplayed
            painter.fillRect(int(x), int(y), max(1, int(bar_w)), int(bar_h), color)

        # Draw a thin vertical cursor at the current position.
        # Clamped so it never overflows the widget bounds.
        cx = max(0, min(int(played_x), w - 2))
        painter.fillRect(cx, 0, 2, h, QColor("#cdd6f4"))

        painter.end()
