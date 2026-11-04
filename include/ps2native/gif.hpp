#pragma once
// GIF (ps2tek "GIF", "GIF I/O"; ps2sdk gif_tags.h): the EE writes GIF packets
// for PATH3 directly into the GIF FIFO at 0x10006000, one quadword per SQ.
// PATH3 PACKED (including PRE and vertex unpacking), REGLIST (two registers
// per quadword, final padding discarded), IMAGE/IMAGE2 and chained tags.
//
// Timing: the GS takes each quadword as it arrives, so the FIFO never holds
// data (FQC = 0; hypothesis: no back-pressure is modeled, and the firmware
// polls FQC = 0 before refilling). VIF1 DIRECT also uses this parser as PATH2;
// interleaving paths is refused until arbitration is implemented. PATH1/VU1
// XGKICK is not implemented.
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
    void set_vif_path3_mask(bool masked) noexcept { vif_path3_mask_ = masked; }
    // One quadword written to the GIF FIFO (PATH3).
    void write_fifo(std::uint64_t low, std::uint64_t high, GsRegisters& gs);
    void write_path2(std::uint64_t low, std::uint64_t high, GsRegisters& gs);
    // One line per completed GIF packet (for traces and tests).
    [[nodiscard]] const std::vector<std::string>& packets() const noexcept { return packets_; }

private:
    void write_path(std::uint64_t low, std::uint64_t high, GsRegisters& gs, unsigned path);
    unsigned path_{3};
    void consume();
    void packed(unsigned descriptor, std::uint64_t low, std::uint64_t high, GsRegisters& gs);
    std::uint32_t ctrl_{}, mode_{};
    bool vif_path3_mask_{};
    // Current PATH3 packet: a GIFtag was taken and loops remain.
    bool in_packet_{};
    bool end_of_packet_{};
    std::uint32_t loops_left_{};
    std::uint32_t nreg_{};
    std::uint64_t regs_{};
    std::uint32_t reg_index_{};
    std::uint32_t written_{}; // A+D writes in this packet
    unsigned format_{};
    std::uint32_t q_{0x3F800000u};
    std::vector<std::string> packets_;
};
} // namespace ps2native
