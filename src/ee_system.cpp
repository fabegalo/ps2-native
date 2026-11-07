#include "ps2native/ee_system.hpp"
namespace ps2native {
const char* stop_kind_name(StopKind kind) noexcept {
    switch(kind) {
        case StopKind::GuestException:return "guest exception";
        case StopKind::NotModeled:return "not modeled";
        case StopKind::Undefined:return "architecturally undefined";
    }
    return "?";
}
void check_cache_privilege(ExecutionContext& ctx) {
    const auto status=ctx.state.cp0(Cop0::Status);
    if(!(status&(kStatusEXL|kStatusERL|(1u<<28u))) && (status&kStatusKSU))
        throw GuestStop(StopKind::GuestException,"COP0 unusable in supervisor/user mode with Status.CU0 = 0; vectoring is not modeled");
}
void execute_cache_instruction(ExecutionContext& ctx,std::uint32_t word) {
    ee_system_detail::SystemOps cpu(ctx,dynamic_cast<SystemEventObserver*>(ctx.observer));cpu.cache(word);
}
} // namespace ps2native
