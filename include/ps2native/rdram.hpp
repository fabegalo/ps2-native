#pragma once
// Direct RDRAM serial control interface (CMD/SCK in parallel, SIO0/SIO1 daisy
// chain). Transaction formats (SRQ/SA/SINT/SD packets), the INIT control
// register, Figure 27 (SIO pin initialization) and Algorithm InitDev come from
// the Rambus "Direct RDRAM 64/72-Mbit" Advance Information sheet (3/12/98). The
// serial opcode encoding is the one of later Direct RDRAMs (Elpida EDR2518ABSE
// 288-Mbit sheet E0260E40, Table 20-2): CLRR moved from 0011 to 1011, 0011 and
// the 64-Mbit TCEN/TCAL codes are reserved. The PS2 BIOS uses that encoding:
// it issues SETR, waits, then SOP 1011 where InitDev clears the reset bit, and
// never sends 0011. Everything neither sheet defines is refused.
#include "ps2native/device_timing.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace ps2native {
// SOP3..SOP0 encodings (E0260E40 Table 20-2). Every other encoding is reserved.
inline constexpr std::uint8_t kRdramSrd = 0x0u;  // serial read of a control register
inline constexpr std::uint8_t kRdramSwr = 0x1u;  // serial write of a control register
inline constexpr std::uint8_t kRdramSetr = 0x2u; // set reset: registers take their reset values
inline constexpr std::uint8_t kRdramSetf = 0x4u; // set fast (normal) clock mode
inline constexpr std::uint8_t kRdramClrr = 0xBu; // clear reset: registers keep their values

// INIT control register (address 0x021): SDEVID4..0 in bits 0-4, PSX bit 6,
// SRP bit 7, NSR bit 8, PSR bit 9, LSR bit 10 (64/72-Mbit sheet). Bit 11 is a
// field of later parts (their INIT adds TEN/TSQ/DIS; the PS2 BIOS writes bit
// 11 as 1 in every INIT it builds): stored as written, no modeled effect.
// Bits 5 and 12-15 are refused.
inline constexpr std::uint16_t kRdramInit = 0x021u;
inline constexpr std::uint16_t kRdramInitSdevid = 0x001Fu;
inline constexpr std::uint16_t kRdramInitPsx = 1u << 6;
inline constexpr std::uint16_t kRdramInitSrp = 1u << 7;
inline constexpr std::uint16_t kRdramInitBit11 = 1u << 11;
inline constexpr std::uint16_t kRdramInitFields = 0x0FDFu;
// Reset values (SETR): SDEVID = 0x1F, SRP = 1; the other fields are undefined,
// so these are also the only INIT bits with a defined value after a SETR.
inline constexpr std::uint16_t kRdramInitReset = kRdramInitSrp | kRdramInitSdevid;

// Other control registers the PS2 BIOS writes, with the 64/72-Mbit layouts
// where the BIOS builds its values field by field the same way. Reset values
// are undefined: reads before a write are refused.
inline constexpr std::uint16_t kRdramTest34 = 0x022u; // test register (later parts): stored, no modeled effect
inline constexpr std::uint16_t kRdramDevid = 0x040u;  // DEVID4..0 in bits 0-4
inline constexpr std::uint16_t kRdramCca = 0x043u;    // CCA6..0 bits 0-6, ASYMA1..0 bits 7-8
inline constexpr std::uint16_t kRdramCcb = 0x044u;    // CCB6..0 bits 0-6, ASYMB1..0 bits 7-8
inline constexpr std::uint16_t kRdramNapx = 0x045u;   // NAPXA4..0 bits 0-4, NAPXB4..0 bits 5-9, DQS bit 10
inline constexpr std::uint16_t kRdramPdnxa = 0x046u;  // PDNXA12..0
inline constexpr std::uint16_t kRdramPdnxb = 0x047u;  // PDNXB12..0
inline constexpr std::uint16_t kRdramTparm = 0x048u;  // TCAS1..0 bits 0-1, TCLS1..0 bits 2-3, TDAC2..0 bits 4-6
inline constexpr std::uint16_t kRdramTfrm = 0x049u;   // TFRM3..0 in bits 0-3
inline constexpr std::uint16_t kRdramTrdly = 0x04Au;  // TRDLY1..0 in bits 0-1, values 0-2
inline constexpr std::uint16_t kRdramSkip = 0x04Bu;   // SKIP (128-Mbit parts; MSE/MS): only 0 is accepted
inline constexpr std::uint16_t kRdramTcycle = 0x04Cu; // TCYCLE13..0 in 64 ps units
inline constexpr std::uint16_t kRdramTest77 = 0x04Du; // test register, written with zero
inline constexpr std::uint16_t kRdramTest78 = 0x04Eu; // test register: stored, no modeled effect

// Transaction lengths in SCK cycles: SWR and SRD are four 16-bit packets
// (Figures 24/25, T0..T64); SETR/CLRR/SETF are one SRQ packet (Figure 26), and
// so is the SIO pin initialization sequence (Figure 27, T0..T16).
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

// The devices of the serial chain, in daisy-chain order (device 0 is closest
// to the controller). The controller's SIO reaches device n only while devices
// 0..n-1 repeat SIO0 to SIO1, sampled before the transaction: INIT.SRP = 1, or,
// while SRP is undefined, after the SIO pin initialization (every device
// repeats its packet, Figure 27).
//
// The PS2 BIOS programs the timing registers, numbers the chain and writes
// DEVID before the first SETR, and never writes TCYCLE, TPARM, TFRM or TRDLY
// again. So SETR is modeled as resetting only what has a defined reset value
// (INIT SDEVID and SRP) and keeping the other registers (hypothesis: the
// sheets call their reset values undefined, and the firmware relies on them).
//
// An SRD that selects no device returns 0 (hypothesis: no device drives SIO0
// during the SD packet, and the SINT packet before it is 0). The BIOS ends its
// chain enumeration on any value whose SDEVID differs from 0x1F.
class RdramChain {
public:
    struct Device {
        bool sio_ready{};      // SIO pin initialization received since power-on
        bool reset_applied{};  // a SETR has reached it since power-on
        bool reset_bit{};      // set by SETR, cleared by CLRR
        bool fast_clock{};     // SETF received
        std::uint16_t init{};
        std::uint16_t init_defined{}; // INIT bits with a defined value
        std::map<std::uint16_t, std::uint16_t> registers; // other control registers as written
        bool operator==(const Device&) const = default;
    };

    explicit RdramChain(std::size_t devices);
    // Power-on: SIO pin directions and every register undefined.
    void power_on();
    // InitDev step 1 (Figure 27): CMD framing 1100 with SIO0 driven high for
    // 16 SCK cycles, repeated from SIO0 to SIO1 by every device, establishes
    // the SIO0/SIO1 directions along the whole chain. Registers are unchanged.
    void sio_pin_initialization();
    // Runs one transaction; returns SD15..SD0 for SRD. Throws DeviceRefusal
    // for anything the data sheet does not define, with nothing modified.
    std::optional<std::uint16_t> transact(const RdramSerialRequest& request);
    [[nodiscard]] static std::uint32_t sck_cycles(std::uint8_t sop);
    [[nodiscard]] const std::vector<Device>& devices() const noexcept { return devices_; }

private:
    std::vector<Device> devices_;
};
} // namespace ps2native
