# Retune — desktop audio player with real-time pitch and tempo control.
# Copyright (c) 2026 Christopher_VN
# Licensed under the MIT License.
#
# audio/processor.py
# ------------------
# Utility module for offline audio processing.
# Currently not used by the main playback pipeline — the C++ engine handles
# all real-time pitch shifting and time stretching via librubberband.
# This module is kept as a convenience for future features such as
# batch processing, equalisation, or audio analysis.

import numpy as np
import pyrubberband as pyrb


class AudioProcessor:

    @staticmethod
    def pitch_shift(audio: np.ndarray, sr: int, semitones: int) -> np.ndarray:
        """Shift pitch by the given number of semitones. No-op if zero."""
        if semitones == 0:
            return audio
        return pyrb.pitch_shift(audio, sr, semitones)

    @staticmethod
    def time_stretch(audio: np.ndarray, sr: int, rate: float) -> np.ndarray:
        """Stretch audio by rate without affecting pitch. No-op if rate is 1.0."""
        if rate == 1.0:
            return audio
        return pyrb.time_stretch(audio, sr, rate)
