// Own firmware tests; no guest sound driver or fabricated DMA completion.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
std::vector<std::uint8_t> program(bool access) {
    m20::Assembler a(kEeResetVector);
    const auto sw=[&](std::uint32_t address,std::uint32_t value){a.la(8,address);a.la(9,value);a.sw(9,0,8);};
    sw(0xBF801070u,0);sw(0xBF801074u,1u<<9);sw(0xBF801078u,1);
    a.la(9,0x00400401u);a.mtc0(9,m20::Status);a.nop();
    a.la(8,0xBF801070u);a.lw(10,0,8);a.nop();a.andi(10,10,1u<<9);
    a.la(11,0xA0000100u);a.sw(10,0,11);
    if(access){a.la(8,0xBF90019Au);a.la(9,0x8000u);a.sh(9,0,8);}
    a.label("idle");a.beq(0,0,"idle");a.nop();
    auto rom=m20::build_firmware().rom;const auto code=m20::le_words(a.finish());
    std::copy(code.begin(),code.end(),rom.begin());return rom;
}
}
int main() try {
    Iop iop;const auto rom=program(false);iop.reset(rom,nullptr);iop.run_until(100);
    require(iop.stopped() && iop.stop_reason()->find("SPU2")!=std::string::npos,
            "default reset does not invent a known inactive SPU2 interrupt");
    iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.run_until(100);
    require(!iop.stopped() && iop.ee_read(0x100,4)==0,"explicit quiescent experiment permits idle IRQ observation");
    iop.reset(program(true),nullptr,nullptr,false,std::nullopt,{},false,true);iop.run_until(100);
    require(iop.stopped(),"quiescent assumption cannot silently swallow SPU2 control writes");
    iop.reset(rom,nullptr);iop.run_until(100);
    require(iop.stopped(),"ordinary reset removes the experiment");
    for(const bool cold : {false,true}) {
        m20::Assembler a(kEeResetVector);a.la(10,0xA0000100u);
        for(unsigned core=0;core<2;++core) {
            a.la(8,0xBF90019Au+core*0x400u);
            if(!cold)a.sh(0,0,8);
            a.raw((0x25u<<26)|(8u<<21)|(9u<<16));a.nop();a.sw(9,core*4u,10);
        }
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> reset_rom(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),reset_rom.begin());iop.reset(reset_rom,nullptr);iop.run_until(80);
        if(cold)require(iop.stopped() && iop.stop_reason()->find("before a known halfword write")!=std::string::npos,
                        "ATTR cold state is not invented");
        else require(!iop.stopped() && iop.ee_read(0x100,4)==0 && iop.ee_read(0x104,4)==0,
                     "both cores accept explicit disable and retain their disabled control word");
    }
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
