/*
 * Retune — desktop audio player with real-time pitch and tempo control.
 * Copyright (c) 2025 Christopher_VN
 * Licensed under the MIT License.
 *
 * engine.cpp
 * ----------
 * pybind11 bindings that expose AudioEngine to Python.
 * The progress callback for export_wav is wrapped so the GIL is re-acquired
 * before calling back into Python from the export worker thread.
 */

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>
#include "player.hpp"

namespace py = pybind11;

PYBIND11_MODULE(retune_engine, m) {
    m.doc() = "Retune C++ audio engine";

    py::class_<AudioEngine>(m, "AudioEngine")
    .def(py::init<>())
    .def("load",         &AudioEngine::load)
    .def("play",         &AudioEngine::play)
    .def("pause",        &AudioEngine::pause)
    .def("stop",         &AudioEngine::stop)
    .def("set_pitch",    &AudioEngine::set_pitch)
    .def("set_tempo",    &AudioEngine::set_tempo)
    .def("seek",         &AudioEngine::seek)
    .def("reverse",      &AudioEngine::reverse)
    .def("is_reversed",  &AudioEngine::is_reversed)
    .def("is_playing",   &AudioEngine::is_playing)
    .def("get_position", &AudioEngine::get_position)
    .def("get_duration", &AudioEngine::get_duration)
    .def("get_waveform", &AudioEngine::get_waveform)
    // export_wav accepts an optional Python callable as progress callback.
    // The lambda re-acquires the GIL before each call into Python.
    .def("export_wav", [](AudioEngine& self,
                          const std::string& path,
                          py::object cb) {
        std::function<void(float)> progress_cb;
        if (!cb.is_none())
            progress_cb = [cb](float p) {
                py::gil_scoped_acquire gil;
                cb(p);
            };
        return self.export_wav(path, progress_cb);
                          }, py::arg("path"), py::arg("progress_cb") = py::none())
    .def("export_mp3", [](AudioEngine& self,
                          const std::string& path,
                          int quality,
                          py::object cb) {
        std::function<void(float)> progress_cb;
        if (!cb.is_none())
            progress_cb = [cb](float p) {
                py::gil_scoped_acquire gil;
                cb(p);
            };
        return self.export_mp3(path, quality, progress_cb);
                          }, py::arg("path"),
         py::arg("quality") = 2,
         py::arg("progress_cb") = py::none())
    .def("set_finished_callback", &AudioEngine::set_finished_callback);
}
