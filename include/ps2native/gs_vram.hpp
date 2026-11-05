#pragma once
#include <cstdint>
    // HOST_INPUT_BEGIN
#include <memory>
#include <span>
    // HOST_INPUT_END
#include <vector>
    // HOST_INPUT_BEGIN
#include "ps2native/gs_gpu.hpp"
    // HOST_INPUT_END

namespace ps2native {
class GsVram {
    friend struct CheckpointAccess;
public:
    static constexpr std::uint32_t kBytes = 4u * 1024u * 1024u;
    GsVram();
    // HOST_INPUT_BEGIN
    // Copies are materialized guest snapshots, with no host device binding.
    GsVram(const GsVram&);
    GsVram& operator=(const GsVram&);
    GsVram(GsVram&&) noexcept = default;
    GsVram& operator=(GsVram&&) noexcept = default;
    void set_gpu_backend(std::shared_ptr<GsGpu> backend);
    [[nodiscard]] const std::shared_ptr<GsGpu>& gpu_backend() const noexcept { return gpu_; }
    void enqueue_gpu(const GsGpuCommand& command);
    void synchronize() const;
    [[nodiscard]] std::span<const std::uint32_t> raw_words() const { synchronize(); return words_; }
    [[nodiscard]] std::span<const std::uint32_t> known_bits() const { synchronize(); return known_; }
    [[nodiscard]] std::uint64_t gpu_primitives() const noexcept { return gpu_primitives_; }
    [[nodiscard]] std::uint64_t cpu_fallbacks() const noexcept { return cpu_fallbacks_; }
    void record_cpu_fallback() noexcept { if(gpu_) ++cpu_fallbacks_; }
    // HOST_INPUT_END
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
    [[nodiscard]] std::uint64_t revision() const { synchronize(); return revision_; }
    [[nodiscard]] std::uint64_t pixel_writes() const { synchronize(); return pixel_writes_; }
private:
    void write_word(unsigned at, std::uint32_t value, std::uint32_t mask,
                    std::uint32_t unknown, bool conditional);
    mutable std::vector<std::uint32_t> words_, known_;
    mutable std::uint64_t revision_{};
    mutable std::uint64_t pixel_writes_{};
    // HOST_INPUT_BEGIN
    std::shared_ptr<GsGpu> gpu_;
    mutable std::vector<GsGpuCommand> pending_gpu_;
    std::uint64_t gpu_primitives_{}, cpu_fallbacks_{};
    // HOST_INPUT_END
};
} // namespace ps2native
