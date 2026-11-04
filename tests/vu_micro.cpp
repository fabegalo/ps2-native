// Original integer LIW fixtures, not firmware microprograms.
#include "boot_checks.hpp"
#include "ps2native/vu_control.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
constexpr std::uint32_t nop=0x8000033C,upper_nop=0x2FF;
std::uint32_t addiu(unsigned dest,unsigned source,unsigned value) {
    return (8u<<25u)|(dest<<16u)|(source<<11u)|(value&0x7FFu)|((value&0x7800u)<<10u);
}
void instruction(VuControl& vu,unsigned unit,unsigned address,std::uint32_t low,std::uint32_t upper=upper_nop) {
    vu.write_memory((unit ? 0x11008000u : 0x11000000u)+address*8u,8,{low|(std::uint64_t{upper}<<32u),0});
}
ObservedWord integer(const VuControl& vu,unsigned unit,unsigned index) {
    return unit ? vu.observe_data(0x4200u+index*16u) : vu.observe(index);
}
}
int main() try {
    for(unsigned unit=0;unit<2u;++unit) {
        VuControl vu;instruction(vu,unit,0,addiu(1,0,5));instruction(vu,unit,1,addiu(2,1,1));
        instruction(vu,unit,2,nop,upper_nop|0x40000000u);instruction(vu,unit,3,nop);
        vu.start_micro(unit,0,10);require(vu.read(29)==(unit ? 256u : 1u),"the genuine selected VU busy bit rises at start");
        vu.advance_micro(10);require(vu.diagnostic_micro_instructions(unit)==0,"start never executes instructions inline");
        vu.advance_micro(12);require(vu.diagnostic_micro_instructions(unit)==2 && integer(vu,unit,1).unknown,
            "IALU forwarding permits a dependent next instruction before external S writeback");
        vu.advance_micro(15);require(integer(vu,unit,1).value==5 && !integer(vu,unit,1).unknown && integer(vu,unit,2).unknown,
            "external integer visibility follows the documented four-stage writeback");
        vu.advance_micro(17);require(vu.micro_running(unit),"E delay instruction drains through the pipeline before Ready");
        vu.advance_micro(18);require(!vu.micro_running(unit) && vu.read(29)==0 && integer(vu,unit,2).value==6 &&
            vu.diagnostic_micro_instructions(unit)==4 && vu.diagnostic_micro_pc(unit)==32,"E retires its one delay pair, exact results and the next program counter");
        vu.reset();instruction(vu,unit,0,addiu(1,0,1));instruction(vu,unit,1,(0x21u<<25)|(15u<<16)|2u);
        instruction(vu,unit,2,addiu(1,1,2));instruction(vu,unit,3,addiu(1,0,99));instruction(vu,unit,4,addiu(2,1,4));
        instruction(vu,unit,5,nop,upper_nop|0x40000000u);instruction(vu,unit,6,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(20);
        require(integer(vu,unit,1).value==3 && integer(vu,unit,2).value==7 && integer(vu,unit,15).value==3 &&
            vu.diagnostic_micro_instructions(unit)==6,"BAL executes the true delay slot, saves the return index and skips untaken storage");
        vu.reset();instruction(vu,unit,0,addiu(1,0,0x7FFF));instruction(vu,unit,1,addiu(1,1,0x7FFF));
        instruction(vu,unit,2,addiu(1,1,3));instruction(vu,unit,3,nop,upper_nop|0x40000000u);instruction(vu,unit,4,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(20);require(integer(vu,unit,1).value==1,"Imm15 includes its upper four bits and arithmetic wraps to sixteen bits");
        vu.reset();instruction(vu,unit,0,addiu(1,2,1));vu.start_micro(unit,0,0);
        rejects([&]{vu.advance_micro(1);},"contains unknown bits");require(vu.micro_running(unit) && integer(vu,unit,1).unknown,
            "unknown operands cannot fabricate a result or a successful end");
        rejects([&]{vu.start_micro(unit,0,2);},"busy");vu.write(28,unit ? 0x200u : 2u);require(!vu.micro_running(unit),"selected VU reset cancels the failed pipeline");
        vu.reset();instruction(vu,unit,0,addiu(1,0,5),0x28);vu.start_micro(unit,0,0);
        rejects([&]{vu.advance_micro(1);},"upper instruction");require(integer(vu,unit,1).unknown,"unsupported upper instruction cannot commit a valid lower half");
        vu.reset();instruction(vu,unit,0,0x81E1033Cu);vu.start_micro(unit,0,0);
        rejects([&]{vu.advance_micro(1);},"lower instruction");
        require(vu.diagnostic_micro_instructions(unit)==0,"a real MOVE cannot be silently mistaken for the lower NOP alias");
    }
    VuControl cold;cold.start_micro(1,0,0);rejects([&]{cold.advance_micro(1);},"unwritten micro memory");
    std::cout<<"Clocked VU integer LIW, bypass/S visibility, BAL delay, E drain, reset and refusals passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
