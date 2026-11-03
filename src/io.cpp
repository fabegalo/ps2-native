#include "ps2native/io.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace ps2native {
namespace {
std::filesystem::path host_path(const std::filesystem::path& input) {
#ifdef _WIN32
    // Extended Win32 path syntax bypasses legacy MAX_PATH for file I/O without
    // requiring registry changes, and preserves Unicode from the native path.
    if (input.native().rfind(L"\\\\?\\", 0) == 0) return input;
    const auto absolute = std::filesystem::absolute(input).lexically_normal();
    const auto& native = absolute.native();
    if (native.rfind(L"\\\\", 0) == 0)
        return std::filesystem::path(L"\\\\?\\UNC\\" + native.substr(2));
    return std::filesystem::path(L"\\\\?\\" + native);
#else
    return input;
#endif
}
} // namespace

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream file(host_path(path), std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open input file");
    const auto end = file.tellg();
    if (end < 0 || end > static_cast<std::streamoff>(128 * 1024 * 1024))
        throw std::runtime_error("Input too large or unreadable (128 MiB limit)");
    std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
    file.seekg(0);
    if (!data.empty() && !file.read(reinterpret_cast<char*>(data.data()), end))
        throw std::runtime_error("Could not read complete input file");
    return data;
}

bool write_text_if_changed(const std::filesystem::path& dest, std::string_view data) {
    const auto native_dest = host_path(dest);
    if (std::ifstream existing(native_dest, std::ios::binary); existing) {
        const std::string previous((std::istreambuf_iterator<char>(existing)),
                                   std::istreambuf_iterator<char>());
        if (previous == data) return false;
    }
    if (!native_dest.parent_path().empty())
        std::filesystem::create_directories(native_dest.parent_path());
    std::ofstream out(native_dest, std::ios::binary | std::ios::trunc);
    if (!out || !out.write(data.data(), static_cast<std::streamsize>(data.size())) || !out.flush())
        throw std::runtime_error("Failed writing generated C++ source");
    return true;
}
} // namespace ps2native
