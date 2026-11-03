#include "ps2native/gif.hpp"

#include <iomanip>
#include <sstream>

namespace ps2native {
namespace {
std::string hex(std::uint64_t value, int digits) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(digits) << std::setfill('0') << value;
    return stream.str();
}
constexpr std::uint32_t kCtrlReset = 1u << 0, kCtrlStop = 1u << 3;
constexpr std::uint32_t kModeMaskPath3 = 1u << 0, kModeIntermittent = 1u << 2;
// GIF_STAT (ps2tek): M3R 0, M3P 1, IMT 2, PSE 3, IP3 5, P3Q 6, P2Q 7, P1Q 8,
// OPH 9, APATH 10-11, DIR 12, FQC 24-28.
constexpr std::uint32_t kStatM3r = 1u << 0, kStatImt = 1u << 2, kStatPse = 1u << 3, kStatOph = 1u << 9;
constexpr unsigned kStatApathShift = 10u;
// GIFtag (ps2sdk gif_tags.h): NLOOP 0-14, EOP 15, PRE 46, PRIM 47-57, FLG
// 58-59 (0 PACKED, 1 REGLIST, 2 and 3 IMAGE), NREG 60-63 (0 means 16); the
// high doubleword holds the 4-bit register descriptors.
constexpr unsigned kFlgPacked = 0u;
constexpr std::uint64_t kDescriptorAd = 0xEu;
} // namespace

void Gif::reset() {
    ctrl_ = 0u;
    mode_ = 0u;
    in_packet_ = false;
    end_of_packet_ = false;
    loops_left_ = 0u;
    packets_.clear();
}

std::uint32_t Gif::read_stat() const {
    std::uint32_t stat = 0u;
    if ((mode_ & kModeMaskPath3) != 0u) stat |= kStatM3r;
    if ((mode_ & kModeIntermittent) != 0u) stat |= kStatImt;
    if ((ctrl_ & kCtrlStop) != 0u) stat |= kStatPse;
    // Hypothesis: while a PATH3 packet still expects quadwords, PATH3 is the
    // active path and the GIF is outputting (APATH = 3, OPH = 1).
    if (in_packet_) stat |= kStatOph | (3u << kStatApathShift);
    return stat; // FQC = 0: the FIFO never holds data here
}

void Gif::write_ctrl(std::uint32_t value) {
    if ((value & ~(kCtrlReset | kCtrlStop)) != 0u)
        throw DeviceRefusal("GIF_CTRL = " + hex(value, 8) + ": only bits 0 (reset) and 3 (stop) are modeled");
    if ((value & kCtrlReset) != 0u) {
        // Reset drops the packet in progress. Hypothesis: GIF_MODE and the
        // stop bit return to their power-on values (ps2tek does not say).
        mode_ = 0u;
        in_packet_ = false;
        end_of_packet_ = false;
        loops_left_ = 0u;
    }
    ctrl_ = value & kCtrlStop;
}

void Gif::write_mode(std::uint32_t value) {
    if ((value & ~(kModeMaskPath3 | kModeIntermittent)) != 0u)
        throw DeviceRefusal("GIF_MODE = " + hex(value, 8) + ": only bits 0 and 2 are modeled");
    mode_ = value;
}

void Gif::write_fifo(std::uint64_t low, std::uint64_t high, GsRegisters& gs) {
    const auto shown = "GIF FIFO quadword " + hex(high, 16) + "_" + hex(low, 16);
    if ((ctrl_ & kCtrlStop) != 0u) throw DeviceRefusal(shown + " while GIF_CTRL.PSE stops the GIF: not modeled");
    if ((mode_ & kModeMaskPath3) != 0u) throw DeviceRefusal(shown + " with PATH3 masked (GIF_MODE.M3R): not modeled");
    if (!in_packet_ || loops_left_ == 0u) {
        // GIFtag.
        const auto nloop = static_cast<std::uint32_t>(low & 0x7FFFu);
        const bool eop = ((low >> 15) & 1u) != 0u;
        const bool pre = ((low >> 46) & 1u) != 0u;
        const auto flg = static_cast<unsigned>((low >> 58) & 3u);
        auto nreg = static_cast<std::uint32_t>((low >> 60) & 0xFu);
        if (nreg == 0u) nreg = 16u;
        if ((low & 0x00003FFFFFFF0000ull & ~(1ull << 15)) != 0u)
            throw DeviceRefusal(shown + ": GIFtag bits 16-45 are reserved; their effect is not documented");
        if (pre) throw DeviceRefusal(shown + ": GIFtag PRE (PRIM write) starts drawing state; drawing is not modeled");
        if (flg != kFlgPacked)
            throw DeviceRefusal(shown + ": GIFtag FLG " + std::to_string(flg) +
                                " (REGLIST or IMAGE) is not modeled (PACKED only)");
        for (std::uint32_t i = 0; i < nreg; ++i)
            if (((high >> (4u * i)) & 0xFu) != kDescriptorAd)
                throw DeviceRefusal(shown + ": register descriptor " + hex((high >> (4u * i)) & 0xFu, 1) +
                                    " is not modeled (A+D only)");
        if (!in_packet_) written_ = 0u;
        in_packet_ = nloop != 0u || !eop;
        end_of_packet_ = eop;
        loops_left_ = nloop;
        nreg_ = nreg;
        regs_ = high;
        reg_index_ = 0u;
        if (!in_packet_) packets_.push_back("PATH3 packet: " + std::to_string(written_) + " A+D writes");
        return;
    }
    // A+D: data in the low doubleword, register address in bits 64-71.
    if ((high & ~0xFFull) != 0u) throw DeviceRefusal(shown + ": A+D bits 72-127 set; their effect is not documented");
    gs.general_write(static_cast<std::uint8_t>(high), low);
    ++written_;
    if (++reg_index_ == nreg_) {
        reg_index_ = 0u;
        if (--loops_left_ == 0u && end_of_packet_) {
            in_packet_ = false;
            packets_.push_back("PATH3 packet: " + std::to_string(written_) + " A+D writes");
        }
    }
}
} // namespace ps2native
