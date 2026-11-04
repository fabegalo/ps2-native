#pragma once
// MECHACON NVM: the EEPROM the MECHACON keeps for each console (configuration
// blocks, model name, i.LINK and console IDs). It differs per unit, so it is
// user data: loaded from a dump of the user's own console, never bundled,
// generated or defaulted.
//
// Image format: 1024 bytes, the 512 16-bit words sceCdReadNVM addresses
// (0..511), each little-endian, as PS2Ident's DumpMECHACON_EEPROM writes them
// (PCSX2's .nvm files share this byte layout). Word N is bytes 2N (low) and
// 2N+1 (high); sceCdReadNVM returns them as status, high, low (ps2sdk).
//
// Configuration blocks (ps2sdk cdvdman): 16 bytes, of which byte 15 is the
// 8-bit sum of bytes 0..14; the firmware compares it after ReadConfig. The
// checksum is reported here, not enforced: an unused block may not hold one.
#include "ps2native/console.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ps2native {
inline constexpr std::size_t kNvmBytes = 1024;
inline constexpr std::size_t kNvmWords = kNvmBytes / 2u;
inline constexpr std::size_t kNvmConfigBlockBytes = 16;
inline constexpr unsigned kNvmConfigAreas = 3;

struct NvmArea {
    std::uint16_t offset; // byte offset of block 0
    std::uint8_t blocks;  // blocks OpenConfig may open
};
struct NvmLayoutInfo {
    std::string_view name;
    std::array<NvmArea, kNvmConfigAreas> config; // OpenConfig "block" 0, 1, 2
    std::uint16_t model_name; // 16 bytes, NUL-terminated ASCII (sceCdRM, S command 0x17)
    std::uint16_t ilink_id;   // 8 bytes (sceCdRI, S command 0x12)
    std::uint16_t console_id; // 8 bytes
    std::uint16_t region_parameters; // Slim region record; SCMD 0x36 exposes its first eight bytes.
    std::string_view source;
};
[[nodiscard]] const NvmLayoutInfo& nvm_layout_info(NvmLayout layout) noexcept;

struct NvmImage {
    std::vector<std::uint8_t> bytes;
    std::string sha256;
    NvmLayout layout{NvmLayout::Dragon};
    std::string model_name;
    // Per configuration area, per block: byte 15 matches the sum of 0..14.
    std::array<std::vector<bool>, kNvmConfigAreas> config_checksum_ok;

    [[nodiscard]] std::uint16_t word(std::uint32_t address) const;
    [[nodiscard]] std::span<const std::uint8_t, kNvmConfigBlockBytes> config_block(unsigned area,
                                                                                 unsigned index) const;
};

struct NvmLoadOptions {
    std::optional<std::string> expected_sha256; // lowercase or uppercase hex
};

// Validates `bytes` as the NVM of a console of profile `hardware`: size, the
// optional SHA-256, a MECHACON layout known for the profile, and the model
// name, model ID and console model ID the NVM carries against the profile
// (PS2Ident database values). Throws std::runtime_error naming the mismatch.
[[nodiscard]] NvmImage load_nvm_image(std::span<const std::uint8_t> bytes, const ConsoleHardware& hardware,
                                      const NvmLoadOptions& options = {});
} // namespace ps2native
