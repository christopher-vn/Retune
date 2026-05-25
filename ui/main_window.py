# Retune — desktop audio player with real-time pitch and tempo control.
# Copyright (c) 2026 Christopher_VN
# Licensed under the MIT License.
#
# ui/main_window.py
# -----------------
# Main application window.
# Owns the AudioPlayer instance and wires all UI controls to it.
# File loading and export run in background QThreads to keep the UI responsive.
# All user-facing strings are looked up via i18n.get() for localization.

from PySide6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QSlider, QFileDialog, QSizePolicy, QMenu
)
from PySide6.QtCore import Qt, QThread, Signal, QObject, QTimer
from PySide6.QtGui import QDragEnterEvent, QDropEvent

from audio.player import AudioPlayer
from ui.widgets.waveform import WaveformWidget
from ui.widgets.card import Card
from ui.widgets.dev_monitor import DevMonitor
from i18n import get as tr


# ---------------------------------------------------------------------------
# Background workers
# ---------------------------------------------------------------------------

class LoadWorker(QObject):
    """Loads an audio file in a background thread to avoid blocking the UI."""
    finished = Signal(bool)

    def __init__(self, player: AudioPlayer, path: str):
        super().__init__()
        self._player = player
        self._path   = path

    def run(self):
        ok = self._player.load(self._path)
        self.finished.emit(ok)


class ExportWorker(QObject):
    """Renders and writes the processed audio to disk in a background thread."""
    finished = Signal(bool)
    progress = Signal(float)  # Value in [0, 1]

    def __init__(self, player: AudioPlayer, path: str):
        super().__init__()
        self._player = player
        self._path   = path

    def run(self):
        ok = self._player.export(self._path, self._on_progress)
        self.finished.emit(ok)

    def _on_progress(self, value: float):
        self.progress.emit(value)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def format_time(seconds: float) -> str:
    """Format a duration in seconds as M:SS."""
    s = int(seconds)
    return f"{s // 60}:{s % 60:02d}"


# ---------------------------------------------------------------------------
# Main window
# ---------------------------------------------------------------------------

class MainWindow(QMainWindow):
    def __init__(self, dev_mode: bool = False, lang: str = "en"):
        super().__init__()
        self._dev_mode = dev_mode
        self._lang     = lang
        self._dev_monitor: DevMonitor | None = None

        # Audio backend
        self.player = AudioPlayer()
        self.player.playback_finished.connect(self._on_playback_finished)
        self.player.load_error.connect(self._on_load_error)

        # State
        self._current_path  = ""
        self._duration      = 0.0
        self._load_thread:  QThread | None = None
        self._track_loaded  = False
        self._repeat        = False

        # Timer that polls playback position at 10 Hz to update the waveform
        # cursor and the elapsed time label.
        self._progress_timer = QTimer()
        self._progress_timer.setInterval(100)
        self._progress_timer.timeout.connect(self._update_progress)

        self.setWindowTitle("Retune")
        self.setMinimumSize(560, 460)
        self.resize(580, 480)
        self.setAcceptDrops(True)

        self._build_ui()
        self._apply_styles()

    def t(self, key: str, **kwargs) -> str:
        """Return a localized string for the current language."""
        return tr(self._lang, key, **kwargs)

    # -----------------------------------------------------------------------
    # UI construction
    # -----------------------------------------------------------------------

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(20, 20, 20, 20)
        root.setSpacing(8)
        root.setAlignment(Qt.AlignmentFlag.AlignTop)

        # --- Drop zone ---
        # Accepts clicks and drag-and-drop. Shrinks to a single line after the
        # first file is loaded to give more space to the track card.
        self.drop_zone = QLabel(self.t("drop_zone_default"))
        self.drop_zone.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.drop_zone.setObjectName("dropZone")
        self.drop_zone.setFixedHeight(56)
        self.drop_zone.setCursor(Qt.CursorShape.PointingHandCursor)
        self.drop_zone.mousePressEvent = lambda _: self._open_file_dialog()
        root.addWidget(self.drop_zone)

        # --- Track card ---
        track_card = Card()
        track_layout = QVBoxLayout(track_card)
        track_layout.setContentsMargins(16, 14, 16, 14)
        track_layout.setSpacing(10)

        # Header row: music icon | track name + duration | menu button
        track_header = QHBoxLayout()
        track_header.setSpacing(10)

        self.track_icon = QLabel("♪")
        self.track_icon.setObjectName("trackIcon")
        self.track_icon.setFixedSize(32, 32)
        self.track_icon.setAlignment(Qt.AlignmentFlag.AlignCenter)
        track_header.addWidget(self.track_icon)

        track_meta = QVBoxLayout()
        track_meta.setSpacing(1)

        self.track_label = QLabel(self.t("no_file"))
        self.track_label.setObjectName("trackName")
        self.track_label.setMaximumWidth(380)
        self.track_label.setWordWrap(False)
        self.track_label.setTextFormat(Qt.TextFormat.PlainText)
        track_meta.addWidget(self.track_label)

        self.track_duration = QLabel("")
        self.track_duration.setObjectName("trackDuration")
        track_meta.addWidget(self.track_duration)

        track_header.addLayout(track_meta)
        track_header.addStretch()

        self.btn_menu = QPushButton("⋯")
        self.btn_menu.setObjectName("btnMenu")
        self.btn_menu.setFixedSize(32, 32)
        self.btn_menu.clicked.connect(self._show_track_menu)
        track_header.addWidget(self.btn_menu)

        track_layout.addLayout(track_header)

        # Waveform display — also handles seek on click/drag
        self.waveform = WaveformWidget()
        self.waveform.seek_requested.connect(self._on_seek)
        track_layout.addWidget(self.waveform)
        track_layout.addSpacing(4)

        # Elapsed / total time labels
        time_row = QHBoxLayout()
        self.time_current = QLabel("0:00")
        self.time_current.setObjectName("timeLabel")
        self.time_total = QLabel("0:00")
        self.time_total.setObjectName("timeLabel")
        time_row.addWidget(self.time_current)
        time_row.addStretch()
        time_row.addWidget(self.time_total)
        track_layout.addLayout(time_row)

        # Transport controls: play/pause (wide), stop, repeat
        transport = QHBoxLayout()
        transport.setSpacing(10)

        self.btn_play = QPushButton("▶  " + self.t("play"))
        self.btn_play.setObjectName("btnPlay")
        self.btn_play.setEnabled(False)
        self.btn_play.clicked.connect(self._toggle_playback)
        self.btn_play.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed
        )
        transport.addWidget(self.btn_play, stretch=3)

        self.btn_stop = QPushButton("⏹")
        self.btn_stop.setObjectName("btnStop")
        self.btn_stop.setEnabled(False)
        self.btn_stop.clicked.connect(self._stop_playback)
        self.btn_stop.setFixedSize(44, 34)
        transport.addWidget(self.btn_stop, stretch=0)

        self.btn_repeat = QPushButton("↺")
        self.btn_repeat.setObjectName("btnRepeat")
        self.btn_repeat.setCheckable(True)
        self.btn_repeat.setFixedSize(44, 34)
        self.btn_repeat.clicked.connect(self._on_repeat_toggled)
        transport.addWidget(self.btn_repeat, stretch=0)

        track_layout.addLayout(transport)
        root.addWidget(track_card)

        # --- Controls row: pitch (left) and tempo (right) ---
        controls_row = QHBoxLayout()
        controls_row.setSpacing(10)

        # Pitch card (-12 to +12 semitones)
        pitch_card = Card()
        pitch_layout = QVBoxLayout(pitch_card)
        pitch_layout.setContentsMargins(16, 16, 16, 16)
        pitch_layout.setSpacing(10)

        pitch_header = QHBoxLayout()
        pitch_title = QLabel(self.t("pitch"))
        pitch_title.setObjectName("cardTitle")
        self.pitch_value_label = QLabel("0")
        self.pitch_value_label.setObjectName("pitchBadge")
        self.pitch_value_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.pitch_value_label.setFixedSize(48, 26)
        pitch_header.addWidget(pitch_title)
        pitch_header.addStretch()
        pitch_header.addWidget(self.pitch_value_label)
        pitch_layout.addLayout(pitch_header)

        self.pitch_slider = QSlider(Qt.Orientation.Horizontal)
        self.pitch_slider.setMinimum(-12)
        self.pitch_slider.setMaximum(12)
        self.pitch_slider.setValue(0)
        self.pitch_slider.setTickPosition(QSlider.TickPosition.TicksBelow)
        self.pitch_slider.setTickInterval(1)
        self.pitch_slider.setEnabled(False)
        self.pitch_slider.valueChanged.connect(self._on_pitch_changed)
        pitch_layout.addWidget(self.pitch_slider)

        self.pitch_hint = QLabel("C → C")
        self.pitch_hint.setObjectName("cardHint")
        self.pitch_hint.setAlignment(Qt.AlignmentFlag.AlignCenter)
        pitch_layout.addWidget(self.pitch_hint)

        controls_row.addWidget(pitch_card)

        # Tempo card (25% to 200%)
        tempo_card = Card()
        tempo_layout = QVBoxLayout(tempo_card)
        tempo_layout.setContentsMargins(16, 16, 16, 16)
        tempo_layout.setSpacing(10)

        tempo_header = QHBoxLayout()
        tempo_title = QLabel(self.t("tempo"))
        tempo_title.setObjectName("cardTitle")
        self.tempo_value_label = QLabel("100%")
        self.tempo_value_label.setObjectName("pitchBadge")
        self.tempo_value_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.tempo_value_label.setFixedSize(56, 26)
        tempo_header.addWidget(tempo_title)
        tempo_header.addStretch()
        tempo_header.addWidget(self.tempo_value_label)
        tempo_layout.addLayout(tempo_header)

        self.tempo_slider = QSlider(Qt.Orientation.Horizontal)
        self.tempo_slider.setMinimum(25)
        self.tempo_slider.setMaximum(200)
        self.tempo_slider.setValue(100)
        self.tempo_slider.setTickPosition(QSlider.TickPosition.TicksBelow)
        self.tempo_slider.setTickInterval(25)
        self.tempo_slider.setEnabled(False)
        self.tempo_slider.valueChanged.connect(self._on_tempo_changed)
        tempo_layout.addWidget(self.tempo_slider)

        self.tempo_hint = QLabel("× 1.00")
        self.tempo_hint.setObjectName("cardHint")
        self.tempo_hint.setAlignment(Qt.AlignmentFlag.AlignCenter)
        tempo_layout.addWidget(self.tempo_hint)

        controls_row.addWidget(tempo_card)
        root.addLayout(controls_row)

        # Dev monitor — only visible in --dev mode
        if self._dev_mode:
            self._dev_monitor = DevMonitor()
            root.addWidget(self._dev_monitor)

    def _apply_styles(self):
        self.setStyleSheet("""
            QMainWindow, QWidget {
                background-color: #24273a;
                color: #cdd6f4;
                font-family: sans-serif;
                font-size: 13px;
            }
            QLabel {
                background-color: transparent;
                border: none;
                outline: none;
            }
            #dropZone {
                border: 1px dashed #45475a;
                border-radius: 10px;
                color: #6c7086;
                padding: 12px 16px;
                font-size: 13px;
            }
            #dropZone:hover {
                border-color: #89b4fa;
                color: #89b4fa;
            }
            #trackIcon {
                background-color: #1e2a3a;
                border: 1px solid #2a3f5f;
                border-radius: 12px;
                color: #89b4fa;
                font-size: 16px;
            }
            #trackName     { color: #cdd6f4; font-size: 13px; font-weight: bold; }
            #trackDuration { color: #6c7086; font-size: 11px; }
            #timeLabel     { color: #6c7086; font-size: 11px; }
            #cardTitle {
                color: #a6adc8;
                font-size: 12px;
                background-color: #2a2a3d;
                border: 1px solid #3a3a52;
                border-radius: 10px;
                padding: 3px 10px;
            }
            #pitchBadge {
                color: #89b4fa;
                font-size: 15px;
                font-weight: bold;
                background-color: #1e2a3a;
                border: 1px solid #2a3f5f;
                border-radius: 8px;
            }
            #cardHint { color: #6c7086; font-size: 11px; }
            #btnPlay {
                background-color: #313244;
                color: #cdd6f4;
                border: 1px solid #45475a;
                border-radius: 8px;
                padding: 6px 16px;
                font-size: 13px;
            }
            #btnPlay:hover    { background-color: #45475a; }
            #btnPlay:disabled { color: #45475a; border-color: #313244; }
            #btnStop {
                background-color: #2a1f2e;
                color: #f38ba8;
                border: 1px solid #3d2535;
                border-radius: 8px;
                font-size: 13px;
            }
            #btnStop:hover    { background-color: #3d2535; }
            #btnStop:disabled {
                color: #45475a;
                border-color: #313244;
                background-color: #313244;
            }
            #btnRepeat {
                background-color: #2a2a3d;
                color: #6c7086;
                border: 1px solid #3a3a52;
                border-radius: 8px;
                font-size: 18px;
                padding: 0px;
            }
            #btnRepeat:hover   { background-color: #3a3a52; color: #a6adc8; }
            #btnRepeat:checked {
                background-color: #1e2a3a;
                color: #89b4fa;
                border-color: #2a3f5f;
            }
            #btnMenu {
                background-color: #2a2a3d;
                color: #a6adc8;
                border: 1px solid #3a3a52;
                border-radius: 10px;
                font-size: 16px;
                padding: 0px;
            }
            #btnMenu:hover { background-color: #3a3a52; }
            QMenu {
                background-color: #1e1e2e;
                border: 1px solid #313244;
                border-radius: 8px;
                padding: 4px;
            }
            QMenu::item {
                padding: 7px 20px;
                border-radius: 6px;
                color: #cdd6f4;
                font-size: 13px;
            }
            QMenu::item:selected { background-color: #313244; }
            QSlider { background-color: transparent; }
            QSlider::groove:horizontal {
                height: 4px;
                background: #313244;
                border-radius: 2px;
            }
            QSlider::sub-page:horizontal {
                background: #89b4fa;
                border-radius: 2px;
            }
            QSlider::handle:horizontal {
                width: 16px; height: 16px;
                margin: -6px 0;
                background: #89b4fa;
                border-radius: 8px;
            }
            QSlider::handle:horizontal:disabled { background: #45475a; }
            QSlider::tick-mark:horizontal       { background: #45475a; }
            #devHeader      { color: #6c7086; font-size: 11px; font-family: monospace; }
            #devMetricTitle { color: #6c7086; font-size: 10px; font-family: monospace; }
            #devMetricValue {
                color: #a6e3a1;
                font-size: 13px;
                font-weight: bold;
                font-family: monospace;
            }
        """)

    # -----------------------------------------------------------------------
    # Drag and drop
    # -----------------------------------------------------------------------

    def dragEnterEvent(self, event: QDragEnterEvent):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dropEvent(self, event: QDropEvent):
        urls = event.mimeData().urls()
        if urls:
            self._load_file(urls[0].toLocalFile())

    # -----------------------------------------------------------------------
    # File loading
    # -----------------------------------------------------------------------

    def _open_file_dialog(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Open audio file", "",
            "Audio (*.mp3 *.wav *.flac *.ogg *.aac)"
        )
        if path:
            self._load_file(path)

    def _load_file(self, path: str):
        """Kick off a background load and clear the current waveform."""
        self._progress_timer.stop()
        self._current_path = path
        self._set_controls_enabled(False)
        self.drop_zone.setText(self.t("drop_zone_loading"))
        self.waveform.clear()

        self._load_thread  = QThread()
        self._load_worker  = LoadWorker(self.player, path)
        self._load_worker.moveToThread(self._load_thread)
        self._load_thread.started.connect(self._load_worker.run)
        self._load_worker.finished.connect(self._on_load_finished)
        self._load_worker.finished.connect(self._load_thread.quit)
        self._load_thread.finished.connect(self._load_thread.deleteLater)
        self._load_thread.start()

    def _on_load_finished(self, ok: bool):
        if ok:
            # Truncate very long filenames to keep the UI tidy.
            name = self._current_path.split("/")[-1]
            if len(name) > 50:
                name = name[:47] + "..."
            self.track_label.setText(name)

            self._duration = self.player.get_duration()
            self.track_duration.setText(format_time(self._duration))
            self.time_total.setText(format_time(self._duration))
            self.time_current.setText("0:00")

            # Reset controls to their default values for the new file.
            self.pitch_slider.setValue(0)
            self.tempo_slider.setValue(100)
            self._set_controls_enabled(True)

            # Build the waveform thumbnail from 200 peak amplitude points.
            peaks = self.player.get_waveform(200)
            self.waveform.set_waveform(peaks)

            if not self._track_loaded:
                self._track_loaded = True
            self.drop_zone.setText(self.t("drop_zone_loaded"))
        else:
            self.drop_zone.setText(self.t("drop_zone_default"))

    def _on_load_error(self, msg: str):
        self.drop_zone.setText(self.t("drop_zone_default"))

    def _set_controls_enabled(self, enabled: bool):
        """Enable or disable all playback-dependent controls at once."""
        self.btn_play.setEnabled(enabled)
        self.btn_stop.setEnabled(enabled)
        self.pitch_slider.setEnabled(enabled)
        self.tempo_slider.setEnabled(enabled)

    # -----------------------------------------------------------------------
    # Playback
    # -----------------------------------------------------------------------

    def _toggle_playback(self):
        if self.player.is_playing():
            self.player.pause()
            self._progress_timer.stop()
            self.btn_play.setText("▶  " + self.t("play"))
        else:
            self.player.play()
            self._progress_timer.start()
            self.btn_play.setText("⏸  " + self.t("pause"))

    def _stop_playback(self):
        self.player.stop()
        self._progress_timer.stop()
        self.waveform.set_progress(0.0)
        self.time_current.setText("0:00")
        self.btn_play.setText("▶  " + self.t("play"))

    def _on_playback_finished(self):
        """Called when the engine signals end-of-file."""
        if self._repeat:
            # Restart immediately without resetting UI state.
            self.player.play()
        else:
            self._progress_timer.stop()
            self.waveform.set_progress(1.0)
            self.btn_play.setText("▶  " + self.t("play"))

    def _on_repeat_toggled(self, checked: bool):
        self._repeat = checked

    def _on_seek(self, ratio: float):
        """Handle a seek request from the waveform widget."""
        if self._duration <= 0:
            return
        seconds = ratio * self._duration
        self.player.seek(seconds)
        self.time_current.setText(format_time(seconds))

    def _update_progress(self):
        """Poll the engine for the current position and refresh the display."""
        if self._duration <= 0:
            return
        pos = self.player.get_position()
        self.time_current.setText(format_time(pos))
        self.waveform.set_progress(pos / self._duration)

    # -----------------------------------------------------------------------
    # Pitch and tempo
    # -----------------------------------------------------------------------

    def _on_pitch_changed(self, value: int):
        sign  = "+" if value > 0 else ""
        label = "0" if value == 0 else f"{sign}{value}"
        self.pitch_value_label.setText(label)
        notes = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]
        self.pitch_hint.setText(f"C → {notes[(value % 12 + 12) % 12]}")
        self.player.set_pitch(value)

    def _on_tempo_changed(self, value: int):
        self.tempo_value_label.setText(f"{value}%")
        self.tempo_hint.setText(f"× {value / 100:.2f}")
        self.player.set_tempo(value)

    # -----------------------------------------------------------------------
    # Context menu (three-dot button)
    # -----------------------------------------------------------------------

    def _show_track_menu(self):
        menu = QMenu(self)

        # Reverse action — label changes based on current state
        is_rev = self.player.is_reversed() if self._track_loaded else False
        label  = "⇄  " + (self.t("menu_unreverse") if is_rev else self.t("menu_reverse"))
        action_reverse = menu.addAction(label)
        action_reverse.setEnabled(self._track_loaded)

        action_save = menu.addAction("💾  " + self.t("menu_save_as"))
        action_save.setEnabled(self._track_loaded)

        if self._dev_mode:
            menu.addSeparator()
            action_dev = menu.addAction("⚙  " + self.t("menu_dev_monitor"))
            action_dev.setCheckable(True)
            action_dev.setChecked(
                self._dev_monitor is not None and self._dev_monitor.isVisible()
            )

        action = menu.exec(self.btn_menu.mapToGlobal(
            self.btn_menu.rect().bottomLeft()
        ))

        if action == action_reverse:
            self._toggle_reverse()
        elif action == action_save:
            self._save_file()
        elif (self._dev_mode and action is not None
              and action.text() == "⚙  " + self.t("menu_dev_monitor")):
            if self._dev_monitor:
                self._dev_monitor.setVisible(not self._dev_monitor.isVisible())
                self.adjustSize()

    # -----------------------------------------------------------------------
    # Export
    # -----------------------------------------------------------------------

    def _save_file(self):
        from PySide6.QtWidgets import QProgressDialog
        path, _ = QFileDialog.getSaveFileName(
            self, self.t("save_dialog_title"), "",
            "MP3 (*.mp3);;WAV (*.wav)"
        )
        if not path:
            return

        # Ensure correct extension based on selected filter.
        if not path.endswith(".mp3") and not path.endswith(".wav"):
            path += ".mp3"

        was_playing = self.player.is_playing()
        if was_playing:
            self.player.pause()

        # Modal progress dialog — no cancel button (None as cancel text)
        self._progress_dialog = QProgressDialog(
            self.t("save_in_progress"), None, 0, 100, self
        )
        self._progress_dialog.setWindowTitle("Retune")
        self._progress_dialog.setWindowModality(Qt.WindowModality.WindowModal)
        self._progress_dialog.setMinimumDuration(0)
        self._progress_dialog.setAutoClose(True)
        self._progress_dialog.setValue(0)

        worker_thread = QThread()
        worker = ExportWorker(self.player, path)
        worker.moveToThread(worker_thread)
        worker_thread.started.connect(worker.run)
        worker.progress.connect(
            lambda v: self._progress_dialog.setValue(int(v * 100))
        )
        worker.finished.connect(self._on_export_finished)
        worker.finished.connect(worker_thread.quit)
        worker_thread.finished.connect(worker_thread.deleteLater)

        self._export_was_playing = was_playing
        self._export_thread = worker_thread
        self._export_worker = worker
        worker_thread.start()

    def _on_export_finished(self, ok: bool):
        self._set_controls_enabled(True)
        self.drop_zone.setText(self.t("drop_zone_loaded"))
        if not ok:
            from PySide6.QtWidgets import QMessageBox
            QMessageBox.warning(self, "Retune", self.t("save_error"))
        # Resume playback if it was interrupted by the export
        if getattr(self, "_export_was_playing", False):
            self.player.play()
            self._progress_timer.start()
            self.btn_play.setText("⏸  " + self.t("pause"))

    # -----------------------------------------------------------------------
    # Reverse
    # -----------------------------------------------------------------------

    def _toggle_reverse(self):
        self.player.reverse()
        # Refresh the waveform thumbnail since the data order changed.
        peaks = self.player.get_waveform(200)
        self.waveform.set_waveform(peaks)

    # -----------------------------------------------------------------------
    # Window lifecycle
    # -----------------------------------------------------------------------

    def closeEvent(self, event):
        self._progress_timer.stop()
        if self._dev_monitor:
            self._dev_monitor.stop()
        self.player.stop()
        event.accept()
