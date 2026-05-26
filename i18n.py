# Copyright (c) 2026 Christopher_VN
# Licensed under the MIT License.
#
# i18n.py
# -------
# Localization strings for all UI text.
# To add a new language, add a new key to STRINGS with the same set of keys
# as the existing entries and pass the language code to MainWindow.

STRINGS = {
    "en": {
        # Drop zone
        "drop_zone_default": "Drop an audio file here or click to browse",
        "drop_zone_loading": "Loading...",
        "drop_zone_loaded":  "+ Open another file",

        # Track card
        "no_file": "No file loaded",

        # Transport
        "play":  "Play",
        "pause": "Pause",

        # Pitch card
        "pitch":          "Pitch",
        "semitones_fmt":  "{sign}{n}",
        "note_fmt":       "C -> {note}",

        # Tempo card
        "tempo":           "Tempo",
        "tempo_ratio_fmt": "x {ratio:.2f}",

        # Context menu (three-dot button)
        "menu_reverse":     "Reverse",
        "menu_unreverse":   "Undo reverse",
        "menu_save_as":     "Save as...",
        "menu_dev_monitor": "Dev monitor",

        # Save dialog
        "save_dialog_title":  "Save as",
        "save_dialog_filter": "MP3 (*.mp3);;WAV (*.wav)",
        "save_in_progress":   "Saving track...",
        "save_error":         "Could not save file.",

        # Language picker dialog
        "lang_dialog_title": "Language",
        "lang_dialog_label": "Choose interface language:",
        "lang_confirm":      "Continue",
    },
    "ru": {
        # Drop zone
        "drop_zone_default": "Перетащите аудиофайл сюда или нажмите для выбора",
        "drop_zone_loading": "Загрузка...",
        "drop_zone_loaded":  "+ Открыть другой файл",

        # Track card
        "no_file": "Файл не загружен",

        # Transport
        "play":  "Воспроизвести",
        "pause": "Пауза",

        # Pitch card
        "pitch":          "Тональность",
        "semitones_fmt":  "{sign}{n}",
        "note_fmt":       "C -> {note}",

        # Tempo card
        "tempo":           "Темп",
        "tempo_ratio_fmt": "x {ratio:.2f}",

        # Context menu
        "menu_reverse":     "Реверс",
        "menu_unreverse":   "Отменить реверс",
        "menu_save_as":     "Сохранить как...",
        "menu_dev_monitor": "Dev monitor",

        # Save dialog
        "save_dialog_title":  "Сохранить как",
        "save_dialog_filter": "MP3 (*.mp3);;WAV (*.wav)",
        "save_in_progress":   "Сохранение трека...",
        "save_error":         "Не удалось сохранить файл.",

        # Language picker dialog
        "lang_dialog_title": "Язык",
        "lang_dialog_label": "Выберите язык интерфейса:",
        "lang_confirm":      "Продолжить",
    }
}


def get(lang: str, key: str, **kwargs) -> str:
    """
    Return the localized string for the given language code and key.
    Falls back to English if the language or key is not found.
    Format arguments are applied via str.format if provided.
    """
    s = STRINGS.get(lang, STRINGS["en"]).get(key, key)
    return s.format(**kwargs) if kwargs else s
