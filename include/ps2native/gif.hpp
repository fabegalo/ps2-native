#pragma once
// GIF (ps2tek "GIF", "GIF I/O"; ps2sdk gif_tags.h): the EE writes GIF packets
// for PATH3 directly into the GIF FIFO at 0x10006000, one quadword per SQ.
// Modeled: GIF_CTRL (reset, temporary stop), GIF_MODE (PATH3 mask,
// intermittent mode), GIF_STAT reads, and PATH3 packets of GIFtags in PACKED
// mode whose register descriptors are A+D (0xE), which write GS general
// registers (GsRegisters::general_write). Everything that would move pixels or
// draw (PRE, REGLIST, IMAGE, other descriptors, vertex kicks) is refused.
//
// Timing: the GS takes each quadword as it arrives, so the FIFO never holds
// data (FQC = 0; hypothesis: no back-pressure is modeled, and the firmware
// polls FQC = 0 before refilling). PATH1 and PATH2 (VU1 XGKICK, VIF1 DIRECT)
// are never active: no VU or VIF is modeled and no DMA channel can start.
#include "ps2native/device_timing.hpp"
#include "ps2native/gs_registers.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ps2native {
inline constexpr std::uint32_t kGifCtrl = 0x10003000u;
inline constexpr std::uint32_t kGifMode = 0x10003010u;
inline constexpr std::uint32_t kGifStat = 0x10003020u;
inline constexpr std::uint32_t kGifFifo = 0x10006000u;

class Gif {
public:
    void reset();
    [[nodiscard]] std::uint32_t read_stat() const;
    void write_ctrl(std::uint32_t value);
    void write_mode(std::uint32_t value);
    // One quadword written to the GIF FIFO (PATH3).
    void write_fifo(std::uint64_t low, std::uint64_t high, GsRegisters& gs);
    // One line per completed GIF packet (for traces and tests).
    [[nodiscard]] const std::vector<std::string>& packets() const noexcept { return packets_; }

private:
    std::uint32_t ctrl_{}, mode_{};
    // Current PATH3 packet: a GIFtag was taken and loops remain.
    bool in_packet_{};
    bool end_of_packet_{};
    std::uint32_t loops_left_{};
    std::uint32_t nreg_{};
    std::uint64_t regs_{};
    std::uint32_t reg_index_{};
    std::uint32_t written_{}; // A+D writes in this packet
    std::vector<std::string> packets_;
};
} // namespace ps2native
