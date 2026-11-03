#pragma once
// Direct RDRAM serial control interface (CMD/SCK in parallel, SIO0/SIO1 daisy
// chain), after the Rambus "Direct RDRAM 64/72-Mbit" Advance Information data
// sheet (3/12/98): transaction formats (SRQ/SA/SINT/SD packets), serial opcodes,
// the INIT control register and Algorithm InitDev. Only what that sheet defines
// is modeled; the PS2's 128-Mbit parts may extend it (the PS2 BIOS also sends
// opcodes the 64-Mbit sheet lists as reserved), and every such case is refused.
#include "ps2native/device_timing.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ps2native {
// SOP3..SOP0 encodings (Table 13). Every other encoding is reserved.
inline constexpr std::uint8_t kRdramSrd = 0x0u;  // serial read of a control register
inline constexpr std::uint8_t kRdramSwr = 0x1u;  // serial write of a control register
inline constexpr std::uint8_t kRdramSetr = 0x2u; // set reset: registers take their reset values
inline constexpr std::uint8_t kRdramClrr = 0x3u; // clear reset: registers keep their values
inline constexpr std::uint8_t kRdramSetf = 0x4u; // set fast (normal) clock mode
inline constexpr std::uint8_t kRdramTcen = 0x6u; // temperature calibrate enable (not modeled)
inline constexpr std::uint8_t kRdramTcal = 0x7u; // temperature calibrate (not modeled)

// INIT control register (address 0x021): SDEVID4..0 in bits 0-4, PSX bit 6,
// SRP bit 7, NSR bit 8, PSR bit 9, LSR bit 10; bits 5 and 11-15 read as 0.
inline constexpr std::uint16_t kRdramInit = 0x021u;
inline constexpr std::uint16_t kRdramInitSdevid = 0x001Fu;
inline constexpr std::uint16_t kRdramInitPsx = 1u << 6;
inline constexpr std::uint16_t kRdramInitSrp = 1u << 7;
inline constexpr std::uint16_t kRdramInitFields = 0x07DFu;
// Reset values (SETR): SDEVID = 0x1F, SRP = 1; PSX/NSR/PSR/LSR are undefined,
// so these are also the only INIT bits with a defined value after a SETR.
inline constexpr std::uint16_t kRdramInitReset = kRdramInitSrp | kRdramInitSdevid;

// Transaction lengths in SCK cycles: SWR and SRD are four 16-bit packets
// (Figures 24/25, T0..T64); SETR/CLRR/SETF are one SRQ packet (Figure 26).
inline constexpr std::uint32_t kRdramSckLong = 64u;
inline constexpr std::uint32_t kRdramSckShort = 16u;
// Algorithm InitDev step 3: wait 16 SCK cycles between SETR and CLRR.
inline constexpr std::uint32_t kRdramSckSetrToClrr = 16u;
// Minimum SCK cycle time for serial control register transactions (tCYCLE1):
// 1000 ns in the 64/72-Mbit sheet and in Samsung's 128-Mbit K4R271669F sheet.
inline constexpr std::uint64_t kRdramSckMaxHz = 1'000'000u;

struct RdramSerialRequest {
    std::uint8_t sop{};      // SOP3..SOP0
    bool broadcast{};        // SBC
    std::uint8_t sdev{};     // SDEV4..SDEV0
    std::uint16_t sa{};      // SA11..SA0
    std::uint16_t sd{};      // SD15..SD0 (SWR only)
};

// The devices of one channel, in daisy-chain order (device 0 is closest to the
// controller). The controller's SIO reaches device n only while devices
// 0..n-1 repeat SIO0 to SIO1 (INIT.SRP = 1), sampled before the transaction.
class RdramChain {
public:
    struct Device {
        bool reset_applied{};  // a SETR has reached it since power-on
        bool reset_bit{};      // set by SETR, cleared by CLRR
        bool fast_clock{};     // SETF received
        std::uint16_t init{};
        std::uint16_t init_defined{}; // INIT bits with a defined value
    };

    explicit RdramChain(std::size_t devices);
    // Power-on: every register undefined until a SETR arrives.
    void power_on();
    // Runs one transaction; returns SD15..SD0 for SRD. Throws DeviceRefusal
    // for anything the data sheet does not define, with nothing modified.
    std::optional<std::uint16_t> transact(const RdramSerialRequest& request);
    [[nodiscard]] static std::uint32_t sck_cycles(std::uint8_t sop);
    [[nodiscard]] const std::vector<Device>& devices() const noexcept { return devices_; }

private:
    std::vector<Device> devices_;
};
} // namespace ps2native
