#pragma once
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace ps2native {
// Centralized host filesystem boundary. std::filesystem::path preserves UTF-16
// on Windows (the CLI uses wmain there), avoiding ANSI-only argv paths.
[[nodiscard]] std::vector<std::uint8_t> read_file(const std::filesystem::path& path);
// Returns true if updated; identical output remains untouched, preserving mtimes.
[[nodiscard]] bool write_text_if_changed(const std::filesystem::path& path, std::string_view data);
} // namespace ps2native
