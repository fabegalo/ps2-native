#pragma once
#include <cstdint>
#include <vector>

namespace ps2native {
class GsVram {
public:
    static constexpr std::uint32_t kBytes = 4u * 1024u * 1024u;
    GsVram();
    void invalidate(); // power-on contents are unknown; CSR.RESET does not erase VRAM
    static void validate_format(unsigned psm);
    // Base in bytes, width in pixels. PSMCT32/24: 64x32 pages, 8x8 blocks,
    // 8x2 columns. Addresses wrap at 4 MiB, including non-page-aligned bases.
    [[nodiscard]] static std::uint32_t address(std::uint32_t base, unsigned width,
                                             unsigned psm, unsigned x, unsigned y);
    [[nodiscard]] std::uint32_t read(std::uint32_t base, unsigned width, unsigned psm,
                                     unsigned x, unsigned y) const;
    void write(std::uint32_t base, unsigned width, unsigned psm, unsigned x, unsigned y,
               std::uint32_t value, std::uint32_t preserve = 0u);
    [[nodiscard]] std::uint32_t word_at(std::uint32_t byte_address) const;
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] std::uint64_t pixel_writes() const noexcept { return pixel_writes_; }
private:
    std::vector<std::uint32_t> words_, known_;
    std::uint64_t revision_{};
    std::uint64_t pixel_writes_{};
};
} // namespace ps2native
