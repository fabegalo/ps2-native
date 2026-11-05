#pragma once
// EE memory controller (MCH) registers 0x1000F400..0x1000F4FF: the serial
// interface to the Direct RDRAM chain, where MCH_RICM (0x1000F430) starts one
// serial control transaction and MCH_DRD (0x1000F440) holds its data, and the
// control registers the BIOS memory setup uses (below).
//
// MCH_RICM layout, as the BIOS serial helpers build it (fields of the SRQ and
// SA packets of the RDRAM data sheet): SDEV4..0 bits 0-4, SBC bit 5, SOP3..0
// bits 6-9, SDEV5 bit 10, SA11..0 bits 16-27, bit 31 start (write) / busy
// (read); the BIOS polls bit 31 before and after each transaction.
//
// Hypotheses: SCK runs at the data sheet's maximum serial rate (tCYCLE1 =
// 1000 ns), so a transaction stays busy for its SCK length at that rate; the
// MCH is idle with RICM fields 0 after reset; read data is zero-extended in
// MCH_DRD.
//
// A request is validated when MCH_RICM is written (a refusal names that store
// and changes nothing), but its effect on the RDRAMs and its SRD data become
// visible only when it completes: SWR data reaches the device in the last
// packets and SRD data travels back in the last one.
//
// RICM SOP 1110 (reserved as an RDRAM opcode) is the MCH's SIO pin
// initialization, InitDev step 1 (hypothesis): the BIOS sends it once, in
// broadcast with SDEV/SA 0, after configuring F480/F490 and before the first
// SETR, the one InitDev step without an SRQ encoding (CMD framing 1100).
//
// Control registers F400, F410, F420, F450, F460, F480, F490, F4A0 and F4C0
// (no public document; names by address). The BIOS memory setup writes them
// with values from its MCH table and the RDRAM timing and reads F400 back to
// modify one bit; they are modeled as latches that read back what was written
// (reads before any write are refused, except F400, see reset()) and have no
// modeled effect beyond that.
// F410 is a command register: the BIOS writes 0 before the setup, writes bit
// 31 alone and polls until bit 31 reads 0 (no timeout), then writes a timing
// word with bit 31 clear. Bit 31 written as 1 starts an operation that keeps
// it set for kMchControlBusySck SCK cycles (hypothesis: the operation and its
// length are unknown; any finite length satisfies the firmware). Other offsets
// in the block and every other access shape are refused.
#include "ps2native/device_timing.hpp"
#include "ps2native/rdram.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace ps2native {
inline constexpr std::uint32_t kMchBase = 0x1000F400u;
inline constexpr std::uint32_t kMchEnd = 0x1000F500u;
inline constexpr std::uint32_t kMchRicm = 0x1000F430u;
inline constexpr std::uint32_t kMchDrd = 0x1000F440u;
inline constexpr std::uint32_t kMchConfig = 0x1000F400u;
inline constexpr std::uint32_t kMchControl = 0x1000F410u;
inline constexpr std::uint32_t kMchRicmBusy = 1u << 31;
inline constexpr std::uint32_t kMchRicmSdevHigh = 1u << 10;
inline constexpr std::uint32_t kMchRicmFields = 0x0FFF07FFu;
// Bits the BIOS SRD helper leaves set from a stale register value (it masks
// an uninitialized t1, which still holds 0xB000F430 from the SWR helper), so
// bits 12-15 and 28-29 reach the MCH in every SRD the BIOS issues. The BIOS
// works on hardware, so they are taken as unused: accepted and ignored
// (hypothesis). Bits 11 and 30 stay refused.
inline constexpr std::uint32_t kMchRicmIgnored = 0x3000F000u;
// RICM SOP of the SIO pin initialization (see above).
inline constexpr std::uint8_t kMchSioPinInit = 0xEu;
// Length of the F410 operation (hypothesis): one SRQ packet.
inline constexpr std::uint32_t kMchControlBusySck = 16u;
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
    friend struct CheckpointAccess;
public:
    explicit MchRegisters(std::size_t rdram_devices = kPs2RdramDevices) : chain_(rdram_devices) {}
    // Hardware reset: RDRAMs at power-on, no transaction in progress.
    void reset();
    [[nodiscard]] std::uint64_t read(std::uint32_t physical, std::size_t bytes, std::uint64_t now);
    void write(std::uint32_t physical, std::size_t bytes, std::uint64_t value, std::uint64_t now);
    // The RDRAM chain as of EE cycle `now`: a transaction changes it only when it completes.
    [[nodiscard]] const RdramChain& chain(std::uint64_t now) const noexcept {
        return pending_ && now >= busy_until_ ? *pending_ : chain_;
    }

private:
    void settle(std::uint64_t now);

    RdramChain chain_;                     // state before the transaction in progress
    std::optional<RdramChain> pending_;    // state once it completes
    std::optional<std::uint16_t> pending_read_; // SRD data it returns
    std::uint32_t ricm_{};                 // fields of the last transaction, bit 31 excluded
    std::uint64_t busy_until_{};           // EE cycle the transaction completes
    std::optional<std::uint16_t> write_data_; // SD for the next SWR
    std::optional<std::uint16_t> read_data_;  // SD of the last SRD
    std::optional<std::uint64_t> setr_done_;  // completion of the last SETR
    std::array<std::optional<std::uint32_t>, 16> control_{}; // F400..F4F0 latches, bit 31 of F410 excluded
    std::uint64_t control_busy_until_{};   // EE cycle the F410 operation completes
};
} // namespace ps2native
