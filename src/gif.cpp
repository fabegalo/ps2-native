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
} // namespace

void Gif::reset() {
    ctrl_ = 0u;
    mode_ = 0u;
    vif_path3_mask_ = false;
    path1_requested_ = false;
    in_packet_ = false;
    end_of_packet_ = false;
    loops_left_ = 0u;
    q_ = 0x3F800000u;
    packets_.clear();
}

std::uint32_t Gif::read_stat() const {
    std::uint32_t stat = 0u;
    if ((mode_ & kModeMaskPath3) != 0u) stat |= kStatM3r;
    if (vif_path3_mask_) stat |= 2u; // M3P, independent of GIF_MODE.M3R.
    if ((mode_ & kModeIntermittent) != 0u) stat |= kStatImt;
    if ((ctrl_ & kCtrlStop) != 0u) stat |= kStatPse;
    // Hypothesis: while a PATH3 packet still expects quadwords, PATH3 is the
    // active path and the GIF is outputting (APATH = 3, OPH = 1).
    if (in_packet_) stat |= kStatOph | (path_ << kStatApathShift);
    if(path1_requested_ && !(in_packet_ && path_==1u))stat|=1u<<8u;
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
        path1_requested_ = false;
    }
    ctrl_ = value & kCtrlStop;
}

void Gif::write_mode(std::uint32_t value) {
    if ((value & ~(kModeMaskPath3 | kModeIntermittent)) != 0u)
        throw DeviceRefusal("GIF_MODE = " + hex(value, 8) + ": only bits 0 and 2 are modeled");
    mode_ = value;
}

void Gif::write_fifo(std::uint64_t low, std::uint64_t high, GsRegisters& gs) {
    write_path(low, high, gs, 3u);
}
void Gif::write_path2(std::uint64_t low, std::uint64_t high, GsRegisters& gs) {
    write_path(low, high, gs, 2u);
}
void Gif::write_path1(std::uint64_t low,std::uint64_t high,GsRegisters& gs) {
    write_path1_observed({low,0},{high,0},gs);
}
void Gif::write_path1_observed(ObservedDoubleword low,ObservedDoubleword high,GsRegisters& gs) {
    if(!path1_requested_)throw DeviceRefusal("GIF PATH1 transfer without a VU1 request");
    if(low.unknown || high.unknown) {
        std::uint64_t allowed_low=0,allowed_high=0;
        if(in_packet_ && loops_left_ && format_==0u && ((regs_>>(reg_index_*4u))&15u)==1u) {
            // PACKED RGBA's byte fields remain opaque in the GS color latch.
            // Every other bit in its four words is padding (EE manual 7.2.2).
            allowed_low=allowed_high=~0ull;
        }
        if(in_packet_ && loops_left_ && format_==0u &&
           ((regs_>>(reg_index_*4u))&15u)==2u) {
            // GS manual v6 3.4.4, RGBAQ/ST (pp.52,119,123): the low
            // eight mantissa bits of S, T AND Q are discarded on input.
            // PACKED STQ's fourth word is unused (EE manual 7.2.2).
            allowed_low=0x000000FF000000FFull;
            allowed_high=0xFFFFFFFF000000FFull;
        }
        if((low.unknown&~allowed_low) || (high.unknown&~allowed_high))
            throw DeviceRefusal("PATH1 consumes unknown VU1 packet bits in a retained GIF/GS field");
    }
    write_path(low.value&~low.unknown,high.value&~high.unknown,gs,1u,low.unknown,high.unknown);
    if(!in_packet_)path1_requested_=false;
}
void Gif::write_path(std::uint64_t low, std::uint64_t high, GsRegisters& gs, unsigned path,
                     std::uint64_t unknown_low,std::uint64_t unknown_high) {
    const auto shown = [&] { return "GIF FIFO quadword " + hex(high, 16) + "_" + hex(low, 16); };
    if ((ctrl_ & kCtrlStop) != 0u) throw DeviceRefusal(shown() + " while GIF_CTRL.PSE stops the GIF: not modeled");
    if(path!=1u && path1_requested_ && !(in_packet_ && path_==path))
        throw DeviceRefusal("GIF new packet waits for pending PATH1; CPU FIFO backpressure is not modeled");
    if (path == 3u && ((mode_ & kModeMaskPath3) != 0u || vif_path3_mask_) && !(in_packet_ && path_==3u))
        throw DeviceRefusal(shown() + " with PATH3 masked (GIF_MODE.M3R or VIF.MSKPATH3): FIFO backpressure not modeled");
    if (in_packet_ && path_ != path) throw DeviceRefusal("GIF concurrent paths: arbitration not modeled");
    path_ = path;
    if (!in_packet_ || loops_left_ == 0u) {
        // GIFtag.
        const auto nloop = static_cast<std::uint32_t>(low & 0x7FFFu);
        const bool eop = ((low >> 15) & 1u) != 0u;
        const bool pre = ((low >> 46) & 1u) != 0u;
        const auto flg = static_cast<unsigned>((low >> 58) & 3u);
        auto nreg = static_cast<std::uint32_t>((low >> 60) & 0xFu);
        if (nreg == 0u) nreg = 16u;
        // EE manual 7.2.2 (p.151): bits 16-45 have no field in the
        // GIFtag; ps2tek calls them unused. They carry no control/data
        // semantics and do not alter NLOOP/EOP, PRE, PRIM, FLG or NREG.
        if (nloop && flg < 2u)
            for (std::uint32_t i = 0; i < nreg; ++i) {
                const auto descriptor = (high >> (4u * i)) & 15u;
                if (descriptor == 0xBu || (flg == 1u && descriptor == 0xEu))
                    throw DeviceRefusal(shown() + ": invalid register descriptor for GIF format");
            }
        if (nloop && pre && flg == 0u) gs.general_write(0x00u, (low >> 47) & 0x7FFu);
        if (!in_packet_) written_ = 0u;
        in_packet_ = nloop != 0u || !eop;
        end_of_packet_ = eop;
        loops_left_ = nloop;
        nreg_ = nreg;
        regs_ = high;
        reg_index_ = 0u;
        format_ = flg;
        q_ = 0x3F800000u;
        if (!in_packet_) packets_.push_back("PATH" + std::to_string(path_) + " packet: " + std::to_string(written_) + " A+D writes");
        return;
    }
    if (format_ >= 2u) {
        gs.image_write(low, high);
        ++written_;
        // IMAGE counts quadwords, irrespective of NREG.
        reg_index_ = nreg_ - 1u;
        consume();
        return;
    }
    if (format_ == 0u) {
        packed(static_cast<unsigned>((regs_ >> (reg_index_ * 4u)) & 15u), low, high, gs,unknown_low,unknown_high);
        consume();
        return;
    }
    // REGLIST: each half is a raw 64-bit GS register, odd final half is padding.
    for (const auto data : {low, high}) {
        if (loops_left_ == 0u) break;
        const auto descriptor = static_cast<std::uint8_t>((regs_ >> (reg_index_ * 4u)) & 15u);
        if (descriptor != 15u) gs.general_write(descriptor, data);
        ++written_;
        consume();
    }
}
void Gif::consume() {
    if (++reg_index_ == nreg_) {
        reg_index_ = 0u;
        if (--loops_left_ == 0u && end_of_packet_) {
            in_packet_ = false;
            packets_.push_back("PATH" + std::to_string(path_) + " packet: " + std::to_string(written_) + " register/image writes");
        }
    }
}
void Gif::packed(unsigned d, std::uint64_t lo, std::uint64_t hi, GsRegisters& gs,
                 std::uint64_t unknown_low,std::uint64_t unknown_high) {
    if (d == 0xEu) {
        if (hi & ~0xFFull) throw DeviceRefusal("GIF A+D bits 72-127 set; their effect is not documented");
        gs.general_write(static_cast<std::uint8_t>(hi), lo);
    } else if (d == 1u) {
        const auto rgba = (lo & 255u) | (((lo >> 32) & 255u) << 8) |
                          ((hi & 255u) << 16) | (((hi >> 32) & 255u) << 24);
        const auto unknown=(unknown_low&255u)|(((unknown_low>>32)&255u)<<8)|
                           ((unknown_high&255u)<<16)|(((unknown_high>>32)&255u)<<24);
        gs.general_write_observed(1u, {rgba | (std::uint64_t{q_} << 32),unknown});
    } else if (d == 2u) {
        gs.general_write(2u, lo); q_ = static_cast<std::uint32_t>(hi)&0xFFFFFF00u;
    } else if (d == 3u) {
        gs.general_write(3u, (lo & 0x3FFFu) | (((lo >> 32) & 0x3FFFu) << 16));
    } else if (d == 4u || d == 5u || d == 0xCu || d == 0xDu) {
        auto index = static_cast<std::uint8_t>(d);
        const auto xy = (lo & 0xFFFFu) | (((lo >> 32) & 0xFFFFu) << 16);
        std::uint64_t zf = hi << 32;
        if (d == 4u || d == 0xCu) zf = (((hi >> 4) & 0xFFFFFFu) << 32) | (((hi >> 36) & 255u) << 56);
        if (hi & (1ull << 47)) index |= 8u;
        gs.general_write(index, xy | zf);
    } else if (d == 0xAu) {
        gs.general_write(0xAu, ((hi >> 36) & 255u) << 56);
    } else if (d != 15u) {
        gs.general_write(static_cast<std::uint8_t>(d), d == 0u ? lo & 0x7FFu : lo);
    }
    ++written_;
}
} // namespace ps2native
