#pragma once
#include <cstdint>
#include <memory>
#include <string>
class GSRasterBackend;
struct GSRegisters;

// Diagnostic CPU raster backend that also records a bounded PCSX2 GS dump
// (.gs) of the original command stream, starting at the given guest vsync.
std::unique_ptr<GSRasterBackend> ps2n_pcsx2_gs_dump(const GSRegisters& registers, const std::string& path,
                                                    uint64_t startTick, uint64_t frames);
