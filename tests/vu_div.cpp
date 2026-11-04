// Original exact binary quotients test the shared VU FDIV contract.
#include "boot_checks.hpp"
#include "ps2native/vu_control.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void vector(VuControl& vu,unsigned unit,unsigned index,std::uint32_t x,std::uint32_t y=0) {
    const ObservedQuadword data{{x|(std::uint64_t{y}<<32u),0},{0,0}};
    if(!unit)vu.write_vector(index,data);
    else {vu.write_vector(31,data);vu.write(2,0x400u+index);vu.vector_store_increment(31,2,15);}
}
void prime_q(VuControl& vu,unsigned unit,std::uint32_t value) {
    if(!unit)vu.write(22,value);
    else {vu.write_vector(31,{{value,0},{0,0}});vu.write(2,0x436);vu.vector_store_increment(31,2,8);}
}
ObservedWord q(const VuControl& vu,unsigned unit){return unit ? vu.observe_data(0x4360) : vu.observe(22);}
ObservedWord status(const VuControl& vu,unsigned unit){return unit ? vu.observe_data(0x4300) : vu.observe(16);}
std::uint32_t divide_code(unsigned fs,unsigned ft,unsigned fsf=0,unsigned ftf=0) {
    return 0x800003BCu|(fs<<11u)|(ft<<16u)|(fsf<<21u)|(ftf<<23u);
}
void pair(VuControl& vu,unsigned unit,unsigned address,std::uint32_t lower,std::uint32_t upper=0x2FF) {
    vu.write_memory((unit ? 0x11008000u : 0x11000000u)+address*8u,8,{lower|(std::uint64_t{upper}<<32u),0});
}
std::uint32_t product_q(unsigned dest,unsigned fs) {return (8u<<21u)|(fs<<11u)|(dest<<6u)|0x1Cu;}
std::uint32_t vx(const VuControl& vu,unsigned unit,unsigned index) {
    return unit ? vu.observe_data(0x4000u+index*16u).value : static_cast<std::uint32_t>(vu.observe_vector(index).low.value);
}
}
int main() try {
    for(const unsigned b : {0x3F800009u,0x3F800021u,0x3F800081u,0x3F800201u}) {
        unsigned flags=0x4008u;
        require(!ee_fpu_multiply(0x3F800000,b,flags) && flags==0x4008u,"normal multiply refuses an uncertified early-digit product without modifying exception flags");
        const auto experimental=ee_fpu_multiply(0x3F800000,b,flags,true);
        require(experimental && *experimental==b-1u && flags==8u,"the explicit experiment covers the stated early-digit class and preserves ordinary O/U flag semantics");
        require(ee_fpu_multiply(0xBF800000,b,flags,true)==((b-1u)|0x80000000u),"the carry experiment preserves the real product sign");
    }
    {unsigned flags=0;require(!ee_fpu_multiply(0x3F800000,0x3FC98851,flags,true),"later negative digits remain unresolved; they cannot be generalized from the early-digit experiment");}
    for(unsigned unit=0;unit<2;++unit) {
        {
            VuControl strict;vector(strict,unit,1,0x3F800000);prime_q(strict,unit,0x3F800009);
            pair(strict,unit,0,0x10010007,product_q(3,1));strict.start_micro(unit,0,0);
            rejects([&]{strict.advance_micro(1);},"ambiguous low-column Booth");
            require(strict.diagnostic_micro_instructions(unit)==0,"unmeasured early-digit carry remains a strict refusal without a paired lower side effect");
            strict.reset();strict.diagnostic_power_two_carry();vector(strict,unit,1,0x3F800000);prime_q(strict,unit,0x3F800009);
            pair(strict,unit,0,0x10010007,product_q(3,1)|0x40000000u);pair(strict,unit,1,0x8000033C);
            strict.start_micro(unit,0,0);strict.advance_micro(6);
            require(vx(strict,unit,3)==0x3F800008 && !strict.micro_running(unit),"explicit carry experiment calculates the externally observed class result through the real VU pipeline");
            rejects([&]{strict.diagnostic_power_two_carry();},"precede micro execution");
            strict.reset();vector(strict,unit,1,0x3F800000);prime_q(strict,unit,0x3F800009);
            pair(strict,unit,0,0x8000033C,product_q(3,1));strict.start_micro(unit,0,0);
            rejects([&]{strict.advance_micro(1);},"ambiguous low-column Booth");
        }
        VuControl vu;vector(vu,unit,1,0x3F800000);vector(vu,unit,2,0,0x40000000);
        pair(vu,unit,0,divide_code(1,2,0,1),0x400002FF);pair(vu,unit,1,0x8000033C);
        vu.start_micro(unit,0,0);vu.advance_micro(6);
        require(!vu.micro_running(unit) && vu.read(29)==(unit ? 1u<<13u : 1u<<5u) && q(vu,unit).unknown==~0u,
            "E completes its own pipeline while the real independent FDIV remains busy and Q is still unwritten");
        vu.advance_micro(7);require(q(vu,unit).unknown==~0u,"DIV does not publish Q before seven cycles");
        vu.advance_micro(8);require(q(vu,unit).value==0x3F000000 && !q(vu,unit).unknown && vu.read(29)==0,
            "the actual selected VF fields produce one half at the FDIV completion event");
        vu.reset();vector(vu,unit,1,0x3F800000);vector(vu,unit,2,0x40000000);prime_q(vu,unit,0x40000000);
        pair(vu,unit,0,divide_code(1,2));pair(vu,unit,1,0x800003BF,product_q(3,1));
        pair(vu,unit,2,0x8000033C,0x400002FF);pair(vu,unit,3,0x8000033C);
        vu.start_micro(unit,0,0);vu.advance_micro(7);
        require(vu.diagnostic_micro_instructions(unit)==1 && q(vu,unit).value==0x40000000,
            "WAITQ stalls both halves without replacing the real previous Q");
        vu.advance_micro(14);require(!vu.micro_running(unit) && vx(vu,unit,3)==0x3F000000,
            "an upper MULq paired with WAITQ uses the newly completed quotient and commits at S");
        vu.reset();vector(vu,unit,1,0x3F800000);vector(vu,unit,2,0x40000000);prime_q(vu,unit,0x40000000);
        pair(vu,unit,0,divide_code(1,2));pair(vu,unit,1,0x8000033C,product_q(3,1)|0x40000000u);pair(vu,unit,2,0x8000033C);
        vu.start_micro(unit,0,0);vu.advance_micro(7);
        require(!vu.micro_running(unit) && vx(vu,unit,3)==0x40000000 && q(vu,unit).value==0x40000000,
            "MULq without WAITQ has no invented dependency and uses the previous Q even while DIV runs");
        vu.advance_micro(8);require(q(vu,unit).value==0x3F000000,"the independent divider still finishes after that E termination");
        vu.reset();vector(vu,unit,1,0x3F800000);vector(vu,unit,2,0x40000000);prime_q(vu,unit,0x40000000);
        pair(vu,unit,0,divide_code(1,2));pair(vu,unit,1,divide_code(2,1),product_q(3,1));
        pair(vu,unit,2,0x8000033C,0x400002FF);pair(vu,unit,3,0x8000033C);
        vu.start_micro(unit,0,0);vu.advance_micro(7);require(vu.diagnostic_micro_instructions(unit)==1,"a second DIV has a true FDIV resource hazard");
        vu.advance_micro(14);require(!vu.micro_running(unit) && vx(vu,unit,3)==0x3F000000 && q(vu,unit).value==0x3F000000,
            "the stalled pair uses the first completed Q before issuing its second real division");
        vu.advance_micro(15);require(q(vu,unit).value==0x40000000 && vu.read(29)==0,"the second quotient completes on its own seven-cycle event");
        vu.reset();prime_q(vu,unit,0x40000000);
        vu.write_memory(unit ? 0x1100C000u : 0x11004000u,4,{0x3F800000,0});
        pair(vu,unit,0,0x8101037C);pair(vu,unit,1,divide_code(1,0,0,3),product_q(3,0)|0x40000000u);pair(vu,unit,2,0x8000033C);
        vu.start_micro(unit,0,0);vu.advance_micro(4);
        require(vu.diagnostic_micro_instructions(unit)==1,"DIV waits for the actual selected VF source instead of consuming its unwritten old value");
        vu.advance_micro(10);require(!vu.micro_running(unit) && q(vu,unit).value==0x40000000,"the delayed DIV starts at VF writeback and remains busy after E");
        vu.advance_micro(12);require(q(vu,unit).value==0x3F800000 && !q(vu,unit).unknown,"the operand stall preserves the real quotient and its seven-cycle latency");
        vu.reset();vector(vu,unit,1,0x3F800000);vector(vu,unit,2,0x40A00000);prime_q(vu,unit,0x40000000);
        pair(vu,unit,0,0x8000033C,(8u<<21u)|(2u<<16u)|(1u<<11u)|0x1BCu);
        pair(vu,unit,1,0x8000033C,(8u<<21u)|(1u<<11u)|(3u<<6u)|0x21u|0x40000000u);pair(vu,unit,2,0x8000033C);
        vu.start_micro(unit,0,0);vu.advance_micro(7);require(vx(vu,unit,3)==0x40E00000,"opcode 0x21 performs MADDq with the actual accumulator bypass");
        vu.reset();vector(vu,unit,1,0x3F800000);prime_q(vu,unit,0x40000000);
        pair(vu,unit,0,0x8000033C,(8u<<21u)|(1u<<11u)|(3u<<6u)|0x20u|0x40000000u);pair(vu,unit,1,0x8000033C);
        vu.start_micro(unit,0,0);vu.advance_micro(6);require(vx(vu,unit,3)==0x40400000,"opcode 0x20 is ADDq and does not read an accumulator");
        vu.reset();vector(vu,unit,1,0x3F800000);vector(vu,unit,2,0);
        pair(vu,unit,0,divide_code(1,2),0x400002FF);pair(vu,unit,1,0x8000033C);vu.start_micro(unit,0,0);vu.advance_micro(8);
        require(q(vu,unit).value==0x7FFFFFFF && status(vu,unit).value==0x820,"nonzero divided by zero produces the VU maximum and real D/sticky-D flags");
        pair(vu,unit,0,divide_code(0,0),0x400002FF);vu.start_micro(unit,0,10);vu.advance_micro(18);
        require(q(vu,unit).value==0x7FFFFFFF && status(vu,unit).value==0xC10,"zero divided by zero replaces current D/I while preserving sticky exception history");
        vu.reset();prime_q(vu,unit,0x40000000);pair(vu,unit,0,divide_code(1,0,0,3),product_q(3,0));vu.start_micro(unit,0,0);
        rejects([&]{vu.advance_micro(1);},"unknown VF operand");
        const auto output=unit ? vu.observe_data(0x4030).unknown : static_cast<std::uint32_t>(vu.observe_vector(3).low.unknown);
        require(output==~0u && q(vu,unit).value==0x40000000 && vu.diagnostic_micro_instructions(unit)==0,
            "an unknown DIV input cannot commit its otherwise valid upper result or fabricate a pending quotient");
    }
    {
        VuControl vu;for(unsigned unit=0;unit<2;++unit) {
            vector(vu,unit,1,0x3F800000);vector(vu,unit,2,0x40000000);
            pair(vu,unit,0,divide_code(1,2),0x400002FF);pair(vu,unit,1,0x8000033C);
        }
        vu.start_micro(0,0,0);vu.start_micro(1,0,0);vu.advance_micro(2);vu.write(28,0x200);
        require(vu.micro_running(0) && !vu.micro_running(1) && (vu.read(29)&0x2020u)==0x20u,"selected VU1 reset preserves VU0's independent pending FDIV");
        vu.advance_micro(8);require(q(vu,0).value==0x3F000000 && q(vu,1).unknown==~0u,"reset cancels only the selected pending quotient without inventing Q register contents");
    }
    std::cout<<"VU DIV/MULq/WAITQ exact quotients, resource/operand stalls, old Q, flags, E independence and selected reset passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
