# Retune

A desktop audio player focused on real-time pitch and tempo control.
Built for musicians, transcribers, and anyone who works with audio on a computer.

---

Десктопный аудиоплеер с управлением тональностью и темпом в реальном времени.
Создан для музыкантов, транскрибировщиков и всех, кто работает с аудио за компьютером.

---

## Features / Возможности

- Real-time pitch shifting (semitone precision, -12 to +12)
- Real-time tempo control (25% to 200%, without affecting pitch)
- Waveform display with seekbar
- Track reverse
- Export processed track to WAV or MP3
- Loop playback
- Drag and drop support
- Light/dark adaptive theme

---

- Изменение тональности в реальном времени (точность до полутона, от -12 до +12)
- Управление темпом в реальном времени (от 25% до 200%, без изменения тональности)
- Отображение волновой формы с перемоткой
- Реверс трека
- Экспорт обработанного трека в WAV или MP3
- Повтор воспроизведения
- Поддержка drag and drop
- Адаптивная тема оформления

---

## Requirements / Зависимости

- Python 3.10+
- CMake 3.15+
- GCC / Clang with C++17 support
- libsndfile
- PortAudio
- librubberband
- libmp3lame

Install Python dependencies / Установка Python зависимостей:

```
pip install -r requirements.txt
```

Install system libraries on Debian/Ubuntu:

```
sudo apt install libsndfile1-dev portaudio19-dev librubberband-dev libmp3lame-dev
```

On Arch Linux:

```
sudo pacman -S libsndfile portaudio rubberband lame
```

---

## Building / Сборка

```
mkdir -p audio/engine/build
cd audio/engine/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ../../..
```

---

## Running / Запуск

```
python main.py
```

Developer mode with memory monitor:

```
python main.py --dev
```

---

## Supported formats / Поддерживаемые форматы

MP3, WAV, FLAC, OGG, AAC

---

## Project structure / Структура проекта

```
retune/
├── main.py               # Entry point
├── config.py             # User config (~/.config/retune/config.json)
├── i18n.py               # Localization strings (English, Russian)
├── requirements.txt
├── audio/
│   ├── player.py         # Python wrapper for the audio engine
│   ├── processor.py      # Audio processing utilities
│   └── engine/           # C++ audio engine (pybind11)
│       ├── player.cpp
│       ├── player.hpp
│       ├── engine.cpp
│       └── CMakeLists.txt
└── ui/
    ├── main_window.py    # Main application window
    ├── language_dialog.py
    └── widgets/
        ├── waveform.py   # Waveform display widget
        ├── card.py       # Rounded card widget
        └── dev_monitor.py
```

---

## TODO

- Playlist / track queue
- Loop points and markers (A-B repeat)
- Keyboard shortcuts
- Recent files list
- Language switching from settings without restart
- Application icon
- Distribution packaging (AppImage for Linux, installer for Windows)

## License / Лицензия

MIT License

Copyright (c) 2026 Christopher_VN

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
