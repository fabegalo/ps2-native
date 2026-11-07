#pragma once
#include "ps2native/device_timing.hpp"
#include <optional>
#include <array>
#include <deque>

namespace ps2native {
// EE User's Manual 6.0, sections 8.1.2 and 8.3. Only idle reset and
// configuration, BCLR and bit-addressed table loads are implemented. MPEG decoding
// and conversion remain explicit refusals. Table execution is synchronous once
// all input bytes exist; this is a functional model, not IPU pipeline timing.
class IpuRegisters {
    friend struct CheckpointAccess;
public:
    static constexpr std::uint32_t kControl=0x10002010u;
    static constexpr std::uint32_t kCommand=0x10002000u,kPosition=0x10002020u;
    static constexpr std::uint32_t kSettings=0x07F30000u;
    void reset() {
        // No command has been issued and neither FIFO has received data.
        // Initial configuration, CBP/ECD/SCD and write-only RST readback are
        // deliberately unknown, rather than inheriting another emulator's 0.
        control_={0,kSettings|0x4000FF00u};
        bit_position_.reset();
        input_.clear(); pending_.reset(); iq_={}; vq_.reset(); thresholds_.reset();
    }
    IpuRegisters() {reset();}
    [[nodiscard]] ObservedWord observe_control() const noexcept {
        auto result=control_;
        result.value=(result.value&~0x8000000Fu)|static_cast<std::uint32_t>(input_.size()/16u)|
                     (pending_ ? 0x80000000u : 0u);
        // The functional queue retains real bytes, but does not model when
        // the IPU prefetches qwords into its internal buffer. Do not present
        // its queue length as a measured hardware IFC value.
        if(!input_.empty()) {result.value&=~15u;result.unknown|=15u;}
        return result;
    }
    [[nodiscard]] std::uint32_t read_control() const {
        const auto result=observe_control();
        if(result.unknown) throw DeviceRefusal("IPU_CTRL read consumes unverified status/reset bits");
        return result.value;
    }
    bool write_control(std::uint32_t value) {
        if(value&~(kSettings|0xC000FFFFu)) throw DeviceRefusal("IPU_CTRL reserved bits set");
        control_.value=(control_.value&~kSettings)|(value&kSettings);
        control_.unknown&=~kSettings;
        if(value&0x40000000u) {
            // Reset abandons the command and buffered data, clearing BUSY,
            // IFC and OFC. An idle reset has no command to end and no IRQ.
            // CBP/ECD/SCD are not documented as cleared by this operation.
            control_.value&=~0x800000FFu;
            control_.unknown&=~0x800000FFu;
            bit_position_.reset();
            const bool interrupted=pending_.has_value();
            input_.clear(); pending_.reset();
            // Reset retention of tables is not established in this subset.
            iq_={}; vq_.reset(); thresholds_.reset();
            return interrupted;
        }
        return false;
    }
    bool write_command(std::uint32_t value) {
        if(pending_) throw DeviceRefusal("IPU command issued while BUSY");
        const auto command=value>>28u;
        if(command==5u || command==6u || command==9u) {
            const auto allowed=command==5u ? 0x5800003Fu : command==6u ? 0x60000000u : 0x91FF01FFu;
            if(value&~allowed) throw DeviceRefusal("IPU table command reserved bits set");
            if(command!=9u && !bit_position_)
                throw DeviceRefusal("IPU table load requires BCLR to establish its bit position");
            control_.value&=~0xC000u; control_.unknown&=~0xC000u;
            if(command==9u) {thresholds_=value&0x01FF01FFu;return true;}
            pending_=value;
            return load_table();
        }
        if(command) throw DeviceRefusal("IPU decode command is not modeled");
        if(value&~0x7Fu) throw DeviceRefusal("IPU BCLR reserved option bits set");
        // EE manual pp.175/188: command issue clears ECD/SCD; BCLR
        // discards the input bitstream and establishes its next bit position.
        bit_position_=value&0x7Fu;
        control_.value&=~0x8000C0FFu;
        control_.unknown&=~0x8000C0FFu;
        input_.clear();
        return true;
    }
    bool write_fifo(std::uint64_t low,std::uint64_t high) {
        if(input_.size()+16u>128u) throw DeviceRefusal("IPU input FIFO full: CPU backpressure not modeled");
        for(unsigned i=0;i<16u;++i)
            input_.push_back(static_cast<std::uint8_t>((i<8u ? low : high)>>((i&7u)*8u)));
        return load_table();
    }
    [[nodiscard]] std::uint32_t read_position() const {
        if(!bit_position_) throw DeviceRefusal("IPU_BP requires BCLR to establish its bit position");
        if(!input_.empty()) throw DeviceRefusal("IPU_BP FIFO/internal prefetch distribution is not modeled");
        return *bit_position_|static_cast<std::uint32_t>(input_.size()/16u)<<8u;
    }
    [[nodiscard]] std::size_t buffered_bytes() const noexcept {return input_.size();}
    [[nodiscard]] const auto& quantization_table(unsigned index) const {return iq_.at(index);}
    [[nodiscard]] const auto& vector_table() const noexcept {return vq_;}
    [[nodiscard]] const auto& thresholds() const noexcept {return thresholds_;}
private:
    // Adapted from ps2-web-recomp's bits_at (Samir Nuri, MIT).
    // e026b971324068773eed88bf502437de9e581b6b, runtime/src/ipu/ipu.cpp.
    // See third_party/ps2-web-recomp/LICENSE. Caller checks the entire payload.
    [[nodiscard]] std::uint32_t bits_at(std::size_t bit,unsigned n) const {
        if(!n) return 0;
        std::uint64_t v=0;
        const std::size_t byte=bit>>3;
        for(std::size_t i=0;i<8;++i)
            v=(v<<8)|(byte+i<input_.size() ? input_[byte+i] : 0u);
        return static_cast<std::uint32_t>((v<<(bit&7u))>>(64-n));
    }
    bool load_table() {
        if(!pending_) return false;
        const bool iq=(*pending_>>28u)==5u;
        const auto count=iq ? 64u : 32u;
        const auto first=*bit_position_+(iq ? (*pending_&63u) : 0u);
        const auto end=first+count*8u;
        if(input_.size()*8u<end) return false;
        auto bit=first;
        if(iq) {
            std::array<std::uint8_t,64> bytes{};
            for(auto& byte:bytes){byte=static_cast<std::uint8_t>(bits_at(bit,8));bit+=8;}
            iq_[(*pending_>>27u)&1u]=bytes;
        } else {
            std::array<std::uint8_t,32> bytes{};
            for(auto& byte:bytes){byte=static_cast<std::uint8_t>(bits_at(bit,8));bit+=8;}
            vq_=bytes;
        }
        // Retain the partial qword so BP still indexes the queued bitstream.
        for(unsigned i=0;i<(end/128u)*16u;++i) input_.pop_front();
        bit_position_=end&127u;
        pending_.reset();
        return true;
    }
    ObservedWord control_;
    std::optional<std::uint32_t> bit_position_;
    std::deque<std::uint8_t> input_;
    std::optional<std::uint32_t> pending_,thresholds_;
    std::array<std::optional<std::array<std::uint8_t,64>>,2> iq_;
    std::optional<std::array<std::uint8_t,32>> vq_;
};
} // namespace ps2native
