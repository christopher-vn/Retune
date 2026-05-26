/*
 * Retune — desktop audio player with real-time pitch and tempo control.
 * Copyright (c) 2026 Christopher_VN
 * Licensed under the MIT License.
 *
 * player.cpp
 * ----------
 * Implementation of AudioEngine with streaming file read.
 *
 * In streaming mode (_streaming = true):
 *   - _sf stays open for the lifetime of the loaded track.
 *   - process() calls read_frames() which does sf_read_float() under _sf_mutex.
 *   - seek() calls sf_seek() under _sf_mutex.
 *   - _raw is empty; RSS stays low regardless of file size.
 *
 * In in-memory mode (_streaming = false, after reverse()):
 *   - _sf is closed.
 *   - process() reads directly from _raw.
 *   - _read_pos counts individual samples (not frames).
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
    close_file();
    Pa_Terminate();
}

// ---------- file helpers ----------

void AudioEngine::close_file() {
    std::lock_guard<std::mutex> lock(_sf_mutex);
    if (_sf) {
        sf_close(_sf);
        _sf = nullptr;
    }
}

// Read up to 'frames' frames from the current source into non-interleaved dst.
// In streaming mode reads from _sf; in in-memory mode reads from _raw.
// Returns actual frames read. Called from the audio thread under _sf_mutex
// (streaming) or without lock (in-memory, _raw is read-only then).
long AudioEngine::read_frames(long frames,
                              std::vector<std::vector<float>>& dst) {
    if (_streaming) {
        // Temporary interleaved buffer, then deinterleave into dst.
        std::vector<float> tmp(frames * _channels);
        sf_count_t got = sf_read_float(_sf, tmp.data(), frames * _channels);
        long got_frames = got / _channels;
        for (long f = 0; f < got_frames; ++f)
            for (int c = 0; c < _channels; ++c)
                dst[c][f] = tmp[f * _channels + c];
        return got_frames;
    } else {
        // In-memory: _read_pos counts samples.
        long pos          = _read_pos.load();
        long total        = static_cast<long>(_raw.size());
        long frames_left  = (total - pos) / _channels;
        long to_read      = std::min(frames, frames_left);
        for (long f = 0; f < to_read; ++f)
            for (int c = 0; c < _channels; ++c)
                dst[c][f] = _raw[pos + f * _channels + c];
        return to_read;
    }
}

// ---------- load ----------

bool AudioEngine::load(const std::string& path) {
    stop();
    close_file();
    _raw.clear();

    SF_INFO info{};
    SNDFILE* sf = sf_open(path.c_str(), SFM_READ, &info);
    if (!sf) return false;

    _path         = path;
    _sr           = info.samplerate;
    _channels     = info.channels > 2 ? 2 : info.channels;
    _duration     = static_cast<double>(info.frames) / _sr;
    _total_frames = info.frames;
    _sf_info      = info;

    // Store the open handle for streaming reads.
    {
        std::lock_guard<std::mutex> lock(_sf_mutex);
        _sf = sf;
    }

    _streaming     = true;
    _reversed      = false;
    _read_pos      = 0;
    _paused        = false;
    _tempo_percent = 100.0;
    reset_stretcher();
    return true;
}

// ---------- export MP3 ----------

bool AudioEngine::export_mp3(const std::string& path,
                             int quality,
                             std::function<void(float)> progress_cb) {
    double semitones, tempo_percent;
    {
        std::lock_guard<std::mutex> lock(_stretcher_mutex);
        semitones     = _semitones;
        tempo_percent = _tempo_percent;
    }

    SNDFILE* in_sf    = nullptr;
    long total_frames = _total_frames;

    if (!_streaming && !_raw.empty()) {
        total_frames = static_cast<long>(_raw.size()) / _channels;
    } else {
        SF_INFO in_info = _sf_info;
        in_sf = sf_open(_path.c_str(), SFM_READ, &in_info);
        if (!in_sf) return false;
    }

    RubberBandStretcher::Options opts =
        RubberBandStretcher::OptionProcessRealTime |
        RubberBandStretcher::OptionPitchHighConsistency;

    RubberBandStretcher exp_stretcher(_sr, _channels, opts);
    exp_stretcher.setPitchScale(std::pow(2.0, semitones / 12.0));
    exp_stretcher.setTimeRatio(100.0 / std::max(1.0, tempo_percent));

    lame_global_flags* lame = lame_init();
    if (!lame) {
        if (in_sf) sf_close(in_sf);
        return false;
    }

    lame_set_in_samplerate(lame, _sr);
    lame_set_num_channels(lame, _channels);
    lame_set_quality(lame, quality);
    lame_set_VBR(lame, vbr_default);
    lame_set_VBR_quality(lame, quality);

    if (lame_init_params(lame) < 0) {
        lame_close(lame);
        if (in_sf) sf_close(in_sf);
        return false;
    }

    FILE* out_file = fopen(path.c_str(), "w+b");
    if (!out_file) {
        lame_close(lame);
        if (in_sf) sf_close(in_sf);
        return false;
    }

    std::vector<std::vector<float>> in_buf(_channels, std::vector<float>(BLOCK));
    std::vector<std::vector<float>> out_buf(_channels);
    std::vector<float*> in_ptrs(_channels), out_ptrs(_channels);
    std::vector<float>  tmp_interleaved(BLOCK * _channels);

    const int mp3_buf_size = BLOCK * 5 + 7200;
    std::vector<unsigned char> mp3_buf(mp3_buf_size);

    for (int c = 0; c < _channels; ++c)
        in_ptrs[c] = in_buf[c].data();

    long pos = 0;

    while (true) {
        int avail = exp_stretcher.available();
        if (avail > 0) {
            for (int c = 0; c < _channels; ++c) {
                out_buf[c].resize(avail);
                out_ptrs[c] = out_buf[c].data();
            }
            size_t got = exp_stretcher.retrieve(out_ptrs.data(), avail);

            int encoded = 0;
            if (_channels == 2) {
                encoded = lame_encode_buffer_ieee_float(
                    lame,
                    out_buf[0].data(), out_buf[1].data(),
                    static_cast<int>(got),
                    mp3_buf.data(), mp3_buf_size
                );
            } else {
                encoded = lame_encode_buffer_ieee_float(
                    lame,
                    out_buf[0].data(), out_buf[0].data(),
                    static_cast<int>(got),
                    mp3_buf.data(), mp3_buf_size
                );
            }
            if (encoded > 0)
                fwrite(mp3_buf.data(), 1, encoded, out_file);

            if (progress_cb && total_frames > 0)
                progress_cb(static_cast<float>(pos) / total_frames);
        } else {
            if (pos >= total_frames) break;

            long to_feed  = std::min<long>(BLOCK, total_frames - pos);
            bool is_final = (pos + to_feed >= total_frames);

            if (in_sf) {
                sf_count_t got = sf_read_float(
                    in_sf, tmp_interleaved.data(), to_feed * _channels);
                to_feed = got / _channels;
                for (long f = 0; f < to_feed; ++f)
                    for (int c = 0; c < _channels; ++c)
                        in_buf[c][f] = tmp_interleaved[f * _channels + c];
            } else {
                long sample_pos = pos * _channels;
                for (long f = 0; f < to_feed; ++f)
                    for (int c = 0; c < _channels; ++c)
                        in_buf[c][f] = _raw[sample_pos + f * _channels + c];
            }

            exp_stretcher.process(in_ptrs.data(), to_feed, is_final);
            pos += to_feed;


        }
    }

    int flushed = lame_encode_flush(lame, mp3_buf.data(), mp3_buf_size);
    if (flushed > 0)
        fwrite(mp3_buf.data(), 1, flushed, out_file);

    // Write Xing/Info header for accurate VBR seeking.
    lame_mp3_tags_fid(lame, out_file);

    fclose(out_file);
    lame_close(lame);
    if (in_sf) sf_close(in_sf);
    if (progress_cb) progress_cb(1.0f);
    return true;
}

std::vector<float> AudioEngine::get_waveform(int n_points) const {
    std::vector<float> result(n_points, 0.f);
    if (n_points <= 0) return result;

    if (!_streaming && !_raw.empty()) {
        // In-memory mode: read directly from _raw.
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
    } else {
        // Streaming mode: open a separate file handle for the read pass
        // so we don't disturb the playback position.
        SF_INFO info = _sf_info;
        SNDFILE* sf  = sf_open(_path.c_str(), SFM_READ, &info);
        if (!sf) return result;

        long frames_per_point = std::max(1L, _total_frames / n_points);
        std::vector<float> tmp(frames_per_point * _channels);

        for (int i = 0; i < n_points; ++i) {
            sf_count_t got = sf_read_float(sf, tmp.data(),
                                           frames_per_point * _channels);
            long got_frames = got / _channels;
            float peak = 0.f;
            for (long f = 0; f < got_frames; ++f)
                for (int c = 0; c < _channels; ++c) {
                    float s = std::abs(tmp[f * _channels + c]);
                    if (s > peak) peak = s;
                }
            result[i] = peak;
        }
        sf_close(sf);
    }

    float max_peak = *std::max_element(result.begin(), result.end());
    if (max_peak > 0.f)
        for (auto& v : result) v /= max_peak;

    return result;
}

// ---------- reverse ----------

void AudioEngine::reverse() {
    bool was_playing = _playing.load();
    stop();

    // Load entire file into _raw if not already in memory.
    if (_streaming) {
        SF_INFO info = _sf_info;
        SNDFILE* sf  = sf_open(_path.c_str(), SFM_READ, &info);
        if (!sf) return;

        long total_samples = _total_frames * _channels;
        std::vector<float> tmp(total_samples);
        sf_read_float(sf, tmp.data(), total_samples);
        sf_close(sf);

        if (_channels == 1) {
            _raw.resize(_total_frames * 2);
            for (long i = 0; i < _total_frames; ++i) {
                _raw[i * 2 + 0] = tmp[i];
                _raw[i * 2 + 1] = tmp[i];
            }
            _channels = 2;
        } else {
            _raw = std::move(tmp);
        }

        // Close the streaming handle — we no longer need it.
        close_file();
        _streaming = false;
    }

    // Reverse frames in place.
    long total_frames = static_cast<long>(_raw.size()) / _channels;
    for (long i = 0; i < total_frames / 2; ++i) {
        long j = total_frames - 1 - i;
        for (int c = 0; c < _channels; ++c)
            std::swap(_raw[i * _channels + c], _raw[j * _channels + c]);
    }

    _reversed = !_reversed;
    _read_pos = 0;
    reset_stretcher();

    if (was_playing) play();
}

bool AudioEngine::is_reversed() const { return _reversed; }

// ---------- export ----------

bool AudioEngine::export_wav(const std::string& path,
                             std::function<void(float)> progress_cb) {
    // Snapshot parameters under the mutex.
    double semitones, tempo_percent;
    {
        std::lock_guard<std::mutex> lock(_stretcher_mutex);
        semitones     = _semitones;
        tempo_percent = _tempo_percent;
    }

    SF_INFO out_info{};
    out_info.samplerate = _sr;
    out_info.channels   = _channels;
    out_info.format     = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

    SNDFILE* out_sf = sf_open(path.c_str(), SFM_WRITE, &out_info);
    if (!out_sf) return false;

    // Open the source — either reopen the file or read from _raw.
    SNDFILE* in_sf        = nullptr;
    long     total_frames = _total_frames;

    if (!_streaming && !_raw.empty()) {
        total_frames = static_cast<long>(_raw.size()) / _channels;
    } else {
        SF_INFO in_info = _sf_info;
        in_sf = sf_open(_path.c_str(), SFM_READ, &in_info);
        if (!in_sf) { sf_close(out_sf); return false; }
    }

    RubberBandStretcher::Options opts =
        RubberBandStretcher::OptionProcessRealTime |
        RubberBandStretcher::OptionPitchHighConsistency;

    RubberBandStretcher exp_stretcher(_sr, _channels, opts);
    exp_stretcher.setPitchScale(std::pow(2.0, semitones / 12.0));
    exp_stretcher.setTimeRatio(100.0 / std::max(1.0, tempo_percent));

    std::vector<std::vector<float>> in_buf(_channels, std::vector<float>(BLOCK));
    std::vector<std::vector<float>> out_buf(_channels);
    std::vector<float*> in_ptrs(_channels), out_ptrs(_channels);
    std::vector<float>  write_buf;
    std::vector<float>  tmp_interleaved(BLOCK * _channels);

    for (int c = 0; c < _channels; ++c) {
        in_ptrs[c] = in_buf[c].data();
    }

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
            sf_write_float(out_sf, write_buf.data(), got * _channels);

            if (progress_cb && total_frames > 0)
                progress_cb(static_cast<float>(pos) / total_frames);
        } else {
            if (pos >= total_frames) break;

            long to_feed  = std::min<long>(BLOCK, total_frames - pos);
            bool is_final = (pos + to_feed >= total_frames);

            if (in_sf) {
                // Streaming source: read interleaved then deinterleave.
                sf_count_t got = sf_read_float(
                    in_sf, tmp_interleaved.data(), to_feed * _channels);
                to_feed = got / _channels;
                for (long f = 0; f < to_feed; ++f)
                    for (int c = 0; c < _channels; ++c)
                        in_buf[c][f] = tmp_interleaved[f * _channels + c];
            } else {
                // In-memory source (_raw).
                long sample_pos = pos * _channels;
                for (long f = 0; f < to_feed; ++f)
                    for (int c = 0; c < _channels; ++c)
                        in_buf[c][f] = _raw[sample_pos + f * _channels + c];
            }

            exp_stretcher.process(in_ptrs.data(), to_feed, is_final);
            pos += to_feed;
        }
    }

    sf_close(out_sf);
    if (in_sf) sf_close(in_sf);
    if (progress_cb) progress_cb(1.0f);
    return true;
}

// ---------- stretcher ----------

void AudioEngine::reset_stretcher() {
    std::lock_guard<std::mutex> lock(_stretcher_mutex);
    delete _stretcher;

    RubberBandStretcher::Options opts =
        RubberBandStretcher::OptionProcessRealTime |
        RubberBandStretcher::OptionPitchHighConsistency;

    _stretcher = new RubberBandStretcher(_sr, _channels, opts);
    _stretcher->setPitchScale(std::pow(2.0, _semitones / 12.0));
    _stretcher->setTimeRatio(100.0 / std::max(1.0, _tempo_percent));

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
    std::lock_guard<std::mutex> lock(_stretcher_mutex);
    _semitones = semitones;
    if (_stretcher)
        _stretcher->setPitchScale(std::pow(2.0, semitones / 12.0));
}

void AudioEngine::set_tempo(double percent) {
    std::lock_guard<std::mutex> lock(_stretcher_mutex);
    _tempo_percent = percent;
    if (_stretcher)
        _stretcher->setTimeRatio(100.0 / std::max(1.0, percent));
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

    long target = static_cast<long>(seconds * _sr);
    target = std::max(0L, std::min(target, _total_frames - 1));

    if (_streaming) {
        std::lock_guard<std::mutex> lock(_sf_mutex);
        sf_seek(_sf, target, SEEK_SET);
        _read_pos = target;
    } else {
        _read_pos = target * _channels;
    }

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
    if (_playing) return;
    if (!_sf && _raw.empty()) return;

    if (!_paused) {
        if (_streaming) {
            std::lock_guard<std::mutex> lock(_sf_mutex);
            sf_seek(_sf, 0, SEEK_SET);
            _read_pos = 0;
        } else {
            _read_pos = 0;
        }
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
    }
}

void AudioEngine::stop() {
    _playing = false;
    _paused  = false;
    if (_streaming && _sf) {
        std::lock_guard<std::mutex> lock(_sf_mutex);
        sf_seek(_sf, 0, SEEK_SET);
        _read_pos = 0;
    } else {
        _read_pos = 0;
    }
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
    if (_streaming)
        return static_cast<double>(_read_pos.load()) / _sr;
    else
        return static_cast<double>(_read_pos.load() / _channels) / _sr;
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

int AudioEngine::process(float* output, unsigned long frames_needed) {
    size_t written = 0;

    while (written < frames_needed) {
        std::unique_lock<std::mutex> lock(_stretcher_mutex);

        if (!_stretcher) {
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
            // Check if we've reached the end.
            long pos = _read_pos.load();
            bool exhausted = _streaming
                ? (pos >= _total_frames)
                : (pos >= static_cast<long>(_raw.size()));

            if (exhausted) {
                lock.unlock();
                std::memset(output + written * _channels, 0,
                            (frames_needed - written) * _channels * sizeof(float));
                _playing = false;
                if (_finished_cb) _finished_cb();
                return paComplete;
            }

            // Feed the next block.
            long to_feed;
            if (_streaming) {
                std::lock_guard<std::mutex> sf_lock(_sf_mutex);
                lock.unlock();  // Release stretcher lock while doing file I/O
                std::unique_lock<std::mutex> relock(_stretcher_mutex);

                long frames_left = _total_frames - pos;
                to_feed = std::min<long>(BLOCK, frames_left);
                bool is_final = (pos + to_feed >= _total_frames);

                std::vector<float> tmp(to_feed * _channels);
                sf_count_t got = sf_read_float(_sf, tmp.data(), to_feed * _channels);
                to_feed = got / _channels;

                for (long f = 0; f < to_feed; ++f)
                    for (int c = 0; c < _channels; ++c)
                        _rb_in_buf[c][f] = tmp[f * _channels + c];

                _stretcher->process(_rb_in_ptrs.data(), to_feed, is_final);
                relock.unlock();
                _read_pos += to_feed;
            } else {
                long total      = static_cast<long>(_raw.size());
                long frames_left = (total - pos) / _channels;
                to_feed = std::min<long>(BLOCK, frames_left);
                bool is_final = (pos + to_feed * _channels >= total);

                for (long f = 0; f < to_feed; ++f)
                    for (int c = 0; c < _channels; ++c)
                        _rb_in_buf[c][f] = _raw[pos + f * _channels + c];

                _stretcher->process(_rb_in_ptrs.data(), to_feed, is_final);
                lock.unlock();
                _read_pos += to_feed * _channels;
            }
        }
    }

    return paContinue;
}
