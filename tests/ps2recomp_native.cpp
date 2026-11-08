// CPU-only SDK codegen fixture: no runtime instance, devices, HLE or guest memory are exercised.
#include "ps2_runtime_macros.h"
#include <iostream>

int main() {
    if (g_ps2RecompiledFunctionTableBase != 0x100000u ||
        g_ps2RecompiledFunctionTableSlotCount != 1u || !g_ps2RecompiledFunctionTable[0]) return 1;
    R5900Context context{};
    auto* ctx = &context;
    constexpr std::uint32_t stop = 0xFFFFFFF0u;
    SET_GPR_U32(ctx,31,stop);
    g_ps2RecompiledFunctionTable[0](nullptr,ctx,nullptr);
    if (GPR_U32(ctx,2) != 42u || GPR_U32(ctx,0) != 0u || context.pc != stop) return 1;
    std::cout << "SDK generated C++ compiled and executed: v0=42, JR return preserved\n";
}
