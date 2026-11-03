#pragma once
// EE memory controller (MCH) registers 0x1000F400..0x1000F4FF. Only the serial
// interface to the Direct RDRAM chain is modeled: MCH_RICM (0x1000F430) starts
// one serial control transaction and MCH_DRD (0x1000F440) holds its data.
//
// MCH_RICM layout, as the BIOS serial helpers build it (fields of the SRQ and
// SA packets of the RDRAM data sheet): SDEV4..0 bits 0-4, SBC bit 5, SOP3..0
// bits 6-9, SDEV5 bit 10, SA11..0 bits 16-27, bit 31 start (write) / busy
// (read); the BIOS polls bit 31 before and after each transaction.
//
// Hypotheses: SCK runs at the data sheet's maximum serial rate (tCYCLE1 =
// 1000 ns), so a transaction stays busy for its SCK length at that rate; the
// MCH is idle with RICM fields 0 after reset; read data is zero-extended in
// MCH_DRD. Everything else in the block (F400, F410 command register, F420,
// F450-F4C0) and every other access shape is refused.
#include "ps2native/device_timing.hpp"
#include "ps2native/rdram.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace ps2native {
inline constexpr std::uint32_t kMchBase = 0x1000F400u;
inline constexpr std::uint32_t kMchEnd = 0x1000F500u;
inline constexpr std::uint32_t kMchRicm = 0x1000F430u;
inline constexpr std::uint32_t kMchDrd = 0x1000F440u;
inline constexpr std::uint32_t kMchRicmBusy = 1u << 31;
inline constexpr std::uint32_t kMchRicmSdevHigh = 1u << 10;
inline constexpr std::uint32_t kMchRicmFields = 0x0FFF07FFu;
// Retail consoles carry 32 MiB of main RAM as two 128-Mbit Direct RDRAMs.
inline constexpr std::size_t kPs2RdramDevices = 2u;

// EE cycles a transaction of `sck` SCK cycles keeps MCH_RICM busy.
[[nodiscard]] constexpr std::uint64_t mch_busy_cycles(std::uint32_t sck) noexcept {
    return (static_cast<std::uint64_t>(sck) * kEeClockHz + kRdramSckMaxHz - 1u) / kRdramSckMaxHz;
}

[[nodiscard]] constexpr std::uint32_t mch_ricm(std::uint8_t sop, bool broadcast, std::uint8_t sdev,
                                               std::uint16_t sa) noexcept {
    return kMchRicmBusy | (static_cast<std::uint32_t>(sa & 0xFFFu) << 16) |
           (static_cast<std::uint32_t>(sop & 0xFu) << 6) | (broadcast ? 1u << 5 : 0u) | (sdev & 0x1Fu);
}

class MchRegisters {
public:
    explicit MchRegisters(std::size_t rdram_devices = kPs2RdramDevices) : chain_(rdram_devices) {}
    // Hardware reset: RDRAMs at power-on, no transaction in progress.
    void reset();
    [[nodiscard]] std::uint64_t read(std::uint32_t physical, std::size_t bytes, std::uint64_t now);
    void write(std::uint32_t physical, std::size_t bytes, std::uint64_t value, std::uint64_t now);
    [[nodiscard]] const RdramChain& chain() const noexcept { return chain_; }

private:
    RdramChain chain_;
    std::uint32_t ricm_{};                 // fields of the last transaction, bit 31 excluded
    std::uint64_t busy_until_{};           // EE cycle the transaction completes
    std::optional<std::uint16_t> write_data_; // SD for the next SWR
    std::optional<std::uint16_t> read_data_;  // SD of the last SRD
    std::optional<std::uint64_t> setr_done_;  // completion of the last SETR
};
} // namespace ps2native
