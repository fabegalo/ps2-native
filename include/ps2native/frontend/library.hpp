#pragma once
// The launcher's local library: references to user files (BIOS dumps, disc
// images, NVM) and their cached identification. Files are registered where
// they are; nothing is copied. Persisted as a versioned UTF-8 text file
// written atomically; unknown keys from newer versions are ignored.
#include "ps2native/frontend/content.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ps2native::frontend {
// What a library entry may do, as a contract the UI reads. Only states with
// evidence are ever assigned: no game is marked as booting or playable
// without a recorded test.
enum class Compatibility : std::uint8_t {
    Pending,             // identification in progress
    Missing,             // registered file not accessible
    InvalidImage,        // the image could not be read as a PlayStation disc
    UnsupportedFormat,   // compressed or other formats not readable yet
    NotPlayStation2,     // a PlayStation (PS1) disc
    RuntimeUnavailable,  // identified PS2 disc; the game runtime does not exist yet
    // Reserved for evidence from the future runtime (never assigned today):
    Boots, Menus, InGame, Playable
};
[[nodiscard]] const char* compatibility_name(Compatibility value) noexcept;
// The file itself has a problem the user can fix (missing, unreadable, compressed).
// A valid disc that is not a PS2 title (PlayStation) is identified, not an issue.
[[nodiscard]] constexpr bool needs_attention(Compatibility value) noexcept {
    return value == Compatibility::Missing || value == Compatibility::InvalidImage || value == Compatibility::UnsupportedFormat;
}

struct FileStamp {
    bool exists{};
    std::uint64_t size{};
    std::int64_t modified{}; // file_time_type ticks since its epoch (change detection only)
};
[[nodiscard]] FileStamp stamp_file(const std::filesystem::path& path);

struct LibraryEntry {
    std::string id;           // stable, derived from the normalized path
    ContentKind kind{ContentKind::Disc};
    std::filesystem::path path; // absolute, as the user knows it (no extended prefix)
    std::string title;          // display name
    FileStamp stamp;            // file state when it was last identified
    std::int64_t added{};       // Unix seconds
    bool favorite{};
    ProbeStatus status{ProbeStatus::Pending};
    std::string error;
    std::map<std::string, std::string> info;
    std::vector<std::string> details;
    // BIOS only: the console it runs as and that console's NVM.
    std::string console_profile;
    std::filesystem::path nvm_path;
    ProbeStatus nvm_status{ProbeStatus::Pending};
    std::string nvm_error;
    std::map<std::string, std::string> nvm_info;

    [[nodiscard]] std::string value(const std::string& key) const;
};
[[nodiscard]] Compatibility compatibility_of(const LibraryEntry& entry) noexcept;

[[nodiscard]] std::filesystem::path normalize_library_path(const std::filesystem::path& path);
[[nodiscard]] std::string library_id_for(const std::filesystem::path& path);
// Stores a probe in the entry (status, error, info, details, title default).
void apply_probe(LibraryEntry& entry, const ProbeResult& result, const FileStamp& stamp);
void apply_nvm_probe(LibraryEntry& entry, const ProbeResult& result);
// True when the file changed (size or time) or was never identified.
[[nodiscard]] bool needs_probe(const LibraryEntry& entry, const FileStamp& current) noexcept;

class Library {
public:
    [[nodiscard]] const std::vector<LibraryEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] LibraryEntry* find(std::string_view id) noexcept;
    [[nodiscard]] const LibraryEntry* find(std::string_view id) const noexcept;
    // The existing entry for this file, or a new pending one (second: inserted).
    std::pair<LibraryEntry*, bool> add(ContentKind kind, const std::filesystem::path& path, std::int64_t now_unix);
    bool remove(std::string_view id);
    void replace(LibraryEntry entry); // by id; appended when absent

    [[nodiscard]] std::string serialize() const;
    // Throws std::runtime_error for a file that is not a library or a newer major version.
    [[nodiscard]] static Library parse(std::string_view text);
    // An absent file is an empty library.
    [[nodiscard]] static Library load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;

private:
    std::vector<LibraryEntry> entries_;
};

// Launcher preferences. Options for components that do not exist yet are kept
// (so the contract is stable) but the UI shows them as unavailable.
struct LauncherSettings {
    std::vector<std::filesystem::path> game_folders;
    std::filesystem::path covers_folder;
    std::filesystem::path boot_tool;   // ps2n-boot override; empty: next to the launcher
    std::string active_bios;           // library id
    bool scan_folders_on_start{true};
    std::uint64_t diagnostic_budget{10'000'000};
    std::string diagnostic_trace{"events"}; // events | full
    std::string renderer{"gs-faithful"};    // gs-faithful | native (future runtime)
    bool reduce_motion{};
    std::string library_view{"grid"};       // grid | list

    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] static LauncherSettings parse(std::string_view text);
    [[nodiscard]] static LauncherSettings load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
};

// Escaping of the line format shared by the library and settings files.
[[nodiscard]] std::string escape_value(std::string_view text);
[[nodiscard]] std::string unescape_value(std::string_view text);
[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view text);
} // namespace ps2native::frontend
