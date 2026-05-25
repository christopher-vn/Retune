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
 *
 * Thread-safety notes:
 *   - The PortAudio callback runs on a dedicated audio thread.
 *   - All public methods may be called from the UI (main) thread.
 *   - _stretcher_mutex guards the stretcher pointer and its parameters
 *     (_semitones, _tempo_percent) so that set_pitch() / set_tempo()
 *     from the UI thread cannot race with the audio callback.
 *   - _read_pos and playback state flags use std::atomic for lock-free
 *     access from both threads.
 */

#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include <mutex>

#include <rubberband/RubberBandStretcher.h>
#include <portaudio.h>
#include <sndfile.h>

class AudioEngine {
public:
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

    // Set pitch shift in semitones. Thread-safe — may be called from UI thread
    // while audio is playing.
    void   set_pitch(double semitones);

    // Set playback tempo as a percentage (100 = normal speed). Thread-safe.
    void   set_tempo(double percent);

    // Seek to a position given in seconds.
    void   seek(double seconds);

    // Reverse the raw audio buffer in place.
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
    std::vector<float> get_waveform(int n_points) const;

    // Export the processed audio to a WAV file.
    // progress_cb is called periodically with a value in [0, 1].
    bool   export_wav(const std::string& path,
                      std::function<void(float)> progress_cb = nullptr);

    // Register a callback invoked when playback finishes naturally.
    void   set_finished_callback(FinishedCallback cb);

private:
    static int pa_callback(const void* input, void* output,
                           unsigned long frames,
                           const PaStreamCallbackTimeInfo* time_info,
                           PaStreamCallbackFlags flags,
                           void* user_data);

    int  process(float* output, unsigned long frames_needed);

    // Recreate the stretcher under _stretcher_mutex.
    // Must NOT be called from the audio thread.
    void reset_stretcher();

    void cleanup_stream();

    // --- Audio data (written once on load, read-only after that) ---
    std::vector<float> _raw;
    int    _channels  = 2;
    int    _sr        = 44100;
    double _duration  = 0.0;

    // Current read position (samples, not frames). Atomic for audio thread.
    std::atomic<long> _read_pos{0};

    // --- Processing parameters ---
    // Guarded by _stretcher_mutex when accessed from set_pitch/set_tempo
    // (UI thread) concurrently with process() (audio thread).
    double _semitones     = 0.0;
    double _tempo_percent = 100.0;

    // --- RubberBand ---
    // _stretcher and its parameters are protected by _stretcher_mutex.
    mutable std::mutex                       _stretcher_mutex;
    RubberBand::RubberBandStretcher*         _stretcher = nullptr;

    std::vector<std::vector<float>> _rb_in_buf;
    std::vector<float*>             _rb_in_ptrs;
    std::vector<std::vector<float>> _rb_out_buf;
    std::vector<float*>             _rb_out_ptrs;

    // --- PortAudio ---
    PaStream* _stream = nullptr;

    // --- State flags (atomic for cross-thread access) ---
    std::atomic<bool> _playing{false};
    std::atomic<bool> _paused{false};
    bool              _reversed{false};

    FinishedCallback _finished_cb;

    static constexpr int BLOCK = 512;
};
