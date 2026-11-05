#include "ps2native/gs_gpu.hpp"
#include "ps2native/gs_vram.hpp"
#include <exception>

namespace ps2native {
GsGpuResult GsGpu::execute(std::span<std::uint32_t> words,std::span<std::uint32_t> known,
    std::span<const GsGpuCommand> commands) {
    if(vram_owner_)vram_owner_->release_gpu();
    const GsGpuRange all{0u,static_cast<std::uint32_t>(words.size())};
    return execute(words,known,commands,std::span{&all,1u},std::span{&all,1u});
}
#ifdef PS2N_HAS_VULKAN
std::shared_ptr<GsGpu> create_gs_vulkan();
#endif
#ifdef PS2N_HAS_DIRECTX
std::shared_ptr<GsGpu> create_gs_directx();
#endif
std::shared_ptr<GsGpu> create_gs_gpu(std::string& reason,GsGpuApi api) noexcept {
    reason.clear();
    if(api==GsGpuApi::DirectX) {
#ifdef PS2N_HAS_DIRECTX
        try { return create_gs_directx(); }
        catch(const std::exception& e){reason=e.what();}
        catch(...){reason="DirectX initialization failed with an unknown host error";}
#else
        reason="DirectX GS backend was not compiled (optional Windows backend unavailable or disabled)";
#endif
        return {};
    }
#ifdef PS2N_HAS_VULKAN
    try { return create_gs_vulkan(); }
    catch (const std::exception& e) { reason = e.what(); }
    catch (...) { reason = "Vulkan initialization failed with an unknown host error"; }
#else
    reason = "Vulkan GS backend was not compiled (optional Vulkan headers unavailable or disabled)";
#endif
    return {};
}
} // namespace ps2native
