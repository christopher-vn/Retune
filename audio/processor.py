import numpy as np
import pyrubberband as pyrb


class AudioProcessor:
    """
    Отдельный модуль обработки аудио.
    Сейчас используется напрямую из player.py,
    в будущем сюда вынесем тайм-стретчинг, эквалайзер и т.д.
    """

    @staticmethod
    def pitch_shift(audio: np.ndarray, sr: int, semitones: int) -> np.ndarray:
        if semitones == 0:
            return audio
        return pyrb.pitch_shift(audio, sr, semitones)

    @staticmethod
    def time_stretch(audio: np.ndarray, sr: int, rate: float) -> np.ndarray:
        if rate == 1.0:
            return audio
        return pyrb.time_stretch(audio, sr, rate)
