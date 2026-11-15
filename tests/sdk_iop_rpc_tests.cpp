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
        iop.reset();
        Irx delayedArgument;
        delayedArgument.words(0, {0x27bdffe0, 0xafbf001c, 0x3c080001,
            0x35040400, 0x24050011, 0x24060004,
            0x0c004025, 0x8d050420, // jal memset; lw a1,0x420(t0)
            0x8fbf001c, 0x00001021, 0x27bd0020, 0x03e00008, 0});
        delayedArgument.words(0x80, {0x41e00000, 0, 0x101, 0x63737973, 0x0062696c,
            0x03e00008, 0x2400000e, 0, 0});
        delayedArgument.words(0x420, {0x5a});
        delayedArgument.install(host);
        require(iop.loadModuleBuffer(0x1000).startResult == 0, "Delayed import fixture did not start");
        uint32_t filled = 0;
        require(iop.readMemory(0x10400, &filled, sizeof filled) && filled == 0x5a5a5a5a,
            "IOP import consumed a stale argument from the caller's load delay slot");
        // A device exception must not strand later RPCs outside their registered IOP thread.
        iop.reset();
        constexpr uint32_t threadedSid=0xa11ce056;
        Irx threaded(0x10000,0x900);
        threaded.words(0,{0x27bdffe0,0xafbf001c,0x3c040001,0x34840540,
            0x0c004185,0,0x00402021,0x0c004187,0x00002821,
            0x8fbf001c,0x00001021,0x27bd0020,0x03e00008,0});
        threaded.words(0x100,{0x27bdffe0,0xafbf001c,0x0c004189,0,
            0x00402821,0x3c040001,0x34840500,0x0c004199,0,
            0x3c040001,0x34840440,0x3c05a11c,0x34a5e056,
            0x3c060001,0x34c60300,0x3c070001,0x34e70400,
            0xafa00010,0xafa00014,0x3c080001,0x35080500,0xafa80018,
            0x0c00419b,0,0x3c040001,0x34840500,0x0c00419d,0,
            0x8fbf001c,0x00001021,0x27bd0020,0x03e00008,0});
        threaded.words(0x300,{0x27bdffe0,0xafbf001c,0x0c004189,0,
            0x3c080001,0xad020400,0x3c020001,0x34420400,
            0x8fbf001c,0,0x27bd0020,0x03e00008,0});
        threaded.words(0x540,{0,0,0x10100,0x1000,0x20});
        threaded.words(0x600,{0x41e00000,0,0x101,0x61626874,0x00006573,
            0x03e00008,0x24000004,0x03e00008,0x24000006,0x03e00008,0x24000014,0,0});
        threaded.words(0x650,{0x41e00000,0,0x101,0x63666973,0x0000646d,
            0x03e00008,0x24000013,0x03e00008,0x24000011,0x03e00008,0x24000016,0,0});
        threaded.install(host);
        require(iop.loadModuleBuffer(0x1000).startResult==0,"Threaded RPC fixture did not start");
        iop.runEeCycles(8192);
        RpcRequest threadedRequest{}; threadedRequest.sid=threadedSid; threadedRequest.receive={0x800,4};
        require(iop.handleRpc(threadedRequest).handled&&host.word(0x800)==1,"RPC did not run on its registered IOP thread");
        Irx fault(0x18000);
        fault.words(0,{0x3c081f80,0x35081548,0x3c090100,0x35290001,0xad090000,0x03e00008,0});
        fault.install(host); bool deviceRejected=false;
        try { (void)iop.loadModuleBuffer(0x1000); }
        catch(const std::runtime_error& error) { deviceRejected=std::string(error.what()).find("Unsupported SIO2 DMA mode")!=std::string::npos; }
        require(deviceRejected,"Invalid device operation did not raise the expected fault");
        require(iop.handleRpc(threadedRequest).handled&&host.word(0x800)==1,
            "Device exception left a stale active CPU and moved RPC execution outside its IOP thread");
        // A firmware dependency must own even the callback-registration exports, and reload after reset.
        Irx firmware(0x18000);
        firmware.words(0,{0x27bdffe0,0xafbf001c,0x3c040001,0x34848200,0x0c006025,0,
            0x8fbf001c,0x00001021,0x27bd0020,0x03e00008,0});
        firmware.words(0x80,{0x41e00000,0,0x101,0x64616f6c,0x65726f63,0x03e00008,0x24000006,0,0});
        firmware.words(0x100,{0x24820001,0x03e00008,0});
        firmware.words(0x120,{0x00001021,0x03e00008,0});
        firmware.words(0x200,{0x41c00000,0,0x103,0x72636573,0x006e616d,
            0x18120,0x18120,0x18120,0x18120,0x18100,0x18100,0x18100,0});
        Irx client;
        client.words(0,{0x27bdffe0,0xafbf001c,0x24040033,0x0c004025,0,
            0x8fbf001c,0,0x27bd0020,0x03e00008,0});
        client.words(0x80,{0x41e00000,0,0x103,0x72636573,0x006e616d,0x03e00008,0x24000004,0,0});
        host.file=firmware.bytes;
        for(unsigned reset=0;reset<2;++reset) {
            iop.reset(); client.install(host);
            require(iop.loadModuleBuffer(0x1000).startResult==0x34,
                "SECRMAN import bypassed the firmware export or failed to reload after IOP reset");
        }
        iop.reset(); host.file.clear(); client.install(host); bool firmwareRejected=false;
        try { (void)iop.loadModuleBuffer(0x1000); }
        catch(const std::runtime_error& error) { firmwareRejected=std::string(error.what()).find("Original SECRMAN is unavailable")!=std::string::npos; }
        require(firmwareRejected,"Missing firmware dependency falsely acknowledged callback registration");
        std::cout << "PASS synchronous IOP RPC, load delay, context restoration and firmware dependency\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
