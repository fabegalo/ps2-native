#pragma once
#include "ps2native/elf.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ps2native {
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

// ROM0 as an executable image at its reset-vector address (KSEG1 0xBFC00000),
// so the AOT can translate statically reachable firmware code.
[[nodiscard]] ElfImage bios_code_image(const BiosImage& bios);
inline constexpr std::uint32_t kEeResetVector = 0xBFC00000u;
} // namespace ps2native
