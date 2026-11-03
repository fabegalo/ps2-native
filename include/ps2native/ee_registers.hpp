#pragma once
// Device registers the EE boot path models: the EE registers at physical
// 0x10000000..0x1000FFFF, the GS privileged registers (GsRegisters, which also
// clock HBLANK for the EE timers), the MCH serial interface to the RDRAM chain
// (MchRegisters) and, in the IOP register window
// 0x1F800000..0x1F9FFFFF, only the tool board id byte. Each register documents what is known and what
// is a hypothesis; every other address, access size or mode still fails with a
// reason.
#include "ps2native/console.hpp"
#include "ps2native/device_timing.hpp"
#include "ps2native/gs_registers.hpp"
#include "ps2native/mch.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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

class EeRegisters {
public:
    // State after a hardware reset of the console `hardware` (nullptr: none
    // selected, so board-specific registers refuse reads); the clock is
    // attached separately.
    void reset(const ConsoleHardware* hardware = nullptr);
    // EE cycle counter (EEState::retired_instructions during boot).
    void set_clock(const std::uint64_t* ee_cycles) noexcept { clock_ = ee_cycles; }
    [[nodiscard]] std::uint64_t read(std::uint32_t physical, std::size_t bytes);
    void write(std::uint32_t physical, std::size_t bytes, std::uint64_t value);
    [[nodiscard]] const GsRegisters& gs() const noexcept { return gs_; }
    [[nodiscard]] const MchRegisters& mch() const noexcept { return mch_; }
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
    // 0x1000F500: unidentified register written by the BIOS reset code.
    std::optional<std::uint32_t> f500_;
    // DEV9 0x1F801470/0x1F801472: write latches, same rule as 0x1000F500.
    std::array<std::optional<std::uint16_t>, 2> dev9_147x_{};
    // EE SIO line setup (LCR, FCR, BGR) as last written; transmission is
    // instantaneous (hypothesis: baud timing is not modeled).
    std::array<std::optional<std::uint32_t>, 3> sio_setup_{};
    std::string sio_output_;
    const std::uint64_t* clock_{};
    const ConsoleHardware* hardware_{};
};
} // namespace ps2native
