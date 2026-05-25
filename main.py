# Retune — desktop audio player with real-time pitch and tempo control.
# Copyright (c) 2026 Christopher_VN
# Licensed under the MIT License.
#
# main.py
# -------
# Application entry point.
# Loads user config, shows the language picker on first run, then opens
# the main window. Pass --dev to enable the in-app memory monitor.

import sys
from PySide6.QtWidgets import QApplication

import config
from ui.language_dialog import LanguageDialog
from ui.main_window import MainWindow


def main():
    dev_mode = "--dev" in sys.argv

    app = QApplication(sys.argv)
    app.setApplicationName("Retune")
    app.setApplicationVersion("0.1.0")

    # Load persisted settings from ~/.config/retune/config.json
    cfg = config.load()

    # On first run there is no language preference — ask the user.
    if config.is_first_run(cfg):
        dialog = LanguageDialog()
        if dialog.exec() == dialog.DialogCode.Accepted:
            cfg["language"] = dialog.selected_language()
        else:
            cfg["language"] = "en"
        config.save(cfg)

    lang = cfg.get("language", "en")

    window = MainWindow(dev_mode=dev_mode, lang=lang)
    window.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
