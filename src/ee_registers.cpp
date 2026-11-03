#include "ps2native/ee_registers.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace ps2native {
namespace {
std::string hex(std::uint32_t value, int digits = 8) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(digits) << std::setfill('0') << value;
    return stream.str();
}

constexpr std::uint32_t kTimersBase = 0x10000000u;
constexpr std::uint32_t kTimersEnd = 0x10002000u;
constexpr std::uint32_t kF500 = 0x1000F500u;

// Tn_MODE fields (ps2tek).
constexpr std::uint32_t kModeClock = 3u << 0;
constexpr std::uint32_t kModeGateEnable = 1u << 2;
constexpr std::uint32_t kModeClearOnCompare = 1u << 6;
constexpr std::uint32_t kModeEnable = 1u << 7;
constexpr std::uint32_t kModeCompareIrq = 1u << 8;
constexpr std::uint32_t kModeOverflowIrq = 1u << 9;
constexpr std::uint32_t kModeConfig = 0x3FFu;
enum TimerReg : unsigned { Count = 0, Mode = 1, Comp = 2, Hold = 3 };
const char* timer_reg_name(unsigned reg) {
    static constexpr const char* kNames[] = {"COUNT", "MODE", "COMP", "HOLD"};
    return reg < 4u ? kNames[reg] : "?";
}

// Clock source period in EE cycles: BUSCLK, BUSCLK/16, BUSCLK/256, HBLANK.
std::uint64_t timer_period(std::uint32_t mode) {
    switch (mode & kModeClock) {
        case 0: return kEeCyclesPerBusCycle;
        case 1: return kEeCyclesPerBusCycle * 16u;
        case 2: return kEeCyclesPerBusCycle * 256u;
        default: return kEeCyclesPerBusCycle * kBusCyclesPerNtscLine;
    }
}
} // namespace

void EeRegisters::reset() {
    // Hypothesis: counters, modes (disabled) and the latch are zero/unset after reset.
    timers_ = {};
    f500_.reset();
}

std::uint64_t EeRegisters::now() const {
    if (clock_ == nullptr) throw DeviceRefusal("no EE clock is attached to the EE registers");
    return *clock_;
}

std::uint16_t EeRegisters::timer_count(const Timer& timer) const {
    if ((timer.mode & kModeEnable) == 0u) return timer.count_at_anchor;
    // Clock edges occur at fixed multiples of the period since reset.
    const auto period = timer_period(timer.mode);
    const auto ticks = now() / period - timer.anchor_cycle / period;
    return static_cast<std::uint16_t>(timer.count_at_anchor + ticks);
}

void EeRegisters::write_timer(unsigned index, unsigned reg, std::uint32_t value) {
    auto& timer = timers_[index];
    const auto name = "T" + std::to_string(index) + "_" + timer_reg_name(reg);
    switch (reg) {
        case Count:
            timer.count_at_anchor = static_cast<std::uint16_t>(value);
            timer.anchor_cycle = now();
            return;
        case Mode: {
            if (value & kModeEnable) {
                if (value & kModeGateEnable)
                    throw DeviceRefusal(name + " " + hex(value) + ": HBLANK/VBLANK gating is not modeled");
                if (value & (kModeCompareIrq | kModeOverflowIrq))
                    throw DeviceRefusal(name + " " + hex(value) + ": timer interrupts need the INTC, not modeled");
                if (value & kModeClearOnCompare)
                    throw DeviceRefusal(name + " " + hex(value) + ": clear-on-compare is not modeled");
            }
            // The counter keeps its value across the mode change and continues
            // from it with the new clock source (or stops when disabled).
            timer.count_at_anchor = timer_count(timer);
            timer.anchor_cycle = now();
            // Bits 10/11 are write-1-to-clear flags; no flag is ever set here
            // because compare/overflow tracking is refused above.
            timer.mode = value & kModeConfig;
            return;
        }
        case Comp:
            timer.compare = static_cast<std::uint16_t>(value);
            return;
        default:
            throw DeviceRefusal(name + ": counter hold (SBUS capture) is not modeled");
    }
}

std::uint32_t EeRegisters::read_timer(unsigned index, unsigned reg) {
    const auto& timer = timers_[index];
    const auto name = "T" + std::to_string(index) + "_" + timer_reg_name(reg);
    switch (reg) {
        case Count:
            return timer_count(timer);
        case Comp:
            if (!timer.compare) throw DeviceRefusal(name + " read before any write (reset value not verified)");
            return *timer.compare;
        case Mode:
            throw DeviceRefusal(name + " read: compare/overflow flag semantics are not modeled");
        default:
            throw DeviceRefusal(name + ": counter hold (SBUS capture) is not modeled");
    }
}

std::uint64_t EeRegisters::read(std::uint32_t physical, std::size_t bytes) {
    if (physical >= kTimersBase && physical < kTimersEnd) {
        const auto offset = physical & 0x7FFu;
        if (bytes != 4u || offset % 16u != 0u || offset >= 0x40u)
            throw DeviceRefusal("EE timer access of " + std::to_string(bytes) + " byte(s) at offset " +
                                hex(offset, 3) + " is not modeled (32-bit Tn_COUNT/MODE/COMP only)");
        return read_timer((physical >> 11) & 3u, offset / 16u);
    }
    if (physical == kF500) {
        // Unidentified register; what reads return has not been verified.
        throw DeviceRefusal("0x1000F500 read: an unidentified register; its read value is not verified");
    }
    if (physical == kToolBoardIdPhysical) {
        if (bytes != 1u)
            throw DeviceRefusal("tool board id access of " + std::to_string(bytes) +
                                " byte(s) is not modeled (8-bit read only)");
        return kRetailToolBoardId; // no tool board (retail value: hypothesis)
    }
    throw DeviceRefusal("no register model");
}

void EeRegisters::write(std::uint32_t physical, std::size_t bytes, std::uint64_t value) {
    if (physical >= kTimersBase && physical < kTimersEnd) {
        const auto offset = physical & 0x7FFu;
        if (bytes != 4u || offset % 16u != 0u || offset >= 0x40u)
            throw DeviceRefusal("EE timer access of " + std::to_string(bytes) + " byte(s) at offset " +
                                hex(offset, 3) + " is not modeled (32-bit Tn_COUNT/MODE/COMP only)");
        write_timer((physical >> 11) & 3u, offset / 16u, static_cast<std::uint32_t>(value));
        return;
    }
    if (physical == kF500 && bytes == 4u) {
        // Written by the BIOS reset code before the RDRAM setup. Its function
        // is undocumented (PCSX2 keeps it as plain storage): the value is
        // latched and has no modeled effect.
        f500_ = static_cast<std::uint32_t>(value);
        return;
    }
    if (physical == kToolBoardIdPhysical)
        throw DeviceRefusal("tool board id write: no evidence of what a write does");
    throw DeviceRefusal("no register model");
}
} // namespace ps2native
