#pragma once
// Serial bus to the slim consoles' video encoder, as the EE firmware drives it.
//
// Evidence (firmware code, read from the user's ROM0 dumps; no other source
// documents the device):
// - The ROM module ROMGSCRT (GS CRT mode setup) and the kernel's copy of it
//   program a device through two read/write helpers. Both send the address
//   byte 0x42 (write) before a register number; reads then send 0x43 (read)
//   and take one data byte; writes send the register number and one value.
//   That is an I2C transaction to the 7-bit slave 0x21 with a register
//   pointer, so the device is a register-file slave on a serial bus.
// - The firmware reads register 0x7E (and 0x7F) as a chip identification:
//   the kernel spins forever on 0x1D, writes 0 to register 0xB1 on 0x1C and
//   continues otherwise; ROMGSCRT programs registers 0x30-0x33, 0x52, 0x63,
//   0x77, 0x90-0x93, ... per video mode when (0x7E & 0xFE) == 0x1C, and
//   branches on 0x7F & 0xF0 (0x10, 0x20). The registers follow the CRT mode,
//   hence a video encoder (PCSX2 calls the block "DVE"; behavioural reference
//   only, no code is taken).
// - The kernel uses it only when DEV9_R_REV has high nibble 3 (the expansion
//   bay-less SSBUS interface of the slim models); otherwise it pulses the
//   DEV9 registers 0x1F801470/0x1F801472 instead.
// - Two bus masters with different programming models carry the same
//   transactions: 16-bit registers at EE 0x1A000000 (ROMVER 0200, SCPH-700xx
//   kernels) and 8-bit registers at EE 0x1F801900 (ROMVER 0220, SCPH-77xxx
//   and 9xxxx kernels). Their register roles below are inferred from how the
//   firmware uses them (hypotheses, named per register).
//
// What is not known: the encoder's register values. A read of a slave
// register is refused unless a diagnostic assumption supplies its value (no
// console profile has a measurement); a completed transaction is never
// invented as a "chip present" answer. Transfers complete before the next
// access (hypothesis: the bus clock is not modeled), and the slave always
// acknowledges (hypothesis: the firmware is written for a board on which the
// encoder is fitted).
#include "ps2native/device_timing.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ps2native {
inline constexpr std::uint8_t kVideoEncoderWriteAddress = 0x42u; // I2C 7-bit 0x21, write
inline constexpr std::uint8_t kVideoEncoderReadAddress = 0x43u;  // the same slave, read
inline constexpr std::uint32_t kVideoEncoderMaster1A = 0x1A000000u;
inline constexpr std::uint32_t kVideoEncoderMaster190x = 0x1F801900u;
inline constexpr std::uint32_t kVideoEncoderMasterBytes = 0x20u;

// Values the user assumes for an experiment. Never defaults: a boot that uses
// them is a diagnostic, not a boot of a measured console, and says so.
struct DiagnosticAssumptions {
    // Read values of video encoder registers (register, value).
    std::vector<std::pair<std::uint8_t, std::uint8_t>> video_encoder_registers;
    // The F510 zero write has no verified hardware semantics. It may only be
    // omitted by an explicitly selected experiment, never a console default.
    bool omit_f510_zero_write{};
    // SBUS clock/BD6 configuration has incomplete hardware evidence. This
    // experiment retains initialization writes without inventing readback.
    bool sbus_configuration_latches{};
    // Unmeasured IOP DMA interrupt-control reset. Only low two bits are
    // modeled; absent means that a guest write is required before use.
    std::optional<std::uint32_t> iop_dmacinten;
    bool iop_sio2_irq_clear{}; // Unmeasured cold I_STAT.17; diagnostic only.
    bool spu2_admas_control_latch{}; // Unmeasured whole control readback after an explicit known write; no cold value.
    std::optional<std::uint32_t> spu2_adma_word_cycles; // Unmeasured shared-bus rate, cross-controller round-robin and full-bank priming/underflow wait.
    std::optional<std::uint32_t> spu2_pio_word_cycles; // Explicit unmeasured PIO throughput.
    std::optional<std::array<std::uint16_t,2>> spu2_effect_end_reset; // Unmeasured full initial EEA readbacks.
    bool spu2_quiescent_reset{}; // Unmeasured inactive SPU2 cores/voices/inputs, no transfer and clear IRQ9.
    // Experimental PCRTC timing after power-on/CSR reset, never a console
    // default. Guest SYNCV writes replace it until the next reset.
    std::optional<std::uint64_t> gs_reset_syncv;
    [[nodiscard]] bool empty() const noexcept { return video_encoder_registers.empty() && !gs_reset_syncv && !omit_f510_zero_write && !sbus_configuration_latches && !iop_dmacinten && !iop_sio2_irq_clear && !spu2_quiescent_reset && !spu2_pio_word_cycles && !spu2_effect_end_reset && !spu2_adma_word_cycles && !spu2_admas_control_latch; }
};

// The slave and the transactions that reach it (shared by both masters).
class VideoEncoderBus {
public:
    void reset(const DiagnosticAssumptions* assumptions);
    // Write transaction: `bytes` after the address byte (register pointer,
    // then at most one value). Throws DeviceRefusal for what is not modeled.
    void write(std::uint8_t address, std::span<const std::uint8_t> bytes);
    // Read transaction of `count` bytes from the register pointer.
    [[nodiscard]] std::vector<std::uint8_t> read(std::uint8_t address, std::size_t count);
    // One line per transaction, in order (for traces and tests).
    [[nodiscard]] const std::vector<std::string>& transactions() const noexcept { return log_; }
    // Value last written to `index` since reset (registers are write targets;
    // reading them back is not modeled).
    [[nodiscard]] std::optional<std::uint8_t> written(std::uint8_t index) const { return written_[index]; }

private:
    std::optional<std::uint8_t> pointer_;
    std::array<std::optional<std::uint8_t>, 256> written_{};
    std::array<std::optional<std::uint8_t>, 256> assumed_{};
    std::vector<std::string> log_;
};

// 16-bit register master at EE 0x1A000000 (offsets, firmware roles):
//   0x00 command: bit 7 start, bits 0-3 byte count (0x81, 0x82)
//   0x02 address byte (0x42 write, 0x43 read)
//   0x04, 0x08 configuration (1 and 3 at start-up), latched
//   0x06 status: bit 1 transfer done, bit 0 error; written 3 to clear both
//   0x0A enable: 0, a ~555-iteration delay, then 1 before the first use
//   0x10, 0x12, ... data bytes, one per halfword (sent, or received at 0x10)
// 8-bit register master at EE 0x1F801900:
//   0x00 data port: writes queue bytes to send, reads take a received byte
//   0x04 address byte (0x42, also for reads), 0x05 written 0
//   0x06 command: 0x01 send the queue, 0x03 receive one byte, 0x11 send the
//        queue (register write; the firmware then checks status bit 2)
//   0x08 written 8 before each transaction (clears queue and status);
//        reads: bit 0 busy, bit 2 error (the register write is retried)
//   0x18 reset: 8-bit 0, a ~555-iteration delay, then 16-bit 1
//   0x07, 0x0A-0x0D configuration (start-up values), latched
class VideoEncoderMaster {
public:
    enum class Layout : std::uint8_t { Halfword1A, Byte190x };
    explicit VideoEncoderMaster(Layout layout) : layout_(layout) {}
    void reset();
    [[nodiscard]] std::uint32_t read(std::uint32_t offset, std::size_t bytes, VideoEncoderBus& bus);
    void write(std::uint32_t offset, std::size_t bytes, std::uint32_t value, VideoEncoderBus& bus);
    [[nodiscard]] Layout layout() const noexcept { return layout_; }

private:
    [[nodiscard]] std::string name(std::uint32_t offset) const;
    void start_1a(std::uint8_t command, VideoEncoderBus& bus);
    void start_190x(std::uint8_t command, VideoEncoderBus& bus);

    Layout layout_;
    // Latched configuration and control writes, by offset.
    std::array<std::optional<std::uint16_t>, kVideoEncoderMasterBytes> latched_{};
    std::optional<std::uint16_t> status_;  // empty until cleared or set by a transfer
    std::optional<bool> enabled_;          // empty: never written since power-on
    std::array<std::optional<std::uint8_t>, 8> data_{}; // 0x1A: data halfwords 0x10..0x1E
    std::vector<std::uint8_t> send_;                    // 0x1F801900: queued bytes
    std::vector<std::uint8_t> received_;                // 0x1F801900: received bytes
};
} // namespace ps2native
