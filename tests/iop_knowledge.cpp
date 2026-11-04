// Exact bit knowledge in authored R3000A programs, including a load-delay hazard.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
Iop run(m20::Assembler& a) {
    a.label("idle");a.beq(0,0,"idle");a.nop();
    const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
    std::copy(code.begin(),code.end(),rom.begin());Iop iop;iop.reset(rom,nullptr);iop.run_until(100);return iop;
}
void input(m20::Assembler& a) {
    a.lui(8,0xA000u);a.lw(9,0x300,8);a.nop(); // Unwritten RAM is wholly symbolic.
}
}
int main() try {
    for(const unsigned destination : {9u,10u,11u}) {
        m20::Assembler a(kEeResetVector);input(a);
        a.ori(9,9,0xFFFFu);a.lui(10,0xFFFFu);a.r(9,10,destination,0,0x25u);
        a.sw(destination,0x100,8);const auto iop=run(a);
        require(!iop.stopped() && iop.ee_read(0x100,4)==0xFFFFFFFFu,
                "complementary known one bits prove the full OR result, including both operand aliases");
    }
    {
        m20::Assembler a(kEeResetVector);input(a);a.ori(9,9,0xA5u);a.la(10,0xA5u);
        a.r(9,10,11,0,0x24u);a.sw(11,0x100,8);const auto iop=run(a);
        require(!iop.stopped() && iop.ee_read(0x100,4)==0xA5u,"ORI proves set bits without inventing unknown zero bits");
    }
    for(const bool immediate : {false,true}) {
        m20::Assembler a(kEeResetVector);input(a);
        if(immediate)a.ori(11,9,0);else a.r(0,9,11,0,0x25u);
        a.beq(11,0,"idle");a.nop();const auto iop=run(a);
        require(iop.stopped() && iop.stop_reason()->find("GPR 11 used while its value is unknown")!=std::string::npos,
                "OR with zero preserves uncertainty and refuses its later branch consumption");
    }
    {
        m20::Assembler a(kEeResetVector);a.lui(8,0xA000u);a.la(9,7);a.lw(9,0x300,8);
        a.ori(9,9,0x10u);a.sw(9,0x100,8);const auto iop=run(a);
        require(!iop.stopped() && iop.ee_read(0x100,4)==0x17u,
                "ORI in the load delay slot reads the old value and overrides the pending load to that register");
    }
    std::cout<<"R3000A OR/ORI preserve known bits, aliases, unknown consumption and load delay\n";
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
