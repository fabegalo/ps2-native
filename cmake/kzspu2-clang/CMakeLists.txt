# kzspu2 alone, for toolchains the main build cannot use for it (clang-cl on Windows ARM64).
cmake_minimum_required(VERSION 3.21)
project(kzspu2 C CXX)
set(_pcsx2 "${PS2N_ROOT}/external/pcsx2")
set(_kzspu2 "${PS2N_ROOT}/external/pcsx2-components/kzspu2")
include("${PS2N_ROOT}/cmake/Kzspu2.cmake")
