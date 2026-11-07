#pragma once
#include <cstdint>
    // HOST_INPUT_BEGIN
#include <array>
#include <memory>
#include <span>
    // HOST_INPUT_END
#include <vector>
    // HOST_INPUT_BEGIN
#include "ps2native/gs_gpu.hpp"
    // HOST_INPUT_END

namespace ps2native {
    // HOST_INPUT_BEGIN
enum class GsGpuFallback : unsigned { Texture, Antialias, Gouraud, UncertainColor,
    Addressing, Depth, Blend, TextureHazard, UnknownDestination, Count };
    // HOST_INPUT_END
class GsVram {
    friend struct CheckpointAccess;
public:
    static constexpr std::uint32_t kBytes = 4u * 1024u * 1024u;
    // HOST_INPUT_BEGIN
    static constexpr unsigned kPageBytes=8192u, kPages=kBytes/kPageBytes;
    // HOST_INPUT_END
    GsVram();
    // HOST_INPUT_BEGIN
    // Copies are materialized guest snapshots, with no host device binding.
    GsVram(const GsVram&);
    GsVram& operator=(const GsVram&);
    ~GsVram();
    GsVram(GsVram&&) noexcept;
    GsVram& operator=(GsVram&&) noexcept;
    void set_gpu_backend(std::shared_ptr<GsGpu> backend);
    [[nodiscard]] const std::shared_ptr<GsGpu>& gpu_backend() const noexcept { return gpu_; }
    void enqueue_gpu(const GsGpuCommand& command,
                     const std::array<bool,kPages>* texture_pages=nullptr);
    // Complete ordered commands without transferring resident VRAM to the CPU.
    void submit_gpu() const;
    void synchronize() const;
    void synchronize_pages(const std::array<bool,kPages>& pages) const;
    [[nodiscard]] bool page_known(unsigned page,std::uint32_t mask) const;
    [[nodiscard]] bool bits_known(unsigned byte_address,std::uint32_t mask) const;
    [[nodiscard]] std::span<const std::uint32_t> raw_words() const { synchronize(); return words_; }
    [[nodiscard]] std::span<const std::uint32_t> known_bits() const { synchronize(); return known_; }
    [[nodiscard]] std::uint64_t gpu_primitives() const noexcept { return gpu_primitives_; }
    [[nodiscard]] std::uint64_t cpu_fallbacks() const noexcept { return cpu_fallbacks_; }
    void record_cpu_fallback(GsGpuFallback reason) noexcept {
        if(gpu_){++cpu_fallbacks_;++fallback_reasons_[static_cast<unsigned>(reason)];}
    }
    [[nodiscard]] const auto& fallback_reasons() const noexcept { return fallback_reasons_; }
    [[nodiscard]] std::uint32_t read_bits(std::uint32_t base,unsigned width,unsigned psm,
                                         unsigned x,unsigned y,std::uint32_t mask) const;
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
    [[nodiscard]] std::uint64_t revision() const { submit_gpu(); return revision_; }
    [[nodiscard]] std::uint64_t pixel_writes() const { submit_gpu(); return pixel_writes_; }
private:
    // HOST_INPUT_BEGIN
    friend class GsGpu;
    void acquire_gpu() const;
    void release_gpu() const;
    void detach_gpu() noexcept;
    void synchronize_page(unsigned page) const;
    void cache_page_knowledge(unsigned page) const;
    void execute_pending(const std::array<bool,kPages>* read_pages=nullptr,unsigned read_page=kPages) const;
    // HOST_INPUT_END
    void write_word(unsigned at, std::uint32_t value, std::uint32_t mask,
                    std::uint32_t unknown, bool conditional);
    mutable std::vector<std::uint32_t> words_, known_;
    mutable std::uint64_t revision_{};
    mutable std::uint64_t pixel_writes_{};
    // HOST_INPUT_BEGIN
    std::shared_ptr<GsGpu> gpu_;
    mutable std::vector<GsGpuCommand> pending_gpu_;
    // Host cache ownership is independent of guest knowledge. Cpu includes
    // power-on unknown pages, which must be uploaded before any GPU access.
    enum class Location : std::uint8_t { Cpu, Equal, Gpu };
    mutable std::array<Location,kPages> locations_{};
    mutable std::array<bool,kPages> pending_pages_{}, pending_reads_{}, knowledge_cached_{};
    mutable std::array<std::uint32_t,kPages> page_knowledge_{};
    std::uint64_t gpu_primitives_{}, cpu_fallbacks_{};
    std::array<std::uint64_t,static_cast<unsigned>(GsGpuFallback::Count)> fallback_reasons_{};
    // HOST_INPUT_END
};
} // namespace ps2native
