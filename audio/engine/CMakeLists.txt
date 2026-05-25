cmake_minimum_required(VERSION 3.15)
project(retune_engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# pybind11
find_package(pybind11 REQUIRED)

# системные либы
find_package(PkgConfig REQUIRED)
pkg_check_modules(RUBBERBAND REQUIRED rubberband)
pkg_check_modules(PORTAUDIO  REQUIRED portaudio-2.0)
pkg_check_modules(SNDFILE    REQUIRED sndfile)

pybind11_add_module(retune_engine
    engine.cpp
    player.cpp
)

target_include_directories(retune_engine PRIVATE
    ${RUBBERBAND_INCLUDE_DIRS}
    ${PORTAUDIO_INCLUDE_DIRS}
    ${SNDFILE_INCLUDE_DIRS}
)

target_link_libraries(retune_engine PRIVATE
    ${RUBBERBAND_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
    ${SNDFILE_LIBRARIES}
)

target_compile_options(retune_engine PRIVATE
    -O3 -march=native
    ${RUBBERBAND_CFLAGS_OTHER}
    ${PORTAUDIO_CFLAGS_OTHER}
    ${SNDFILE_CFLAGS_OTHER}
)

# Кладём .so рядом с audio/
set_target_properties(retune_engine PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/..
)
