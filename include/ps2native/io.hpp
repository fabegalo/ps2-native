#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ps2native {
// UTF-8 for options/diagnostics, never an ANSI round-trip for native paths.
[[nodiscard]] std::string path_utf8(const std::filesystem::path& path);
// Absolute extended Win32 path (drive or UNC); preserves already extended paths.
// On other hosts the path is unchanged. Performs no filesystem access.
[[nodiscard]] std::filesystem::path native_io_path(const std::filesystem::path& path);
void create_directories(const std::filesystem::path& path);
[[nodiscard]] bool file_exists(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::file_time_type file_write_time(const std::filesystem::path& path);
bool remove_file(const std::filesystem::path& path);
// Cleanup of owned synthetic test directories, not user content.
void remove_directory_tree(const std::filesystem::path& path);
// A directory reference, never a copy: junction on Windows, symlink on POSIX.
// Reuse only when the existing reference resolves to the same directory.
void ensure_directory_alias(const std::filesystem::path& alias, const std::filesystem::path& target);
[[nodiscard]] bool files_equivalent(const std::filesystem::path& first, const std::filesystem::path& second);
// Centralized host filesystem boundary. std::filesystem::path preserves UTF-16
// on Windows (the CLI uses wmain there), avoiding ANSI-only argv paths.
[[nodiscard]] std::vector<std::uint8_t> read_file(const std::filesystem::path& path);
// Returns true if updated; identical output remains untouched, preserving mtimes.
[[nodiscard]] bool write_text_if_changed(const std::filesystem::path& path, std::string_view data);
[[nodiscard]] bool write_binary_if_changed(const std::filesystem::path& path, std::span<const std::uint8_t> data);
// Writes a sibling temporary file and renames it over `path`, so a reader (or
// a crash) never observes a partially written file. Parent directories are created.
void write_file_atomically(const std::filesystem::path& path, std::span<const std::uint8_t> data);
// The path as the user knows it: native_io_path's Win32 extended prefix
// ("\\?\" or "\\?\UNC\") removed. Unchanged on other hosts. No filesystem access.
[[nodiscard]] std::filesystem::path display_path(const std::filesystem::path& path);
[[nodiscard]] std::uint64_t regular_file_size(const std::filesystem::path& path);

// Random-access reader for large user files (disc images of several GiB):
// reads only the requested ranges, never the whole file.
class InputFile {
public:
    explicit InputFile(const std::filesystem::path& path);
    InputFile(InputFile&&) noexcept;
    InputFile& operator=(InputFile&&) noexcept;
    ~InputFile();
    [[nodiscard]] std::uint64_t size() const noexcept { return size_; }
    // Reads up to out.size() bytes at `offset`; returns the bytes read (short at end of file).
    [[nodiscard]] std::size_t read_at(std::uint64_t offset, std::span<std::uint8_t> out);
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    struct Stream;
    std::filesystem::path path_;
    std::uint64_t size_{};
    Stream* stream_{};
};
} // namespace ps2native
