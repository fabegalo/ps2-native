#include "ps2native/gs_registers.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace ps2native {
namespace {
std::string hex(std::uint64_t value, int digits) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(digits) << std::setfill('0') << value;
    return stream.str();
}

struct ModeRegister {
    const char* name;
    std::uint64_t valid; // bits defined by the field layout; the rest are reserved
};
// SMODE1..SYNCV in address order (0x12000010 + 0x10 * n), ps2sdk gs_privileged.h.
// SRFSH has no documented fields (ps2sdk masks it to zero), so every bit is
// latched as written; SYNCV fields cover all 64 bits.
constexpr ModeRegister kModeLayouts[] = {
    {"SMODE1", (1ull << 38) - 1u}, // RC..VHP, bits 0-37
    {"SMODE2", 0xFull},            // INT, FFMD, DPMS
    {"SRFSH", ~0ull},
    {"SYNCH1", (1ull << 59) - 1u}, // HFP, HBP, HSEQ, HSVS, HS
    {"SYNCH2", (1ull << 27) - 1u}, // HF, HB
    {"SYNCV", ~0ull},              // VFP, VFPE, VBP, VBPE, VDP, VS
};
constexpr std::uint64_t kCsrActions = kCsrSignal | kCsrFinish | kCsrHsint | kCsrVsint | kCsrEdwint |
                                      kCsrFlush | kCsrReset;

std::optional<std::size_t> mode_index(std::uint32_t physical) {
    if (physical < kGsSmode1 || physical > kGsSyncv || physical % 16u != 0u) return std::nullopt;
    return (physical - kGsSmode1) / 16u;
}
} // namespace

void GsRegisters::reset(std::uint64_t now) {
    ++generation_;
    mode_ = {};
    csr_written_.reset();
    line_ = {Sync::Running, 2u, {now, kEeCyclesPerBusCycle * kBusCyclesPerNtscLine}};
}

std::uint64_t GsRegisters::read(std::uint32_t physical, std::size_t bytes) const {
    if (physical == kGsCsr)
        throw DeviceRefusal("GS CSR read: VSINT/FIELD/NFIELD need VBLANK timing and REV/ID depend on the "
                            "console's GS revision; not modeled");
    if (const auto index = mode_index(physical))
        throw DeviceRefusal(std::string("GS ") + kModeLayouts[*index].name + " read (" +
                            std::to_string(bytes) + " byte(s)): write-only register; reads are not modeled");
    throw DeviceRefusal("no register model");
}

void GsRegisters::change_line_state(const LineState& next, const LineClockGuard& guard) {
    if (next == line_) return;
    if (guard) guard();
    line_ = next;
    ++generation_;
}

void GsRegisters::write(std::uint32_t physical, std::size_t bytes, std::uint64_t value, std::uint64_t now,
                        const LineClockGuard& guard) {
    if (const auto index = mode_index(physical)) {
        const auto& reg = kModeLayouts[*index];
        if (bytes != 8u)
            throw DeviceRefusal(std::string("GS ") + reg.name + " write of " + std::to_string(bytes) +
                                " byte(s) is not modeled (64-bit stores only)");
        if ((value & ~reg.valid) != 0u)
            throw DeviceRefusal(std::string("GS ") + reg.name + " " + hex(value, 16) + ": reserved bits " +
                                hex(value & ~reg.valid, 16) + " set; their effect is not documented");
        if (physical == kGsSmode1) {
            // SINT = 1 holds video sync (ps2sdk sets PRST|SINT, then clears
            // both to start a TV mode); no HBLANK is generated meanwhile.
            LineState next{};
            const auto cmod = static_cast<unsigned>((value >> kSmode1CmodShift) & 3u);
            if ((value & kSmode1Sint) != 0u) {
                next = {Sync::Stopped, 0u, {}};
            } else if (cmod == 2u || cmod == 3u) {
                const auto period =
                    kEeCyclesPerBusCycle * (cmod == 2u ? kBusCyclesPerNtscLine : kBusCyclesPerPalLine);
                // A running clock with the same line length keeps its phase;
                // a restarted or retimed one starts its lines now (hypothesis).
                const bool same = line_.sync == Sync::Running && line_.clock.period == period;
                next = {Sync::Running, cmod, {same ? line_.clock.epoch : now, period}};
            } else {
                next = {Sync::UnsupportedMode, cmod, {}};
            }
            change_line_state(next, guard);
        }
        if (physical == kGsSyncv && mode_[*index] != value) ++generation_;
        mode_[*index] = value;
        return;
    }
    if (physical == kGsCsr) {
        if (bytes != 8u && bytes != 4u)
            throw DeviceRefusal("GS CSR write of " + std::to_string(bytes) +
                                " byte(s) is not modeled (32/64-bit stores at 0x12001000 only)");
        if ((value & ~kCsrActions) != 0u)
            throw DeviceRefusal("GS CSR " + hex(value, 16) + ": reserved or read-only bits " +
                                hex(value & ~kCsrActions, 16) + " set; their write effect is not documented");
        if ((value & kCsrReset) != 0u) {
            // GS reset: the privileged registers return to their power-on
            // state (PCSX2 clears them too); the line clock restarts as NTSC.
            change_line_state({Sync::Running, 2u, {now, kEeCyclesPerBusCycle * kBusCyclesPerNtscLine}}, guard);
            mode_ = {};
            ++generation_;
        }
        // SIGNAL/FINISH/HSINT/VSINT/EDWINT: 1 clears the event and re-enables
        // it. This model never raises GS events (no GIF, no GS interrupts),
        // so there is nothing to clear. FLUSH: the GIF FIFO is not modeled
        // and therefore holds nothing to flush.
        csr_written_ = value;
        return;
    }
    throw DeviceRefusal("no register model");
}

GsRegisters::LineClock GsRegisters::line_clock() const {
    switch (line_.sync) {
        case Sync::Running:
            return line_.clock;
        case Sync::Stopped:
            throw DeviceRefusal("GS video sync is held (SMODE1.SINT = 1): no HBLANK is generated");
        case Sync::UnsupportedMode:
            break;
    }
    throw DeviceRefusal("HBLANK timing for SMODE1.CMOD = " + std::to_string(line_.cmod) +
                        " is not modeled (NTSC 2 and PAL 3 only)");
}

GsRegisters::VerticalClock GsRegisters::vertical_clock() const {
    VerticalClock clock;
    switch (line_.sync) {
        case Sync::Stopped:
            clock.state = VerticalClock::State::Held;
            return clock;
        case Sync::UnsupportedMode:
            clock.reason = "VBLANK timing for SMODE1.CMOD = " + std::to_string(line_.cmod) +
                           " is not modeled (NTSC 2 and PAL 3 only)";
            return clock;
        case Sync::Running:
            break;
    }
    const auto& syncv = mode_[(kGsSyncv - kGsSmode1) / 16u];
    if (!syncv) {
        clock.reason = "GS SYNCV not written since reset: the vertical timing is unknown";
        return clock;
    }
    const auto v = *syncv;
    const auto vfp = v & 0x3FFu, vfpe = (v >> 10) & 0x3FFu, vbp = (v >> 20) & 0xFFFu, vbpe = (v >> 32) & 0x3FFu,
               vdp = (v >> 42) & 0x7FFu, vs = (v >> 53) & 0x7FFu;
    const auto field = vfp + vfpe + vbp + vbpe + vdp + vs;
    if (vdp == 0u || field == vdp) {
        clock.reason = "GS SYNCV " + hex(v, 16) + " has no display or no blanking half-lines";
        return clock;
    }
    if (line_.clock.period % 2u != 0u) {
        clock.reason = "the line length is not a whole number of EE cycles per half-line";
        return clock;
    }
    clock.state = VerticalClock::State::Running;
    clock.epoch = line_.clock.epoch;
    clock.half_line = line_.clock.period / 2u;
    clock.field = static_cast<std::uint32_t>(field);
    clock.blank = static_cast<std::uint32_t>(field - vdp);
    return clock;
}

std::uint64_t GsRegisters::VerticalClock::next_edge(std::uint64_t from, bool end) const {
    const auto period = half_line * field;
    const auto first = epoch + (end ? half_line * blank : 0u);
    if (from <= first) return first;
    const auto fields = (from - first + period - 1u) / period;
    return first + fields * period;
}

std::optional<std::uint64_t> GsRegisters::latched(std::uint32_t physical) const {
    if (const auto index = mode_index(physical)) return mode_[*index];
    if (physical == kGsCsr) return csr_written_;
    return std::nullopt;
}
} // namespace ps2native
