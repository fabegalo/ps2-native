#pragma once
#include "ps2native/device_timing.hpp"
#include <optional>

namespace ps2native {
// EE User's Manual 6.0, sections 8.1.2 and 8.3. Only idle reset and
// configuration/BCLR are implemented; all decode commands and FIFO I/O refuse.
class IpuRegisters {
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
    }
    IpuRegisters() {reset();}
    [[nodiscard]] ObservedWord observe_control() const noexcept {return control_;}
    [[nodiscard]] std::uint32_t read_control() const {
        if(control_.unknown) throw DeviceRefusal("IPU_CTRL read consumes unverified status/reset bits");
        return control_.value;
    }
    void write_control(std::uint32_t value) {
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
        }
    }
    void write_command(std::uint32_t value) {
        if(value>>28u) throw DeviceRefusal("IPU decode command is not modeled");
        if(value&~0x7Fu) throw DeviceRefusal("IPU BCLR reserved option bits set");
        // EE manual pp.175/188: command issue clears ECD/SCD; BCLR
        // discards the input bitstream and establishes its next bit position.
        // No FIFO input/decode engine can have queued data in this subset.
        bit_position_=value&0x7Fu;
        control_.value&=~0x8000C0FFu;
        control_.unknown&=~0x8000C0FFu;
    }
    [[nodiscard]] std::uint32_t read_position() const {
        if(!bit_position_) throw DeviceRefusal("IPU_BP requires BCLR to establish its bit position");
        return *bit_position_;
    }
private:
    ObservedWord control_;
    std::optional<std::uint32_t> bit_position_;
};
} // namespace ps2native
