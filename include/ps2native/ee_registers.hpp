#pragma once
// Device registers the EE boot path models: the EE registers at physical
// 0x10000000..0x1000FFFF, the GS privileged registers (GsRegisters, which also
// clock HBLANK for the EE timers), the MCH serial interface to the RDRAM chain
// (MchRegisters), IOP RAM as the EE sees it at 0x1C000000 (Iop, run up to the
// EE time on each access) and, in the IOP register window
// 0x1F800000..0x1F9FFFFF, only the tool board id byte. Each register documents what is known and what
// is a hypothesis; every other address, access size or mode still fails with a
// reason.
#include "ps2native/console.hpp"
#include "ps2native/device_timing.hpp"
#include "ps2native/gs_registers.hpp"
#include "ps2native/iop.hpp"
#include "ps2native/mch.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace ps2native {
// 0x1F803204: tool (DTL-T) board id, read as a byte by the EE RDRAM module and
// the IOP EECONF module. Retail consoles have no tool board; 0xFF (bus pulled
// high) is a hypothesis backed by 0xBF803800 reading 0xFFFF on every retail
// console in the PS2Ident database. The firmware only checks for tool board
// classes (high nibble 1-3, or 0x60/0x61), which 0xFF is not.
inline constexpr std::uint32_t kToolBoardIdPhysical = 0x1F803204u;
inline constexpr std::uint8_t kRetailToolBoardId = 0xFFu;
// DEV9 interface registers 0x1F801470/0x1F801472 (ps2sdk dev9regs.h names
// them only by address). The RDRAM module pulses them (1472 = 1, 1470 = 0,
// 1472 = 0, ~555-iteration delay, 1472 = 1); their function is undocumented.
inline constexpr std::uint32_t kDev9Reg1470 = 0x1F801470u;
inline constexpr std::uint32_t kDev9Reg1472 = 0x1F801472u;
// EE SIO (debug UART), register names from ps2sdk sio.h: LCR 0x1000F100,
// LSR F110, IER F120, ISR F130, FCR F140, BGR F150, TXFIFO F180, RXFIFO F1C0.
inline constexpr std::uint32_t kSioLcr = 0x1000F100u;
inline constexpr std::uint32_t kSioLsr = 0x1000F110u;
inline constexpr std::uint32_t kSioIer = 0x1000F120u;
inline constexpr std::uint32_t kSioIsr = 0x1000F130u;
inline constexpr std::uint32_t kSioFcr = 0x1000F140u;
inline constexpr std::uint32_t kSioBgr = 0x1000F150u;
inline constexpr std::uint32_t kSioTxFifo = 0x1000F180u;
// DMAC D_ENABLER (EE User's Manual): read-only view of D_ENABLEW; its low half
// identifies the board (see console.hpp).
inline constexpr std::uint32_t kDmacEnabler = 0x1000F520u;
// EE interrupt controller (ps2tek): I_STAT 0x1000F000 (pending sources 0-14,
// write 1 to clear) and I_MASK 0x1000F010 (write 1 to reverse a bit).
// 0x1F801900..0x1F80190F: bytes the EE kernel writes at "Initialize GS"
// (0x190C = 7, 0x190D/0x1904/0x1905/0x190A/0x190B = 0, 0x1907 = 0x12). No
// public documentation names them (ps2tek, ps2sdk and PCSX2's IOP register
// definitions do not; PCSX2 keeps unknown IOP-page writes as plain storage).
inline constexpr std::uint32_t kUndocumented190x = 0x1F801900u;
// 0x1A000000..0x1A00000F: the SCPH-70012 kernel writes 16-bit values here at
// the same "Initialize GS" step (0x1A000008 = 3, 0x1A000004 = 1) where the
// later kernels write the 0x1F801900 block; equally undocumented (PCSX2 calls
// it "ba0", ignores writes and answers one read address with a counter).
inline constexpr std::uint32_t kUndocumented1A = 0x1A000000u;
inline constexpr std::uint32_t kIntcStat = 0x1000F000u;
inline constexpr std::uint32_t kIntcMask = 0x1000F010u;
inline constexpr std::uint32_t kIntcSources = 0x7FFFu;

class EeRegisters {
public:
    // State after a hardware reset of the console `hardware` (nullptr: none
    // selected, so board-specific registers refuse reads), with the IOP at its
    // reset vector in `rom0` (empty: no IOP execution); the clock is attached
    // separately.
    void reset(const ConsoleHardware* hardware = nullptr, std::span<const std::uint8_t> rom0 = {});
    // EE cycle counter (EEState::retired_instructions during boot).
    void set_clock(const std::uint64_t* ee_cycles) noexcept { clock_ = ee_cycles; }
    [[nodiscard]] std::uint64_t read(std::uint32_t physical, std::size_t bytes);
    void write(std::uint32_t physical, std::size_t bytes, std::uint64_t value);
    [[nodiscard]] const GsRegisters& gs() const noexcept { return gs_; }
    [[nodiscard]] const MchRegisters& mch() const noexcept { return mch_; }
    [[nodiscard]] const Iop& iop() const noexcept { return iop_; }
    // INTC sources enabled in I_MASK (an interrupt reaches COP0 IP2 when one
    // of them is pending).
    [[nodiscard]] std::uint32_t intc_mask() const noexcept { return intc_mask_; }
    // Runs the IOP up to the current EE time.
    void sync_iop();
    // Bytes the guest sent through the EE SIO TX FIFO since reset.
    [[nodiscard]] const std::string& sio_output() const noexcept { return sio_output_; }

private:
    // EE timers T0..T3 (Tn_COUNT/MODE/COMP/HOLD at 0x10000000 + n * 0x800).
    struct Timer {
        std::uint32_t mode{};
        std::uint16_t count_at_anchor{};
        std::uint64_t anchor_cycle{};
        std::optional<std::uint16_t> compare; // unset until written
    };
    [[nodiscard]] std::uint64_t now() const;
    [[nodiscard]] std::uint16_t timer_count(const Timer& timer) const;
    void write_timer(unsigned index, unsigned reg, std::uint32_t value);
    [[nodiscard]] std::uint32_t read_timer(unsigned index, unsigned reg);

    std::array<Timer, 4> timers_{};
    GsRegisters gs_;
    MchRegisters mch_;
    Iop iop_;
    // 0x1000F500: unidentified register written by the BIOS reset code.
    std::optional<std::uint32_t> f500_;
    // DEV9 0x1F801470/0x1F801472: write latches, same rule as 0x1000F500.
    std::array<std::optional<std::uint16_t>, 2> dev9_147x_{};
    // EE SIO line setup (LCR, FCR, BGR) as last written; transmission is
    // instantaneous (hypothesis: baud timing is not modeled).
    std::array<std::optional<std::uint32_t>, 3> sio_setup_{};
    std::string sio_output_;
    std::uint32_t intc_mask_{};
    // 0x1F801900..0x1F80190F as written (byte granularity), no modeled effect.
    std::array<std::optional<std::uint8_t>, 16> undocumented_190x_{};
    // 0x1A000000..0x1A00000F, same rule.
    std::array<std::optional<std::uint8_t>, 16> undocumented_1a_{};
    const std::uint64_t* clock_{};
    const ConsoleHardware* hardware_{};
};
} // namespace ps2native
