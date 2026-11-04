#include "ps2native/gs_registers.hpp"

#include <iomanip>
#include <sstream>
#include <string>
#include <tuple>

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

void GsRegisters::reset(std::uint64_t now, std::optional<std::uint64_t> diagnostic_reset_syncv) {
    if (diagnostic_reset_syncv) {
        const auto v = *diagnostic_reset_syncv;
        const auto display = (v >> 42) & 0x7FFu;
        const auto blank = (v & 0x3FFu) + ((v >> 10) & 0x3FFu) + ((v >> 20) & 0xFFFu) +
                           ((v >> 32) & 0x3FFu) + ((v >> 53) & 0x7FFu);
        if (!display || !blank) throw DeviceRefusal("diagnostic GS reset SYNCV needs display and blanking half-lines");
    }
    ++generation_;
    mode_ = {};
    diagnostic_reset_syncv_ = diagnostic_reset_syncv;
    mode_.back() = diagnostic_reset_syncv_;
    reset_drawing();
    reset_events(now);
    vram_.invalidate();
    csr_written_.reset();
    line_ = {Sync::Running, 2u, {now, kEeCyclesPerBusCycle * kBusCyclesPerNtscLine}};
}

std::uint64_t GsRegisters::read(std::uint32_t physical, std::size_t bytes) const {
    if (physical == kGsCsr && bytes == 1u) {
        if (unknown_events_)
            throw DeviceRefusal("GS CSR event byte: HSINT/VSINT timing unknown since last acknowledge");
        return events_;
    }
    if (physical == kGsCsr)
        throw DeviceRefusal("GS CSR read: VSINT/FIELD/NFIELD need VBLANK timing and REV/ID depend on the "
                            "console's GS revision; not modeled");
    if ((physical == kGsSiglblid && (bytes == 4u || bytes == 8u)) ||
        (physical == kGsSiglblid + 4u && bytes == 4u)) {
        const auto shift = (physical - kGsSiglblid) * 8u;
        const auto mask = bytes == 8u ? ~0ull : 0xFFFFFFFFull << shift;
        if ((siglblid_known_ & mask) != mask)
            throw DeviceRefusal("GS SIGLBLID read consumes ID bits not written since reset");
        return (siglblid_ & mask) >> shift;
    }
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

void GsRegisters::reset_events(std::uint64_t now) {
    events_ = unknown_events_ = 0u;
    event_cycle_ = now;
    irq_pending_ = irq_unknown_ = finish_pending_ = false;
    // ID reset contents are not established for the selected chip. Masked
    // SIGNAL/LABEL writes define only the bits they actually replace.
    siglblid_ = siglblid_known_ = 0u;
}

void GsRegisters::raise_event(std::uint32_t event) {
    if ((events_ & event) == 0u && (event & ~(imr_ >> 8))) irq_pending_ = true;
    events_ |= event;
    unknown_events_ &= ~event; // a definite event resolves the flag to one
}

void GsRegisters::update_interrupt_request() {
    if (unknown_events_ & ~(imr_ >> 8) & 0x1Fu) irq_unknown_ = true;
}

void GsRegisters::advance(std::uint64_t now) {
    if (now < event_cycle_) throw DeviceRefusal("GS event clock moved backwards");
    if (now == event_cycle_) return;
    const auto from = event_cycle_ + 1u;
    if (line_.sync == Sync::Running) {
        const auto& clock = line_.clock;
        if ((now - clock.epoch) / clock.period > (event_cycle_ - clock.epoch) / clock.period)
            raise_event(kCsrHsint);
    } else if (line_.sync == Sync::UnsupportedMode && !(events_ & kCsrHsint)) {
        unknown_events_ |= kCsrHsint;
    }
    const auto clock = vertical_clock();
    if (clock.state == VerticalClock::State::Running) {
        if (clock.next_edge(from, false) <= now) raise_event(kCsrVsint);
    } else if (clock.state == VerticalClock::State::Unknown && !(events_ & kCsrVsint)) {
        unknown_events_ |= kCsrVsint;
    }
    update_interrupt_request();
    event_cycle_ = now;
}

GsRegisters::InterruptRequest GsRegisters::take_interrupt_request(std::uint64_t now) {
    advance(now);
    const InterruptRequest result{irq_pending_, irq_unknown_};
    irq_pending_ = irq_unknown_ = false;
    return result;
}

void GsRegisters::finish_transfer() {
    if (finish_pending_) {
        finish_pending_ = false;
        raise_event(kCsrFinish);
    }
}

void GsRegisters::write(std::uint32_t physical, std::size_t bytes, std::uint64_t value, std::uint64_t now,
                        const LineClockGuard& guard) {
    const auto before = std::tuple{events_, unknown_events_, event_cycle_, irq_pending_, irq_unknown_};
    try { write_impl(physical, bytes, value, now, guard); }
    catch (...) {
        std::tie(events_, unknown_events_, event_cycle_, irq_pending_, irq_unknown_) = before;
        throw;
    }
}

void GsRegisters::write_impl(std::uint32_t physical, std::size_t bytes, std::uint64_t value, std::uint64_t now,
                            const LineClockGuard& guard) {
    // Preserve events at/before a write before changing timing, masks or CSR.
    advance(now);
    if ((physical == kGsSiglblid && (bytes == 4u || bytes == 8u)) ||
        (physical == kGsSiglblid + 4u && bytes == 4u)) {
        const auto shift = (physical - kGsSiglblid) * 8u;
        const auto mask = bytes == 8u ? ~0ull : 0xFFFFFFFFull << shift;
        siglblid_ = (siglblid_ & ~mask) | ((value << shift) & mask);
        siglblid_known_ |= mask;
        return;
    }
    if (physical == kGsPmode || physical == kGsDispfb1 || physical == kGsDisplay1 ||
        physical == kGsDispfb2 || physical == kGsDisplay2 || physical == kGsBgcolor) {
        if (bytes != 8u) throw DeviceRefusal("GS display register requires a 64-bit store");
        const std::uint64_t mask = physical == kGsPmode ? 0xFFFFull :
            (physical == kGsBgcolor ? 0xFFFFFFull :
             ((physical == kGsDispfb1 || physical == kGsDispfb2) ? 0x003FFFFF000FFFFFull :
                                                               0x007FFFFF1FFFFFFFull));
        if (value & ~mask) throw DeviceRefusal("GS display register reserved bits set");
        display_[(physical - kGsPmode) / 16u] = value;
        return;
    }
    if (physical == kGsImr) {
        if (bytes != 8u && bytes != 4u) throw DeviceRefusal("GS IMR requires a 32/64-bit store");
        if ((value & ~0x7F00ull) || (value & 0x6000u) != 0x6000u)
            throw DeviceRefusal("GS IMR reserved bits (13/14 must be one)");
        const auto previous = imr_;
        imr_ = static_cast<std::uint32_t>(value);
        // An event already latched while masked becomes observable on unmask.
        const auto newly_unmasked = static_cast<std::uint32_t>((~value & previous) >> 8) & 0x1Fu;
        if (events_ & newly_unmasked) irq_pending_ = true;
        if (unknown_events_ & newly_unmasked) irq_unknown_ = true;
        return;
    }
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
            mode_.back() = diagnostic_reset_syncv_;
            reset_drawing(); // VRAM survives a soft GS reset
            reset_events(now);
            ++generation_;
        }
        // W1C does not acknowledge the EE's separate INTC latch. FLUSH drops
        // the partial upload in this synchronous GS (no internal draw queue).
        events_ &= ~static_cast<std::uint32_t>(value & 0x1Fu);
        unknown_events_ &= ~static_cast<std::uint32_t>(value & 0x1Fu);
        if (value & kCsrFlush) { transfer_ = {}; finish_pending_ = false; }
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

void GsRegisters::general_write(std::uint8_t index, std::uint64_t value) {
    const auto shown = "GS general register " + hex(index, 2) + " = " + hex(value, 16);
    switch (index) {
        // State registers (ps2sdk gs_gp.h addresses): PRIM, RGBAQ, ST, UV, TEX0,
        // CLAMP, FOG, TEX1, TEX2, XYOFFSET, PRMODECONT, PRMODE, TEXCLUT,
        // SCANMSK, MIPTBP1/2, TEXA, FOGCOL, TEXFLUSH, SCISSOR, ALPHA, DIMX,
        // DTHE, COLCLAMP, TEST, PABE, FBA, FRAME, ZBUF, BITBLTBUF, TRXPOS, TRXREG.
        case 0x00:
            if (value & ~0x7FFull) throw DeviceRefusal(shown + ": PRIM reserved bits set");
            first_vertex_.reset();
            general_[index] = value;
            return;
        case 0x01: case 0x02: case 0x03: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A:
        case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C:
        case 0x22: case 0x34: case 0x35: case 0x36: case 0x37: case 0x3B: case 0x3D: case 0x3F: case 0x40:
        case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: case 0x48: case 0x49:
        case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: case 0x50: case 0x51: case 0x52:
            general_[index] = value;
            return;
        case 0x53: { // TRXDIR: XDIR 0 host->local, 1 local->host, 2 local->local, 3 off
            start_transfer(value);
            general_[index] = value;
            return;
        }
        case 0x04: case 0x05: case 0x0C: case 0x0D:
            vertex(index, value);
            general_[index] = value;
            return;
        case 0x54:
            transfer_data(value);
            return;
        case 0x7F: return; // NOP
        case 0x60: case 0x62: {
            // A second unacknowledged SIGNAL stalls the real GS. Until GIF
            // backpressure can retain/retry it, stop before changing its ID.
            if (index == 0x60 && (events_ & kCsrSignal))
                throw DeviceRefusal(shown + ": second SIGNAL requires GIF backpressure (not modeled)");
            const auto shift = index == 0x60 ? 0u : 32u;
            const auto mask = (value >> 32) << shift;
            const auto data = (value & 0xFFFFFFFFull) << shift;
            siglblid_ = (siglblid_ & ~mask) | (data & mask);
            siglblid_known_ |= mask;
            if (index == 0x60) raise_event(kCsrSignal);
            return;
        }
        case 0x61:
            // Raster and local copies complete synchronously; an unfinished
            // host upload must finish before FINISH can be reported.
            if (transfer_.active) finish_pending_ = true;
            else raise_event(kCsrFinish);
            return;
        default:
            throw DeviceRefusal(shown + ": no GS general register at this address");
    }
}

std::optional<std::uint64_t> GsRegisters::latched(std::uint32_t physical) const {
    if (const auto index = mode_index(physical)) return mode_[*index];
    if (physical == kGsCsr) return csr_written_;
    if (physical == kGsImr) return imr_;
    if (physical <= kGsBgcolor && physical >= kGsPmode && physical % 16u == 0u)
        return display_[(physical - kGsPmode) / 16u];
    return std::nullopt;
}
} // namespace ps2native
