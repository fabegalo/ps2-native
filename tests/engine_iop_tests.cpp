#include "ps2native/engine/console.hpp"
#include <cstdio>
#include <functional>
#include "ps2native/io.hpp"
#include <stdexcept>
using ps2native::engine::Console;
static void check(bool value,const char* why) { if(!value) throw std::runtime_error(why); }
static void rejects(const std::function<void()>& call) {
    bool failed=false;try { call(); } catch(const std::runtime_error&) { failed=true; }
    check(failed,"Expected explicit IOP refusal");
}
int main() {
    try {
        Console c;c.initialize_sif();
        c.iop_write(0x100,0x12345678,4);
        check(c.iop_read(0xa0000100,4)==0x12345678,"IOP aliases use Console RAM");
        rejects([&]{c.iop_read(0,8);});rejects([&]{c.iop_read(0x1f801000,4);});
        c.iop_write(0x1f801000,0x1234,4);check(c.iop_read(0xbf801000,4)==0x1234,"Bus configuration latches retain written values across aliases");
        rejects([&]{c.iop_write(0x1f802000,0,4);});
        check(ps2::ld32(0xbc000100)==0x12345678,"EE observes the same IOP backing RAM");
        ps2::st32(0x1c000104,0x87654321);
        check(c.iop_read(0x104,4)==0x87654321,"EE writes are visible to IOP");
        c.iop.gpr[4]=0xa0000200;c.iop.gpr[5]=0x80000100;c.iop.gpr[6]=4;
        c.iop_import("sysclib",12);
        check(c.iop_read(0x200,4)==0x12345678,"IOP service pointers share RAM aliases");
        const auto malformed=std::filesystem::temp_directory_path()/"ps2n-engine-invalid-irx.bin";
        const std::uint8_t bad[]={0,1,2};
        ps2native::write_file_atomically(malformed,bad);
        const auto heap=c.iop_heap_top;
        rejects([&]{c.load_iop(malformed);});
        std::filesystem::remove(malformed);
        check(c.iop_heap_top==heap && c.iop_read(0x100,4)==0x12345678,"Rejected IRX preserves RAM and loader state");
        // LW delay is visible to the next instruction, then commits once.
        const std::uint32_t load[]={0x8c080100,0x01004825,0x01005025};
        for(unsigned i=0;i<3;++i)c.iop_write(0x1000+4*i,load[i],4);
        c.iop.pc=0x1000;c.iop.gpr[8]=7;c.iop_active=true;c.iop_next_tick=8;
        c.advance_to(8);check(c.iop.gpr[8]==7,"Load result must stay pending");
        c.advance_to(16);check(c.iop.gpr[9]==7 && c.iop.gpr[8]==0x12345678,"Load delay uses old GPR then commits");
        c.advance_to(24);check(c.iop.gpr[10]==0x12345678 && c.iop_instructions==3,"Each IOP instruction costs exactly eight shared ticks");
        c.iop_active=false;c.iop={};c.iop.pc=0x2000;c.iop.gpr[29]=0x1ef000;
        // Register an actual native command handler through the same import contract.
        c.iop.gpr[4]=0x1234;c.iop.gpr[5]=0x3000;c.iop.gpr[6]=0x6000;
        c.iop_import("sifcmd",10);
        const std::uint32_t handler[]={0x8c880010,0,0xaca80000,0x03e00008,0};
        for(unsigned i=0;i<5;++i)c.iop_write(0x3000+4*i,handler[i],4);
        const std::uint32_t command[]={20,0,0x1234,0,0xaabbccdd};
        c.sif_command({reinterpret_cast<const std::uint8_t*>(command),sizeof(command)});
        check(c.iop_read(0x6000,4)==0,"Native callback must wait for its scheduled event");
        c.run_iop();
        check(c.iop_read(0x6000,4)==0xaabbccdd,"IOP instructions consume the actual SIF payload");
        check(c.iop.pc==0x2000 && !c.iop_active,"Native handler restores the interrupted CPU context");
        const auto cost=c.ticks;
        rejects([&]{c.advance_to(cost-1);});
        // A native handler for a built-in command replaces that HLE route.
        c.sif.iop_handlers[0x80000002]={0x3000,0x6004,0};
        const std::uint32_t init[]={20,0,0x80000002,0,0x55};
        const auto old_receive=c.sif.ee_receive;
        c.sif_command({reinterpret_cast<const std::uint8_t*>(init),sizeof(init)});c.run_iop();
        check(c.sif.ee_receive==old_receive && c.iop_read(0x6004,4)==0x55,"Native SIF service takes precedence without duplicate HLE processing");
        // Real JAL/import-delay-slot execution sends a reply into the existing EE DMA path.
        c.iop={};c.iop.pc=0x4000;c.iop.gpr[29]=0x1ef000;c.iop.gpr[31]=0xfffffff0;
        c.iop_active=true;c.iop_next_tick=c.ticks+8;
        const std::uint32_t send[]={0x24044321,0x24057000,0x24060010,0x24070000,
            0xafa00010,0xafa00014,0x03e08021,0x0c001400,0,0x02000008,0};
        for(unsigned i=0;i<std::size(send);++i)c.iop_write(0x4000+4*i,send[i],4);
        c.iop_write(0x5000,0x03e00008,4);c.iop_write(0x5004,0x2402000c,4);
        c.iop_imports[0x5000]={"sifcmd",12};
        c.sif.ee_receive=0x8000;c.sif.armed=true;
        c.run_iop();
        check(ps2::ld32(0x8008)==0x4321 && c.sif.interrupt && (c.dmac_status&(1u<<5)),
            "Actual IOP instructions produce the shared EE SIF0 DMA completion/IRQ");
        rejects([&]{c.iop_import("thbase",999);});
        rejects([&]{c.iop_import("thmsgbx",10);});
        c.iop_threads.clear();c.iop_current=1;c.iop_active=true;c.iop_reschedule=true;
        c.iop.pc=0x4000;c.iop.pendingLoad=true;c.iop.pendingLoadReg=9;c.iop.pendingLoadValue=0x99;
        c.iop_threads[1].status=4;c.iop_threads[1].wait=1;
        c.iop_threads[2].status=2;c.iop_threads[2].priority=10;c.iop_threads[2].context.pc=0x6000;
        c.iop_threads[3].status=2;c.iop_threads[3].priority=20;c.iop_threads[3].context.pc=0x7000;
        c.iop_schedule();
        check(c.iop_current==2 && c.iop.pc==0x6000,"The highest-priority ready IOP context runs");
        check(c.iop_threads[1].context.pendingLoad && c.iop_threads[1].context.pendingLoadValue==0x99,"Context switches preserve pending loads");
        c.iop_ready(1,-418);c.iop_schedule();
        check(c.iop_current==1 && c.iop.pendingLoad && static_cast<std::int32_t>(c.iop.gpr[2])==-418,"Wakeup restores the suspended context and real wait result");
        c.iop_threads.clear();c.iop_current=0;c.iop_active=false;c.iop_reschedule=false;
        c.iop_threads[1].status=4;c.iop_threads[1].wait=1;
        c.iop_module_returned=false;rejects([&]{c.run_iop();});
        c.iop_threads[1].status=8;rejects([&]{c.run_iop();});
        c.iop_module_returned=true;c.run_iop();
        std::puts("engine-iop: shared clock, RAM, load delays and native SIF routing PASS");
    } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
}
