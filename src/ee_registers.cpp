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

constexpr std::uint32_t kModeHblank = 3u;

// Clock edges for BUSCLK, BUSCLK/16 and BUSCLK/256 occur at fixed multiples
// of the period since reset; HBLANK edges come from the GS line clock.
GsRegisters::LineClock timer_clock(std::uint32_t mode, const GsRegisters& gs) {
    switch (mode & kModeClock) {
        case 0: return {0u, kEeCyclesPerBusCycle};
        case 1: return {0u, kEeCyclesPerBusCycle * 16u};
        case 2: return {0u, kEeCyclesPerBusCycle * 256u};
        default: return gs.line_clock();
    }
}
} // namespace

void EeRegisters::reset(const ConsoleHardware* hardware, std::span<const std::uint8_t> rom0) {
    hardware_ = hardware;
    iop_.reset(rom0, hardware);
    // Hypothesis: counters, modes (disabled) and the latch are zero/unset after reset.
    timers_ = {};
    f500_.reset();
    dev9_147x_ = {};
    sio_setup_ = {};
    sio_output_.clear();
    gs_.reset(0u);
    mch_.reset();
    // Hypothesis: every source masked at power-on. The kernel reads I_MASK
    // before writing it (EnableIntc tests the bit, then reverses it).
    intc_mask_ = 0u;
    undocumented_190x_ = {};
    undocumented_1a_ = {};
}

void EeRegisters::sync_iop() { iop_.run_until(now() / kEeCyclesPerIopCycle); }

std::uint64_t EeRegisters::now() const {
    if (clock_ == nullptr) throw DeviceRefusal("no EE clock is attached to the EE registers");
    return *clock_;
}

std::uint16_t EeRegisters::timer_count(const Timer& timer) const {
    if ((timer.mode & kModeEnable) == 0u) return timer.count_at_anchor;
    // The anchor never precedes the clock epoch: a GS line clock change is
    // refused while a timer counts HBLANK (see write()).
    const auto clock = timer_clock(timer.mode, gs_);
    const auto ticks = (now() - clock.epoch) / clock.period - (timer.anchor_cycle - clock.epoch) / clock.period;
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
                if ((value & kModeClock) == kModeHblank) (void)gs_.line_clock(); // refuses without HBLANK
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
    if (physical >= kIopRamEePhysical && physical < kIopRamEePhysical + 0x00800000u) {
        sync_iop();
        return iop_.ee_read(physical - kIopRamEePhysical, bytes);
    }
    if (physical >= kTimersBase && physical < kTimersEnd) {
        const auto offset = physical & 0x7FFu;
        if (bytes != 4u || offset % 16u != 0u || offset >= 0x40u)
            throw DeviceRefusal("EE timer access of " + std::to_string(bytes) + " byte(s) at offset " +
                                hex(offset, 3) + " is not modeled (32-bit Tn_COUNT/MODE/COMP only)");
        return read_timer((physical >> 11) & 3u, offset / 16u);
    }
    if (physical >= kGsPrivilegedBase && physical < kGsPrivilegedEnd) return gs_.read(physical, bytes);
    if (physical == kF500) {
        // Unidentified register; what reads return has not been verified.
        throw DeviceRefusal("0x1000F500 read: an unidentified register; its read value is not verified");
    }
    if (physical == kDmacEnabler) {
        if (bytes != 4u)
            throw DeviceRefusal("D_ENABLER read of " + std::to_string(bytes) + " byte(s) is not modeled (32-bit only)");
        if (hardware_ == nullptr)
            throw DeviceRefusal("D_ENABLER read: no console hardware profile selected; its value identifies the "
                                "mainboard and is not in ROM0 (select one, e.g. --console=SCPH-77008)");
        if (!hardware_->d_enabler)
            throw DeviceRefusal("D_ENABLER read: the value of " + std::string(hardware_->model) + ":" +
                                std::string(hardware_->mainboard) + " has not been measured (" +
                                std::string(hardware_->source) + ")");
        // Constant: D_ENABLEW writes, which would change CPND, are refused.
        return *hardware_->d_enabler;
    }
    if (physical == kToolBoardIdPhysical) {
        if (bytes != 1u)
            throw DeviceRefusal("tool board id access of " + std::to_string(bytes) +
                                " byte(s) is not modeled (8-bit read only)");
        return kRetailToolBoardId; // no tool board (retail value: hypothesis)
    }
    if (physical == kDev9Reg1470 || physical == kDev9Reg1472)
        throw DeviceRefusal("DEV9 " + hex(physical) + " read: an undocumented register; its read value is not verified");
    if (physical >= kMchBase && physical < kMchEnd) return mch_.read(physical, bytes, now());
    if (physical == kIntcMask) {
        if (bytes != 4u) throw DeviceRefusal("I_MASK read of " + std::to_string(bytes) + " byte(s) is not modeled");
        return intc_mask_;
    }
    if ((physical >= kUndocumented190x && physical < kUndocumented190x + 16u) ||
        (physical >= kUndocumented1A && physical < kUndocumented1A + 16u))
        throw DeviceRefusal(hex(physical) + " read: an undocumented register; its read value is not verified");
    if (physical == kIntcStat)
        throw DeviceRefusal("I_STAT read: no EE interrupt source (GS, SBUS, VBLANK, VIF, VU, IPU, timers, SFIFO) "
                            "is modeled, so the pending bits are unknown");
    if (physical == kSioIsr) {
        if (bytes != 4u) throw DeviceRefusal("SIO_ISR read of " + std::to_string(bytes) + " byte(s) is not modeled");
        // Bits 12-15: TX FIFO level, 0 because transmission is instantaneous;
        // bits 8-11: RX FIFO level, 0 because nothing is received; bits 0-2:
        // interrupt status, 0 with interrupts disabled (hypothesis, as PCSX2,
        // which notes it is not hardware tested).
        return 0u;
    }
    if (physical >= kSioLcr && physical < kSioTxFifo + 0x80u)
        throw DeviceRefusal("EE SIO read: only SIO_ISR is modeled (LSR/RX FIFO status and receive are not)");
    throw DeviceRefusal("no register model");
}

void EeRegisters::write(std::uint32_t physical, std::size_t bytes, std::uint64_t value) {
    if (physical >= kIopRamEePhysical && physical < kIopRamEePhysical + 0x00800000u)
        throw DeviceRefusal("EE write to IOP RAM is not modeled (the IOP model owns its RAM)");
    if (physical >= kTimersBase && physical < kTimersEnd) {
        const auto offset = physical & 0x7FFu;
        if (bytes != 4u || offset % 16u != 0u || offset >= 0x40u)
            throw DeviceRefusal("EE timer access of " + std::to_string(bytes) + " byte(s) at offset " +
                                hex(offset, 3) + " is not modeled (32-bit Tn_COUNT/MODE/COMP only)");
        write_timer((physical >> 11) & 3u, offset / 16u, static_cast<std::uint32_t>(value));
        return;
    }
    if (physical >= kGsPrivilegedBase && physical < kGsPrivilegedEnd) {
        gs_.write(physical, bytes, value, now(), [this] {
            for (unsigned n = 0; n < timers_.size(); ++n)
                if ((timers_[n].mode & kModeEnable) != 0u && (timers_[n].mode & kModeClock) == kModeHblank)
                    throw DeviceRefusal("changing the GS line clock while T" + std::to_string(n) +
                                        " counts HBLANK is not modeled");
        });
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
    if (physical == kDev9Reg1470 || physical == kDev9Reg1472) {
        if (bytes != 2u)
            throw DeviceRefusal("DEV9 " + hex(physical) + " write of " + std::to_string(bytes) +
                                " byte(s) is not modeled (16-bit stores only)");
        // Undocumented (PCSX2 stores it as an unknown DEV9 write): latched,
        // no modeled effect; DEV9 itself is not modeled, so nothing can
        // observe a missing effect without failing explicitly.
        dev9_147x_[physical == kDev9Reg1472 ? 1u : 0u] = static_cast<std::uint16_t>(value);
        return;
    }
    if (physical == kSioTxFifo) {
        if (bytes != 1u)
            throw DeviceRefusal("SIO TX FIFO write of " + std::to_string(bytes) +
                                " byte(s) is not modeled (byte stores only)");
        sio_output_.push_back(static_cast<char>(value));
        return;
    }
    if (physical == kSioIer) {
        if (bytes != 4u || value != 0u)
            throw DeviceRefusal("SIO_IER " + hex(static_cast<std::uint32_t>(value)) +
                                ": SIO interrupts need the INTC, not modeled (only 32-bit 0 is accepted)");
        return;
    }
    if (physical == kSioLcr || physical == kSioFcr || physical == kSioBgr) {
        if (bytes != 4u)
            throw DeviceRefusal("EE SIO setup write of " + std::to_string(bytes) + " byte(s) is not modeled");
        // Line format, FIFO control and baud rate only shape the serial
        // signal; with instantaneous transmission they are latched as written.
        // The FIFO reset bits have nothing to clear (TX drains at once, RX is empty).
        sio_setup_[physical == kSioLcr ? 0u : physical == kSioFcr ? 1u : 2u] = static_cast<std::uint32_t>(value);
        return;
    }
    if (physical >= kMchBase && physical < kMchEnd) {
        mch_.write(physical, bytes, value, now());
        return;
    }
    if (physical == kIntcMask) {
        if (bytes != 4u) throw DeviceRefusal("I_MASK write of " + std::to_string(bytes) + " byte(s) is not modeled");
        intc_mask_ ^= static_cast<std::uint32_t>(value) & kIntcSources;
        return;
    }
    for (auto [base, latch] : {std::pair{kUndocumented190x, &undocumented_190x_},
                               std::pair{kUndocumented1A, &undocumented_1a_}}) {
        if (physical < base || physical >= base + 16u) continue;
        if (physical + bytes > base + 16u)
            throw DeviceRefusal(hex(physical) + " write crosses the end of the undocumented " + hex(base) + " block");
        // Latched as written; its function is unknown and nothing reads it back.
        for (std::size_t i = 0; i < bytes; ++i)
            (*latch)[physical - base + i] = static_cast<std::uint8_t>(value >> (8u * i));
        return;
    }
    if (physical == kIntcStat) {
        if (bytes != 4u) throw DeviceRefusal("I_STAT write of " + std::to_string(bytes) + " byte(s) is not modeled");
        // Acknowledge (write 1 to clear): nothing is ever raised here and
        // reads are refused, so there is no state to change.
        return;
    }
    throw DeviceRefusal("no register model");
}
} // namespace ps2native
