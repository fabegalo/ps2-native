#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace ps2native {
// Owned snapshot, top-left origin, tightly packed R,G,B,A bytes (host alpha 255).
// A snapshot is not proof that a field completed. Presentation events carry
// the EE cycle of a modeled VBLANK; timing is absent when PCRTC is unknown.
struct FrameTiming {
    std::uint64_t epoch{}, half_line_cycles{};
    std::uint32_t field_half_lines{}, blank_half_lines{};
};
struct Framebuffer {
    std::uint32_t width{}, height{}, stride{};
    std::uint64_t ee_cycle{}, revision{};
    std::optional<FrameTiming> timing;
    std::vector<std::uint8_t> rgba;
};
// Dependency-free 32-bit BMP, explicit little endian and bottom-up BGRA.
void save_framebuffer_bmp(const std::filesystem::path& path, const Framebuffer& frame);
} // namespace ps2native
