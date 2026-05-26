/*
 * Retune — desktop audio player with real-time pitch and tempo control.
 * Copyright (c) 2026 Christopher_VN
 * Licensed under the MIT License.
 *
 * player.hpp
 * ----------
 * Declaration of AudioEngine.
 *
 * Streaming architecture:
 *   In normal playback the file stays open via _sf / _sf_info and samples
 *   are read block by block inside process() — _raw is NOT populated.
 *   This keeps RSS proportional to the block size rather than file size.
 *
 *   _raw IS populated in two cases:
 *     1. reverse() — the entire file is read, reversed in place, and
 *        playback switches to in-memory mode (_streaming = false).
 *     2. get_waveform() — a separate lightweight read pass.
 *
 * Thread-safety:
 *   _stretcher_mutex guards the RubberBand stretcher and its parameters.
 *   _sf_mutex guards the libsndfile handle and file position.
 *   _read_pos, _playing, _paused are std::atomic.
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
#include <lame/lame.h>

class AudioEngine {
public:
    using FinishedCallback = std::function<void()>;

    AudioEngine();
    ~AudioEngine();

    bool   load(const std::string& path);
    void   play();
    void   pause();
    void   stop();

    // Thread-safe — may be called from the UI thread during playback.
    void   set_pitch(double semitones);
    void   set_tempo(double percent);

    void   seek(double seconds);

    // Loads the full file into memory, reverses it, switches to in-memory mode.
    void   reverse();
    bool   is_reversed() const;

    bool   is_playing()    const;
    double get_position()  const;
    double get_duration()  const;
    int    get_sample_rate() const;

    // Performs a separate lightweight read pass; does not affect playback.
    std::vector<float> get_waveform(int n_points) const;

    bool   export_wav(const std::string& path,
                      std::function<void(float)> progress_cb = nullptr);

    // Export to MP3 using libmp3lame. quality: 0 (best) to 9 (worst).
    bool   export_mp3(const std::string& path,
                      int quality = 2,
                      std::function<void(float)> progress_cb = nullptr);

    void   set_finished_callback(FinishedCallback cb);

private:
    static int pa_callback(const void* input, void* output,
                           unsigned long frames,
                           const PaStreamCallbackTimeInfo* time_info,
                           PaStreamCallbackFlags flags,
                           void* user_data);

    int  process(float* output, unsigned long frames_needed);
    void reset_stretcher();
    void cleanup_stream();
    void close_file();

    // Read up to 'frames' frames from the current source into dst (non-interleaved).
    // Returns the number of frames actually read.
    long read_frames(long frames, std::vector<std::vector<float>>& dst);

    // --- File ---
    std::string _path;
    SNDFILE*    _sf      = nullptr;
    SF_INFO     _sf_info = {};
    mutable std::mutex _sf_mutex;  // Guards _sf and file position in streaming mode

    // --- In-memory buffer (used only in reversed / non-streaming mode) ---
    std::vector<float> _raw;
    bool               _streaming = true;  // True = read from file; False = read from _raw

    // --- Metadata ---
    int    _channels = 2;
    int    _sr       = 44100;
    double _duration = 0.0;
    long   _total_frames = 0;  // Total frames in the file

    // Current playback position in frames (streaming) or samples (in-memory).
    std::atomic<long> _read_pos{0};

    // --- Processing parameters (guarded by _stretcher_mutex) ---
    double _semitones     = 0.0;
    double _tempo_percent = 100.0;

    // --- RubberBand ---
    mutable std::mutex                   _stretcher_mutex;
    RubberBand::RubberBandStretcher*     _stretcher = nullptr;

    std::vector<std::vector<float>> _rb_in_buf;
    std::vector<float*>             _rb_in_ptrs;
    std::vector<std::vector<float>> _rb_out_buf;
    std::vector<float*>             _rb_out_ptrs;

    // --- PortAudio ---
    PaStream* _stream = nullptr;

    // --- State ---
    std::atomic<bool> _playing{false};
    std::atomic<bool> _paused{false};
    bool              _reversed{false};

    FinishedCallback _finished_cb;

    static constexpr int BLOCK = 2048;
};
