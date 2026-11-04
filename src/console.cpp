#include "ps2native/console.hpp"

#include <array>

namespace ps2native {
namespace {
// Mainboards, chip revisions and shipped ROMVERs from the PS2Ident database;
// D_ENABLER values from PS2Ident logs ("EE_F520"). The three BIOS revisions
// keep the same MCH table and the 2006-09 one adds only the 0x4322 key, the
// value read on EE 4.3 consoles. MECHACON version, ModelID and ConModelID
// and the SSBUS (DEV9) revision from the database entry of the same model,
// mainboard and ROMVER.
// MPUBoardID 0xFFFF on both SCPH-70004/GH-032 profiles is measured in
// PS2Ident.db, revision 4b84d1882b47fbcdcff7d34ef36e9e726512b2e2.
// The matching records also report ROMVER 0200EC20040614, GS REV 0x1B/ID 0x55.
// Their measured SPEED rev1/capabilities/rev8 are 0x13/0x0B/0x02.
// PS2Ident.dev9GetHardwareInfo stores all three as complete u16 samples.
constexpr std::array kProfiles{
    ConsoleHardware{"SCPH-70012", "GH-032-22", "0200AC20040614", 0x00003101u, ProfileEvidence::SameChipRevision,
                    "EE 3.1 / GS 1.11 board; 0x3101 read on SCPH-50001 (EE 3.1, GS 1.09) and "
                    "SCPH-55000 (EE 3.1, GS 1.11)", 0x24u, "PS2Ident database entry of this model and ROMVER",
                    MechaconInfo{{0x01u, 6u, 4u, 0u}, {0x2Bu, 0xD4u, 0x20u}, {0x2Au, 0xD4u}, NvmLayout::Dragon}, std::uint8_t{0x32}},
    ConsoleHardware{"SCPH-70012", "GH-035-11", "0200AC20040614", std::nullopt, ProfileEvidence::Unknown,
                    "EE 4.2 / GS 1.12 board; no log of this board, and the table holds several EE 4.x "
                    "keys (0x4000, 0x4110, 0x4111, 0x4212) with two different configurations", 0x24u, "PS2Ident database entry of this model and ROMVER",
                    MechaconInfo{{0x01u, 6u, 0u, 0u}, {0x2Bu, 0xD4u, 0x20u}, {0x2Au, 0xD4u}, NvmLayout::Dragon}, std::uint8_t{0x32}},
    // SCPH-70004 ships on two GH-032 boards with the same EE 3.1 / GS 1.11,
    // IOP, SSBUS and model IDs; the database entries differ only in the
    // MECHACON minor version (6.2 on GH-032-11, 6.4 on GH-032-32).
    ConsoleHardware{"SCPH-70004", "GH-032-11", "0200EC20040614", 0x00003101u, ProfileEvidence::SameChipRevision,
                    "EE 3.1 / GS 1.11 board; 0x3101 read on SCPH-50001 (EE 3.1, GS 1.09) and "
                    "SCPH-55000 (EE 3.1, GS 1.11)", 0x24u, "PS2Ident database entry of this model and ROMVER",
                    MechaconInfo{{0x02u, 6u, 2u, 0u}, {0x27u, 0xD4u, 0x20u}, {0x26u, 0xD4u}, NvmLayout::Dragon}, std::uint8_t{0x32}, std::uint16_t{0xFFFF}, std::array<std::uint8_t,2>{0x1B,0x55}, std::array<std::uint16_t,3>{0x13,0x0B,0x02}},
    ConsoleHardware{"SCPH-70004", "GH-032-32", "0200EC20040614", 0x00003101u, ProfileEvidence::SameChipRevision,
                    "EE 3.1 / GS 1.11 board; 0x3101 read on SCPH-50001 (EE 3.1, GS 1.09) and "
                    "SCPH-55000 (EE 3.1, GS 1.11)", 0x24u, "PS2Ident database entry of this model and ROMVER",
                    MechaconInfo{{0x02u, 6u, 4u, 0u}, {0x27u, 0xD4u, 0x20u}, {0x26u, 0xD4u}, NvmLayout::Dragon}, std::uint8_t{0x32}, std::uint16_t{0xFFFF}, std::array<std::uint8_t,2>{0x1B,0x55}, std::array<std::uint16_t,3>{0x13,0x0B,0x02}},
    ConsoleHardware{"SCPH-77008", "GH-051", "0220EC20060210", 0x00004111u, ProfileEvidence::Measured,
                    "PS2Ident log of an SCPH-77008 (EE 4.2, GS 1.12), MechaResearch/MechaPwn issue 80", 0x30u, "PS2Ident database entry of this model and ROMVER",
                    MechaconInfo{{0x05u, 6u, 10u, 0u}, {0x5Bu, 0xD4u, 0x20u}, {0x5Au, 0xD4u}, NvmLayout::Dragon}, std::uint8_t{0x31}},
    ConsoleHardware{"SCPH-90000", "GH-070", "0220JC20060905", 0x00004322u, ProfileEvidence::SameChipRevision,
                    "EE 4.3 / GS 1.14 board; 0x4322 read on two SCPH-90001 (EE 4.3, GS 1.14 with ROMVER "
                    "0220AC20060905, and GS 1.15)", 0x30u, "PS2Ident database entry of this model and ROMVER",
                    MechaconInfo{{0x00u, 6u, 12u, 0u}, {0x78u, 0xD4u, 0x20u}, {0x77u, 0xD4u}, NvmLayout::Dragon}, std::uint8_t{0x31}},
    ConsoleHardware{"SCPH-90006", "GH-070", "0220HC20060905", 0x00004322u, ProfileEvidence::SameChipRevision,
                    "EE 4.3 / GS 1.14 board; 0x4322 read on two SCPH-90001 (EE 4.3, GS 1.14 with ROMVER "
                    "0220AC20060905, and GS 1.15)", 0x30u, "PS2Ident database entry of this model and ROMVER",
                    MechaconInfo{{0x04u, 6u, 12u, 0u}, {0x79u, 0xD4u, 0x20u}, {0x78u, 0xD4u}, NvmLayout::Dragon}, std::uint8_t{0x31}},
};
} // namespace

std::span<const ConsoleHardware> known_console_hardware() noexcept { return kProfiles; }

const ConsoleHardware* find_console_hardware(std::string_view name) noexcept {
    const auto colon = name.find(':');
    const auto model = name.substr(0, colon);
    const auto board = colon == std::string_view::npos ? std::string_view{} : name.substr(colon + 1);
    const ConsoleHardware* found = nullptr;
    for (const auto& profile : kProfiles) {
        if (profile.model != model || (!board.empty() && profile.mainboard != board)) continue;
        if (found != nullptr) return nullptr; // the model ships on several boards: name one
        found = &profile;
    }
    return found;
}

const char* profile_evidence_name(ProfileEvidence evidence) noexcept {
    switch (evidence) {
        case ProfileEvidence::Measured: return "measured";
        case ProfileEvidence::SameChipRevision: return "same chip revision (hypothesis)";
        case ProfileEvidence::Unknown: return "not measured";
    }
    return "?";
}
} // namespace ps2native
