#pragma once
#include "ps2native/memory.hpp"
#include <iostream>

namespace ps2native::cli {
struct GsGpuOptions {
    bool requested{};
    GsGpuApi api{GsGpuApi::Vulkan};
    std::shared_ptr<GsGpu> backend;
    bool parse(const std::string& flag) {
        if(flag=="--gs-backend=vulkan"){requested=true;api=GsGpuApi::Vulkan;}
        else if(flag=="--gs-backend=directx"){requested=true;api=GsGpuApi::DirectX;}
        else if(flag=="--gs-backend=cpu")requested=false;
        else return false;
        return true;
    }
    void attach(Memory& memory) {
        if(!requested)return;
        std::string reason;backend=create_gs_gpu(reason,api);
        if(!backend)std::cout<<"GS_BACKEND cpu; selected GPU unavailable: "<<reason<<'\n';
        else std::cout<<"GS_BACKEND experimental "<<(api==GsGpuApi::Vulkan?"Vulkan":"DirectX")<<" compute: "<<backend->device_name()<<'\n';
        memory.set_gs_gpu_backend(backend);
    }
    void print(const Memory& memory) const {
        if(!requested)return;
        memory.gs().synchronize_graphics();
        if(!backend)return;
        const auto& s=backend->stats();
        std::cout<<"GS_GPU primitives="<<s.primitives<<" batches="<<s.batches
            <<" cpu_fallbacks="<<memory.gs().vram().cpu_fallbacks()
            <<" upload_bytes="<<s.upload_bytes<<" readback_bytes="<<s.readback_bytes
            <<" gpu_ns="<<s.gpu_ns<<" upload_ns="<<s.upload_ns<<" submit_ns="<<s.submit_ns
            <<" wait_ns="<<s.wait_ns<<" readback_ns="<<s.readback_ns<<'\n';
    }
};
} // namespace ps2native::cli
