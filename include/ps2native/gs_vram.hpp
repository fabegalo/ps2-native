#pragma once
#include <cstdint>
#include <vector>

namespace ps2native {
class GsVram {
    friend struct CheckpointAccess;
public:
    static constexpr std::uint32_t kBytes = 4u * 1024u * 1024u;
    GsVram();
    void invalidate(); // power-on contents are unknown; CSR.RESET does not erase VRAM
    static void validate_format(unsigned psm);
    // Base in bytes, width in pixels. PSMCT32/24: 64x32 pages, 8x8 blocks,
    // 8x2 columns. Addresses wrap at 4 MiB, including non-page-aligned bases.
    // PSMCT16/16S: 64x64 pages, 16x8 blocks and 16x2 columns; raw halfwords.
    [[nodiscard]] static std::uint32_t address(std::uint32_t base, unsigned width,
                                             unsigned psm, unsigned x, unsigned y);
    [[nodiscard]] std::uint32_t read(std::uint32_t base, unsigned width, unsigned psm,
                                     unsigned x, unsigned y) const;
    void write(std::uint32_t base, unsigned width, unsigned psm, unsigned x, unsigned y,
               std::uint32_t value, std::uint32_t preserve = 0u,
               std::uint32_t unknown = 0u, bool conditional = false);
    [[nodiscard]] std::uint32_t word_at(std::uint32_t byte_address) const;
    // ZBUF PSM nibble: 0/1/2/A. Depth uses a different block permutation.
    [[nodiscard]] static std::uint32_t depth_address(std::uint32_t base, unsigned width,
                                                    unsigned psm, unsigned x, unsigned y);
    [[nodiscard]] std::uint32_t read_depth(std::uint32_t base, unsigned width, unsigned psm,
                                          unsigned x, unsigned y) const;
    void write_depth(std::uint32_t base, unsigned width, unsigned psm, unsigned x, unsigned y,
                     std::uint32_t value, bool conditional = false);
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] std::uint64_t pixel_writes() const noexcept { return pixel_writes_; }
private:
    void write_word(unsigned at, std::uint32_t value, std::uint32_t mask,
                    std::uint32_t unknown, bool conditional);
    std::vector<std::uint32_t> words_, known_;
    std::uint64_t revision_{};
    std::uint64_t pixel_writes_{};
};
} // namespace ps2native
