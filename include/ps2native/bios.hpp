#pragma once
#include "ps2native/elf.hpp"
#include "ps2native/nvm.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ps2native {
struct ConsoleHardware;
// One ROMDIR file of a ROM0 image, with its EXTINFO fields.
struct BiosModule {
    std::string name;
    std::uint32_t offset{};           // in ROM0 (files are laid out in ROMDIR order, 16-byte aligned)
    std::uint32_t size{};
    std::uint16_t extinfo_size{};
    std::uint32_t extinfo_offset{};   // within the EXTINFO file
    std::optional<std::uint32_t> date;      // EXTINFO date field, raw
    std::optional<std::uint16_t> version;   // EXTINFO version field
    std::string comment;                    // EXTINFO comment field
};
// ROMVER file: "VVvvRKYYYYMMDD" (version, region, console kind, build date).
struct BiosVersion {
    std::string romver;
    unsigned major{};
    unsigned minor{};
    char region_code{};
    std::string region;
    char kind_code{};
    std::string kind;
    std::string date;   // YYYY-MM-DD
};
// A validated, user-supplied ROM0 dump. Never bundled or redistributed.
struct BiosImage {
    std::vector<std::uint8_t> rom0;
    std::string sha256;
    std::uint32_t romdir_offset{};
    std::vector<BiosModule> modules;
    BiosVersion version;
    [[nodiscard]] const BiosModule* find(std::string_view name) const noexcept;
};
struct BiosLoadOptions {
    // User-supplied known-good SHA-256 of their dump (64 hex digits).
    std::optional<std::string> expected_sha256;
};

// Validates size (exactly 4 MiB), the ROMDIR (RESET first, ROMDIR self-
// consistent, sequential 16-byte aligned files inside the image), EXTINFO
// (sizes and every field), ROMVER (version, region, kind, date) and the
// optional expected hash. Any inconsistency is an explicit error.
[[nodiscard]] BiosImage load_bios_rom0(std::span<const std::uint8_t> bytes,
                                       const BiosLoadOptions& options = {});

struct BrowserFirmware {
    BiosImage bios;
    NvmImage nvm;
    ElfImage program;
    std::string console_profile;
    std::optional<UnpackedEeImage> unpacked;
};
// The original OSDSYS ELF, including its original loader/decompressor when packed.
[[nodiscard]] BrowserFirmware load_browser_firmware(std::span<const std::uint8_t> rom0,
    std::string_view console_profile, std::span<const std::uint8_t> nvm,
    const BiosLoadOptions& bios_options = {}, const NvmLoadOptions& nvm_options = {});

// Read-only IOP reboot container; metadata does not imply execution of its IRX files.
struct IopRebootImage {
    std::vector<std::uint8_t> bytes;
    std::string sha256;
    std::vector<BiosModule> modules;
    [[nodiscard]] const BiosModule* find(std::string_view name) const noexcept;
};
[[nodiscard]] IopRebootImage load_iop_reboot_image(std::vector<std::uint8_t> bytes,
                                                  const BiosLoadOptions& options = {});

// Explicitly reviewed pairing, never inferred from filenames or region alone.
// The caller supplies provenance for the compatibility decision; the loader
// verifies both hashes, the console/ROMVER binding and ROM1's internal metadata.
struct Rom1Identity {
    std::string rom0_sha256, romver, model, mainboard;
    std::string sha256, build, dvd_version, provenance;
};
struct Rom1Image {
    std::vector<std::uint8_t> bytes;
    Rom1Identity identity;
};
[[nodiscard]] Rom1Image load_bios_rom1(std::span<const std::uint8_t> bytes,
    const BiosImage& bios, const ConsoleHardware& hardware, const Rom1Identity& identity);

inline constexpr std::uint32_t kEeResetVector = 0xBFC00000u;
} // namespace ps2native
