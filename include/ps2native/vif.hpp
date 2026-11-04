#pragma once
#include "ps2native/gif.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace ps2native {
// VIF1 executable subset: NOP and DIRECT. Command words arrive in little-endian
// order, DIRECT data starts on the next qword boundary. Unsupported commands
// (including MSCAL/MPG/UNPACK) fail explicitly; no fake VU execution.
class Vif1 {
public:
    void reset() {
        direct_left_ = 0; error_mask_.reset(); mark_.reset(); code_.reset();
        programmed_={}; pending_=Pending::None; pending_index_=0; mark_detected_=false;
    }
    [[nodiscard]] std::uint32_t read_register(std::uint32_t offset) const;
    void write_register(std::uint32_t offset,std::uint32_t value,Gif& gif);
    void write_fifo(std::uint64_t low, std::uint64_t high, Gif& gif, GsRegisters& gs);
    [[nodiscard]] std::uint32_t direct_remaining() const noexcept { return direct_left_; }
private:
    std::uint32_t direct_left_{};
    std::optional<std::uint32_t> error_mask_,mark_,code_;
    enum class Pending { None, Mask, Row, Col };
    Pending pending_{Pending::None};
    unsigned pending_index_{};
    bool mark_detected_{};
    std::array<std::optional<std::uint32_t>,32> programmed_{};
};
} // namespace ps2native
