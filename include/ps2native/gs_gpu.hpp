#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace ps2native {
enum class GsGpuApi { Vulkan, DirectX };
// A validated primitive from the existing GS setup. No guest state lives in
// this packet. The Vulkan shader consumes the same 32-bit, tightly packed ABI.
struct GsGpuCommand {
    std::int32_t left{}, top{}, right{}, bottom{};
    std::int32_t x0{}, y0{}, x1{}, y1{}, x2{}, y2{};
    std::uint32_t triangle{}, color{}, z{}, base{}, bw{}, psm{}, zbase{}, zpsm{};
    std::uint32_t test{}, frame_mask{}, alpha{}, alpha_fix{}, scanmask{};
    std::uint32_t fba{}, fba_known{}, conditional{}, saturate{}, pabe{}, blending{}, depth_write{};
};
static_assert(sizeof(GsGpuCommand) == 120u);

struct GsGpuResult { std::uint64_t revisions{}, pixel_writes{}; };
struct GsGpuStats {
    std::uint64_t batches{}, primitives{}, upload_bytes{}, readback_bytes{};
    std::uint64_t upload_ns{}, submit_ns{}, wait_ns{}, readback_ns{}, gpu_ns{};
    bool gpu_timestamps{};
};
class GsGpu {
public:
    virtual ~GsGpu() = default;
    // Synchronous materialization boundary. Commands execute in order. The
    // caller owns the one logical VRAM; the device allocation is a cache.
    virtual GsGpuResult execute(std::span<std::uint32_t> words,
        std::span<std::uint32_t> known, std::span<const GsGpuCommand> commands) = 0;
    [[nodiscard]] virtual const GsGpuStats& stats() const noexcept = 0;
    [[nodiscard]] virtual const std::string& device_name() const noexcept = 0;
};
// Missing build support, loader or suitable device is reported explicitly.
// A failure after successful selection propagates from execute, never falls
// back silently with a partly executed batch.
[[nodiscard]] std::shared_ptr<GsGpu> create_gs_gpu(std::string& unavailable_reason,
    GsGpuApi api=GsGpuApi::Vulkan) noexcept;
} // namespace ps2native
