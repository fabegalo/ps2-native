#include "ps2native/gs_vram.hpp"
#include "ps2native/device_timing.hpp"
#include <algorithm>
#include <string>

namespace ps2native {
GsVram::GsVram() : words_(kBytes / 4u), known_(kBytes / 4u) {}
void GsVram::invalidate() {
    std::fill(known_.begin(), known_.end(), 0u); ++revision_; pixel_writes_ = 0u;
}
void GsVram::validate_format(unsigned psm) {
    if (psm > 1u) throw DeviceRefusal("GS pixel format " + std::to_string(psm) +
                                     " is not modeled (PSMCT32/PSMCT24 only)");
}
std::uint32_t GsVram::address(std::uint32_t base, unsigned width, unsigned psm, unsigned x, unsigned y) {
    validate_format(psm);
    if (!width || width > 4032u || width % 64u || base % 256u || x > 2047u || y > 2047u)
        throw DeviceRefusal("GS VRAM address: invalid base, buffer width or coordinate");
    const auto page = (y / 32u) * (width / 64u) + x / 64u;
    const auto block = ((x >> 3) & 1u) | ((y >> 2) & 2u) | ((x >> 2) & 4u) |
                       ((y >> 1) & 8u) | ((x >> 1) & 16u);
    const auto word = (x & 1u) | ((y & 1u) << 1) | ((x & 6u) << 1) | ((y & 6u) << 3);
    return (base + page * 8192u + block * 256u + word * 4u) & (kBytes - 1u);
}
std::uint32_t GsVram::read(std::uint32_t base, unsigned width, unsigned psm, unsigned x, unsigned y) const {
    const auto at = address(base, width, psm, x, y) / 4u;
    const auto bits = psm == 1u ? 0xFFFFFFu : 0xFFFFFFFFu;
    if ((known_[at] & bits) != bits) throw DeviceRefusal("GS VRAM read: pixel contains unwritten bits");
    return words_[at] & bits;
}
void GsVram::write(std::uint32_t base, unsigned width, unsigned psm, unsigned x, unsigned y,
                   std::uint32_t value, std::uint32_t preserve) {
    const auto at = address(base, width, psm, x, y) / 4u;
    if (psm == 1u) preserve |= 0xFF000000u;
    words_[at] = (words_[at] & preserve) | (value & ~preserve);
    known_[at] |= ~preserve;
    ++revision_;
    ++pixel_writes_;
}
std::uint32_t GsVram::word_at(std::uint32_t byte_address) const {
    if (byte_address >= kBytes || byte_address % 4u) throw DeviceRefusal("GS VRAM raw address out of range");
    if (known_[byte_address / 4u] != 0xFFFFFFFFu) throw DeviceRefusal("GS VRAM read: word contains unwritten bits");
    return words_[byte_address / 4u];
}
} // namespace ps2native
