// Constant output levels are configuration; they do not fabricate audio or IRQ state.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
Iop run(unsigned mode) {
    m20::Assembler a(kEeResetVector);a.la(10,0xA0000100u);
    for(unsigned core=0;core<2;++core)for(unsigned field=0;field<8;++field) {
        a.la(8,0xBF900760u+core*0x28u+field*2u);
        const auto value=field<2 ? 0x400u+core*0x100u+field : 0x8000u+core*0x100u+field;
        a.la(9,value);a.sh(9,0,8);
        a.raw((0x25u<<26)|(8u<<21)|(9u<<16));a.nop();a.sw(9,(core*8u+field)*4u,10);
    }
    if(mode==1){a.la(8,0xBF900788u);a.la(9,0x8000u);a.sh(9,0,8);}
    else if(mode==2){a.la(8,0xBF900770u);a.raw((0x25u<<26)|(8u<<21)|(9u<<16));}
    else if(mode==3){a.la(8,0xBF801070u);a.lw(9,0,8);a.nop();a.andi(9,9,0x200u);a.addiu(9,9,1);}
    a.label("idle");a.beq(0,0,"idle");a.nop();
    const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
    std::copy(code.begin(),code.end(),rom.begin());Iop iop;iop.reset(rom,nullptr);iop.run_until(300);return iop;
}
}
int main() try {
    const auto iop=run(0);require(!iop.stopped(),"constant output levels retain all sixteen independent halfwords");
    for(unsigned core=0;core<2;++core)for(unsigned field=0;field<8;++field)
        require(iop.ee_read(0x100u+(core*8u+field)*4u,4)==(field<2 ? 0x400u+core*0x100u+field : 0x8000u+core*0x100u+field),
                "both core banks retain independent signed constant levels");
    const char* causes[]={"","volume sweep","device not implemented","SPU2"};
    for(unsigned mode=1;mode<=3;++mode){const auto blocked=run(mode);require(blocked.stopped() && blocked.stop_reason()->find(causes[mode])!=std::string::npos,
        "configuration cannot claim sweep/current-level processing or establish an unmeasured IRQ9");}
    std::cout<<"Constant SPU2 levels retained; sweeps, current levels, audio and IRQ reset remain explicit\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
