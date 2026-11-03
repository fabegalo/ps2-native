#pragma once
// Identification of user-supplied content for the desktop launcher. BIOS ROM0
// dumps and console NVM images go through the core parsers (load_bios_rom0,
// load_nvm_image) and console profiles; disc images through their ISO 9660
// volume descriptor and SYSTEM.CNF. Probes only read: nothing is copied,
// modified, cached outside the library file or bundled.
//
// This layer has no GUI dependency: the Qt launcher and headless tests share it.
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ps2native::frontend {
enum class ContentKind : std::uint8_t { Bios, Disc };
enum class ProbeStatus : std::uint8_t {
    Pending,     // registered, not identified yet
    Valid,       // identified by the parser of its format
    Invalid,     // the parser refused it (error names the reason)
    Unsupported, // a format the project cannot read yet (explicit, not a failure of the file)
    Missing      // the registered file is not accessible any more
};
[[nodiscard]] const char* content_kind_name(ContentKind kind) noexcept;
[[nodiscard]] std::optional<ContentKind> content_kind_from_name(std::string_view name) noexcept;
[[nodiscard]] const char* probe_status_name(ProbeStatus status) noexcept;
[[nodiscard]] std::optional<ProbeStatus> probe_status_from_name(std::string_view name) noexcept;

// Keys of ProbeResult::info. BIOS: sha256, romver, version, region,
// region_code, console_kind, build_date, romdir_files, profiles (comma-separated
// MODEL:MAINBOARD of the profiles shipped with this ROMVER). Disc: format,
// platform, media, serial, boot, disc_version, video_mode, region, volume_id,
// volume_date, sectors, cover. Both: size, warning.
struct ProbeResult {
    ProbeStatus status{ProbeStatus::Pending};
    std::string error;
    std::map<std::string, std::string> info;
    std::vector<std::string> details; // BIOS: one line per ROMDIR file
};

struct ProbeContext {
    // Optional folder with user cover images named after the serial
    // (SLUS-20946.png/.jpg/.jpeg/.webp). Images next to the disc image with its
    // stem are found too. Never downloaded or bundled.
    std::filesystem::path covers_folder;
};

// What a file most likely is, from size and extension: 4 MiB files are ROM0
// candidates, disc image extensions are discs. Empty: not a launcher content.
[[nodiscard]] std::optional<ContentKind> guess_content_kind(const std::filesystem::path& path);
[[nodiscard]] bool is_disc_image_extension(const std::filesystem::path& path);
[[nodiscard]] bool is_nvm_candidate(const std::filesystem::path& path);

// CUE sheet of a multi-track CD dump. A sheet is one disc: probe_disc on the
// .cue identifies it from its data track, and find_candidates lists the sheet
// instead of each referenced track file.
struct CueTrack {
    unsigned number{};
    std::string mode; // "MODE1/2352", "MODE2/2352", "AUDIO", ...
    std::filesystem::path file; // referenced file, resolved next to the sheet
};
struct CueSheet {
    std::vector<CueTrack> tracks;
    std::vector<std::filesystem::path> files; // distinct referenced files, in order
    // First data track (not AUDIO), or nullptr.
    [[nodiscard]] const CueTrack* data_track() const noexcept;
};
// Throws std::runtime_error for an unreadable or malformed sheet (no FILE/TRACK).
[[nodiscard]] CueSheet parse_cue_sheet(const std::filesystem::path& path);

[[nodiscard]] ProbeResult probe_bios(const std::filesystem::path& path);
[[nodiscard]] ProbeResult probe_disc(const std::filesystem::path& path, const ProbeContext& context = {});
[[nodiscard]] ProbeResult probe(ContentKind kind, const std::filesystem::path& path, const ProbeContext& context = {});

// MECHACON NVM of the console `console_profile` ("MODEL:MAINBOARD"), validated by
// load_nvm_image against that profile. info: sha256, model_name, layout,
// checksums ("0:+++ 1:+- 2:+"), console_profile.
[[nodiscard]] ProbeResult probe_nvm(const std::filesystem::path& path, std::string_view console_profile);

// Console profiles ("MODEL:MAINBOARD") of the core database shipped with `romver`.
[[nodiscard]] std::vector<std::string> console_profiles_for_romver(std::string_view romver);

// "SLUS_209.46" or "SLUS-20946" -> "SLUS-20946"; empty when not a disc serial.
[[nodiscard]] std::string normalize_serial(std::string_view boot_file);
// Region of a disc from its serial prefix (NTSC-U, PAL, NTSC-J, NTSC-K, NTSC-C); empty when unknown.
[[nodiscard]] std::string region_from_serial(std::string_view serial);

// Files under `folder` (recursive, permission-denied subtrees skipped) that are
// candidates for `kind`. Large folders are walked without reading file contents,
// except for the size check of BIOS candidates. At most `limit` results.
[[nodiscard]] std::vector<std::filesystem::path> find_candidates(const std::filesystem::path& folder, ContentKind kind,
                                                                 std::size_t limit = 10000);
} // namespace ps2native::frontend
