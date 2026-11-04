#pragma once
#include "ps2native/gif.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace ps2native {
class VuControl;
// Shared VIF0/VIF1 configuration parser. Command words arrive in little-endian
// order; VIF1 DIRECT data starts on the next qword boundary. Unsupported commands
// (including MSCAL/UNPACK) fail explicitly; MPG writes real shared micro RAM,
// and never starts or pretends to execute the uploaded instructions.
class Vif {
public:
    enum class Unit { Vu0, Vu1 };
    explicit Vif(Unit unit=Unit::Vu1) : unit_(unit) {}
    void reset() {
        direct_left_ = 0; error_mask_.reset(); mark_.reset(); code_.reset();
        programmed_={}; pending_=Pending::None; pending_index_=0; mark_detected_=false;
        direct_words_=0;direct_buffer_={};
        mpg_left_=mpg_address_=0;mpg_low_.reset();
    }
    [[nodiscard]] std::uint32_t read_register(std::uint32_t offset) const;
    void write_register(std::uint32_t offset,std::uint32_t value,Gif& gif);
    void write_fifo(std::uint64_t low, std::uint64_t high, Gif& gif, GsRegisters& gs,VuControl* vu=nullptr);
    // TTE sends only the upper 64 bits, in lanes 2/3 of a DMA tag.
    void write_dma_tag(std::uint64_t data,Gif& gif,GsRegisters& gs,VuControl* vu=nullptr);
    [[nodiscard]] std::uint32_t direct_remaining() const noexcept { return direct_left_; }
    [[nodiscard]] bool processing() const noexcept {return direct_left_ || mpg_left_ || pending_!=Pending::None;}
private:
    void write_words(std::span<const std::uint32_t> words,Gif& gif,GsRegisters& gs,VuControl* vu);
    [[noreturn]] void refuse(const std::string& reason) const {
        throw DeviceRefusal(std::string(unit_==Unit::Vu0 ? "VIF0" : "VIF1")+reason);
    }
    Unit unit_;
    std::uint32_t direct_left_{};
    std::array<std::uint32_t,4> direct_buffer_{};
    unsigned direct_words_{};
    unsigned mpg_left_{},mpg_address_{};
    std::optional<std::uint32_t> mpg_low_;
    std::optional<std::uint32_t> error_mask_,mark_,code_;
    enum class Pending { None, Mask, Row, Col };
    Pending pending_{Pending::None};
    unsigned pending_index_{};
    bool mark_detected_{};
    std::array<std::optional<std::uint32_t>,32> programmed_{};
};
using Vif1=Vif; // Preserve the existing PATH2 caller interface.
} // namespace ps2native
