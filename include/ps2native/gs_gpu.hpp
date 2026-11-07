#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace ps2native {
class GsVram;
enum class GsGpuApi { Vulkan, DirectX };
// A validated primitive from the existing GS setup. No guest state lives in
// this packet. The Vulkan shader consumes the same 32-bit, tightly packed ABI.
struct GsGpuCommand {
    std::int32_t left{}, top{}, right{}, bottom{};
    std::int32_t x0{}, y0{}, x1{}, y1{}, x2{}, y2{};
    // Triangle flags: bit 0 geometry, bit 1 experimental AA1, bit 2 guest ABE.
    std::uint32_t triangle{}, color{}, z{}, base{}, bw{}, psm{}, zbase{}, zpsm{};
    std::uint32_t test{}, frame_mask{}, alpha{}, alpha_fix{}, scanmask{};
    std::uint32_t fba{}, fba_known{}, conditional{}, saturate{}, pabe{}, blending{}, depth_write{};
    // Immutable draw-time sampling state. STQ is proved and converted by the
    // shared setup; the host sampler never supplies GS rounding semantics.
    std::uint32_t texture_flags{}, tbp{}, tbw{}, tpsm{}, tw{}, th{};
    std::uint32_t clamp_u{}, clamp_v{}, minu{}, maxu{}, minv{}, maxv{}, texa{};
    std::int32_t u0{}, v0{}, u1{}, v1{}, u2{}, v2{};
    std::array<std::uint32_t,256> palette{};
    std::array<std::uint32_t,8> palette_known{};
};
static_assert(sizeof(GsGpuCommand) == 1252u);

struct GsGpuResult { std::uint64_t revisions{}, pixel_writes{}; };
// Word-aligned portions of the single logical VRAM and its known-bit mask.
// Ranges are validated by the backend before any work is submitted.
struct GsGpuRange { std::uint32_t first_word{}, word_count{}; };
struct GsGpuStats {
    std::uint64_t batches{}, primitives{}, upload_bytes{}, readback_bytes{};
    std::uint64_t upload_ns{}, submit_ns{}, wait_ns{}, readback_ns{}, gpu_ns{};
    std::uint64_t upload_syncs{}, readback_syncs{}, submissions{}, waits{};
    std::uint64_t storage_bytes{}, result_upload_bytes{}, result_readback_bytes{};
    std::uint64_t command_upload_bytes{};
    bool gpu_timestamps{};
};
class GsGpu {
public:
    virtual ~GsGpu() = default;
    // Explicit complete materialization for direct callers. The resident
    // owner, if any, is safely detached before this standalone operation.
    GsGpuResult execute(std::span<std::uint32_t> words,
        std::span<std::uint32_t> known, std::span<const GsGpuCommand> commands);
    // Commands execute in order and complete before returning. Only the
    // requested ranges are copied; other device bytes remain resident.
    // Empty command spans permit upload/readback-only synchronization.
    virtual GsGpuResult execute(std::span<std::uint32_t> words,
        std::span<std::uint32_t> known, std::span<const GsGpuCommand> commands,
        std::span<const GsGpuRange> uploads, std::span<const GsGpuRange> readbacks) = 0;
    [[nodiscard]] virtual const GsGpuStats& stats() const noexcept = 0;
    [[nodiscard]] virtual const std::string& device_name() const noexcept = 0;
private:
    friend class GsVram;
    mutable GsVram* vram_owner_{};
};
// Missing build support, loader or suitable device is reported explicitly.
// A failure after successful selection propagates from execute, never falls
// back silently with a partly executed batch.
[[nodiscard]] std::shared_ptr<GsGpu> create_gs_gpu(std::string& unavailable_reason,
    GsGpuApi api=GsGpuApi::Vulkan) noexcept;
} // namespace ps2native
