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
    {
        m20::Assembler a(kEeResetVector);a.la(8,0xBF90019Au);a.la(10,0xA0000100u);
        a.raw((0x25u<<26)|(8u<<21)|(9u<<16));a.nop();a.andi(11,9,0x8000u);a.sw(11,0,10);
        a.label("consume");a.addiu(11,9,1);a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> reset_rom(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),reset_rom.begin());
        iop.reset(reset_rom,nullptr,nullptr,false,std::nullopt,{},false,true);
        const auto boundary=(a.at("consume")-kEeResetVector)/4u;iop.run_until(boundary);
        require(!iop.stopped() && iop.ee_read(0x100,4)==0,"quiescent experiment establishes only an inactive synthesis core");
        iop.run_until(boundary+1u);
        require(iop.stopped() && iop.stop_reason()->find("other control fields are unknown")!=std::string::npos,
                "the experiment does not invent the remaining ATTR bits");
    }
    for(unsigned mode=0;mode<4;++mode) {
        m20::Assembler a(kEeResetVector);a.la(8,0xA0000300u);a.lw(9,0,8);a.nop();
        if(mode!=1)a.andi(9,9,0x7FCFu); // prove inactive core and transfer mode without inventing other fields
        if(mode==2)a.ori(9,9,0x10u);
        if(mode==3)a.ori(9,9,0x8000u);
        a.la(8,0xBF90019Au);a.sh(9,0,8);
        a.raw((0x25u<<26)|(8u<<21)|(9u<<16));a.nop();a.andi(11,9,0x8030u);
        a.la(10,0xA0000100u);a.sw(11,0,10);a.label("consume");a.addiu(11,9,1);
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> image(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),image.begin());iop.reset(image,nullptr);
        const auto boundary=(a.at("consume")-kEeResetVector)/4u;iop.run_until(boundary);
        if(!mode) {
            require(!iop.stopped() && iop.ee_read(0x100,4)==0,"a partial SH retains unknown inactive configuration through an actual device register");
            iop.run_until(boundary+1u);require(iop.stopped() && iop.stop_reason()->find("other control fields are unknown")!=std::string::npos,
                                              "device readback preserves the remaining unknown configuration bits");
        } else require(iop.stopped() && iop.stop_reason()->find("SPU2")!=std::string::npos,
                       "unproved, DMA-active and synthesis-active partial control writes cannot start unmodeled work");
    }
    for(unsigned mode=0;mode<5;++mode) {
        m20::Assembler a(kEeResetVector);
        a.la(8,0xBF9001B0u);a.sh(0,0,8);a.la(8,0xBF9005B0u);a.sh(0,0,8);
        a.la(8,0xBF801070u);a.lw(9,0,8);a.nop();a.andi(9,9,8);
        a.la(10,0xA0000100u);a.sw(9,0,10);a.la(8,0xBF9005B0u);
        if(mode==1){a.la(9,2);a.sh(9,0,8);}
        else if(mode==2)a.raw((0x25u<<26)|(8u<<21)|(9u<<16));
        else if(mode==3)a.sw(0,0,8);
        else if(mode==4){a.la(9,4);a.sh(9,0,8);}
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> image(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),image.begin());iop.reset(image,nullptr);iop.run_until(80);
        if(!mode)require(!iop.stopped() && iop.ee_read(0x100,4)==0,"both AutoDMA disables generate neither data nor a DMA interrupt");
        else require(iop.stopped(),"streaming, readback timing and unsupported AutoDMA widths remain explicit");
    }
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
