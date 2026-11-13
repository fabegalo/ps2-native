#pragma once
#include <memory>
#include <cstdint>
#include <vector>
class GSRasterBackend;
struct GSRegisters;
struct PresentationFrame;
struct GSPresentationRequest;

// Selection is host-only; unavailable devices fail explicitly.
std::unique_ptr<GSRasterBackend> ps2n_parallel_gs(const GSRegisters& registers, bool enhanced);
void ps2n_trace_scanout(const PresentationFrame& frame, const GSPresentationRequest& request);
void ps2n_trace_vram(const std::vector<uint8_t>& vram, uint64_t tick);
