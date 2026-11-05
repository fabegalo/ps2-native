#define NOMINMAX
#include <windows.h>
#include "ps2native/checkpoint.hpp"
#include "checkpoint_capture_api.hpp"
#include <cstring>
extern "C" std::size_t ps2n_checkpoint_cache_clock_offset();
extern "C" __declspec(dllexport) const CaptureAbi* ps2n_capture_abi() {
    static const CaptureAbi abi{1,sizeof(ps2native::EEState),sizeof(ps2native::Memory),
        offsetof(ps2native::EEState,retired_instructions),offsetof(ps2native::EEState,pc),
        ps2n_checkpoint_cache_clock_offset(),PS2N_CAPTURE_IMAGE_SHA256};
    return &abi;
}
extern "C" __declspec(dllexport) DWORD WINAPI ps2n_capture_request(void* opaque) {
    auto& request=*static_cast<CaptureRequest*>(opaque);
    try {
        if(request.version!=1)throw std::runtime_error("capture adapter request version mismatch");
        auto& state=*reinterpret_cast<ps2native::EEState*>(static_cast<std::uintptr_t>(request.state));
        auto& memory=*reinterpret_cast<ps2native::Memory*>(static_cast<std::uintptr_t>(request.memory));
        ps2native::CheckpointContext context;
        context.scenario_sha256=request.scenario;context.host_metadata=request.metadata;context.expected_disc_sha256=request.disc_sha256;
        ps2native::BootReport boundary;boundary.outcome=ps2native::BootOutcome::BudgetExhausted;boundary.pc=state.pc;
        ps2native::save_checkpoint(std::filesystem::path(request.output),state,memory,boundary,context);
        request.status=1;return 0;
    }catch(const std::exception& error){std::strncpy(request.error,error.what(),sizeof(request.error)-1);request.status=2;return 1;}
}
