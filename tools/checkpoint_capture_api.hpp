#pragma once
#include <cstdint>
#include <cstddef>
// POD-only boundary between the debugger and a verified, matching save adapter.
struct CaptureAbi {
    std::uint64_t version,ee_size,memory_size,retired_offset,pc_offset,cache_clock_offset;
    char image_sha256[65];
};
struct CaptureRequest {
    std::uint64_t version{1},state{},memory{};
    wchar_t output[32768]{};
    char scenario[65]{},disc_sha256[65]{},metadata[8192]{};
    std::uint32_t status{};
    char error[4096]{};
};
