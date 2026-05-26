# Retune — desktop audio player with real-time pitch and tempo control.
# Copyright (c) 2026 Christopher_VN
# Licensed under the MIT License.
#
# audio/player.py
# ---------------
# Thin Python wrapper around the C++ AudioEngine (retune_engine.so).
# Exposes Qt signals so the UI can react to playback events without polling.
# All heavy lifting (decoding, stretching, output) happens inside the engine.

import sys, os
# Ensure the compiled .so file is found when running from the project root.
sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

from PySide6.QtCore import QObject, Signal

try:
    import retune_engine
    _Engine = retune_engine.AudioEngine
except ImportError:
    _Engine = None


class AudioPlayer(QObject):
    # Emitted when playback reaches the end of the file naturally.
    playback_finished = Signal()
    # Emitted when a file could not be loaded.
    load_error        = Signal(str)

    def __init__(self):
        super().__init__()
        if _Engine is None:
            raise RuntimeError(
                "retune_engine not found. "
                "Build the C++ module: cd audio/engine/build && cmake .. && make"
            )
        self._engine = _Engine()
        # Register the finished callback so the Qt signal is emitted from
        # within the PortAudio thread via a queued connection.
        self._engine.set_finished_callback(self._on_finished)

    def load(self, path: str) -> bool:
        """Load an audio file. Returns True on success."""
        ok = self._engine.load(path)
        if not ok:
            self.load_error.emit(f"Failed to load: {path}")
        return ok

    def play(self):
        """Start or resume playback."""
        self._engine.play()

    def pause(self):
        """Pause playback, preserving the current position."""
        self._engine.pause()

    def stop(self):
        """Stop playback and reset position to zero."""
        self._engine.stop()

    def set_pitch(self, semitones: int):
        """Shift pitch by the given number of semitones (-12 to +12)."""
        self._engine.set_pitch(float(semitones))

    def set_tempo(self, percent: int):
        """Set playback speed as a percentage of original (25–200)."""
        self._engine.set_tempo(float(percent))

    def seek(self, seconds: float):
        """Jump to a position given in seconds."""
        self._engine.seek(seconds)

    def reverse(self):
        """Reverse the audio buffer in place. Toggles on repeated calls."""
        self._engine.reverse()

    def is_reversed(self) -> bool:
        """Return True if the buffer is currently reversed."""
        return self._engine.is_reversed()

    def is_playing(self) -> bool:
        """Return True if audio is currently playing."""
        return self._engine.is_playing()

    def get_position(self) -> float:
        """Return current playback position in seconds."""
        return self._engine.get_position()

    def get_duration(self) -> float:
        """Return total duration of the loaded file in seconds."""
        return self._engine.get_duration()

    def get_waveform(self, n_points: int) -> list[float]:
        """Return n_points normalized peak amplitudes for waveform display."""
        return self._engine.get_waveform(n_points)

    def export(self, path: str, progress_cb=None) -> bool:
        """
        Export the processed audio to WAV or MP3 depending on the file extension.
        progress_cb is called with a float in [0, 1] during export.
        """
        try:
            if path.endswith(".mp3"):
                return self._engine.export_mp3(path, 2, progress_cb)
            return self._engine.export_wav(path, progress_cb)
        except Exception as e:
            print(f"[AudioPlayer] export error: {e}", flush=True)
            return False

    def _on_finished(self):
        """Called from the PortAudio thread when the buffer is exhausted."""
        self.playback_finished.emit()
