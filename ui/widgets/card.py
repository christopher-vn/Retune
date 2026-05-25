# Retune — desktop audio player with real-time pitch and tempo control.
# Copyright (c) 2026 Christopher_VN
# Licensed under the MIT License.
#
# ui/widgets/card.py
# ------------------
# A QWidget subclass that draws its own rounded background and border via
# QPainter. This is necessary because Qt's stylesheet border-radius property
# is not reliably applied to plain QWidget backgrounds on all platforms.

from PySide6.QtWidgets import QWidget
from PySide6.QtGui import QPainter, QColor, QPainterPath
from PySide6.QtCore import QRectF, Qt


class Card(QWidget):
    def __init__(self, parent=None, radius: int = 16):
        super().__init__(parent)
        self._radius = radius
        self._bg     = QColor("#181825")
        self._border = QColor("#313244")
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, False)
        self.setAutoFillBackground(False)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        # Inset by 0.5 px so the border stroke is not clipped at the edges.
        rect = QRectF(0.5, 0.5, self.width() - 1, self.height() - 1)
        path = QPainterPath()
        path.addRoundedRect(rect, self._radius, self._radius)

        # Fill the background first, then draw the border on top.
        painter.setClipPath(path)
        painter.fillPath(path, self._bg)

        painter.setClipping(False)
        pen = painter.pen()
        pen.setColor(self._border)
        pen.setWidthF(1.0)
        painter.setPen(pen)
        painter.drawPath(path)

        painter.end()
