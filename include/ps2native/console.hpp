#pragma once
// Console hardware profiles: per-mainboard values the firmware reads from the
// hardware itself, which no ROM0 carries. A profile is tied to one model, one
// mainboard family and the ROMVER of the BIOS shipped with it (PS2Ident
// database), so a profile is never used with another revision's firmware.
//
// D_ENABLER (0x1000F520): the BIOS memory setup reads it right after reset and
// looks the full 32-bit word up in a table of MCH generations (keys 0x1201 ..
// 0x4322); a missing key fails the memory setup. Bit 16 (CPND) is the only bit
// the EE User's Manual defines; the BIOS lookup needs it 0 at that point. The
// low half differs between mainboards that share one BIOS (SCPH-70012 ships on
// GH-032 with EE 3.1 and on GH-035 with EE 4.2), so it belongs to the board.
//
// IOP PRId: the IOP reset code reads it first (< 0x10: PS1 path); CDVDMAN
// and other modules branch on the IOP revision.
//
// MECHACON: version and the IDs its NVM carries (PS2Ident database fields
// MECHACONVersion, ModelID = i.LINK ID bytes 1..3, ConModelID = console ID
// bytes 0..1), used to check that an NVM image belongs to this model.
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace ps2native {
enum class ProfileEvidence : std::uint8_t {
    Measured,         // read on a console of this model (PS2Ident log)
    SameChipRevision, // measured on other models with the same EE and GS revisions (hypothesis)
    Unknown           // no measurement: the firmware's read of it is refused
};

// EEPROM organisation of the MECHACON generation (see nvm.hpp).
enum class NvmLayout : std::uint8_t {
    Dragon // MECHACON v5 and later (SCPH-50000 onwards)
};
struct MechaconInfo {
    std::array<std::uint8_t, 4> version; // region, major, minor, system type
    std::array<std::uint8_t, 3> model_id;
    std::array<std::uint8_t, 2> con_model_id;
    NvmLayout layout;
};

struct ConsoleHardware {
    std::string_view model;      // e.g. "SCPH-77008"
    std::string_view mainboard;  // mainboard family, e.g. "GH-051"
    std::string_view romver;     // ROMVER of the BIOS this console ships with
    std::optional<std::uint32_t> d_enabler; // D_ENABLER after reset; empty: not measured
    ProfileEvidence evidence{ProfileEvidence::Measured};
    std::string_view source;     // where the value comes from
    // IOP COP0 PRId ("IOP revision" in the PS2Ident database, read there with
    // mfc0 $15 on the IOP); empty: unknown, so the IOP stops at its PRId read.
    std::optional<std::uint32_t> iop_prid{};
    std::string_view iop_source{};
    // Empty: unknown, so NVM images are refused for this profile.
    std::optional<MechaconInfo> mechacon{};
    // DEV9_R_REV (IOP 0x1F80146E, ps2sdk dev9regs.h): the SSBUS interface
    // revision PS2Ident records as "ssbus revision" (dev9GetHardwareInfo reads
    // DEV9_REG(DEV9_R_REV)); high nibble 0x2 CXD9566 (PCMCIA), 0x3 CXD9611
    // (expansion bay). Empty: unknown, so the IOP stops at its read.
    std::optional<std::uint8_t> dev9_revision{};
    // PS2Ident MPUBoardID: direct 16-bit board measurement at 0xBF803800.
    std::optional<std::uint16_t> mpu_board_id{};
    // Measured GS CSR bytes: REV (bits 16..23), ID (bits 24..31).
    std::optional<std::array<std::uint8_t,2>> gs_revision_id{};
    // PS2Ident SPEED.rev1/rev3/rev8: complete halfword measurements at
    // offsets 0x02/0x04/0x0E. Absence never falls back to another board.
    std::optional<std::array<std::uint16_t,3>> speed_revisions{};
};

[[nodiscard]] std::span<const ConsoleHardware> known_console_hardware() noexcept;
// "MODEL" (only when the model has a single profile) or "MODEL:MAINBOARD";
// nullptr when no profile matches.
[[nodiscard]] const ConsoleHardware* find_console_hardware(std::string_view name) noexcept;
[[nodiscard]] const char* profile_evidence_name(ProfileEvidence evidence) noexcept;
} // namespace ps2native
