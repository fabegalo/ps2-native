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
// Centralized host filesystem boundary. std::filesystem::path preserves UTF-16
// on Windows (the CLI uses wmain there), avoiding ANSI-only argv paths.
[[nodiscard]] std::vector<std::uint8_t> read_file(const std::filesystem::path& path);
// Returns true if updated; identical output remains untouched, preserving mtimes.
[[nodiscard]] bool write_text_if_changed(const std::filesystem::path& path, std::string_view data);
[[nodiscard]] bool write_binary_if_changed(const std::filesystem::path& path, std::span<const std::uint8_t> data);
} // namespace ps2native
