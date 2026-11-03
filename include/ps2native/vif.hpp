#pragma once
#include "ps2native/gif.hpp"
#include <array>
#include <cstdint>

namespace ps2native {
// VIF1 executable subset: NOP and DIRECT. Command words arrive in little-endian
// order, DIRECT data starts on the next qword boundary. Unsupported commands
// (including MSCAL/MPG/UNPACK) fail explicitly; no fake VU execution.
class Vif1 {
public:
    void reset() { direct_left_ = 0; }
    void write_fifo(std::uint64_t low, std::uint64_t high, Gif& gif, GsRegisters& gs);
    [[nodiscard]] std::uint32_t direct_remaining() const noexcept { return direct_left_; }
private:
    std::uint32_t direct_left_{};
};
} // namespace ps2native
