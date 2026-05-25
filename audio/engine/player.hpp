/*
 * Retune — desktop audio player with real-time pitch and tempo control.
 * Copyright (c) 2026 Christopher_VN
 * Licensed under the MIT License.
 *
 * player.hpp
 * ----------
 * Declaration of AudioEngine — the core C++ audio engine.
 * Handles file loading, real-time pitch shifting and time stretching
 * via librubberband, audio output via PortAudio, and WAV export
 * via libsndfile.
 */

#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <functional>

#include <rubberband/RubberBandStretcher.h>
#include <portaudio.h>
#include <sndfile.h>

class AudioEngine {
public:
    // Callback invoked on the PortAudio thread when playback finishes.
    using FinishedCallback = std::function<void()>;

    AudioEngine();
    ~AudioEngine();

    // Load an audio file into memory. Returns false on failure.
    bool   load(const std::string& path);

    // Start playback from the beginning (or resume after pause).
    void   play();

    // Pause playback, preserving the current read position.
    void   pause();

    // Stop playback and reset the read position to zero.
    void   stop();

    // Set pitch shift in semitones. Applied in real time without restart.
    void   set_pitch(double semitones);

    // Set playback tempo as a percentage (100 = normal speed).
    // Applied in real time without restart.
    void   set_tempo(double percent);

    // Seek to a position given in seconds.
    void   seek(double seconds);

    // Reverse the raw audio buffer in place.
    // If playback is active it will be restarted after reversal.
    void   reverse();

    // Return true if the buffer is currently reversed.
    bool   is_reversed() const;

    // Return true if audio is currently playing.
    bool   is_playing() const;

    // Return current playback position in seconds.
    double get_position() const;

    // Return total duration of the loaded file in seconds.
    double get_duration() const;

    // Return sample rate of the loaded file.
    int    get_sample_rate() const;

    // Return a downsampled peak amplitude array for waveform display.
    // n_points controls the resolution; values are normalized to [0, 1].
    std::vector<float> get_waveform(int n_points) const;

    // Export the processed audio (with current pitch/tempo applied) to a WAV file.
    // progress_cb is called periodically with a value in [0, 1]; may be nullptr.
    bool   export_wav(const std::string& path,
                      std::function<void(float)> progress_cb = nullptr);

    // Register a callback to be invoked when playback finishes naturally.
    void   set_finished_callback(FinishedCallback cb);

private:
    // PortAudio stream callback — called from the audio thread.
    static int pa_callback(const void* input, void* output,
                           unsigned long frames,
                           const PaStreamCallbackTimeInfo* time_info,
                           PaStreamCallbackFlags flags,
                           void* user_data);

    // Fill outdata with up to frames_needed interleaved samples.
    // Returns paComplete when the buffer is exhausted.
    int  process(float* output, unsigned long frames_needed);

    // (Re)create the RubberBand stretcher with current pitch/tempo settings.
    void reset_stretcher();

    // Stop and close the PortAudio stream without touching _read_pos.
    void cleanup_stream();

    // --- Audio data ---
    std::vector<float> _raw;       // Interleaved stereo samples (float32)
    int    _channels  = 2;
    int    _sr        = 44100;
    double _duration  = 0.0;       // Seconds

    // Current read position in _raw (in individual samples, not frames).
    std::atomic<long> _read_pos{0};

    // --- Processing parameters ---
    double _semitones     = 0.0;   // Pitch shift in semitones
    double _tempo_percent = 100.0; // Tempo as percentage of original speed

    // --- RubberBand ---
    RubberBand::RubberBandStretcher* _stretcher = nullptr;

    // Per-channel non-interleaved buffers fed to / retrieved from RubberBand.
    std::vector<std::vector<float>> _rb_in_buf;
    std::vector<float*>             _rb_in_ptrs;
    std::vector<std::vector<float>> _rb_out_buf;
    std::vector<float*>             _rb_out_ptrs;

    // --- PortAudio ---
    PaStream* _stream = nullptr;

    // --- State flags ---
    std::atomic<bool> _playing{false};
    std::atomic<bool> _paused{false};  // True between pause() and play()
    bool              _reversed{false};

    FinishedCallback _finished_cb;

    // Number of frames fed to RubberBand per processing block.
    static constexpr int BLOCK = 512;
};
