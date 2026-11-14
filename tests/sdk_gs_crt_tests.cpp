#include "ps2_syscalls.h"
#include "ps2_runtime_macros.h"
#include "ps2_stubs.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

bool ps2ConsumeGsInterruptRequest(); // PS2Recomp gs_frontend.cpp

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() try {
    auto runtime = std::make_unique<PS2Runtime>();
    require(runtime->memory().initialize(), "Fixture RAM initialization failed");
    auto& gs = runtime->memory().gs();
    auto& context = runtime->cpu();
    constexpr uint64_t mask = (0x7full << 3) | (3ull << 13) | (0xfull << 21);
    constexpr uint64_t retained = 0x123456789abcdef0ull & ~mask;
    for (unsigned mode : {2u, 3u}) {
        gs.smode1 = retained;
        gs.pmode = 0;
        SET_GPR_U32(&context, 4, 1);
        SET_GPR_U32(&context, 5, mode);
        SET_GPR_U32(&context, 6, 1);
        ps2_syscalls::SetGsCrt(runtime->memory().getRDRAM(), &context, runtime.get());
        require(((gs.smode1 >> 3) & 127) == 32 && ((gs.smode1 >> 13) & 3) == mode &&
                ((gs.smode1 >> 21) & 15) == 4, "Canonical NTSC/PAL scanout fields missing");
        require((gs.smode1 & ~mask) == retained, "SetGsCrt changed unrelated SMODE1 fields");
        require(gs.smode2 == 3 && (gs.pmode & 1), "SetGsCrt changed the established interlace/output contract");
        gs.smode1 = retained;
        SET_GPR_U32(&context, 4, 0);
        SET_GPR_U32(&context, 5, 1);
        SET_GPR_U32(&context, 6, mode);
        SET_GPR_U32(&context, 7, 1);
        ps2_stubs::sceGsResetGraph(runtime->memory().getRDRAM(), &context, runtime.get());
        require(((gs.smode1 >> 13) & 3) == mode && ((gs.smode1 >> 3) & 127) == 32,
                "sceGsResetGraph bypassed canonical CRT initialization");
        require(GPR_U32((&context), 4) == 0 && GPR_U32((&context), 6) == mode,
                "sceGsResetGraph changed its guest arguments");
    }
    std::cout << "PASS canonical NTSC/PAL CRT fields and preserved state\n";

    // INTC_GS: a SIGNAL (0x60) or FINISH (0x61) 0->1 CSR edge requests one
    // interrupt unless its IMR mask bit (8/9) is set.
    std::vector<uint8_t> vram(4 * 1024 * 1024);
    GS frontend;
    frontend.init(vram.data(), static_cast<uint32_t>(vram.size()), &gs);
    gs.csr = 0;
    gs.imr = 0x7f00;
    frontend.writeRegister(0x61, 0);
    require((gs.csr.load() & 2) && !ps2ConsumeGsInterruptRequest(), "Masked FINISH requested INTC_GS");
    gs.csr = 0;
    gs.imr = 0x7d00;
    frontend.writeRegister(0x61, 0);
    require((gs.csr.load() & 2) && ps2ConsumeGsInterruptRequest() && !ps2ConsumeGsInterruptRequest(),
            "Unmasked FINISH must request INTC_GS exactly once");
    frontend.writeRegister(0x61, 0);
    require(!ps2ConsumeGsInterruptRequest(), "FINISH without a CSR edge requested INTC_GS");
    frontend.writeRegister(0x60, 0);
    require((gs.csr.load() & 1) && !ps2ConsumeGsInterruptRequest(), "Masked SIGNAL requested INTC_GS");
    gs.csr = 0;
    gs.imr = 0x7e00;
    frontend.writeRegister(0x60, 0);
    require(ps2ConsumeGsInterruptRequest(), "Unmasked SIGNAL must request INTC_GS");
    std::cout << "PASS GS SIGNAL/FINISH interrupt requests follow CSR edges and IMR\n";
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
