# Retune — desktop audio player with real-time pitch and tempo control.
# Copyright (c) 2026 Christopher_VN
# Licensed under the MIT License.
#
# ui/language_dialog.py
# ---------------------
# Modal dialog shown on first launch to let the user choose the interface
# language. The choice is persisted by the caller via config.save().
# The dialog cannot be dismissed without making a selection.

from PySide6.QtWidgets import (
    QDialog, QVBoxLayout, QLabel,
    QPushButton, QButtonGroup, QRadioButton
)
from PySide6.QtCore import Qt


class LanguageDialog(QDialog):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Retune — Language / Язык")
        self.setFixedSize(320, 200)
        # Remove the close button so the user must make a choice.
        self.setWindowFlags(
            Qt.WindowType.Dialog |
            Qt.WindowType.CustomizeWindowHint |
            Qt.WindowType.WindowTitleHint
        )
        self._selected = "en"
        self._build_ui()
        self._apply_styles()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(28, 24, 28, 24)
        layout.setSpacing(16)

        label = QLabel("Choose interface language / Выберите язык интерфейса")
        label.setObjectName("langLabel")
        label.setWordWrap(True)
        layout.addWidget(label)

        self._btn_group = QButtonGroup(self)

        self._rb_en = QRadioButton("English")
        self._rb_ru = QRadioButton("Русский")
        self._rb_en.setChecked(True)

        self._btn_group.addButton(self._rb_en)
        self._btn_group.addButton(self._rb_ru)

        layout.addWidget(self._rb_en)
        layout.addWidget(self._rb_ru)
        layout.addStretch()

        btn = QPushButton("Continue / Продолжить")
        btn.setObjectName("btnConfirm")
        btn.clicked.connect(self._confirm)
        layout.addWidget(btn)

    def _apply_styles(self):
        self.setStyleSheet("""
            QDialog, QWidget {
                background-color: #1e1e2e;
                color: #cdd6f4;
                font-family: sans-serif;
                font-size: 13px;
            }
            QLabel {
                background-color: transparent;
                border: none;
            }
            #langLabel {
                color: #a6adc8;
                font-size: 12px;
            }
            QRadioButton {
                font-size: 14px;
                color: #cdd6f4;
                spacing: 8px;
            }
            QRadioButton::indicator {
                width: 16px;
                height: 16px;
                border-radius: 8px;
                border: 1px solid #45475a;
                background: #313244;
            }
            QRadioButton::indicator:checked {
                background: #89b4fa;
                border-color: #89b4fa;
            }
            #btnConfirm {
                background-color: #313244;
                color: #cdd6f4;
                border: 1px solid #45475a;
                border-radius: 8px;
                padding: 8px;
                font-size: 13px;
            }
            #btnConfirm:hover { background-color: #45475a; }
        """)

    def _confirm(self):
        """Store the selected language code and close the dialog."""
        self._selected = "ru" if self._rb_ru.isChecked() else "en"
        self.accept()

    def selected_language(self) -> str:
        """Return the language code chosen by the user ("en" or "ru")."""
        return self._selected
