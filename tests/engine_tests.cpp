#include "ps2native/engine/console.hpp"
#include "ps2native/elf.hpp"
#include <cstdio>
#include <functional>
#include <stdexcept>
using ps2native::engine::Console;
static void require(bool yes, const char* message) { if (!yes) throw std::runtime_error(message); }
static void rejects(const std::function<void()>& action) {
    bool failed = false;
    try { action(); } catch (const std::runtime_error&) { failed = true; }
    require(failed, "Expected explicit rejection");
}
static std::int64_t syscall(Console& s, std::uint32_t n, std::uint32_t a=0, std::uint32_t b=0) {
    s.ee.r[3].w[0] = n; s.ee.r[4].w[0] = a; s.ee.r[5].w[0] = b;
    ps2::syscall(s.ee, 0x1000);
    return s.ee.r[2].sd[0];
}
static void irq(ps2::Cpu& c,std::uint32_t,std::uint32_t){ps2::st32(0x9800,c.r[4].w[0]);ps2::st32(0x9804,c.r[5].w[0]);c.vu0.vf[2].w[0]=77;c.r[2].sw[0]=-1;c.pc=c.r[31].w[0];}
static void unit(ps2::Cpu& c, std::uint32_t, std::uint32_t) { c.r[2].w[0] = 123; c.pc = c.r[31].w[0]; }
int main() {
    try {
        Console s;
        rejects([] { Console second; });
        ps2::st32(0x1000, 0x12345678);
        require(ps2::ld8(0x1000) == 0x78, "Little endian memory");
        require(ps2::ld32(0x80001000) == 0x12345678, "KSEG0 uses the same RAM");
        ps2::st32(0xa0001000, 0xabcdef01);
        require(ps2::ld32(0x1000) == 0xabcdef01, "KSEG1 shares writes");
        rejects([] { ps2::ld64(ps2::kRamSize-4); });
        rejects([] { ps2::st64(ps2::kRamSize-4, 1); });
        rejects([] { ps2::Reg128 r{}; ps2::ld128(ps2::kScratchpadBase+ps2::kScratchpadSize-8,r); });
        ps2::st32(ps2::kScratchpadBase+ps2::kScratchpadSize-4, 42);
        require(ps2::ld32(ps2::kScratchpadBase+ps2::kScratchpadSize-4) == 42, "Scratchpad final word");
        rejects([] { ps2::slow_ld32(0x10002000); });
        rejects([] { ps2::ld32(0x40001000); });
        s.ee.budget = 100000;
        ps2::slow_st32(0x10000800,0xfffe);
        ps2::slow_st32(0x10000810,0x280);
        s.ee.budget -= 2;
        require(ps2::slow_ld32(0x10000800) == 0xffff, "Bus-clock timer advances at EE/2");
        s.ee.budget -= 2;
        rejects([&] { s.account(); });
        s.timers[1] = {}; s.ticks=0; s.ee.budget=100000;
        ps2::slow_st32(0x1000f010, 4); ps2::slow_st32(0x1000f010, 4);
        require(ps2::slow_ld32(0x1000f010) == 0, "INTC mask uses toggle writes");
        ps2::slow_st32(0x1000e010, 0x20000); ps2::slow_st32(0x1000e010, 0x20000);
        require(ps2::slow_ld32(0x1000e010) == 0, "DMAC mask uses toggle writes");
        rejects([] { ps2::slow_st32(0x1000c000, 0x100); });
        static const std::uint16_t entries[] = {1};
        static const ps2::UnitInfo units[] = {{0x1000,4,unit,entries}};
        ps2::register_units(units,1);
        s.ee.pc=0x1000; s.ee.r[31].w[0]=0x1004;
        ps2::dispatch(s.ee,0,0);
        require(s.ee.r[2].w[0] == 123 && s.ee.pc == 0x1004, "AOT dispatch executes native unit");
        rejects([&] { ps2::dispatch(s.ee,0,0); });
        rejects([&] { syscall(s,0xff); });
        static const ps2::UnitInfo irq_units[]={{0x1010,4,irq,entries}};ps2::register_units(irq_units,1);
        s.ee.pc=0x1234;s.ee.cop0[12]|=0x10000;s.ee.r[6].sw[0]=-1;s.ee.r[7].w[0]=0x12345678;
        const auto irq_id=syscall(s,0x10,2,0x1010);
        s.intc_mask=4;s.intc_status=4;
        require(s.interrupt_ready(),"Graphics INTC shares original EE handler delivery");
        s.service_interrupts();
        require(ps2::ld32(0x9800)==2 && ps2::ld32(0x9804)==0x12345678 && !s.intc_status && s.ee.pc==0x1234 && s.ee.vu0.vf[2].w[0]==77,"Interrupt arguments, acknowledgement, continuation and shared VU0 state survive callback");
        syscall(s,0x11,2,irq_id);s.intc_mask=0;s.ee.cop0[12]=0;
        s.code_words[0x1000]=0xabcdef01;
        syscall(s,0x64);
        ps2::st32(0x1000,0);
        rejects([&] { syscall(s,0x64); });
        ps2::st32(0x2000,0xdeadbeef); ps2::st32(0x2004,1); ps2::st32(0x2008,0);
        const auto sema = syscall(s,0x40,0x2000);
        require(sema == 1, "CreateSema uses init_count, not the output count field");
        require(syscall(s,0x45,1) < 0, "Empty semaphore poll must fail");
        syscall(s,0x42,1);
        require(syscall(s,0x45,1) == 1, "Signal/poll consumes token");
        require(syscall(s,0x45,1) < 0, "Token consumed exactly once");
        s.threads.emplace(1,Console::Thread{.state=Console::ThreadState::Running,.priority=64});
        s.threads.emplace(2,Console::Thread{.state=Console::ThreadState::Ready,.priority=32});
        s.threads.at(2).context.pc=0x4000;
        s.ee.pc=0x3000;
        syscall(s,0x44,1);
        require(s.threads.at(1).state == Console::ThreadState::Waiting, "Wait blocks without a token");
        s.schedule();
        require(s.current == 2 && s.ee.pc == 0x4000, "Highest priority ready context selected");
        syscall(s,0x42,1);
        require(s.threads.at(1).state == Console::ThreadState::Ready && s.threads.at(1).context.pc == 0x3000, "Signal resumes waiter at saved PC");
        // PS2SDK ee_thread_t layout: status, entry, stack, size, gp, priority.
        ps2::st32(0x5004,0x1000); ps2::st32(0x5008,0x10000); ps2::st32(0x500c,0x8000);
        ps2::st32(0x5010,0x1234); ps2::st32(0x5014,48);
        const auto thread = syscall(s,0x20,0x5000);
        require(thread > 0 && s.threads.at(thread).entry == 0x1000, "CreateThread consumes PS2SDK ABI");
        syscall(s,0x22,thread,99);
        require(s.threads.at(thread).context.r[4].w[0] == 99 && s.threads.at(thread).context.r[29].w[0] == 0x18000 - 0x2a0, "StartThread argument and context reserve");
        require(syscall(s,0x41,sema) == sema && syscall(s,0x45,sema) < 0, "Deleted semaphore cannot be polled");
        ps2::st32(0x9000,0x12345678);
        syscall(s,0x4a,0x9000); syscall(s,0x4b,0x9004);
        require(ps2::ld32(0x9004)==0x12345678, "OSD get/set share console settings");
        s.initialize_sif();
        require((s.sif_getreg(4)&0x30000) == 0x30000 && !(s.sif_getreg(4)&0x40000), "Only actual HLE service readiness, no original BOOTEND");
        s.sif_setreg(0x80000002,99);
        require(s.sif_getreg(0x80000002)==99, "RPC library sysreg is independent of HLE state");
        std::uint32_t init[]={20,0,0x80000002,0,0x6000};
        auto packet=[](const auto& words) { return std::span(reinterpret_cast<const std::uint8_t*>(words),sizeof(words)); };
        s.sif_command(packet(init));
        std::uint32_t rpc_init[]={16,0,0x80000002,1};
        s.sif_command(packet(rpc_init));
        require(s.sif.replies.size()==1 && ps2::ld32(0x6000)==0, "Reply waits for armed SIF0 DMA");
        syscall(s,0x78);
        require(ps2::ld32(0x6008)==0x80000001 && (s.dmac_status&32), "SIF0 copies actual SetSreg packet and raises DMA status");
        require(!s.interrupt_ready(), "Masked SIF0 IRQ is not delivered");
        s.dmac_status |= 1u<<21; s.ee.cop0[12] |= 0x10000;
        require(s.interrupt_ready(), "SIF0 IRQ becomes deliverable after mask and EI");
        syscall(s,0x6b);
        require(!s.sif.armed, "StopDma cancels rather than arms reception");
        s.initialize_sif(); s.sif.rpc_ready=true;
        std::uint32_t bind[]={36,0,0x80000009,0,1,0x1234,2,0x2345,0x80000001};
        s.sif_command(packet(bind));
        require(s.sif.replies.back()[7]==0x2345 && s.sif.replies.back()[9]!=0, "Bind reply preserves original client and returns registered endpoint");
        bind[8]=0xdead;
        s.sif_command(packet(bind));
        require(s.sif.replies.back()[9]==0, "Unknown RPC server is not reported as present");
        const auto rpc_buffer=s.sif.command_buffer+0x1000;
        const std::uint32_t args[]={1,0x7000,6};
        std::memcpy(s.iop_ram.data()+rpc_buffer,args,sizeof(args));
        std::memcpy(s.ram.data()+0x7000,"actual",6);
        std::uint32_t call[]={56,0,0x8000000a,0,1,0x1234,2,0x2345,3,sizeof(args),0x7100,4,0,s.sif.command_buffer+0x2000};
        s.sif_command(packet(call));
        require(s.output=="actual" && ps2::ld32(0x7100)==6, "TTY consumes guest bytes and returns bytes written");
        ps2::store(s.iop_ram.data()+rpc_buffer,99u);
        s.sif_command(packet(call));
        require(s.output=="actual" && ps2::ld32(0x7100)==static_cast<std::uint32_t>(-9), "Invalid TTY descriptor fails without output");
        call[8]=0; call[9]=16;
        std::memset(s.iop_ram.data()+rpc_buffer,0,16);
        ps2::store(s.iop_ram.data()+rpc_buffer,2u);
        std::memcpy(s.iop_ram.data()+rpc_buffer+4,"tty:",5);
        s.sif_command(packet(call));
        const auto opened=ps2::ld32(0x7100);
        require(opened>=3 && s.sif.tty_descriptors.contains(opened), "TTY open creates a real descriptor");
        call[8]=1; call[9]=4; ps2::store(s.iop_ram.data()+rpc_buffer,opened);
        s.sif_command(packet(call));
        require(ps2::ld32(0x7100)==0 && !s.sif.tty_descriptors.contains(opened), "TTY close releases the descriptor");
        call[9]=0; rejects([&] { s.sif_command(packet(call)); });
        call[8]=99; rejects([&] { s.sif_command(packet(call)); });
        while (s.sif.replies.size()<32) s.sif_command(packet(rpc_init));
        rejects([&] { s.sif_command(packet(rpc_init)); });
        require(s.sif.replies.size()==32, "SIF queue remains bounded on backpressure");
        std::uint32_t malformed[]={100,0,0x80000002,1};
        rejects([&] { s.sif_command(packet(malformed)); });
        ps2::st32(0x8000,0x7000); ps2::st32(0x8004,0x9000); ps2::st32(0x8008,16); ps2::st32(0x800c,0);
        ps2::st32(0x8010,ps2::kRamSize-4); ps2::st32(0x8014,0x9010); ps2::st32(0x8018,16); ps2::st32(0x801c,0);
        rejects([&] { s.sif_dma(0x8000,2); });
        require(s.iop_ram[0x9000]==0, "Invalid DMA list leaves earlier destinations untouched");
        // Returning a guest worker must yield to the supervisor rather than spin at its return marker.
        s.ee.pc=0xfffffff0; s.reschedule_requested=false;
        ps2::dispatch(s.ee,0,0);
        require(s.reschedule_requested && s.threads.at(s.current).state==Console::ThreadState::Dormant, "Worker return requests a context switch");
        s.reschedule_requested=false;
        s.ee.pc=0x123456; s.ee.r[31].w[0]=0x1234;
        syscall(s,0x74,0x55,0x1000);
        require(syscall(s,0x55)==123 && s.ee.pc==0x123456 && s.ee.r[31].w[0]==0x1234, "Installed syscall executes native guest callback and preserves continuation");
        // Fixed modules are selected by actual loaded code, never by an address alone.
        ps2::st32(0x1000,0xabcdef01);
        static const std::uint32_t checks[]={0,0xabcdef01};
        static const ps2::UnitInfo module_units[]={{0,4,unit,entries}};
        static const ps2::ModuleInfo module[]={ {"regression",0x80001000,4,checks,1,module_units,1} };
        ps2::register_modules(module,1);
        s.ee.pc=0x80001000; ps2::dispatch(s.ee,0,0);
        require(s.ee.r[2].w[0]==123 && ps2::window_module_in_place(0x80001000), "Fixed AOT module uses the same RAM");
        ps2::st32(0x1000,0);
        require(!ps2::window_module_in_place(0x80001000), "Changed code invalidates the cached module");
        s.ee.pc=0x80001000;
        rejects([&] { ps2::dispatch(s.ee,0,0); });
        rejects([&] { syscall(s,0x64); });
        // Adapted historical TLB: paired pages, permissions, ASID and replacement share backing RAM.
        s.ee.cop0[0]=7; s.ee.cop0[5]=0x6000; s.ee.cop0[10]=0x40000000;
        s.ee.cop0[2]=0x407; s.ee.cop0[3]=0x507;
        ps2::tlbwi(s.ee);
        ps2::st32(0x40000000,0x11223344); ps2::st32(0x40004000,0x55667788);
        require(ps2::ld32(0x80010000)==0x11223344 && ps2::ld32(0x80014000)==0x55667788, "TLB even/odd page translations");
        s.ee.cop0[2]=0x603; ps2::tlbwi(s.ee);
        ps2::st32(0x80018000,99);
        require(ps2::ld32(0x40000000)==99, "TLB replacement invalidates direct-memory fast path");
        rejects([] { ps2::st32(0x40000000,1); });
        s.ee.cop0[2]=0x600; s.ee.cop0[3]=0x500; s.ee.cop0[10]=0x40000001; ps2::tlbwi(s.ee);
        s.ee.cop0[10]=0;
        rejects([] { ps2::ld32(0x40000000); });
        s.ee.cop0[10]=1;
        rejects([] { ps2::ld32(0x40000000); });
        s.ee.cop0[2]=0x607; s.ee.cop0[3]=0x507; s.ee.cop0[10]=0x40000000; ps2::tlbwi(s.ee);
        s.ee.cop0[0]=8; ps2::tlbwi(s.ee);
        rejects([] { ps2::ld32(0x40000000); });
        s.ee.fcr31=0;
        require(ps2::cfc1(s.ee,31)==0x01000001 && ps2::cfc1(s.ee,0)==0x2e30, "FCR reset/read-only bits and revision");
        ps2::ctc1(s.ee,31,0xffffffff);
        require(ps2::cfc1(s.ee,31)==0x0183c079 && ps2::cfc1(s.ee,16)==0x0183c079, "FCR31 write mask and read aliases");
        ps2::ctc1(s.ee,16,0);
        require(ps2::cfc1(s.ee,31)==0x0183c079, "Only FCR31 is writable");
        s.ee.budget=100000;
        ps2::mtperf(s.ee,0,0xffffffff);
        require(ps2::mfperf(s.ee,0)==0x800ffbfe,"PCCR writable mask");
        ps2::mtperf(s.ee,0,0);ps2::mtperf(s.ee,1,0);
        s.advance_to(s.ticks+32);
        require(ps2::mfperf(s.ee,1)==0,"Disabled performance counter stays unchanged");
        s.threads.clear();s.current=1;
        s.threads[1].state=Console::ThreadState::Waiting;
        rejects([&]{s.schedule();});
        require(!s.exited,"Waiting threads must remain a deadlock, not success");
        s.threads[1].state=Console::ThreadState::Dormant;
        s.vu[1].busy=true;s.vu[1].next_tick=s.ticks+1;
        s.schedule();require(s.exited && s.vu[1].busy,"Retired final EE thread ends the session without waiting for or fabricating a device halt");
        const std::uint8_t bad[] = {0,1,2};
        rejects([&] { (void)ps2native::parse_ee_elf(bad); });
        std::puts("engine: memory, bounds, AOT, invalidation, semaphores and scheduling PASS");
    } catch(const std::exception& e) { std::fprintf(stderr,"%s\n", e.what()); return 1; }
}
