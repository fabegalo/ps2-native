#pragma once
#include "ps2native/elf.hpp"
#include "ps2native/io.hpp"
#include <array>
    // RUNTIME_HELPERS_BEGIN
#include <optional>
    // RUNTIME_HELPERS_END
#include <string>

namespace ps2native {
struct DiscEntry {
    std::string name;
    std::uint32_t lba{}, size{};
    bool directory{};
};
// Read-only ISO 9660 user sectors. No disc copies, firmware search or host
// filesystem paths from the guest. Compressed images/CUE are not accepted here.
class DiscImage {
    friend struct CheckpointAccess;
public:
    explicit DiscImage(const std::filesystem::path& path);
    // RUNTIME_HELPERS_BEGIN
    // Immutable input, not checkpoint payload; bindings pin it alongside the disc hash.
    // Explicit physical-layout experiment: user sectors do not carry DVD PFI.
    struct DiagnosticDvdGeometry { std::uint32_t layer0_sectors; bool opposite_track_path; };
    DiscImage(const std::filesystem::path& path,
              std::optional<DiagnosticDvdGeometry> diagnostic_geometry);
    // RUNTIME_HELPERS_END
    void read_sector(std::uint32_t lba, std::span<std::uint8_t,2048> out);
    [[nodiscard]] std::vector<DiscEntry> list(std::string_view directory = {});
    [[nodiscard]] std::vector<std::uint8_t> read(std::string_view path);
    [[nodiscard]] std::uint32_t file_size(std::string_view path);
    [[nodiscard]] std::string boot_path(); // BOOT2 only; PS-X BOOT is refused
    [[nodiscard]] ElfImage boot_elf();
    [[nodiscard]] const std::string& volume_id() const noexcept { return volume_id_; }
    [[nodiscard]] std::uint64_t image_size() const noexcept { return file_.size(); }
    [[nodiscard]] std::uint32_t user_sector_count() const noexcept {return sectors_;}
    // RUNTIME_HELPERS_BEGIN
    [[nodiscard]] const auto& diagnostic_dvd_geometry() const noexcept { return diagnostic_geometry_; }
    // RUNTIME_HELPERS_END
private:
    [[nodiscard]] std::vector<DiscEntry> entries(const DiscEntry& directory);
    [[nodiscard]] DiscEntry find(std::string_view path);
    [[nodiscard]] DiscEntry record(std::span<const std::uint8_t> bytes);
    InputFile file_;
    std::size_t sector_bytes_{2048}, data_offset_{};
    std::uint32_t sectors_{};
    DiscEntry root_;
    std::string volume_id_;
    // RUNTIME_HELPERS_BEGIN
    std::optional<DiagnosticDvdGeometry> diagnostic_geometry_;
    // RUNTIME_HELPERS_END
};
} // namespace ps2native
