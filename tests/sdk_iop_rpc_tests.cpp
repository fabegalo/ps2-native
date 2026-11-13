#include "iop_compat_test_support.h"

using namespace iop_test;

int main() {
    try {
        constexpr uint32_t sid = 0xA11CE055u;
        Host host;
        IopSubsystem iop(host);
        auto image = rpcServer(sid, 0x12345678u);
        // Three instructions per iteration, beyond the former two-million cap.
        image.words(0x300u, {0x3C08000Bu, 0x2508FFFFu, 0x1500FFFEu, 0u,
                            0x3C020001u, 0x34420400u, 0x03E00008u, 0u});
        image.install(host);
        require(iop.loadModuleBuffer(0x1000u).startResult == 0, "IRX did not start");
        RpcRequest request{};
        request.sid = sid;
        request.receive = {0x800u, 4u};
        require(iop.handleRpc(request).handled, "Long RPC was not handled");
        require(host.word(0x800u) == 0x12345678u, "Long RPC published an incomplete reply");
        const uint32_t spin[] = {0x080040C0u, 0u}; // j 0x10300; nop
        require(iop.writeMemory(0x10300u, spin, sizeof(spin)), "Cannot install timeout control");
        const uint32_t untouched = 0xAABBCCDDu;
        require(host.writeGuest(0x800u, &untouched, sizeof(untouched)), "Cannot set reply sentinel");
        bool rejected = false;
        try { (void)iop.handleRpc(request); }
        catch (const std::runtime_error& error) {
            rejected = std::string(error.what()).find("guest function did not return") != std::string::npos;
        }
        require(rejected, "Nonreturning RPC falsely completed");
        require(host.word(0x800u) == untouched, "Incomplete RPC overwrote the reply");
        iop.reset();
        require(iop.canBindRpc(0x80000003u), "Original IOP heap client cannot bind");
        request.sid = 0x80000003u;
        request.send = {0x900u, 4u};
        uint32_t previousAddress = 0u;
        for (unsigned iteration = 0; iteration < 100u; ++iteration) {
            uint32_t size = 0x10000u;
            require(host.writeGuest(0x900u, &size, 4u), "Cannot write heap request");
            request.function = 1;
            require(iop.handleRpc(request).handled, "IOP heap allocation failed");
            const uint32_t address = host.word(0x800u);
            require(address != 0u && (iteration == 0u || address == previousAddress), "IOP heap did not reuse the released allocation");
            previousAddress = address;
            require(host.writeGuest(0x900u, &address, 4u), "Cannot write heap release");
            request.function = 2;
            require(iop.handleRpc(request).handled && host.word(0x800u) == 0u, "Original IOP heap RPC cannot release allocation");
        }
        std::cout << "PASS synchronous IOP RPC completion and timeout\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
