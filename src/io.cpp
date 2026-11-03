#include "ps2native/io.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fstream>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace ps2native {
namespace {
[[noreturn]] void fail(const char* operation, const std::filesystem::path& path,
                       std::error_code error = {}) {
    throw std::runtime_error(std::string(operation) + " '" + path_utf8(path) + "'" +
                             (error ? ": " + error.message() : ""));
}
std::error_code stream_error() { return {errno ? errno : EIO, std::generic_category()}; }
void make_directories(const std::filesystem::path& native) {
    std::error_code error;
    std::filesystem::create_directories(native, error);
    if (error) fail("Cannot create directories", native, error);
}
} // namespace

std::string path_utf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return {utf8.begin(), utf8.end()};
}
std::filesystem::path native_io_path(const std::filesystem::path& input) {
    const auto& text = input.native();
    if (text.empty() || text.find(std::filesystem::path::value_type{}) != text.npos)
        throw std::runtime_error("Invalid filesystem path: empty or contains NUL");
#ifdef _WIN32
    // Extended paths are literal: don't collapse '..', change separators, or
    // prepend a second prefix. The total Win32 length includes the NUL.
    if (text.size() >= 32767u) fail("Path exceeds the Win32 extended-length limit", input);
    if (text.starts_with(L"\\\\?\\")) return input;
    const DWORD needed = GetFullPathNameW(text.c_str(), 0, nullptr, nullptr);
    if (!needed) fail("Cannot resolve path", input, {static_cast<int>(GetLastError()), std::system_category()});
    std::wstring absolute(needed, L'\0');
    const DWORD length = GetFullPathNameW(text.c_str(), needed, absolute.data(), nullptr);
    if (!length || length >= needed) fail("Cannot resolve path (working directory changed or invalid path)", input);
    absolute.resize(length);
    if (absolute.starts_with(L"\\\\.\\")) fail("Device namespace is not a file path", input);
    std::wstring extended = absolute.starts_with(L"\\\\")
        ? L"\\\\?\\UNC\\" + absolute.substr(2) : L"\\\\?\\" + absolute;
    if (extended.size() >= 32767u) fail("Path exceeds the Win32 extended-length limit", input);
    return std::filesystem::path(std::move(extended));
#else
    return input;
#endif
}
void create_directories(const std::filesystem::path& path) { make_directories(native_io_path(path)); }
bool file_exists(const std::filesystem::path& path) {
    const auto native = native_io_path(path);
    std::error_code error;
    const bool result = std::filesystem::exists(native, error);
    if (error) fail("Cannot check file", native, error);
    return result;
}
std::filesystem::file_time_type file_write_time(const std::filesystem::path& path) {
    const auto native = native_io_path(path);
    std::error_code error;
    const auto result = std::filesystem::last_write_time(native, error);
    if (error) fail("Cannot read file timestamp", native, error);
    return result;
}
bool remove_file(const std::filesystem::path& path) {
    const auto native = native_io_path(path);
    std::error_code error;
    const bool removed = std::filesystem::remove(native, error);
    if (error) fail("Cannot remove file", native, error);
    return removed;
}
void remove_directory_tree(const std::filesystem::path& path) {
    const auto native = native_io_path(path);
    std::error_code error;
    std::filesystem::remove_all(native, error);
    if (error) fail("Cannot remove test directory", native, error);
}
std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    const auto native = native_io_path(path);
    errno = 0;
    std::ifstream file(native, std::ios::binary | std::ios::ate);
    if (!file) fail("Cannot open input file", native, stream_error());
    const auto end = file.tellg();
    if (end < 0 || end > static_cast<std::streamoff>(128 * 1024 * 1024))
        fail("Input too large or unreadable (128 MiB limit)", native);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
    file.seekg(0);
    if (!file || (!data.empty() && !file.read(reinterpret_cast<char*>(data.data()), end)))
        fail("Could not read complete input file", native, stream_error());
    return data;
}
bool write_binary_if_changed(const std::filesystem::path& path, std::span<const std::uint8_t> data) {
    const auto native = native_io_path(path); // resolve once for compare, mkdir and write
    std::error_code error;
    const auto status = std::filesystem::status(native, error);
    if (error && error != std::errc::no_such_file_or_directory) fail("Cannot inspect output file", native, error);
    if (std::filesystem::exists(status)) {
        if (!std::filesystem::is_regular_file(status)) fail("Output is not a regular file", native);
        const auto size = std::filesystem::file_size(native, error);
        if (error) fail("Cannot inspect output size", native, error);
        if (size == data.size()) {
            errno = 0;
            std::ifstream previous(native, std::ios::binary);
            if (!previous) fail("Cannot compare existing output", native, stream_error());
            std::array<std::uint8_t, 64 * 1024> chunk;
            bool same = true;
            for (std::size_t at = 0; at < data.size();) {
                const auto count = std::min(chunk.size(), data.size() - at);
                if (!previous.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(count)))
                    fail("Cannot compare complete output", native, stream_error());
                if (!std::equal(chunk.begin(), chunk.begin() + count, data.begin() + at)) { same = false; break; }
                at += count;
            }
            if (same) return false;
        }
    }
    if (!native.parent_path().empty()) make_directories(native.parent_path());
    errno = 0;
    std::ofstream out(native, std::ios::binary | std::ios::trunc);
    if (!out || (!data.empty() && !out.write(reinterpret_cast<const char*>(data.data()),
                                             static_cast<std::streamsize>(data.size()))) || !out.flush())
        fail("Cannot write output file", native, stream_error());
    out.close();
    if (!out) fail("Cannot close output file", native, stream_error());
    return true;
}
bool write_text_if_changed(const std::filesystem::path& path, std::string_view data) {
    return write_binary_if_changed(path, {reinterpret_cast<const std::uint8_t*>(data.data()), data.size()});
}
} // namespace ps2native
