#include "direct_program.hpp"
#include "boot_checks.hpp"
#include "ps2native/boot.hpp"
#include "ps2native/sha256.hpp"
#include "ps2native/vif.hpp"
#include <chrono>
#include <iostream>
#include <numeric>

namespace {
using namespace ps2native;
using namespace boot_checks;
void check_loader(const ElfImage& elf) {
    EEState state; Memory memory; prepare_direct_elf(elf,&aot_module(),state,memory);
    require(state.pc==elf.entry && state.reg32(29)==0x01FFF000u && state.cp0(Cop0::Status)==0,"Direct ELF environment");
    memory.write32(0x70003FFCu,0x1234);
    require(memory.read32(0x70003FFCu)==0x1234,"direct scratchpad");
    memory.write32(0xA0000100u,0x5678);
    require(memory.read32(0x100)==0x5678 && memory.read32(0x80000100)==0x5678,"shared RAM aliases");
    bus_fault([&]{(void)memory.fetch32(0xBFC00000u);},BusFault::Unmapped,"unmapped");
    bus_fault([&]{(void)memory.fetch32(0xB000F000u);},BusFault::Device,"instruction fetch");
    require(memory.read32(0xB000F000u)==0,"INTC reset state is known before time advances");
    auto wrong=aot_module();wrong.source_sha256="wrong";
    rejects([&]{prepare_direct_elf(elf,&wrong,state,memory);},"provenance");
    wrong=aot_module();wrong.source_kind="bios-rom0";
    rejects([&]{prepare_direct_elf(elf,&wrong,state,memory);},"provenance");
    rejects([&]{prepare_direct_elf(elf,nullptr,state,memory,{0x00100000u,0});},"overlaps");
    rejects([&]{prepare_direct_elf(elf,nullptr,state,memory,{3,0});},"aligned");
    prepare_direct_elf(elf,nullptr,state,memory);
    for(const auto& seg:elf.segments)
        for(auto off=seg.file_size;off<seg.memory_size;++off)
            require(memory.read8(seg.vaddr+off)==0,"ELF BSS zeroed");
    require(memory.read32(0x100)==0,"reload clears RAM");
    // Unknown opcode remains a named interpreter error, no fake success.
    memory.write32(elf.entry,0x7000003Fu); // Reserved/unimplemented MMI encoding.
    BootOptions options; options.max_instructions=1;
    const auto unknown=run_ee_boot(state,memory,nullptr,options);
    require(unknown.outcome==BootOutcome::BlockPoint && unknown.retired==0 &&
            unknown.reason.find("not implemented by the interpreter")!=std::string::npos,"unknown instruction boundary");
}
void check_timer_edges() {
    EEState state; state.pc=0x00100004u;state.delay_pending=true;state.delay_target=0x00100020u;
    state.set_cp0(Cop0::Status,kStatusIE|kStatusEIE|kStatusIM7);
    state.compare_written=true; state.set_cp0(Cop0::Compare,0);
    require(poll_ee_timer_interrupt(state),"Count equality signals IRQ");
    require(state.pc==0x80000200u && state.cp0(Cop0::Epc)==0x00100000u &&
            (state.cp0(Cop0::Cause)&0x80008000u)==0x80008000u && !state.delay_pending,"interrupt BD/EPC cancels branch");
    require(!poll_ee_timer_interrupt(state),"EXL masks nested timer IRQ");
    state=EEState{};state.pc=0x100000;state.compare_written=true;state.set_cp0(Cop0::Compare,0);
    state.set_cp0(Cop0::Status,kStatusIE|kStatusEIE|kStatusIM7|kStatusBEV);
    state.set_cp0(Cop0::Count,0xFFFFFFFFu);state.retired_instructions=1;
    require(poll_ee_timer_interrupt(state) && state.pc==0xBFC00400u,"Count wraps and bootstrap vector");
}
void check_vif() {
    Vif1 vif; Gif gif; GsRegisters gs; gif.reset();gs.reset(0);
    rejects([&]{vif.write_fifo(0x14000000u,0,gif,gs);},"VIF1 command unsupported");
    vif.write_fifo(0x50000001u,~0ull,gif,gs); // padding after DIRECT is skipped
    require(vif.direct_remaining()==1,"DIRECT aligns to next qword");
    vif.write_fifo(1u<<15,0,gif,gs); // empty EOP GIF packet
    require(vif.direct_remaining()==0 && contains(gif.packets(),"PATH2"),"DIRECT to shared GIF PATH2");
    gif.write_fifo(1ull|(1ull<<15)|(1ull<<60),0xEu,gs);
    rejects([&]{gif.write_path2(0,0,gs);},"arbitration");
    vif.reset();gif.reset();vif.write_fifo(0x50000000u,0,gif,gs);
    require(vif.direct_remaining()==65536u,"DIRECT immediate zero means 65536");
}
void check_dma_errors(const ElfImage& elf) {
    const auto program=direct_test::dma();
    for(unsigned bad=0;bad<3;++bad) {
        Memory memory;EEState state;prepare_direct_elf(elf,nullptr,state,memory);
        memory.load_iop_program(program.iop,0x1000,0x1000);
        if(bad==0)memory.write32(direct_test::data+4,1); // not a full qword
        if(bad==1)memory.write32(direct_test::data,0x80200000u); // beyond IOP RAM
        if(bad==2)memory.write32(direct_test::data+8,1); // unsupported tag padding
        BootOptions o;o.max_instructions=20000;
        const auto r=run_ee_boot(state,memory,nullptr,o);
        require(r.outcome==BootOutcome::BlockPoint && r.iop_stop &&
                r.reason.find("SIF1")!=std::string::npos,"malformed SIF destination refuses with cause");
        require(memory.iop().sif1_metrics().bytes==0 && !contains(r.iop_events,"destination complete"),
                "refused tag transfers no payload and never completes");
    }
    Memory memory;EEState state;prepare_direct_elf(elf,nullptr,state,memory);
    const auto unconfigured=direct_test::iop_receiver(false);
    memory.load_iop_program(unconfigured,0x1000,0x1000);
    BootOptions missing_masks;missing_masks.max_instructions=20000;
    const auto unknown_irq=run_ee_boot(state,memory,nullptr,missing_masks);
    require(unknown_irq.outcome==BootOutcome::BlockPoint &&
            unknown_irq.reason.find("interrupt masks not programmed")!=std::string::npos &&
            !contains(unknown_irq.iop_events,"destination complete"),"unknown SIF interrupt state is not replaced by zero");
    prepare_direct_elf(elf,nullptr,state,memory);
    device_refused([&]{memory.write32(0xB000C400u,0x105);},"only normal source mode");
    memory.write32(0xB000C410u,Memory::kMainRamBytes-16);
    memory.write32(0xB000C420u,2);
    device_refused([&]{memory.write32(0xB000C400u,0x101);},"outside main RAM");
    // An active IOP destination with no EE data must stay busy, not invent a
    // response or report successful DMA completion.
    prepare_direct_elf(elf,nullptr,state,memory);memory.load_iop_program(program.iop,0x1000,0x1000);
    state.pc=0x00130000u;memory.write32(state.pc,0x1000FFFFu);memory.write32(state.pc+4,0);
    BootOptions o;o.max_instructions=1000;
    const auto waiting=run_ee_boot(state,memory,nullptr,o);
    require(waiting.outcome==BootOutcome::BudgetExhausted && !waiting.iop_stop &&
            memory.iop().sif1_metrics().destination_empty_cycles>0 && memory.iop().sif1_metrics().bytes==0 &&
            !contains(waiting.iop_events,"destination complete"),"empty SIF FIFO stalls without false completion");
}
void compare(const std::string& mode,const ElfImage& elf,const std::filesystem::path& output) {
    const auto fixture=direct_test::build(mode);
    std::vector<std::string> trace[2],events[2];std::vector<Framebuffer> frames[2];
    std::vector<std::uint8_t> ram[2]; EEState states[2];BootReport reports[2];
    for(unsigned run=0;run<2;++run) {
        Memory memory; auto& state=states[run];const auto* module=run?&aot_module():nullptr;
        prepare_direct_elf(elf,module,state,memory);
        if(!fixture.iop.empty()) memory.load_iop_program(fixture.iop,0x1000,0x1000);
        BootOptions options;options.max_instructions=300000;options.stop_at=fixture.stop;options.trace=TraceLevel::Full;
        if(mode=="frame") options.on_frame=[&](const Framebuffer& f){frames[run].push_back(f);};
        auto& report=reports[run];report=run_ee_boot(state,memory,module,options);
        require(report.outcome==BootOutcome::StopAddress,"Direct "+mode+" stop: "+report.reason);
        require(report.aot_instructions+report.interpreted_instructions==report.retired,"engine accounting");
        if(run) require(report.aot_instructions>0,"host-compiled code executed");
        trace[run]=normalized(report.trace);events[run]=report.iop_events;
        for(const auto& seg:elf.segments) for(unsigned i=0;i<seg.memory_size;++i)ram[run].push_back(memory.read8(seg.vaddr+i));
        if(mode=="cpu") {
            require(memory.read32(direct_test::data)==30000 && memory.read32(direct_test::data+4)==1,"function calls/memory/IRQ handler");
            require(memory.read32(direct_test::data+8)&0x8000,"handler captured timer pending");
            require(report.interpreted_instructions>0,"system operations remain explicit interpreter boundaries");
        } else if(mode=="dma") {
            std::uint32_t sum=0;
            for(unsigned i=0;i<64;++i) {
                const auto expected=0x01020300u+i;sum+=expected;
                require(memory.read32(0xBC004000u+i*4u)==expected,"SIF1 actual transferred bytes");
            }
            require(state.reg32(2)==sum,"IOP computed checksum of transferred data");
            require(state.reg32(3)&(1u<<27),"IOP DMA completion flag");
            require(state.reg32(4)==8,"IOP DMA interrupt in I_STAT");
            require(!report.iop_stop && contains(events[run],"SIF1 EE source complete") &&
                    contains(events[run],"SIF1 IOP destination complete"),"SIF completion ordering recorded");
            require(memory.read32(0x4000)==0,"DMA does not mirror IOP addresses to EE RAM");
            const auto& metrics=memory.iop().sif1_metrics();
            require(metrics.bytes==256 && metrics.peak_fifo_words==32 && metrics.source_full_cycles>0,
                    "SIF1 byte count and bounded FIFO backpressure");
            require((memory.read32(0xB000C400u)&0x100u)==0 && memory.read32(0xB000C420u)==0 &&
                    (memory.read32(0xB000E010u)&0x40u),"EE source completion and QWC");
        } else if(mode=="sif0") {
            std::uint32_t sum=0;
            for(unsigned i=0;i<64;++i) {
                const auto expected=0xCA000000u+i;sum+=expected;
                require(memory.read32(direct_test::data+i*4u)==expected,"SIF0 actual IOP payload reaches EE RAM");
            }
            require(state.reg32(2)==sum && memory.read32(direct_test::data+256)==1,"EE computes checksum after real DMA IRQ");
            require(memory.read32(direct_test::data+260)&kStatusIM3,"guest handler captures DMAC IP3");
            require(!report.iop_stop && memory.iop().sif0_metrics().bytes==256 &&
                    contains(events[run],"SIF0 EE destination complete"),"SIF0 completion preserves original producer execution");
        } else if(mode=="unaligned") {
            require(memory.read64(direct_test::data+960)==0x98A8B8C8D8E8F808ull &&
                    memory.read64(direct_test::data+968)==0x9E7B583512EFCCA9ull,"PSUBB executes in shared AOT/interpreter path");
            unsigned index=0;
            for(unsigned width:{4u,8u})for(unsigned offset=0;offset<width;++offset,++index) {
                const auto result=direct_test::data+128+index*32;
                for(unsigned i=0;i<width;++i)
                    require(memory.read8(result+i)==0x80+offset+i,"unaligned load pair recovers contiguous bytes");
                if(width==4)require(memory.read32(result+4)==0xFFFFFFFFu,"word merge sign extends lower 64 bits");
                require(memory.read64(result+8)==0x0123456789ABCDEFull,"unaligned loads preserve upper GPR half");
                const auto dest=direct_test::data+512+index*32;
                for(unsigned i=0;i<32;++i)
                    require(memory.read8(dest+i)==(i>=offset && i<offset+width ? 0x90+i-offset : 0xCC),
                            "unaligned stores write only selected bytes");
            }
        } else if(mode=="timer") {
            require(state.reg32(2)==42 && memory.read32(direct_test::data)==1,"timer IRQ handler resumes guest");
            require((memory.read32(direct_test::data+4)&kStatusIM2) &&
                    memory.read32(direct_test::data+8)==(1u<<9u),"real timer edge captured in Cause and I_STAT");
            require(memory.read32(kIntcStat|0xA0000000u)==0 && memory.read32(0xB0000010u)==0,"handler acknowledges both latches");
            for(unsigned offset:{32u,48u})
                require(memory.read64(direct_test::data+offset)==0x0123456789ABCDEFull &&
                        memory.read64(direct_test::data+offset+8)==0xFEDCBA9876543210ull,"AOT HI/LO context retains both pipelines");
            require(memory.read64(direct_test::data+64)==0xFEDCBA9876543210ull &&
                    memory.read64(direct_test::data+72)==0xFEDCBA9876543210ull,"AOT PCPYUD");
            require(memory.read64(direct_test::data+80)==0x89ABCDEF89ABCDEFull &&
                    memory.read64(direct_test::data+88)==0x0123456701234567ull,"AOT PEXTLW");
            require(memory.read64(direct_test::data+96)==0x7654321076543210ull &&
                    memory.read64(direct_test::data+104)==0xFEDCBA98FEDCBA98ull,"AOT PEXTUW");
            require(memory.read64(direct_test::data+112)==0x7654321089ABCDEFull &&
                    memory.read64(direct_test::data+120)==0x7654321089ABCDEFull,"AOT PPACW");
        } else if(mode=="exceptions") {
            require(state.reg32(2)==48 && memory.read32(direct_test::data)==3,
                    "guest handler returns to user mode, cancels branch, annuls false likely slot");
            require(state.cp0(Cop0::Status)==0x10 && !state.delay_pending && !state.indirect_entry_pending,
                    "ERET restores original user privilege and clears pending transfer");
            require(memory.read32(direct_test::data+4)==32 && memory.read32(direct_test::data+12)==0x80000020u &&
                    memory.read32(direct_test::data+20)==36,"SYSCALL/BREAK cause and branch delay flag");
            require(memory.read32(direct_test::data+8)==memory.read32(direct_test::data+28) &&
                    memory.read32(direct_test::data+16)==direct_test::text+16 &&
                    memory.read32(direct_test::data+24)==direct_test::text+28,"exception restart PCs");
            require(contains(report.trace,"SYSCALL exception -> 0x80000180") &&
                    contains(report.trace,"BREAK exception -> 0x80000180"),"explicit exception trace");
            require(memory.read64(direct_test::data+48)==0x3210321032103210ull &&
                    memory.read64(direct_test::data+56)==0xCDEFCDEFCDEFCDEFull,"PCPYH broadcasts both independent halfwords in AOT/interpreter");
            require(memory.read64(direct_test::data+64)==0x3210321032103210ull &&
                    memory.read64(direct_test::data+72)==0x3210321032103210ull,"PCPYLD all operands aliased in AOT/interpreter");
        } else {
            const auto frame=memory.gs().scanout(state.retired_instructions);
            require(frame.width==64 && frame.height==32 && frame.rgba[0]==0 && frame.rgba[2]==32,"Direct ELF guest frame");
            require(contains(report.gif_packets,"PATH2"),"guest frame passed VIF1 DIRECT and GIF PATH2");
            require(!frames[run].empty() && !report.frame_unavailable,"VBLANK frame events");
            std::cout<<"frame_rgba_sha256="<<sha256_hex(frame.rgba)<<'\n';
            require(sha256_hex(frame.rgba)=="402316200a4786b2d6d81a071e89e2adbd98f25d804583790329ae3cb6f436a3","Direct ELF golden framebuffer digest");
            save_framebuffer_bmp(output,frame);
            ram[run].insert(ram[run].end(),frame.rgba.begin(),frame.rgba.end());
        }
    }
    require(trace[0]==trace[1] && events[0]==events[1],"instruction/bus/system/IOP event ordering equivalence");
    require(ram[0]==ram[1],"guest memory and framebuffer equivalence");
    require(reports[0].retired==reports[1].retired,"retired equivalence");
    for(unsigned i=0;i<32;++i) require(states[0].gpr[i].low==states[1].gpr[i].low && states[0].gpr[i].high==states[1].gpr[i].high,"GPR equivalence");
    require(states[0].cop0==states[1].cop0 && states[0].fpr==states[1].fpr && states[0].pc==states[1].pc,"COP0/FPU/PC equivalence");
    require(states[0].shift_amount==states[1].shift_amount,"SA context equivalence");
    require(states[0].fcr31==states[1].fcr31,"FPU control context equivalence");
    require(states[0].fpu_acc==states[1].fpu_acc,"FPU accumulator context equivalence");
    require(frames[0].size()==frames[1].size(),"frame event count equivalence");
    for(std::size_t i=0;i<frames[0].size();++i) require(frames[0][i].rgba==frames[1][i].rgba && frames[0][i].ee_cycle==frames[1][i].ee_cycle,"frame event digest and cycle equivalence");
    std::cout<<mode<<" retired="<<reports[1].retired<<" aot="<<reports[1].aot_instructions<<" interpreted="
             <<reports[1].interpreted_instructions<<" switches="<<reports[1].engine_switches<<'\n';
    // Equivalent conditions: same image, no trace tail/frame callbacks, alternating
    // interpreter and hybrid, initialization excluded. Report all samples.
    for(unsigned repeat=0;repeat<3;++repeat)for(unsigned run=0;run<2;++run) {
        Memory memory; EEState state; const auto* module=run?&aot_module():nullptr;
        prepare_direct_elf(elf,module,state,memory);
        if(!fixture.iop.empty())memory.load_iop_program(fixture.iop,0x1000,0x1000);
        BootOptions options;options.max_instructions=300000;options.stop_at=fixture.stop;options.tail_instructions=0;
        const auto start=std::chrono::steady_clock::now();const auto report=run_ee_boot(state,memory,module,options);
        const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        require(report.outcome==BootOutcome::StopAddress && report.retired==reports[run].retired,"benchmark workload equivalence");
        std::cout<<"sample="<<repeat<<" engine="<<(run?"hybrid":"interpreter")<<" runtime_ms="<<ms<<'\n';
    }
}
}
int main(int argc,char** argv) {
    try {
        require(argc==4,"mode ELF BMP arguments");
        const auto elf=parse_ee_elf(read_file(argv[2]));check_loader(elf);check_timer_edges();check_vif();
        if(std::string(argv[1])=="dma")check_dma_errors(elf);
        compare(argv[1],elf,argv[3]);return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
