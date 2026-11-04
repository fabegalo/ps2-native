#include "boot_checks.hpp"
#include "direct_program.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
struct Machine {
    EEState state;
    Memory memory;
    Machine() {
        prepare_direct_elf(parse_ee_elf(direct_test::elf(direct_test::cpu())),nullptr,state,memory);
        memory.write64(0xB2000010u,kSmode1Sint); // no unknown vertical edges
        memory.write32((kIntcStat|0xA0000000u),0x7FFFu);
    }
    void ticks(std::uint64_t n){state.retired_instructions+=n*2u;}
};
void flags() {
    Machine m;
    constexpr auto count=0xB0000000u,mode=count+16u,comp=count+32u;
    m.memory.write32(comp,1);m.memory.write32(count,0xFFFE);
    m.memory.write32((kIntcMask|0xA0000000u),1u<<9u);m.memory.write32(mode,0x380);
    m.ticks(2);
    require(m.memory.read32(count)==0 && m.memory.read32(mode)==0xB80,"overflow count/flag");
    require(m.memory.read32((kIntcStat|0xA0000000u))==(1u<<9u) && m.memory.intc_interrupt_pending(),"overflow latches INTC");
    m.memory.write32((kIntcStat|0xA0000000u),1u<<9u);
    require(!m.memory.intc_interrupt_pending(),"I_STAT acknowledgement lowers INT0 independently of timer flag");
    m.ticks(1);
    require(m.memory.read32(mode)==0xF80 && m.memory.intc_interrupt_pending(),"compare is a separate flag edge");
    m.memory.write32((kIntcStat|0xA0000000u),1u<<9u);m.ticks(65536u*3u);
    require(!m.memory.intc_interrupt_pending(),"uncleared flags cannot retrigger INTC");
    m.memory.write32(mode,0xF80);m.ticks(65536);
    require(m.memory.intc_interrupt_pending(),"W1C flags rearm subsequent edges");
    m.memory.write32((kIntcMask|0xA0000000u),1u<<9u);
    require(!m.memory.intc_interrupt_pending() && m.memory.read32((kIntcStat|0xA0000000u))==(1u<<9u),"mask toggling retains request");
}
void zero_return() {
    Machine m;constexpr auto base=0xB0000800u;
    m.memory.write32(base+32,5);m.memory.write32(base+16,0xC0);
    m.ticks(37);
    require(m.memory.read32(base)==2 && m.memory.read32(base+16)==0xC0,"zero-return repeats without IRQ flags");
    m.memory.write32(base+16,0);
    m.ticks(100);
    require(m.memory.read32(base)==2,"disabled timer freezes");
    m.memory.write32(base,0xFFFF);m.memory.write32(base+32,0);m.memory.write32(base+16,0x3C0);
    m.ticks(1);
    require(m.memory.read32(base)==0 && m.memory.read32(base+16)==0xFC0,"zero target, simultaneous compare/overflow");
}
void uncertainty_and_masks() {
    Machine m;constexpr auto base=0xB0001000u;
    m.memory.write32(base+16,0xF82); // first BUSCLK/256 edge has not occurred yet
    m.state.retired_instructions=511;
    require(m.memory.read32(base)==0,"no event before prescaler edge");
    m.state.retired_instructions=512;
    device_refused([&]{(void)m.memory.read32(base+16);},"undefined COMP");
    // A fresh machine with COMP programmed before counting is fully modeled.
    Machine known;known.memory.write32(base+32,1);known.memory.write32(base+16,0xF82);
    known.state.retired_instructions=512;
    require(known.memory.read32(base+16)==0x782,"configured compare flag at prescaler edge");
    require(known.memory.read32((kIntcStat|0xA0000000u))==(1u<<11u),"T2 selects INTC source 11");
    known.memory.write32((kIntcMask|0xA0000000u),1u<<11u);
    known.state.set_cp0(Cop0::Status,kStatusIE|kStatusEIE|kStatusIM2);
    known.state.pc=0x100004;known.state.delay_pending=true;known.state.delay_target=0x100100;
    require(poll_ee_interrupts(known.state,known.memory) && known.state.pc==0x80000200u &&
            known.state.cp0(Cop0::Epc)==0x100000u && (known.state.cp0(Cop0::Cause)&0x80000400u)==0x80000400u,
            "INT0 enters IRQ vector with correct delay-slot restart");
    require(!poll_ee_interrupts(known.state,known.memory),"EXL inhibits nested delivery");
}
void vertical_latches() {
    Machine m;
    m.memory.write64(0xB2000010u,2u<<13u);
    m.memory.write64(0xB2000060u,2u|(2ull<<42u)); // two blank + two active half-lines
    m.memory.write32(kIntcStat|0xA0000000u,0xCu);
    m.state.retired_instructions=18740;
    require(m.memory.read32(kIntcStat|0xA0000000u)==8,"VBLANK end latches INTC bit 3");
    m.memory.write32(kIntcStat|0xA0000000u,8);
    m.state.retired_instructions=37480;
    require(m.memory.read32(kIntcStat|0xA0000000u)==4,"VBLANK start latches INTC bit 2");
    m.memory.write32(kIntcStat|0xA0000000u,4);
    require(m.memory.read32(kIntcStat|0xA0000000u)==0,"VBLANK event is an edge, not a continuously reasserted level");
    m.memory.write64(kGsImr|0xA0000000u,0x7E00);
    m.memory.write64(kGsImr|0xA0000000u,0x7F00);
    require(m.memory.read32(kIntcStat|0xA0000000u)==0,"unmasking modeled SIGNAL without an event creates no request");
    m.memory.write32(kIntcStat|0xA0000000u,1);
    require(m.memory.read32(kIntcStat|0xA0000000u)==0,"acknowledge preserves known cleared GS latch");
}
}
int main(){try{flags();zero_return();uncertainty_and_masks();vertical_latches();return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
