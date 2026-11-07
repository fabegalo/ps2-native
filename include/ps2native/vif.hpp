#pragma once
#include "ps2native/gif.hpp"
#include "ps2native/vu_float.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>

namespace ps2native {
class VuControl;
// Shared VIF0/VIF1 configuration parser. Command words arrive in little-endian
// order; VIF1 DIRECT data starts on the next qword boundary. Unsupported commands
// fail explicitly; MPG/UNPACK and MSCAL use the same shared, clocked VU.
class Vif {
    friend struct CheckpointAccess;
public:
    enum class Unit { Vu0, Vu1 };
    explicit Vif(Unit unit=Unit::Vu1) : unit_(unit) {}
    void reset() {
        direct_left_ = 0; error_mask_.reset(); mark_.reset(); code_.reset();
        programmed_={}; pending_=Pending::None; pending_index_=0; mark_detected_=false;
        direct_words_=0;direct_buffer_={};
        mpg_left_=mpg_address_=0;mpg_low_.reset();
        unpack_left_=unpack_address_=unpack_index_=unpack_cycle_=0;unpack_unknown_={};unpack_bounds_={};
        dbf_.reset();
        interrupt_stall_=interrupt_edge_=command_interrupt_=false;
        fifo_.clear();input_={};input_index_=input_count_=0;waiting_=padding_=gif_waiting_=false;
    }
    [[nodiscard]] std::uint32_t read_register(std::uint32_t offset) const;
    [[nodiscard]] ObservedWord observe_status() const noexcept {
        return {(interrupt_stall_ ? 0u : waiting_ ? 2u|(gif_waiting_ ? 8u : 4u) : processing() ? 1u : 0u)|(mark_detected_ ? 0x40u : 0u)|(interrupt_stall_ ? 0xC00u : 0u)|
            (dbf_.value_or(false) ? 0x80u : 0u)|(static_cast<std::uint32_t>(fifo_.size())<<24u),
            unit_==Unit::Vu1 && !dbf_ ? 0x80u : 0u};
    }
    void write_register(std::uint32_t offset,std::uint32_t value,Gif& gif);
    void write_fifo(std::uint64_t low, std::uint64_t high, Gif& gif, GsRegisters& gs,VuControl* vu=nullptr,std::uint64_t cycle=0);
    void write_fifo_observed(ObservedQuadword data,Gif& gif,GsRegisters& gs,VuControl* vu=nullptr,std::uint64_t cycle=0,std::span<const std::optional<FloatBounds>> bounds={});
    void write_dma_tag_observed(ObservedDoubleword data,Gif& gif,GsRegisters& gs,VuControl* vu=nullptr,std::uint64_t cycle=0,std::span<const std::optional<FloatBounds>> bounds={});
    // TTE sends only the upper 64 bits, in lanes 2/3 of a DMA tag.
    void write_dma_tag(std::uint64_t data,Gif& gif,GsRegisters& gs,VuControl* vu=nullptr,std::uint64_t cycle=0);
    [[nodiscard]] std::uint32_t direct_remaining() const noexcept { return direct_left_; }
    [[nodiscard]] bool processing() const noexcept {return direct_left_ || mpg_left_ || unpack_left_ || pending_!=Pending::None || !fifo_.empty() || input_index_<input_count_;}
    [[nodiscard]] bool can_accept_dma() const noexcept {return fifo_.size()<(unit_==Unit::Vu1 ? 16u : 8u);}
    // RUNTIME_HELPERS_BEGIN
    // Partial payloads without new words cannot advance, but a queued decoder
    // waiting for VU/PATH1 must still observe its dependency's transition.
    [[nodiscard]] bool drain_pending() const noexcept {
        return waiting_ || gif_waiting_ || !fifo_.empty() || input_index_<input_count_;
    }
    [[nodiscard]] bool take_interrupt() noexcept {const auto edge=interrupt_edge_;interrupt_edge_=false;return edge;}
    // RUNTIME_HELPERS_END
private:
    friend void advance_vpus(std::uint64_t,Vif*,Vif*,VuControl*,Gif&,GsRegisters&);
    void write_words(std::span<const std::uint32_t> words,unsigned first_lane,Gif& gif,GsRegisters& gs,VuControl* vu,std::uint64_t cycle,std::span<const std::uint32_t> unknown={},std::span<const std::optional<FloatBounds>> bounds={});
    void drain(std::uint64_t cycle,VuControl* vu,Gif& gif,GsRegisters& gs);
    void start_unpack(std::uint32_t command,VuControl* vu);
    void unpack_word(ObservedWord word,VuControl& vu,std::optional<FloatBounds> bounds);
    [[noreturn]] void refuse(const std::string& reason) const {
        throw DeviceRefusal(std::string(unit_==Unit::Vu0 ? "VIF0" : "VIF1")+reason);
    }
    Unit unit_;
    std::uint32_t direct_left_{};
    std::array<std::uint32_t,4> direct_buffer_{};
    unsigned direct_words_{};
    unsigned mpg_left_{},mpg_address_{};
    std::optional<std::uint32_t> mpg_low_;
    unsigned unpack_left_{},unpack_address_{},unpack_index_{},unpack_cycle_{};
    unsigned unpack_components_{},unpack_cl_{},unpack_wl_{},unpack_mode_{};
    std::uint32_t unpack_mask_{};
    std::array<std::uint32_t,4> unpack_data_{};
    std::array<std::uint32_t,4> unpack_unknown_{};
    std::array<std::optional<FloatBounds>,4> unpack_bounds_{};
    std::optional<std::uint32_t> error_mask_,mark_,code_;
    enum class Pending { None, Mask, Row, Col };
    Pending pending_{Pending::None};
    unsigned pending_index_{};
    bool mark_detected_{};
    std::optional<bool> dbf_;
    struct Input {std::array<std::uint32_t,4> words{};unsigned first{},count{};std::array<std::uint32_t,4> unknown{};std::array<std::optional<FloatBounds>,4> bounds{};};
    std::deque<Input> fifo_;
    Input input_{};
    unsigned input_index_{},input_count_{};
    bool waiting_{},padding_{},gif_waiting_{};
    bool interrupt_stall_{},interrupt_edge_{},command_interrupt_{};
    std::array<std::optional<std::uint32_t>,32> programmed_{};
};
// Shared VIF/VU event ordering, including a queued call starting on the exact
// Ready event instead of the later host polling time.
void advance_vpus(std::uint64_t cycle,Vif* vif0,Vif* vif1,VuControl* vu,Gif& gif,GsRegisters& gs);
using Vif1=Vif; // Preserve the existing PATH2 caller interface.
} // namespace ps2native
