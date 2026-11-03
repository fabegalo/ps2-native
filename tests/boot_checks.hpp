// Assertion helpers shared by the boot tests (M2.0, M2.1, M2.2).
#pragma once

#include "ps2native/core.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace boot_checks {
inline void require(bool ok, const std::string& reason) {
    if (!ok) throw std::runtime_error("assertion failed: " + reason);
}
template <typename F>
void rejects(F&& action, const std::string& diagnostic) {
    try { action(); }
    catch (const std::exception& error) {
        if (std::string(error.what()).find(diagnostic) != std::string::npos) return;
        throw std::runtime_error("Wrong rejection reason for '" + diagnostic + "': " + error.what());
    }
    throw std::runtime_error("Expected explicit rejection: " + diagnostic);
}
template <typename F>
void bus_fault(F&& action, ps2native::BusFault fault, const std::string& diagnostic) {
    try { action(); }
    catch (const ps2native::BusError& error) {
        require(error.fault == fault, std::string("bus fault kind for ") + diagnostic + ": " + error.what());
        require(std::string(error.what()).find(diagnostic) != std::string::npos,
                "bus fault text '" + diagnostic + "': " + error.what());
        return;
    }
    throw std::runtime_error("Expected bus fault: " + diagnostic);
}
// A modeled device refused the access (BusFault::Device with this text).
template <typename F>
void device_refused(F&& action, const std::string& diagnostic) {
    bus_fault(std::forward<F>(action), ps2native::BusFault::Device, diagnostic);
}
inline std::string hex8(std::uint32_t value) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out;
    for (int shift = 28; shift >= 0; shift -= 4) out.push_back(kDigits[(value >> shift) & 15u]);
    return out;
}
inline bool contains(const std::vector<std::string>& lines, const std::string& text) {
    return std::any_of(lines.begin(), lines.end(),
                       [&](const std::string& line) { return line.find(text) != std::string::npos; });
}
// Trace without engine markers, the stop line and the engine column, so the
// interpreter-only and hybrid runs can be compared line by line.
inline std::vector<std::string> normalized(const std::vector<std::string>& trace) {
    std::vector<std::string> lines;
    for (const auto& line : trace) {
        if (line.starts_with("E ") || line.starts_with("STOP")) continue;
        if (line.starts_with("I ")) lines.push_back(line.substr(0, 13) + "*" + line.substr(16));
        else lines.push_back(line);
    }
    return lines;
}
} // namespace boot_checks
