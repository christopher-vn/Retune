/*
 * Retune — desktop audio player with real-time pitch and tempo control.
 * Copyright (c) 2026 Christopher_VN
 * Licensed under the MIT License.
 *
 * player.cpp
 * ----------
 * Implementation of AudioEngine.
 * Audio data is loaded entirely into memory as interleaved float32 stereo.
 * Pitch shifting and time stretching are performed by librubberband in
 * real-time mode. PortAudio drives the output stream via a callback.
 * WAV export renders the full buffer offline through a separate stretcher
 * instance, writing to disk block by block to avoid large allocations.
 */

#include "player.hpp"
#include <cstring>
#include <stdexcept>
#include <cmath>
#include <algorithm>

using namespace RubberBand;

// ---------- constructor / destructor ----------

AudioEngine::AudioEngine() {
    if (Pa_Initialize() != paNoError)
        throw std::runtime_error("PortAudio init failed");
}

AudioEngine::~AudioEngine() {
    stop();
    delete _stretcher;
    Pa_Terminate();
}

// ---------- load ----------

bool AudioEngine::load(const std::string& path) {
    stop();

    SF_INFO info{};
    SNDFILE* sf = sf_open(path.c_str(), SFM_READ, &info);
    if (!sf) return false;

    _sr       = info.samplerate;
    // Cap at stereo; surround files are downmixed to the first two channels.
    _channels = info.channels > 2 ? 2 : info.channels;
    _duration = static_cast<double>(info.frames) / _sr;

    // Read the entire file as interleaved float32.
    long total_samples = info.frames * info.channels;
    std::vector<float> tmp(total_samples);
    sf_read_float(sf, tmp.data(), total_samples);
    sf_close(sf);

    if (info.channels == 1) {
        // Duplicate mono to both channels so the engine always works in stereo.
        _channels = 2;
        _raw.resize(info.frames * 2);
        for (long i = 0; i < info.frames; ++i) {
            _raw[i * 2 + 0] = tmp[i];
            _raw[i * 2 + 1] = tmp[i];
        }
    } else if (info.channels == _channels) {
        _raw = std::move(tmp);
    } else {
        // More than 2 channels: keep only L and R.
        _raw.resize(info.frames * 2);
        for (long i = 0; i < info.frames; ++i) {
            _raw[i * 2 + 0] = tmp[i * info.channels + 0];
            _raw[i * 2 + 1] = tmp[i * info.channels + 1];
        }
    }

    // Reset all state for the new file.
    _read_pos      = 0;
    _paused        = false;
    _reversed      = false;
    _tempo_percent = 100.0;
    reset_stretcher();
    return true;
}

// ---------- export ----------

bool AudioEngine::export_wav(const std::string& path,
                             std::function<void(float)> progress_cb) {
    if (_raw.empty()) return false;

    // Open the output file before starting the render loop so we can bail
    // early if the path is not writable.
    SF_INFO info{};
    info.samplerate = _sr;
    info.channels   = _channels;
    info.format     = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

    SNDFILE* sf = sf_open(path.c_str(), SFM_WRITE, &info);
    if (!sf) return false;

    // Use a dedicated stretcher so export does not interfere with playback.
    RubberBandStretcher::Options opts =
        RubberBandStretcher::OptionProcessRealTime |
        RubberBandStretcher::OptionPitchHighConsistency;

    RubberBandStretcher exp_stretcher(_sr, _channels, opts);
    exp_stretcher.setPitchScale(std::pow(2.0, _semitones / 12.0));
    double ratio = 100.0 / std::max(1.0, _tempo_percent);
    exp_stretcher.setTimeRatio(ratio);

    // Non-interleaved input/output buffers for RubberBand.
    std::vector<std::vector<float>> in_buf(_channels, std::vector<float>(BLOCK));
    std::vector<std::vector<float>> out_buf(_channels, std::vector<float>(BLOCK));
    std::vector<float*> in_ptrs(_channels), out_ptrs(_channels);

    // write_buf is resized dynamically to match whatever RubberBand returns.
    std::vector<float> write_buf;

    for (int c = 0; c < _channels; ++c) {
        in_ptrs[c]  = in_buf[c].data();
        out_ptrs[c] = out_buf[c].data();
    }

    long total_samples = static_cast<long>(_raw.size());
    long pos = 0;

    // Alternate between feeding input and draining output until exhausted.
    while (true) {
        int avail = exp_stretcher.available();
        if (avail > 0) {
            // Resize output buffers to match what RubberBand has ready.
            write_buf.resize(avail * _channels);
            for (int c = 0; c < _channels; ++c) {
                out_buf[c].resize(avail);
                out_ptrs[c] = out_buf[c].data();
            }
            size_t got = exp_stretcher.retrieve(out_ptrs.data(), avail);

            // Interleave non-planar output and write directly to disk.
            for (size_t f = 0; f < got; ++f)
                for (int c = 0; c < _channels; ++c)
                    write_buf[f * _channels + c] = out_buf[c][f];
            sf_write_float(sf, write_buf.data(), got * _channels);

            if (progress_cb && total_samples > 0)
                progress_cb(static_cast<float>(pos) / total_samples);
        } else {
            if (pos >= total_samples) break;

            // Deinterleave the next block from _raw into per-channel buffers.
            long frames_left = (total_samples - pos) / _channels;
            long to_feed  = std::min<long>(BLOCK, frames_left);
            bool is_final = (pos + to_feed * _channels >= total_samples);

            for (long f = 0; f < to_feed; ++f)
                for (int c = 0; c < _channels; ++c)
                    in_buf[c][f] = _raw[pos + f * _channels + c];

            exp_stretcher.process(in_ptrs.data(), to_feed, is_final);
            pos += to_feed * _channels;
        }
    }

    sf_close(sf);
    if (progress_cb) progress_cb(1.0f);
    return true;
}

// ---------- waveform ----------

std::vector<float> AudioEngine::get_waveform(int n_points) const {
    std::vector<float> result(n_points, 0.f);
    if (_raw.empty() || n_points <= 0) return result;

    long total_frames     = static_cast<long>(_raw.size()) / _channels;
    long frames_per_point = std::max(1L, total_frames / n_points);

    // For each display point, find the peak amplitude across all channels.
    for (int i = 0; i < n_points; ++i) {
        long start = i * frames_per_point;
        long end   = std::min(start + frames_per_point, total_frames);
        float peak = 0.f;
        for (long f = start; f < end; ++f)
            for (int c = 0; c < _channels; ++c) {
                float s = std::abs(_raw[f * _channels + c]);
                if (s > peak) peak = s;
            }
        result[i] = peak;
    }

    // Normalize to [0, 1] so the display is consistent across loudness levels.
    float max_peak = *std::max_element(result.begin(), result.end());
    if (max_peak > 0.f)
        for (auto& v : result) v /= max_peak;

    return result;
}

// ---------- reverse ----------

void AudioEngine::reverse() {
    if (_raw.empty()) return;

    bool was_playing = _playing.load();
    stop();

    // Swap frames symmetrically. Channels within each frame stay in order so
    // left/right assignment is preserved after the reversal.
    long total_frames = static_cast<long>(_raw.size()) / _channels;
    for (long i = 0; i < total_frames / 2; ++i) {
        long j = total_frames - 1 - i;
        for (int c = 0; c < _channels; ++c)
            std::swap(_raw[i * _channels + c], _raw[j * _channels + c]);
    }

    _reversed = !_reversed;
    reset_stretcher();

    if (was_playing) play();
}

bool AudioEngine::is_reversed() const { return _reversed; }

// ---------- stretcher ----------

void AudioEngine::reset_stretcher() {
    delete _stretcher;

    RubberBandStretcher::Options opts =
        RubberBandStretcher::OptionProcessRealTime |
        RubberBandStretcher::OptionPitchHighConsistency;

    _stretcher = new RubberBandStretcher(_sr, _channels, opts);
    _stretcher->setPitchScale(std::pow(2.0, _semitones / 12.0));
    double ratio = 100.0 / std::max(1.0, _tempo_percent);
    _stretcher->setTimeRatio(ratio);

    // Allocate fixed-size non-interleaved buffers for the real-time loop.
    _rb_in_buf.assign(_channels, std::vector<float>(BLOCK, 0.f));
    _rb_out_buf.assign(_channels, std::vector<float>(BLOCK, 0.f));
    _rb_in_ptrs.resize(_channels);
    _rb_out_ptrs.resize(_channels);
    for (int c = 0; c < _channels; ++c) {
        _rb_in_ptrs[c]  = _rb_in_buf[c].data();
        _rb_out_ptrs[c] = _rb_out_buf[c].data();
    }
}

// ---------- pitch / tempo ----------

void AudioEngine::set_pitch(double semitones) {
    _semitones = semitones;
    // Convert semitones to a linear frequency ratio and apply immediately.
    if (_stretcher)
        _stretcher->setPitchScale(std::pow(2.0, semitones / 12.0));
}

void AudioEngine::set_tempo(double percent) {
    _tempo_percent = percent;
    if (_stretcher) {
        // time_ratio > 1 slows down, < 1 speeds up.
        double ratio = 100.0 / std::max(1.0, percent);
        _stretcher->setTimeRatio(ratio);
    }
}

// ---------- seek ----------

void AudioEngine::seek(double seconds) {
    bool was_playing = _playing.load();

    // Stop the stream without clearing _read_pos yet.
    if (was_playing) {
        _playing = false;
        if (_stream) {
            Pa_StopStream(_stream);
            Pa_CloseStream(_stream);
            _stream = nullptr;
        }
    }

    // Clamp the target frame to the valid range.
    long target_frame = static_cast<long>(seconds * _sr);
    long total_frames = static_cast<long>(_raw.size()) / _channels;
    target_frame = std::max(0L, std::min(target_frame, total_frames - 1));
    _read_pos = target_frame * _channels;

    // The stretcher must be reset after a position jump to flush its buffers.
    reset_stretcher();

    if (was_playing) {
        _paused = false;

        PaStreamParameters out{};
        out.device                    = Pa_GetDefaultOutputDevice();
        out.channelCount              = _channels;
        out.sampleFormat              = paFloat32;
        out.suggestedLatency          =
            Pa_GetDeviceInfo(out.device)->defaultLowOutputLatency;
        out.hostApiSpecificStreamInfo = nullptr;

        PaError err = Pa_OpenStream(
            &_stream, nullptr, &out,
            _sr, BLOCK, paClipOff,
            &AudioEngine::pa_callback, this
        );
        if (err == paNoError) {
            _playing = true;
            Pa_StartStream(_stream);
        }
    }
}

// ---------- transport ----------

void AudioEngine::play() {
    if (_raw.empty()) return;
    if (_playing) return;

    // Resume from the current position if paused; otherwise restart from zero.
    if (!_paused) {
        _read_pos = 0;
        reset_stretcher();
    }
    _paused = false;

    cleanup_stream();

    PaStreamParameters out{};
    out.device                    = Pa_GetDefaultOutputDevice();
    out.channelCount              = _channels;
    out.sampleFormat              = paFloat32;
    out.suggestedLatency          =
        Pa_GetDeviceInfo(out.device)->defaultLowOutputLatency;
    out.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_OpenStream(
        &_stream, nullptr, &out,
        _sr, BLOCK, paClipOff,
        &AudioEngine::pa_callback, this
    );
    if (err != paNoError) return;

    _playing = true;
    Pa_StartStream(_stream);
}

void AudioEngine::pause() {
    if (_playing && _stream) {
        _playing = false;
        _paused  = true;
        Pa_StopStream(_stream);
        Pa_CloseStream(_stream);
        _stream = nullptr;
        // _read_pos is intentionally left unchanged so play() can resume here.
    }
}

void AudioEngine::stop() {
    _playing  = false;
    _paused   = false;
    _read_pos = 0;
    cleanup_stream();
}

void AudioEngine::cleanup_stream() {
    if (_stream) {
        Pa_StopStream(_stream);
        Pa_CloseStream(_stream);
        _stream = nullptr;
    }
}

bool   AudioEngine::is_playing()      const { return _playing; }
double AudioEngine::get_duration()    const { return _duration; }
int    AudioEngine::get_sample_rate() const { return _sr; }

double AudioEngine::get_position() const {
    long frames = _read_pos.load() / _channels;
    return static_cast<double>(frames) / _sr;
}

void AudioEngine::set_finished_callback(FinishedCallback cb) {
    _finished_cb = std::move(cb);
}

// ---------- PortAudio callback ----------

int AudioEngine::pa_callback(
    const void*, void* output,
    unsigned long frames,
    const PaStreamCallbackTimeInfo*,
    PaStreamCallbackFlags,
    void* user_data)
{
    auto* self = static_cast<AudioEngine*>(user_data);
    auto* out  = static_cast<float*>(output);
    return self->process(out, frames);
}

// Called from the PortAudio audio thread — must be real-time safe.
int AudioEngine::process(float* output, unsigned long frames_needed) {
    long   total_samples = static_cast<long>(_raw.size());
    size_t written       = 0;

    while (written < frames_needed) {
        int available = static_cast<int>(_stretcher->available());

        if (available > 0) {
            // Drain as many frames as we need from the stretcher output.
            size_t can_get = std::min<size_t>(available, frames_needed - written);
            _stretcher->retrieve(_rb_out_ptrs.data(), can_get);

            // Interleave into the PortAudio output buffer.
            for (size_t f = 0; f < can_get; ++f)
                for (int c = 0; c < _channels; ++c)
                    output[(written + f) * _channels + c] = _rb_out_buf[c][f];

            written += can_get;
        } else {
            long pos = _read_pos.load();
            if (pos >= total_samples) {
                // Buffer exhausted — fill remainder with silence and signal done.
                std::memset(output + written * _channels, 0,
                            (frames_needed - written) * _channels * sizeof(float));
                _playing = false;
                if (_finished_cb) _finished_cb();
                return paComplete;
            }

            // Feed the next block of raw samples into the stretcher.
            long frames_left = (total_samples - pos) / _channels;
            long to_feed     = std::min<long>(BLOCK, frames_left);
            bool is_final    = (pos + to_feed * _channels >= total_samples);

            for (long f = 0; f < to_feed; ++f)
                for (int c = 0; c < _channels; ++c)
                    _rb_in_buf[c][f] = _raw[pos + f * _channels + c];

            _stretcher->process(_rb_in_ptrs.data(), to_feed, is_final);
            _read_pos += to_feed * _channels;
        }
    }

    return paContinue;
}
