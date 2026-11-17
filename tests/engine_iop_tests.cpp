#include "ps2native/engine/console.hpp"
#include <cstdio>
#include <tuple>
#include <functional>
#include "ps2native/io.hpp"
#include <stdexcept>
#include "iop_cdvd.hpp"
#include <chrono>
#include <cstdlib>
using ps2native::engine::Console;
static void check(bool value,const char* why) { if(!value) throw std::runtime_error(why); }
static void rejects(const std::function<void()>& call) {
    bool failed=false;try { call(); } catch(const std::runtime_error&) { failed=true; }
    check(failed,"Expected explicit IOP refusal");
}
int main(int argc,char** argv) {
    try {
        Console c;c.initialize_sif();
        c.iop_write(0x1f8010b4,0x11223344,4);c.iop_write(0x1f8010b6,0x5566,2);
        check(c.iop_read(0x1f8010b4,4)==0x55663344 && c.iop_read(0x1f8010b5,1)==0x33 && c.iop_read(0x1f8010b6,2)==0x5566,"Partial DMA register accesses preserve their other lanes");
        c.cdvd->dma.at(0x1f8010f4)=0x18000000;c.iop_write(0x1f8010f4,0x1234,2);
        check((c.cdvd->dma.at(0x1f8010f4)&0x7f000000)==0x18000000,"Partial DICR control writes cannot acknowledge unwritten flags");
        c.iop_write(0x1f8010f7,8,1);check((c.cdvd->dma.at(0x1f8010f4)&0x7f000000)==0x10000000,"DICR flag bytes retain write-one-to-clear semantics");c.cdvd->dma.at(0x1f8010f4)=0;
        const auto low=c.iop_allocate(0x70000),high=c.iop_allocate(0x11adf8,true);
        check(low==0x10000 && high>=low+0x70000 && high+0x11ae00==0x1d0000,"Top and bottom allocations share one non-overlapping RAM reservation map");
        check(!c.iop_allocate(0x1c0001),"Oversized IOP allocation cannot wrap");
        c.iop_release(high);check(c.iop_allocate(0x11adf8,true)==high,"Released top memory is reused without relocating live reservations");c.iop_release(high);c.iop_release(low);

        if(argc==2 && std::string_view(argv[1])=="--irq-bench"){
            for(unsigned irq=0;irq<46;++irq)c.iop_irqs[irq]={0x8000,0,0,true};
            c.iop_intr_enabled=true;c.iop_active=true;c.iop.pc=0x8000;
            c.iop_write(0x8000,0x08002000,4);c.iop_write(0x8004,0,4);
            using Clock=std::chrono::steady_clock;constexpr unsigned count=1000000;std::uint64_t total=0;
            auto start=Clock::now();for(unsigned n=0;n<count;++n)total+=c.pending_iop_irq();auto irq_end=Clock::now();
            for(unsigned n=0;n<count;++n)total+=c.iop_work_pending();auto work_end=Clock::now();
            for(unsigned n=0;n<count;++n)c.step_iop();auto end=Clock::now();
            const auto ns=[](auto a,auto b){return std::chrono::duration<double,std::nano>(b-a).count()/count;};
            std::printf("synthetic IRQ benchmark: pending_ns=%.2f active_work_ns=%.2f iop_step_ns=%.2f checksum=%llu instructions=%llu\n",ns(start,irq_end),ns(irq_end,work_end),ns(work_end,end),static_cast<unsigned long long>(total),static_cast<unsigned long long>(c.iop_instructions));return 0;
        }
        for(const auto irq:{2u,3u,5u,17u,36u,45u})c.iop_irqs[irq]={0x8000,0,0,true};
        c.iop_intr_enabled=true;c.iop_pending_irqs=(1u<<5)|(1u<<17);c.iop_irqs.at(5).enabled=false;
        check(c.pending_iop_irq()==17,"A disabled lower IRQ cannot hide the next enabled pending source");
        c.iop_irqs.at(5).enabled=true;check(c.pending_iop_irq()==5,"Base IOP IRQ priority remains ascending");
        auto& dicr0=c.cdvd->dma.at(0x1f8010f4);auto& dicr1=c.cdvd->dma.at(0x1f801574);
        dicr0=0x90100000;dicr1=0xa0200000;c.cdvd->irq=1;
        check(c.pending_iop_irq()==36,"First-bank DMA keeps its virtual priority ahead of CDVD/base IRQs");
        c.iop_irqs.at(36).enabled=false;check(c.pending_iop_irq()==45,"Disabled first-bank handler does not hide second-bank SIO2 DMA");
        c.iop_irqs.at(36).enabled=true;dicr0&=~0x80000000u;check(c.pending_iop_irq()==45,"DMA flags and enable without their bank master cannot fabricate an IRQ");
        dicr1=0;check(c.pending_iop_irq()==2,"CDVD device source keeps priority over generic DMA/base IRQs");
        c.cdvd->irq=0;dicr0=0x80008000;check(c.pending_iop_irq()==3,"Forced DMA master still reaches the generic DMA line without a virtual channel");
        c.iop_irqs.at(3).enabled=false;check(c.pending_iop_irq()==5,"Disabled DMA line still permits a pending base IRQ");
        c.iop_intr_enabled=false;check(c.pending_iop_irq()==~0u,"Global IOP interrupt masking still gates all banks and device lines");
        c.iop_active=true;check(c.iop_work_pending(),"A running IOP remains work even when interrupts are masked");
        for(const auto [offset,value]:{std::pair{0u,0x02000000u},std::pair{4u,0u},std::pair{8u,0x7000u},std::pair{12u,0x400u},std::pair{16u,32u}})c.iop_write(0x9000+offset,value,4);
        c.iop.gpr[4]=0x9000;c.iop.gpr[28]=0x12345678;c.iop_import("thbase",4);const auto created=c.iop.gpr[2];const auto& thread=c.iop_threads.at(created);const auto top=thread.stack+thread.size;
        c.iop.gpr[4]=created;c.iop.gpr[5]=0x87654321;c.iop_import("thbase",6);
        check(thread.context.gpr[29]==top-0x20 && thread.context.gpr[30]==top-0x20 && c.iop_read(top-0xb8,4)==0xfffffffe && c.iop_read(top-0xb8+0x10,4)==0x87654321 && c.iop_read(top-0x50,4)==0,"StartThread publishes its initial register frame and real SP in guest RAM");
        check(c.iop_read(thread.stack,4)==0xffffffff && c.iop_read(top-0xb8+0x70,4)==0x12345678,"Only the reserved context changes; the stack watermark and GP remain intact");
        for(unsigned n=0;n<5;++n)c.iop_write(0xa000+n,'a'+n,1);c.iop_threads.at(created).status=16;
        c.iop.gpr[4]=created;c.iop.gpr[5]=5;c.iop.gpr[6]=0xa000;c.iop_import("thbase",7);
        check(thread.context.gpr[4]==5 && thread.context.gpr[5]==top-8 && thread.context.gpr[29]==top-8-0x20 && c.iop_read(top-8,4)==0x64636261 && c.iop_read(top-4,1)=='e',"StartThreadArgs copies bytes above the separate initial context with original 4-byte alignment");
        c.reset_iop();c.initialize_sif();

        check(c.iop_read(0xbf808270,4)==0xf,"SIO2 idle port status comes from the device reset contract");
        check(c.iop_read(0xbf808268,4)==0x3bc,"SIO2 control reset state");
        c.iop_write(0xbf808200,(3u<<18)|(3u<<8),4);
        c.iop_write(0xbf808260,1,1);c.iop_write(0xbf808260,0x42,1);
        rejects([&]{c.iop_write(0xbf808268,1,4);});
        c.iop_write(0xbf808260,0,1);
        check(c.iop_read(0xbf808280,4)==0 && !(c.iop_pending_irqs&(1u<<17)),"Serial data submission cannot fabricate a completion IRQ");
        c.iop_write(0xbf808268,1,4);
        check(c.iop_read(0xbf80826c,4)==0x1d100 && c.iop_read(0xbf808280,4)==1 && (c.iop_pending_irqs&(1u<<17)),"Open serial bus reports a disconnected device with a real pending SIO2 IRQ");
        for(unsigned i=0;i<3;++i)check(c.iop_read(0xbf808264,1)==0xff,"Disconnected peripheral never returns a successful pad response");
        rejects([&]{c.iop_read(0xbf808264,1);});
        c.iop_write(0xbf808280,1,4);check(!(c.iop_pending_irqs&(1u<<17)),"SIO2 W1C acknowledges its own interrupt source");
        const std::uint32_t alarm_code[]={0x8c085700,0,0x25080001,0xac085700,0x29090002,0x03e00008,0x00091300};
        for(unsigned i=0;i<std::size(alarm_code);++i)c.iop_write(0x7400+i*4,alarm_code[i],4);
        c.iop_write(0x9800,4000,4);c.iop_write(0x9804,0,4);c.iop.gpr[4]=0x9800;c.iop.gpr[5]=0x7400;c.iop.gpr[6]=123;c.iop_import("thbase",35);
        check(c.iop.gpr[2]==0 && c.iop_alarms.size()==1,"SetAlarm registers a real clock deadline");
        c.iop_import("thbase",35);check(static_cast<std::int32_t>(c.iop.gpr[2])==-104,"Duplicate alarm handler/argument pair is rejected");
        c.iop_intr_enabled=false;c.advance_to(32000);check(c.iop_read(0x5700,4)==0,"A masked alarm deadline cannot execute its callback");
        c.iop_intr_enabled=true;c.iop_next_tick=c.ticks+8;c.run_iop();
        check(c.iop_read(0x5700,4)==2 && c.iop_alarms.empty() && !c.iop_callback_active,"Original R3000A alarm callback repeats by its returned period and zero removes it");
        c.iop.gpr[4]=0x7400;c.iop.gpr[5]=123;c.iop_import("thbase",37);check(static_cast<std::int32_t>(c.iop.gpr[2])==-105,"Cancelling an absent alarm cannot fabricate success");
        c.iop_write(0x9800,1,4);c.iop.gpr[4]=0x9800;c.iop.gpr[5]=0x7400;c.iop.gpr[6]=123;c.iop_import("thbase",35);
        check(c.iop.gpr[2]==0 && c.iop_read(0x9800,4)==Console::iop_alarm_min,"Alarm minimum wait follows threadman's 100 us clock conversion");
        c.iop.gpr[4]=0x7400;c.iop.gpr[5]=123;c.iop_import("thbase",37);
        check(c.iop.gpr[2]==0 && c.iop_alarms.empty(),"CancelAlarm removes the exact pending handler/argument pair");
        c.iop_write(0x5700,0,4);c.iop.gpr[4]=1;c.iop.gpr[5]=32;c.iop.gpr[6]=8;c.iop_import("timrman",4);const auto hard_timer=c.iop.gpr[2];
        c.iop.gpr[4]=hard_timer;c.iop.gpr[5]=1000;c.iop.gpr[6]=0x7400;c.iop.gpr[7]=123;c.iop_import("timrman",20);
        check(c.iop.gpr[2]==0 && c.iop_timers[5].due==~0ull,"Installing a timer callback cannot start time or invoke guest code");
        c.iop.gpr[4]=hard_timer;c.iop.gpr[5]=1;c.iop.gpr[6]=0;c.iop.gpr[7]=8;c.iop_import("timrman",22);c.iop_import("timrman",23);
        const auto timer_due=c.iop_timers[5].due;check(c.iop.gpr[2]==0 && timer_due==c.ticks+64000,"Hard timer shares the exact EE/IOP 8:1 clock and configured prescale");
        c.iop_intr_enabled=false;c.advance_to(timer_due);check(c.iop_read(0x5700,4)==0 && (c.iop_pending_irqs&(1u<<16)),"Masked timer IRQ waits without executing its native callback");
        c.iop_intr_enabled=true;c.iop_next_tick=c.ticks+8;c.run_iop();
        check(c.iop_read(0x5700,4)==2 && !c.iop_timers[5].mode && c.iop_timers[5].due==~0ull,"Native timer callback changes the compare period, then zero stops it");
        c.iop.gpr[4]=hard_timer;c.iop_import("timrman",6);
        const std::uint32_t dma_irq[]={0x8c087a00,0,0x25080001,0xac087a00,0x03e00008,0x24020001};
        for(unsigned i=0;i<std::size(dma_irq);++i)c.iop_write(0x7600+i*4,dma_irq[i],4);
        for(const auto irq:{44u,45u}){c.iop.gpr[4]=irq;c.iop.gpr[5]=1;c.iop.gpr[6]=0x7600;c.iop.gpr[7]=0;c.iop_import("intrman",4);c.iop.gpr[4]=irq;c.iop_import("intrman",6);}
        for(const auto bank:{0u,1u}){
            const auto reg=bank?0x1f801570u:0x1f8010f0u,ordinal=14u+bank*2,previous=c.iop_read(reg,4);
            c.iop.gpr[4]=0x12345678;c.iop_import("dmacman",ordinal);check(c.iop_read(reg,4)==0x12345678,"DMACMAN priority setter writes shared MMIO");
            c.iop_import("dmacman",ordinal+1);check(c.iop.gpr[2]==c.iop_read(reg,4),"DMACMAN getter reads the same DMA bank");c.iop_write(reg,previous,4);
        }
        for(const auto channel:{3u,11u})for(const auto ordinal:{4u,6u,8u}){
            const auto base=channel<7?0x1f801080u+channel*16:0x1f801500u+(channel-7)*16,reg=base+(ordinal-4)*2,previous=c.iop_read(reg,4);
            c.iop.gpr[4]=channel;c.iop.gpr[5]=ordinal==8?0:0x1234;c.iop_import("dmacman",ordinal);
            c.iop_import("dmacman",ordinal+1);check(c.iop.gpr[2]==(ordinal==8?0u:0x1234u) && c.iop_read(reg,4)==c.iop.gpr[2],"DMACMAN descriptor accessors retain the shared DMA register");c.iop_write(reg,previous,4);
        }
        const auto free_memory=[&](unsigned ordinal){c.iop_import("sysmem",ordinal);return c.iop.gpr[2];};
        const auto free_before=free_memory(8),max_before=free_memory(7),temporary=c.iop_allocate(4096);
        check(temporary && free_memory(8)==free_before-4096 && free_memory(7)<=max_before,"IOP memory queries account for real allocations and fragmentation");
        c.iop_release(temporary);check(free_memory(8)==free_before && free_memory(7)==max_before,"IOP free memory queries restore released gaps");
        c.iop.gpr[29]=0x1eff00;
        for(unsigned j=0;j<5;++j)c.iop_write(0x9300+j,"a.ba"[j],1);
        const auto find_char=[&](unsigned ordinal,unsigned ch){c.iop.gpr[4]=0x80009300;c.iop.gpr[5]=ch;c.iop_import("sysclib",ordinal);return c.iop.gpr[2];};
        check(find_char(21,'a')==0x80009300 && find_char(32,'a')==0x80009303,"IOP string search returns first/last guest pointer");
        check(find_char(25,0)==0x80009304 && find_char(26,'x')==0,"IOP string search handles terminator and missing byte");
        c.iop.gpr[4]='r';c.iop_import("sysclib",6);check(c.iop.gpr[2]=='R',"IOP toupper converts ASCII lowercase");
        c.iop.gpr[4]=0xffffffff;c.iop_import("sysclib",6);check(c.iop.gpr[2]==0xffffffff,"IOP toupper preserves EOF");
        c.iop.gpr[4]='R';c.iop_import("sysclib",7);check(c.iop.gpr[2]=='r',"IOP tolower converts ASCII uppercase");
        for(const auto& [text,base,expected,consumed]:{std::tuple{"64",10u,64u,2u},std::tuple{"  -17z",10u,0xffffffefu,5u},std::tuple{"0xff!",0u,255u,4u},std::tuple{"2147483648",10u,0x7fffffffu,10u},std::tuple{"no digits",10u,0u,0u}}){
            for(unsigned n=0;n<=std::strlen(text);++n)c.iop_write(0x9100+n,text[n],1);
            c.iop.gpr[4]=0x9100;c.iop.gpr[5]=0x9200;c.iop.gpr[6]=base;c.iop_import("sysclib",36);
            check(c.iop.gpr[2]==expected && c.iop_read(0x9200,4)==0x9100+consumed,"IOP strtol retains 32-bit range, radix and guest end pointer");
        }
        const auto slice=[&](unsigned channel,unsigned address,unsigned direction){
            c.iop.gpr[4]=channel;c.iop.gpr[5]=address;c.iop.gpr[6]=4;c.iop.gpr[7]=1;c.iop_write(c.iop.gpr[29]+16,direction,4);c.iop_import("dmacman",28);
            check(c.iop.gpr[2]==1,"Slice DMA configuration uses the real DMACMAN fifth argument");c.iop.gpr[4]=channel;c.iop_import("dmacman",32);
        };
        c.iop_write(0xbf808200,(3u<<18)|(3u<<8),4);
        for(unsigned i=0;i<16;++i)c.iop_write(0x6400+i,i==0?1:i==1?0x42:i==2?0:0xa5,1);
        slice(12,0x6500,0);slice(11,0x6400,1);c.advance_to(c.ticks+16);
        check(c.iop_read(0xbf801540,4)==0x6400 && c.iop_read(0xbf801550,4)==0x6500 && c.iop_sio2_output.empty(),"Disabled serial DMA retains its descriptors and a receiver waits for actual FIFO data");
        c.iop_write(0xbf801570,0x880000,4);
        check(c.iop_sio2_dma_due[0]>c.ticks && c.iop_sio2_dma_due[1]>c.ticks && c.iop_read(0x7a00,4)==0,"SIO2 DMA cannot fabricate inline completion interrupts");
        c.run_iop();check(c.iop_read(0x7a00,4)==2 && !(c.iop_read(0xbf801548,4)&0x1000000) && !(c.iop_read(0xbf801558,4)&0x1000000),"Both SIO2 DMA deadlines invoke their actual R3000A handlers and retire channel STR");
        check(c.iop_read(0xbf801540,4)==0x6410 && c.iop_read(0xbf801550,4)==0x6510,"Serial DMA uses the shared IOP RAM and advances both addresses");
        for(unsigned i=0;i<16;++i)check(c.iop_read(0x6500+i,1)==(i<3?0xff:0),"Open-bus wire replies and DMA block padding remain distinct");
        c.iop_active=true;c.iop.pc=0x6800;c.iop_instructions=10000000;c.iop_write(0x6800,0,4);
        c.step_iop();check(c.iop_instructions==10000001,"Long IOP sessions use the shared logical execution budget, not a fixed cumulative cap");
        c.execution_limit=80000000;rejects([&]{c.step_iop();});c.execution_limit=100000000;
        c.reset_iop();c.initialize_sif();c.ticks=0;c.iop_instructions=0;c.iop={};c.iop_next_tick=0;
        {
            const std::uint32_t callback[]={0xac047b00,0xac1c7b04,0x24080001,0xac087b08,0x03e00008,0};
            for(unsigned k=0;k<std::size(callback);++k)c.iop_write(0x7c00+k*4,callback[k],4);
            c.iop_write(0x7b00,0,4);c.iop_write(0x7b08,0,4);c.iop_write(0x7d00,0x7e00,4);c.iop_write(0x7d04,0x80007f00,4);c.iop_write(0x7d08,4,4);c.iop_write(0x7d0c,0,4);c.iop_write(0x7e00,0x87654321,4);
            c.iop_intr_enabled=false;c.iop.gpr[4]=0x7d00;c.iop.gpr[5]=1;c.iop.gpr[6]=0x7c00;c.iop.gpr[7]=0x1357;c.iop.gpr[28]=0x2468;c.iop_import("sifman",32);
            check(ps2::ld32(0x7f00)==0x87654321 && c.iop_read(0x7b08,4)==0 && c.sif.dma_callbacks.size()==1,"Native DMA completion is queued after actual data transfer, without an inline callback");
            const auto saved=c.iop;c.iop_active=false;c.advance_to(c.ticks+8);check(c.iop_read(0x7b08,4)==0,"Masked DMA completion cannot execute its IOP callback");
            c.iop_intr_enabled=true;c.iop_next_tick=c.ticks+8;c.advance_to(c.ticks+64);
            check(c.iop_read(0x7b00,4)==0x1357 && c.iop_read(0x7b04,4)==0x2468 && c.iop_read(0x7b08,4)==1 && c.sif.dma_callbacks.empty() && !c.iop_callback_active,"DMA completion executes original callback with its GP and argument exactly once");
            check(c.iop.gpr[2]==saved.gpr[2],"DMA callback restores its interrupted caller result");
        }
        c.reset_iop();c.initialize_sif();c.ticks=0;c.iop_instructions=0;c.iop={};c.iop_next_tick=0;
        c.sif.transfer_id=2;c.sif.completed_id=1;
        for(const auto id:{0u,1u,3u}){c.iop.gpr[4]=id;c.iop_import("sifman",8);check(static_cast<std::int32_t>(c.iop.gpr[2])==-1,"Unknown, zero and completed DMA identifiers cannot fabricate a busy transfer");}
        c.iop.gpr[4]=2;c.iop_import("sifman",8);check(c.iop.gpr[2]==0,"The actual in-flight DMA remains busy");
        c.ee.r[3].w[0]=0x76;c.ee.r[4].w[0]=0;ps2::syscall(c.ee,0x1000);
        check(c.ee.r[2].sw[0]==-1,"EE and IOP query the same SIF transfer state");c.initialize_sif();
        const std::string kernel_tty="kernel %d\n";for(unsigned i=0;i<=kernel_tty.size();++i)c.iop_write(0x6600+i,i==kernel_tty.size()?0:kernel_tty[i],1);
        c.iop.gpr[4]=0x6600;c.iop.gpr[5]=static_cast<std::uint32_t>(-112);c.iop_import("sysmem",14);
        check(c.iop_output=="kernel -112\n" && c.iop.gpr[2]==12,"Kprintf and stdio use the same real bounded IOP TTY service");c.iop_output.clear();
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
