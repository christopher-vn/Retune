# Retune — desktop audio player with real-time pitch and tempo control.
# Copyright (c) 2026 Christopher_VN
# Licensed under the MIT License.
#
# config.py
# ---------
# Manages persistent user settings stored at:
#   ~/.config/retune/config.json
#
# Currently stored settings:
#   language  — "en" or "ru", None if not yet chosen (first run)

import json
import os

_CONFIG_DIR  = os.path.join(os.path.expanduser("~"), ".config", "retune")
_CONFIG_FILE = os.path.join(_CONFIG_DIR, "config.json")

_DEFAULTS = {
    "language": None,  # None signals first-run; dialog will ask the user.
}


def load() -> dict:
    """Load config from disk, falling back to defaults for missing keys."""
    if not os.path.exists(_CONFIG_FILE):
        return dict(_DEFAULTS)
    try:
        with open(_CONFIG_FILE, "r", encoding="utf-8") as f:
            data = json.load(f)
        for k, v in _DEFAULTS.items():
            data.setdefault(k, v)
        return data
    except Exception:
        return dict(_DEFAULTS)


def save(cfg: dict) -> None:
    """Persist config to disk, creating directories as needed."""
    os.makedirs(_CONFIG_DIR, exist_ok=True)
    with open(_CONFIG_FILE, "w", encoding="utf-8") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=2)


def is_first_run(cfg: dict) -> bool:
    """Return True if the user has not yet chosen a language."""
    return cfg.get("language") is None
