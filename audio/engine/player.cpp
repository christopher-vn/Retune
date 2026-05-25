/*
 * Retune — desktop audio player with real-time pitch and tempo control.
 * Copyright (c) 2026 Christopher_VN
 * Licensed under the MIT License.
 *
 * player.cpp
 * ----------
 * Implementation of AudioEngine.
 *
 * Thread-safety strategy:
 *   _stretcher_mutex is taken in two situations:
 *     1. reset_stretcher() — recreates the object (UI thread).
 *     2. set_pitch() / set_tempo() — update stretcher parameters (UI thread).
 *     3. process() — reads from the stretcher (audio thread).
 *   This prevents a UI-thread reset from racing with the audio callback.
 *
 *   _read_pos, _playing, and _paused are std::atomic so they can be read
 *   and written from both threads without a mutex.
 *
 *   _raw is written only during load() which stops playback first, so it
 *   is effectively read-only while the audio thread is active.
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
    {
        std::lock_guard<std::mutex> lock(_stretcher_mutex);
        delete _stretcher;
        _stretcher = nullptr;
    }
    Pa_Terminate();
}

// ---------- load ----------

bool AudioEngine::load(const std::string& path) {
    // Stop any active playback before touching _raw.
    stop();

    SF_INFO info{};
    SNDFILE* sf = sf_open(path.c_str(), SFM_READ, &info);
    if (!sf) return false;

    _sr       = info.samplerate;
    _channels = info.channels > 2 ? 2 : info.channels;
    _duration = static_cast<double>(info.frames) / _sr;

    long total_samples = info.frames * info.channels;
    std::vector<float> tmp(total_samples);
    sf_read_float(sf, tmp.data(), total_samples);
    sf_close(sf);

    if (info.channels == 1) {
        // Duplicate mono to stereo so the engine always works with two channels.
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

    SF_INFO info{};
    info.samplerate = _sr;
    info.channels   = _channels;
    info.format     = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

    SNDFILE* sf = sf_open(path.c_str(), SFM_WRITE, &info);
    if (!sf) return false;

    // Snapshot current parameters under the mutex so the export is consistent
    // even if the user moves a slider while exporting.
    double semitones, tempo_percent;
    {
        std::lock_guard<std::mutex> lock(_stretcher_mutex);
        semitones     = _semitones;
        tempo_percent = _tempo_percent;
    }

    // Use a dedicated stretcher — does not interfere with playback.
    RubberBandStretcher::Options opts =
        RubberBandStretcher::OptionProcessRealTime |
        RubberBandStretcher::OptionPitchHighConsistency;

    RubberBandStretcher exp_stretcher(_sr, _channels, opts);
    exp_stretcher.setPitchScale(std::pow(2.0, semitones / 12.0));
    double ratio = 100.0 / std::max(1.0, tempo_percent);
    exp_stretcher.setTimeRatio(ratio);

    std::vector<std::vector<float>> in_buf(_channels, std::vector<float>(BLOCK));
    std::vector<std::vector<float>> out_buf(_channels, std::vector<float>(BLOCK));
    std::vector<float*> in_ptrs(_channels), out_ptrs(_channels);
    std::vector<float>  write_buf;

    for (int c = 0; c < _channels; ++c) {
        in_ptrs[c]  = in_buf[c].data();
        out_ptrs[c] = out_buf[c].data();
    }

    long total_samples = static_cast<long>(_raw.size());
    long pos = 0;

    while (true) {
        int avail = exp_stretcher.available();
        if (avail > 0) {
            write_buf.resize(avail * _channels);
            for (int c = 0; c < _channels; ++c) {
                out_buf[c].resize(avail);
                out_ptrs[c] = out_buf[c].data();
            }
            size_t got = exp_stretcher.retrieve(out_ptrs.data(), avail);
            for (size_t f = 0; f < got; ++f)
                for (int c = 0; c < _channels; ++c)
                    write_buf[f * _channels + c] = out_buf[c][f];
            sf_write_float(sf, write_buf.data(), got * _channels);

            if (progress_cb && total_samples > 0)
                progress_cb(static_cast<float>(pos) / total_samples);
        } else {
            if (pos >= total_samples) break;
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

    for (int i = 0; i < n_points; ++i) {
        long  start = i * frames_per_point;
        long  end   = std::min(start + frames_per_point, total_frames);
        float peak  = 0.f;
        for (long f = start; f < end; ++f)
            for (int c = 0; c < _channels; ++c) {
                float s = std::abs(_raw[f * _channels + c]);
                if (s > peak) peak = s;
            }
        result[i] = peak;
    }

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
    // Must be called from the UI thread only (not the audio callback).
    std::lock_guard<std::mutex> lock(_stretcher_mutex);

    delete _stretcher;

    RubberBandStretcher::Options opts =
        RubberBandStretcher::OptionProcessRealTime |
        RubberBandStretcher::OptionPitchHighConsistency;

    _stretcher = new RubberBandStretcher(_sr, _channels, opts);
    _stretcher->setPitchScale(std::pow(2.0, _semitones / 12.0));
    double ratio = 100.0 / std::max(1.0, _tempo_percent);
    _stretcher->setTimeRatio(ratio);

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
    // Lock so the audio callback cannot read the stretcher while we update it.
    std::lock_guard<std::mutex> lock(_stretcher_mutex);
    _semitones = semitones;
    if (_stretcher)
        _stretcher->setPitchScale(std::pow(2.0, semitones / 12.0));
}

void AudioEngine::set_tempo(double percent) {
    std::lock_guard<std::mutex> lock(_stretcher_mutex);
    _tempo_percent = percent;
    if (_stretcher) {
        double ratio = 100.0 / std::max(1.0, percent);
        _stretcher->setTimeRatio(ratio);
    }
}

// ---------- seek ----------

void AudioEngine::seek(double seconds) {
    bool was_playing = _playing.load();

    if (was_playing) {
        _playing = false;
        if (_stream) {
            Pa_StopStream(_stream);
            Pa_CloseStream(_stream);
            _stream = nullptr;
        }
    }

    long target_frame = static_cast<long>(seconds * _sr);
    long total_frames = static_cast<long>(_raw.size()) / _channels;
    target_frame = std::max(0L, std::min(target_frame, total_frames - 1));
    _read_pos = target_frame * _channels;

    // Reset the stretcher to flush its internal buffers after the position jump.
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
        // _read_pos is intentionally preserved so play() resumes here.
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
    return self->process(static_cast<float*>(output), frames);
}

// Called from the PortAudio audio thread — must not block.
// Acquires _stretcher_mutex briefly to access the stretcher.
int AudioEngine::process(float* output, unsigned long frames_needed) {
    long   total_samples = static_cast<long>(_raw.size());
    size_t written       = 0;

    while (written < frames_needed) {
        std::unique_lock<std::mutex> lock(_stretcher_mutex);

        if (!_stretcher) {
            // Stretcher not ready — output silence.
            lock.unlock();
            std::memset(output + written * _channels, 0,
                        (frames_needed - written) * _channels * sizeof(float));
            return paContinue;
        }

        int available = static_cast<int>(_stretcher->available());

        if (available > 0) {
            size_t can_get = std::min<size_t>(available, frames_needed - written);
            _stretcher->retrieve(_rb_out_ptrs.data(), can_get);
            lock.unlock();

            for (size_t f = 0; f < can_get; ++f)
                for (int c = 0; c < _channels; ++c)
                    output[(written + f) * _channels + c] = _rb_out_buf[c][f];

            written += can_get;
        } else {
            long pos = _read_pos.load();
            if (pos >= total_samples) {
                lock.unlock();
                std::memset(output + written * _channels, 0,
                            (frames_needed - written) * _channels * sizeof(float));
                _playing = false;
                if (_finished_cb) _finished_cb();
                return paComplete;
            }

            long frames_left = (total_samples - pos) / _channels;
            long to_feed     = std::min<long>(BLOCK, frames_left);
            bool is_final    = (pos + to_feed * _channels >= total_samples);

            for (long f = 0; f < to_feed; ++f)
                for (int c = 0; c < _channels; ++c)
                    _rb_in_buf[c][f] = _raw[pos + f * _channels + c];

            _stretcher->process(_rb_in_ptrs.data(), to_feed, is_final);
            lock.unlock();

            _read_pos += to_feed * _channels;
        }
    }

    return paContinue;
}
