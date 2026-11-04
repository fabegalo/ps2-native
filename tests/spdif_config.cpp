// Authored guest accesses to documented digital-output configuration.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
Iop run(unsigned mode) {
    m20::Assembler a(kEeResetVector);a.la(8,0xBF9007C6u);a.la(10,0xA0000100u);
    if(mode!=1) {
        a.la(9,0x900u);a.sh(9,0,8);a.la(9,0x200u);a.sh(9,2,8);
    }
    if(mode==2)a.sw(0,0,8);
    else if(mode==3)a.lw(9,0,8);
    else if(mode==4)a.sb(0,0,8);
    else if(mode==5){a.la(8,0xBF9007C2u);a.lw(9,0,8);}
    else if(mode==6){a.la(8,0xBF90019Au);a.la(9,0x8000u);a.sh(9,0,8);}
    else {
        a.raw((0x25u<<26)|(8u<<21)|(9u<<16));a.nop();a.sw(9,0,10);
        a.raw((0x25u<<26)|(8u<<21)|(9u<<16)|2u);a.nop();a.sw(9,4,10);
        a.la(9,0x902u);a.sh(9,0,8);
        a.raw((0x25u<<26)|(8u<<21)|(9u<<16));a.nop();a.sw(9,8,10);
    }
    a.label("idle");a.beq(0,0,"idle");a.nop();
    const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
    std::copy(code.begin(),code.end(),rom.begin());Iop iop;iop.reset(rom,nullptr);iop.run_until(80);return iop;
}
}
int main() try {
    const auto configured=run(0);
    require(!configured.stopped() && configured.ee_read(0x100,4)==0x900u && configured.ee_read(0x104,4)==0x200u && configured.ee_read(0x108,4)==0x902u,
            "SPDIF mode and media preserve independent halfwords and guest configuration updates");
    for(unsigned mode=1;mode<=6;++mode) {
        const auto iop=run(mode);
        require(iop.stopped(),"unknown reset, unsupported widths, IRQ status and audio core operation stay explicit");
        if(mode==1)require(iop.stop_reason()->find("before a known halfword write")!=std::string::npos,"SPDIF cold read has no invented reset value");
    }
    for(unsigned mode=0;mode<7;++mode) {
        m20::Assembler a(kEeResetVector);a.la(8,0xBF9007C0u);a.la(9,0xC032u);
        if(mode!=1)a.sh(9,0,8);
        if(mode==2){a.la(9,0x100u);a.sh(9,0,8);}
        else if(mode==3){a.la(9,4u);a.sh(9,0,8);}
        else if(mode==4)a.sw(0,0,8);
        else if(mode==5)a.sb(0,0,8);
        else if(mode==6)a.lw(9,0,8);
        else {
            a.raw((0x25u<<26)|(8u<<21)|(9u<<16));a.nop();a.la(10,0xA0000100u);a.sw(9,0,10);
            a.sh(0,0,8);a.raw((0x25u<<26)|(8u<<21)|(9u<<16));a.nop();a.sw(9,4,10);
        }
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),rom.begin());Iop iop;iop.reset(rom,nullptr);iop.run_until(80);
        if(!mode)require(!iop.stopped() && iop.ee_read(0x100,4)==0xC032u && iop.ee_read(0x104,4)==0,
                         "SPDIF PCM output retains guest configuration and explicit disable without fabricating samples");
        else require(iop.stopped(),"SPDIF reset, bypass, streaming and unsupported access widths remain explicit");
    }
    for(unsigned mode=0;mode<5;++mode) {
        m20::Assembler a(kEeResetVector);a.la(8,0xBF9007CAu);a.la(9,8);
        if(mode==2)a.sw(9,0,8);else a.sh(9,0,8);
        if(mode==3){a.raw((0x25u<<26)|(8u<<21)|(9u<<16));a.nop();}
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),rom.begin());Iop iop;iop.reset(rom,nullptr);
        if(mode)iop.diagnostic_spu2_7ca_latch();
        if(mode==4)iop.reset(rom,nullptr);
        iop.run_until(40);
        if(mode==1)require(!iop.stopped(),"explicit unknown-output latch experiment permits only a known halfword write");
        else require(iop.stopped(),"unknown output effects, reads, word writes and reset remain fail-closed");
    }
    std::cout<<"SPDIF configuration retained; core, status, DMA and audio generation remain explicit\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
