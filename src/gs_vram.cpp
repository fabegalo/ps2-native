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
                   std::uint32_t value, std::uint32_t preserve, std::uint32_t unknown, bool conditional) {
    const auto at = address(base, width, psm, x, y) / 4u;
    if (psm == 1u) preserve |= 0xFF000000u;
    write_word(at, value, ~preserve, unknown, conditional);
    if (!conditional) ++pixel_writes_; // possible writes are not claimed as proven pixels
}
void GsVram::write_word(unsigned at, std::uint32_t value, std::uint32_t mask,
                        std::uint32_t unknown, bool conditional) {
    const auto next = (words_[at] & ~mask) | (value & mask);
    auto knowledge = (known_[at] & ~mask) | (mask & ~unknown);
    // Join draw/skip branches. Only bits proved equal in both remain known.
    if (conditional) knowledge &= known_[at] & ~(words_[at] ^ next);
    words_[at] = next; known_[at] = knowledge; ++revision_;
}
std::uint32_t GsVram::depth_address(std::uint32_t base, unsigned width, unsigned psm,
                                  unsigned x, unsigned y) {
    if (psm == 0u || psm == 1u) {
        (void)address(base, width, 0, x, y);
        // XOR the block number, before adding the buffer base.
        return (base + (address(0, width, 0, x, y) ^ 6144u)) & (kBytes - 1u);
    }
    if (psm != 2u && psm != 10u) throw DeviceRefusal("GS depth format is not modeled");
    (void)address(base, width, 0, x, y); // common bounds and alignment
    const auto page = (y / 64u) * (width / 64u) + x / 64u;
    const auto block = psm == 10u
        ? ((y >> 3) & 1u) | ((x >> 3) & 2u) | ((y >> 3) & 4u) |
          ((y >> 1) & 8u) | ((x >> 1) & 16u)
        : ((y >> 3) & 1u) | ((x >> 3) & 2u) | ((y >> 2) & 4u) |
          ((x >> 2) & 8u) | ((y >> 1) & 16u);
    const auto word = (x & 1u) | ((y & 1u) << 1) | ((x & 6u) << 1) | ((y & 6u) << 3);
    return (base + page * 8192u + (block ^ 24u) * 256u + word * 4u + ((x & 8u) >> 2)) & (kBytes - 1u);
}
std::uint32_t GsVram::read_depth(std::uint32_t base, unsigned width, unsigned psm,
                               unsigned x, unsigned y) const {
    const auto address = depth_address(base, width, psm, x, y);
    const auto shift = (address & 2u) * 8u;
    const auto mask = psm == 0u ? 0xFFFFFFFFu : psm == 1u ? 0xFFFFFFu : 0xFFFFu;
    if (((known_[address / 4u] >> shift) & mask) != mask)
        throw DeviceRefusal("GS depth read: pixel contains unwritten bits");
    return (words_[address / 4u] >> shift) & mask;
}
void GsVram::write_depth(std::uint32_t base, unsigned width, unsigned psm,
                         unsigned x, unsigned y, std::uint32_t value, bool conditional) {
    const auto address = depth_address(base, width, psm, x, y);
    const auto shift = (address & 2u) * 8u;
    const auto mask = (psm == 0u ? 0xFFFFFFFFu : psm == 1u ? 0xFFFFFFu : 0xFFFFu) << shift;
    write_word(address / 4u, value << shift, mask, 0u, conditional);
}
std::uint32_t GsVram::word_at(std::uint32_t byte_address) const {
    if (byte_address >= kBytes || byte_address % 4u) throw DeviceRefusal("GS VRAM raw address out of range");
    if (known_[byte_address / 4u] != 0xFFFFFFFFu) throw DeviceRefusal("GS VRAM read: word contains unwritten bits");
    return words_[byte_address / 4u];
}
} // namespace ps2native
