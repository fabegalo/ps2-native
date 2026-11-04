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
    for (const bool reversed : {false, true}) for (const unsigned destination : {9u, 11u}) {
        m20::Assembler a(kEeResetVector);input(a);a.ori(9,9,0xA5u);
        a.addu(destination,reversed ? 0u : 9u,reversed ? 9u : 0u);
        a.andi(10,destination,0xA5u);a.sw(10,0x100,8);const auto iop=run(a);
        require(!iop.stopped() && iop.ee_read(0x100,4)==0xA5u,
                "ADDU moves preserve known bits in either operand order and when destination aliases source");
        m20::Assembler b(kEeResetVector);input(b);
        b.addu(destination,reversed ? 0u : 9u,reversed ? 9u : 0u);
        b.beq(destination,0,"idle");b.nop();const auto unknown=run(b);
        require(unknown.stopped() && unknown.stop_reason()->find("GPR "+std::to_string(destination)+" used while its value is unknown")!=std::string::npos,
                "a move transports uncertainty and only its later consumption refuses");
    }
    for (const bool alias : {false, true}) {
        m20::Assembler a(kEeResetVector);a.lui(8,0xA000u);a.la(9,7);a.lw(9,0x300,8);
        a.addu(alias ? 9u : 11u,9,0);a.sw(alias ? 9u : 11u,0x100,8);
        const auto iop=run(a);
        require(!iop.stopped() && iop.ee_read(0x100,4)==7u,
                "ADDU in the load delay slot reads the previous register and wins an aliased pending load");
    }
    {
        m20::Assembler a(kEeResetVector);input(a);a.addu(0,9,0);a.addu(10,0,0);a.sw(10,0x100,8);
        const auto iop=run(a);require(!iop.stopped() && iop.ee_read(0x100,4)==0,
                "discarded moves do not consume undefined bits and $zero stays immutable");
    }
    for(unsigned amount=0;amount<32;++amount) for(const unsigned destination : {9u,11u}) {
        m20::Assembler a(kEeResetVector);input(a);a.ori(9,9,0xFFFFu);
        a.r(0,9,destination,amount,0x02u);
        const auto known=(0xFFFFu>>amount)|(amount ? (~0u<<(32u-amount)) : 0u);
        a.la(10,known);a.r(destination,10,10,0,0x24u);a.sw(10,0x100,8);
        const auto iop=run(a);
        require(!iop.stopped() && iop.ee_read(0x100,4)==(0xFFFFu>>amount),
                "SRL transports each known bit and inserts known high zeros for every shift and operand alias");
        m20::Assembler b(kEeResetVector);input(b);b.r(0,9,destination,amount,0x02u);b.addiu(10,destination,1);
        const auto unknown=run(b);require(unknown.stopped() && unknown.stop_reason()->find("used while its value is unknown")!=std::string::npos,
                                       "SRL never invents the unknown source bits shifted into a result");
    }
    {
        m20::Assembler a(kEeResetVector);a.lui(8,0xA000u);a.la(9,0x80);a.lw(9,0x300,8);
        a.r(0,9,9,3,0x02u);a.sw(9,0x100,8);const auto iop=run(a);
        require(!iop.stopped() && iop.ee_read(0x100,4)==0x10u,"SRL honors the old operand in the load delay slot and overrides its pending load");
    }
    for(const auto mask : {0u,1u,0x10u,0xFFCFu,0xFFFFu}) for(const unsigned destination : {9u,11u}) {
        m20::Assembler a(kEeResetVector);input(a);a.ori(9,9,0x10u);a.andi(destination,9,mask);
        a.andi(10,destination,0x10u);a.sw(10,0x100,8);a.r(0,destination,10,16,0x02u);a.sw(10,0x104,8);
        const auto iop=run(a);require(!iop.stopped() && iop.ee_read(0x100,4)==(mask&0x10u) && iop.ee_read(0x104,4)==0,
                                    "ANDI preserves known ones, cleared bits, zero extension and operand aliases");
        m20::Assembler b(kEeResetVector);input(b);b.ori(9,9,0x10u);b.andi(destination,9,mask);b.addiu(10,destination,1);
        const auto consumed=run(b);
        if(mask==0u || mask==0x10u)require(!consumed.stopped() && consumed.gpr(10)==mask+1u,"ANDI can prove an entire zero or known-bit result");
        else require(consumed.stopped() && consumed.stop_reason()->find("used while its value is unknown")!=std::string::npos,
                     "remaining ANDI result bits are not invented when consumed later");
    }
    {
        m20::Assembler a(kEeResetVector);a.lui(8,0xA000u);a.la(9,7);a.lw(9,0x300,8);
        a.andi(9,9,3);a.sw(9,0x100,8);const auto iop=run(a);
        require(!iop.stopped() && iop.ee_read(0x100,4)==3u,"ANDI preserves the aliased R3000A load-delay write ordering");
    }
    for(unsigned amount=0;amount<32u;++amount)for(const unsigned destination:{9u,11u}) {
        m20::Assembler a(kEeResetVector);input(a);a.ori(9,9,0xFFFFu);
        a.r(0,9,destination,amount,0x00u);
        const auto known=(0xFFFFu<<amount)|((1u<<amount)-1u);
        a.la(10,known);a.r(destination,10,10,0,0x24u);a.sw(10,0x100,8);
        const auto iop=run(a);require(!iop.stopped() && iop.ee_read(0x100,4)==(0xFFFFu<<amount),
            "SLL preserves known bits, inserts low zeros and drops only shifted-out uncertainty for every amount/alias");
        m20::Assembler b(kEeResetVector);input(b);b.r(0,9,destination,amount,0x00u);b.beq(destination,0,"idle");b.nop();
        const auto unknown=run(b);require(unknown.stopped(),"SLL retains unknown result bits for later control-flow consumers");
    }
    for(const unsigned function:{4u,6u}) {
        m20::Assembler a(kEeResetVector);input(a);a.ori(9,9,0xFFFFu);
        a.la(11,0xFFFFFFF0u);a.r(9,11,10,0,0x24u); // count = 16; upper bits remain unknown
        a.r(10,9,9,0,function);a.la(11,function==4u ? 0xFFFFFFFFu : 0xFFFF0000u);
        a.r(9,11,11,0,0x24u);a.sw(11,0x100,8);const auto iop=run(a);
        require(!iop.stopped() && iop.ee_read(0x100,4)==(function==4u ? 0xFFFF0000u : 0u),
            "variable shifts share bit propagation and consume only the shift count low five bits");
    }
    {
        m20::Assembler a(kEeResetVector);a.lui(8,0xA000u);a.la(9,0x80);a.lw(9,0x300,8);
        a.r(0,9,9,3,0x00u);a.sw(9,0x100,8);const auto iop=run(a);
        require(!iop.stopped() && iop.ee_read(0x100,4)==0x400u,"SLL preserves the old load-delay source and overrides the pending aliased load");
    }
    std::cout<<"R3000A bit knowledge preserves aliases, unknown consumption and load delay\n";
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
