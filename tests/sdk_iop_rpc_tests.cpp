#include "iop_compat_test_support.h"
#include "emulator/services/iop_module_loader.h"
#include "emulator/iop_emulator.h"
#include "emulator/core/iop_memory.h"
#include "emulator/services/iop_aot_frame.h"
#include "ps2x/iop/iop_aot.h"
#include "emulator/core/iop_kernel.h"
#include "emulator/services/iop_rpc.h"
#include <cstdlib>

using namespace iop_test;

// Small offline-compiled fixture; no runtime instruction decoder is used after reboot.
template <uint32_t Word>
static bool boot_instruction(ps2x::iop::detail::IopCpuState& cpu,
                             ps2x::iop::detail::IopMemory& memory, uint32_t) {
    ps2x::iop::detail::IopAotFrame f(cpu, memory);
    constexpr unsigned op = Word >> 26, rs = Word >> 21 & 31, rt = Word >> 16 & 31;
    constexpr int32_t imm = int16_t(Word);
    if constexpr (Word == 0) {}
    else if constexpr (op == 9) f.write(rt, cpu.gpr[rs] + uint32_t(imm));
    else if constexpr (op == 15) f.write(rt, (Word & 0xffff) << 16);
    else if constexpr (op == 13) f.write(rt, cpu.gpr[rs] | (Word & 0xffff));
    else if constexpr (op == 43) f.store32(rt, cpu.gpr[rs] + uint32_t(imm));
    else if constexpr (op == 35) f.load32(rt, cpu.gpr[rs] + uint32_t(imm));
    else if constexpr (op == 3) { f.write(31, cpu.pc + 8); f.jump((Word & 0x3ffffff) << 2); }
    else if constexpr (op == 0 && (Word & 63) == 8) f.jump(cpu.gpr[rs]);
    else if constexpr (op == 0 && (Word & 63) == 0) f.write(Word >> 11 & 31, cpu.gpr[rt] << (Word >> 6 & 31));
    else static_assert(Word == 0, "Fixture needs another offline-compiled instruction");
    return f.finish();
}

template <uint32_t... Words>
static void boot_code(Irx& image, IopAotModule& module, uint32_t offset) {
    image.words(offset, {Words...});
    ((module.entries.push_back({offset, boot_instruction<Words>, Words}), offset += 4), ...);
}

static void post_boot_test() {
    using ps2x::iop::detail::IopEmulator;
    Irx image;
    IopAotModule module;
    boot_code<0x27bdfff0,0xafbf000c,0x3c1c3344,0x379c5566,0x3c040001,
        0x34840280,0x24050002,0x3c060001,0x34c60440,0x0c004065,0,
        0x3c040001,0x34840200,0x24050000,0x0c004065,0,
        0x3c040001,0x348402c0,0x24050003,0x0c004065,0,
        0x3c040001,0x34840240,0x24050001,0x0c004065,0,
        0x8fbf000c,0x24020000,0x27bd0010,0x03e00008,0>(image,module,0);
    image.words(0x180,{0x41e00000,0,0x101,0x64616f6c,0x65726f63,0x03e00008,0x24000014,0,0});
    // Each callback appends its priority, records GP/delayed/next and returns a distinct status.
#define BOOT_CALLBACK(P) boot_code<0x3c080001,0x8d090400,0,0x00094900,0x35290000u|(P+1), \
    0xad090400,0xad1c0404u+(P*4),0xad050414u+(P*4),0x8c8a0000,0, \
    0xad0a0424u+(P*4),0x03e00008,0x24020010u+P>(image,module,0x200+P*0x40)
    BOOT_CALLBACK(0); BOOT_CALLBACK(1); BOOT_CALLBACK(2); BOOT_CALLBACK(3);
#undef BOOT_CALLBACK
    image.words(0x440,{0xaabbccdd});
    module.image = image.bytes;
    registerIopAotModule(std::move(module));
    Host host;
    IopEmulator iop(host);
    const auto word = [&](uint32_t address) { uint32_t v; require(iop.readMemory(address,&v,4),"Cannot read callback result"); return v; };
    const auto install = [&] { image.install(host); require(iop.loadModuleBuffer(0x1000,nullptr,0).startResult == 0,"Callback initializer failed"); };
    iop.reboot();
    host.file = image.bytes;
    iop.beginBootstrapModule("rom0:CALLBACK-TEST");
    require(!iop.pollBootstrapModule(),"Prepared initializer was reported complete before running");
    bool initialized = false;
    for (unsigned i=0;i<1000;++i) {
        iop.runEeCycles(8);
        if (const auto result=iop.pollBootstrapModule()) {
            require(result->startResult==0,"Resumable initializer returned the wrong result");
            initialized=true; break;
        }
    }
    require(initialized,"Resumable initializer did not progress on the IOP clock");
    const auto allocation=iop.allocateMemory(32);
    require(allocation>=0x10500 && iop.freeMemory(allocation),"Shared sysmem allocated over a fixed native module");
    bool overlap=false;
    try { iop.beginBootstrapModule("rom0:OVERLAP"); } catch(const std::runtime_error&) { overlap=true; }
    require(overlap,"Overlapping fixed-address module replaced live native code");
    require(word(0x10400) == 0 && word(0x10440) == 0xaabbccdd,"Callback ran before boot completed or changed deferred status");
    iop.finishBootstrap();
    require(word(0x10400) == 0x1234,"Post-boot callbacks did not run in priority order");
    for (unsigned p=0;p<4;++p) {
        require(word(0x10404+p*4) == 0x33445566 && word(0x10414+p*4) == 1,"Callback lost GP or deferred flag");
        require(word(0x10424+p*4) == (p==3 ? 0x102c3 : 0x10282),"Callback received the wrong next entry");
    }
    iop.finishBootstrap();
    require(word(0x10400) == 0x1234,"Boot completion ran callbacks twice");
    iop.reboot(); install(); iop.reboot(); iop.finishBootstrap();
    require(!(iop.readSifRegister(4)&0x40000),"Reset retained a callback or fabricated BOOTEND");
    iop.reset(); install();
    require(word(0x10400)==0x3142 && word(0x10440)==0x11 && word(0x10414)==0,
            "Post-bootstrap registration did not run immediately with status and delayed=0");
    iop.reboot(); install();
    const uint32_t corrupt=0;
    require(iop.writeMemory(0x10200,&corrupt,4),"Cannot set callback negative control");
    bool failed=false;
    try { iop.finishBootstrap(); } catch(const std::runtime_error&) { failed=true; }
    require(failed && !(iop.readSifRegister(4)&0x40000),"Invalid callback falsely completed boot");
    failed=false;
    try { iop.finishBootstrap(); } catch(const std::runtime_error&) { failed=true; }
    require(failed,"Failed callback boot was treated as complete on retry");

    Irx handshake;
    IopAotModule handshakeModule;
    boot_code<0x27bdfff0,0xafbf000c,0x0c004025,0,0x24080001,0x3c090001,
        0xad280400,0x8fbf000c,0x24020000,0x27bd0010,0x03e00008,0>(handshake,handshakeModule,0);
    handshake.words(0x80,{0x41e00000,0,0x101,0x6d666973,0x00006e61,0x03e00008,0x24000005,0,0});
    handshakeModule.image=handshake.bytes;
    registerIopAotModule(std::move(handshakeModule));
    iop.reset(); iop.reboot(); host.file=handshake.bytes;
    iop.beginBootstrapModule("rom0:SIF-TEST");
    iop.runEeCycles(8000);
    require(!iop.pollBootstrapModule() && word(0x10400)==0 && !(iop.readSifRegister(4)&0x10000),
            "SIF initializer ran past the missing EE handshake");
    // PS2SDK SIFINIT is 0x10000; CMDINIT alone must not satisfy the transport handshake.
    iop.writeSifRegister(3,0x20000);
    iop.runEeCycles(8000);
    require(!iop.pollBootstrapModule() && word(0x10400)==0,"CMDINIT incorrectly satisfied SIFINIT");
    iop.writeSifRegister(3,0x10000);
    initialized=false;
    for(unsigned i=0;i<1000;++i) {
        iop.runEeCycles(8);
        if(const auto result=iop.pollBootstrapModule()) {
            require(result->startResult==0,"Resumed SIF initializer returned the wrong result");
            initialized=true;break;
        }
    }
    require(initialized && word(0x10400)==1 && (iop.readSifRegister(4)&0x10000),
            "EE handshake did not resume the original SIF initializer");
    iop.reboot();
    require((iop.readSifRegister(3)&0x10000)!=0,"IOP reboot erased EE-owned MSFLAG");
    iop.reset();iop.reboot();
    iop.beginBootstrapModule("rom0:SIF-TEST");iop.runEeCycles(8000);
    iop.reboot();
    failed=false;
    try { (void)iop.pollBootstrapModule(); } catch(const std::runtime_error&) {failed=true;}
    require(failed && iop.threadCount()==0 && !(iop.readSifRegister(4)&0x40000),
            "Reset retained a suspended initializer or fabricated BOOTEND");

    Irx commands;
    IopAotModule commandModule;
    boot_code<0x27bdfff0,0xafbf000c,0x0c004025,0,0x0c004035,0,
        0x24080001,0x3c090001,0xad280400,0x8fbf000c,0x24020000,0x27bd0010,0x03e00008,0>(commands,commandModule,0);
    commands.words(0x80,{0x41e00000,0,0x101,0x6d666973,0x00006e61,0x03e00008,0x24000005,0,0});
    commands.words(0xc0,{0x41e00000,0,0x101,0x63666973,0x0000646d,0x03e00008,0x24000004,0,0});
    commandModule.image=commands.bytes;
    registerIopAotModule(std::move(commandModule));
    iop.reset();iop.reboot();iop.writeSifRegister(3,0x10000);host.file=commands.bytes;
    iop.beginBootstrapModule("rom0:CMD-TEST");iop.runEeCycles(8000);
    require(!iop.pollBootstrapModule() && word(0x10400)==0 && (iop.readSifRegister(4)&0x30000)==0x30000,
            "Command initializer did not advertise its real endpoint while waiting for the EE address");
    const uint32_t packet[]={20,0,0x80000002,0,0x800};
    host.fill(0x800,112,0);
    const auto mailbox=iop.readSifRegister(2);
    require(iop.writeMemory(mailbox,packet,sizeof packet),"Cannot deliver command initialization packet");
    SifTransfer transfer{};transfer.kind=SifTransferKind::SetDma;transfer.phase=SifTransferPhase::AfterCopy;
    transfer.destinationAddress=mailbox;transfer.size=sizeof packet;
    iop.onSifTransfer(transfer);
    initialized=false;
    for(unsigned i=0;i<1000;++i) {
        iop.runEeCycles(8);
        if(const auto result=iop.pollBootstrapModule()) {
            require(result->startResult==0,"Command initializer returned the wrong result");
            initialized=true;break;
        }
    }
    require(initialized && word(0x10400)==1,"EE command packet did not resume the original initializer");
}

static void boot_primitives_test() {
    using namespace ps2x::iop::detail;
    Host host;
    IopMemory memory;
    IopKernel kernel(memory);
    kernel.reset();
    IopRpcBridge rpc(host,memory,kernel);
    rpc.reset();
    IopCpuState cpu;
    rpc.holdBootEnd(true);
    cpu.gpr[4]=0x40000;
    require(rpc.dispatchSifManImport(24,cpu) && cpu.gpr[2]==0x60000,
            "IOP SMFLAG write did not set BOOTEND");
    require(!(rpc.readSifRegister(4)&0x40000),"EE saw BOOTEND before callbacks completed");
    require(rpc.dispatchSifManImport(23,cpu) && cpu.gpr[2]==0x60000,"IOP could not see its own pending SMFLAG");
    cpu.gpr[4]=0x10;
    require(rpc.dispatchSifManImport(24,cpu) && cpu.gpr[2]==0x60010,"Second SMFLAG write erased pending BOOTEND");
    rpc.holdBootEnd(false);
    require(rpc.readSifRegister(4)==0x60010,"EE did not see the actual EESYNC flag after boot completion");
    rpc.writeSifRegister(4,0x10000);
    rpc.writeSifRegister(4,0x20000);
    require(rpc.readSifRegister(4)==0x40010,"OSDSYS clearing init flags erased EESYNC BOOTEND");
    rpc.writeSifRegister(4,0x40000);
    require(rpc.readSifRegister(4)==0x10,"EE could not acknowledge actual BOOTEND");
    rpc.writeSifRegister(3,0xff);
    rpc.writeSifRegister(3,0x100);
    require(rpc.readSifRegister(3)==0x1ff,"EE MSFLAG write erased earlier bits");
    cpu.gpr[4]=0x5;
    require(rpc.dispatchSifManImport(22,cpu) && cpu.gpr[2]==0x1fa && rpc.readSifRegister(3)==0x1fa,
            "IOP MSFLAG write did not clear the requested bits");
    require(kernel.dispatchThreadImport(41,cpu,0) && int32_t(cpu.gpr[2])>0,"System-status event is invalid");
    const auto flag=cpu.gpr[2];
    require(kernel.dispatchThreadImport(41,cpu,0) && cpu.gpr[2]==flag,"System-status event is not shared");
    cpu.gpr[4]=flag;cpu.gpr[5]=0x400;cpu.gpr[6]=0;cpu.gpr[7]=0;
    require(kernel.dispatchEventImport(11,cpu) && int32_t(cpu.gpr[2])==-418,"REBOOT event completed without a request");
    require(kernel.setInternalEventFlag(flag,0x400),"Cannot signal actual reboot event");
    require(kernel.dispatchEventImport(11,cpu) && cpu.gpr[2]==0,"REBOOT event did not observe the real request");
    kernel.reset();
    require(kernel.dispatchThreadImport(41,cpu,0),"System-status event unavailable after reset");
    cpu.gpr[4]=cpu.gpr[2];
    require(kernel.dispatchEventImport(11,cpu) && int32_t(cpu.gpr[2])==-418,"Reset retained the reboot event bits");
}

static void boot_contract_test() {
    Host host;
    {
        IopSubsystem clockProbe(host);
        clockProbe.runEeCycles(7);
        require(clockProbe.debugSnapshot().emulatorCycles == 0, "IOP advanced before a complete clock tick");
        clockProbe.runEeCycles(1);
        require(clockProbe.debugSnapshot().emulatorCycles == 1, "IOP clock deferred an EE-visible tick");
    }
    const auto configuration = [&](std::string text) { host.file.assign(text.begin(), text.end()); };
    configuration("@800\nSYSMEM\nLOADCORE\nEXCEPMAN\n");
    using ps2x::iop::detail::IopModuleLoader;
    const auto direct = IopModuleLoader::readBootModules(host, {});
    require(direct == std::vector<std::string>({"SYSMEM", "LOADCORE", "EXCEPMAN"}),
        "Original boot module order changed");
    const auto text = host.file;
    host.file.assign(64 + text.size(), 0);
    const auto entry = [&](unsigned at, const char* name, uint32_t size) {
        std::memcpy(host.file.data() + at, name, std::strlen(name));
        std::memcpy(host.file.data() + at + 12, &size, 4);
    };
    entry(0, "RESET", 0); entry(16, "ROMDIR", 64);
    entry(32, "IOPBTCONF", static_cast<uint32_t>(text.size()));
    std::copy(text.begin(), text.end(), host.file.begin() + 64);
    const auto image = host.file;
    require(IopModuleLoader::readBootModules(host, "rom0:UDNL rom0:OSDCNF") == direct,
        "OSDCNF overlay ignored its original IOPBTCONF");
    const auto rejected = [&](std::string_view args) {
        bool failed = false;
        try { (void)IopModuleLoader::readBootModules(host, args); }
        catch (const std::runtime_error&) { failed = true; }
        require(failed, "Malformed or unsupported bootstrap configuration accepted");
    };
    host.file[12] = 16; rejected("rom0:UDNL rom0:OSDCNF");
    host.file = image; host.file[44] = 0xff; rejected("rom0:UDNL rom0:OSDCNF");
    host.file = image; host.file[16] = 'X'; rejected("rom0:UDNL rom0:OSDCNF");
    configuration("@800\nSYSMEM\nSYSMEM\n"); rejected({});
    configuration("@800\nSYSMEM\n!CUSTOM\n"); rejected({});
    configuration("@900\nSYSMEM\n"); rejected({});
    configuration("@800\nSYSMEM\n"); rejected("rom0:UDNL host:config");
    host.file = image;
    IopSubsystem iop(host);
    iop.runEeCycles(8000);
    const auto cycles = iop.debugSnapshot().emulatorCycles;
    const auto sendReset = [&](uint32_t mode, bool terminated) {
        const std::string args = "rom0:UDNL rom0:OSDCNF";
        std::array<uint32_t, 26> packet{};
        packet[0] = sizeof(packet); packet[2] = 0x80000003;
        packet[4] = static_cast<uint32_t>(args.size()); packet[5] = mode;
        std::memcpy(reinterpret_cast<char*>(packet.data()) + 24, args.c_str(), args.size() + 1);
        if (!terminated) reinterpret_cast<char*>(packet.data())[27] = 0;
        const auto mailbox = iop.readSifRegister(2);
        require(iop.writeMemory(mailbox, packet.data(), sizeof(packet)), "Cannot DMA reset packet to IOP mailbox");
        SifTransfer transfer{};
        transfer.kind = SifTransferKind::SetDma; transfer.phase = SifTransferPhase::AfterCopy;
        transfer.destinationAddress = mailbox; transfer.size = sizeof(packet);
        iop.onSifTransfer(transfer);
    };
    for (const auto [mode, terminated] : {std::pair{1u, true}, std::pair{0u, false}}) {
        bool failed = false;
        try { sendReset(mode, terminated); } catch (const std::runtime_error&) { failed = true; }
        require(failed && iop.debugSnapshot().emulatorCycles == cycles, "Malformed RESET altered live IOP state");
    }
    sendReset(0, true);
    require(iop.debugSnapshot().emulatorCycles == cycles, "IOP reboot restarted the shared hardware clock");
    bool incomplete = false;
    for(unsigned i=0;i<16 && !incomplete;++i) {
        iop.runEeCycles(8);
        try { require(!(iop.readSifRegister(4)&0x40000),"Pending bootstrap exposed BOOTEND"); }
        catch(const std::runtime_error& error) {
            incomplete=std::string(error.what()).find("rom0:EXCEPMAN")!=std::string::npos;
            if(!incomplete) throw;
        }
    }
    require(incomplete, "Missing original module falsely completed bootstrap");
    iop.writeSifRegister(4, 0x60000);
    bool failedRead=false;
    try { (void)iop.readSifRegister(4); } catch(const std::runtime_error&) {failedRead=true;}
    require(failedRead,"Failed bootstrap error did not propagate to the EE boundary");
    // Original BIOS execution must never fall back to the SDK interpreter.
    ps2x::iop::detail::IopEmulator nativeOnly(host);
    nativeOnly.reboot();
    Irx untranslated;
    untranslated.words(0, {0x03e00008, 0});
    untranslated.install(host);
    bool refusedDecode = false;
    try { (void)nativeOnly.loadModuleBuffer(0x1000, nullptr, 0); }
    catch (const std::runtime_error& error) {
        refusedDecode = std::string(error.what()).find("AOT entry missing") != std::string::npos;
    }
    require(refusedDecode, "BIOS bootstrap interpreted untranslated IOP code");
    require(nativeOnly.instructions() == 0, "Rejected IOP entry executed guest instructions");
    struct SpuClock final : IopSpu2Backend {
        uint64_t resetCycle = 0;
        void reset(uint8_t*, uint32_t, uint64_t cycle) override { resetCycle = cycle; }
        uint16_t read16(uint32_t, uint64_t) override { return 0; }
        void write16(uint32_t, uint16_t, uint64_t) override {}
        void dma(int, uint16_t*, uint32_t, uint32_t, bool, uint64_t) override {}
        uint32_t madr(int) override { return 0; }
        void advance(uint64_t) override {}
        uint64_t nextEvent() override { return UINT64_MAX; }
        uint32_t takeEvents() override { return 0; }
    } spu;
    uint64_t clock = 8000;
    ps2x::iop::detail::IopMemory memory;
    memory.attachSpu2(&spu, &clock);
    memory.reset(clock);
    require(spu.resetCycle == clock, "IOP reboot reset the SPU2 clock to a different epoch");
    iop.reset();
    require(iop.debugSnapshot().emulatorCycles == 0 && !(iop.readSifRegister(4) & 0x40000), "Session reset retained failed bootstrap state");
}

int main() {
    try {
        post_boot_test();
        boot_primitives_test();
        boot_contract_test();
        constexpr uint32_t sid = 0xA11CE055u;
        Host host;
        IopSubsystem iop(host);
        const auto heaplib = iop.loadModule("rom0:HEAPLIB");
        require(heaplib.moduleId > 0 && heaplib.startResult == 0 &&
                iop.loadModule("rom0:HEAPLIB").moduleId == heaplib.moduleId,
                "The existing heaplib HLE was not recognized or initialized more than once");
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
