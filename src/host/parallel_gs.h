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
// GIF packet framing shared with the frontend's per-packet parser.
uint32_t ps2n_gif_payload_bytes(uint64_t tag);             // payload bytes a tag announces
uint32_t ps2n_gif_loops_in(uint64_t tag, uint32_t bytes);  // whole loops of that tag present in bytes
uint32_t ps2n_whole_gif_tags(const uint8_t* data, uint32_t size);  // leading bytes made of complete tags
void ps2n_trace_vram(const std::vector<uint8_t>& vram, uint64_t tick);
