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
    if(access){a.la(8,0xBF90019Au);a.la(9,0x80C0u);a.sh(9,0,8);}
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
    for(const bool stop_clock : {false,true}) {
        m20::Assembler a(kEeResetVector);a.la(8,0xBF90019Au);a.la(9,0x8000u);a.sh(9,0,8);
        a.la(8,0xBF90059Au);a.la(9,0xA000u);a.sh(9,0,8);
        if(stop_clock){for(unsigned n=0;n<1000;++n)a.nop();a.sh(0,0,8);a.la(8,0xBF90019Au);a.sh(0,0,8);}
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> image(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),image.begin());iop.reset(image,nullptr,nullptr,false,std::nullopt,{},false,true);
        iop.run_until(767);require(!iop.stopped() && !iop.spu2_last_sample(0) && !iop.spu2_last_sample(1),"no audio sample precedes its 48 kHz clock edge");
        iop.run_until(768);
        for(unsigned core=0;core<2;++core)require(iop.spu2_sample_count(core)==1 && iop.spu2_last_sample(core)->cycle==768 &&
            iop.spu2_last_sample(core)->left==0 && iop.spu2_last_sample(core)->right==0,"inactive sources produce a real zero stereo sample at the exact clock edge");
        iop.run_until(3072);
        require(!iop.stopped() && iop.spu2_sample_count(0)==(stop_clock ? 1u : 4u) && iop.spu2_sample_count(1)==(stop_clock ? 1u : 4u),
                "both core clocks retain their phase and explicit disables stop future samples");
        iop.reset(image,nullptr);iop.run_until(80);require(iop.stopped(),"quiet reset assumptions never become a default for active synthesis");
    }
    {
        m20::Assembler a(kEeResetVector);a.la(8,0xBF90019Au);a.la(9,0x8000u);a.sh(9,0,8);
        a.la(8,0xBF900344u);a.raw((0x25u<<26)|(8u<<21)|(9u<<16));a.nop();a.andi(11,9,0x7FFu);
        a.la(10,0xA0000100u);a.sw(11,0,10);a.label("consume");a.addiu(11,9,1);
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> image(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),image.begin());iop.reset(image,nullptr,nullptr,false,std::nullopt,{},false,true);
        const auto boundary=(a.at("consume")-kEeResetVector)/4u;iop.run_until(boundary);
        require(!iop.stopped() && iop.ee_read(0x100,4)==0,"quiet STATX has no active request, busy transfer or enabled IRQ");
        iop.run_until(boundary+1u);require(iop.stopped() && iop.stop_reason()->find("SPU2 STATX")!=std::string::npos,"unmodeled STATX bits cannot masquerade as zero");
    }
    for(unsigned mode=0;mode<6;++mode) {
        m20::Assembler a(kEeResetVector);
        if(mode!=5){a.la(8,0xBF90019Au);a.la(9,0x8000u);a.sh(9,0,8);}
        for(unsigned core=0;core<2;++core) {
            a.la(8,0xBF9001A4u+core*0x400u);a.la(9,0xFFFFu);a.sh(9,0,8);
            a.la(9,0xFFFFu);a.sh(9,2,8);
        }
        if(mode==1){a.la(8,0xA0000300u);a.lw(9,0,8);a.nop();a.la(8,0xBF9001A4u);a.sh(9,0,8);}
        else if(mode==2)a.sw(0,0,8);
        else if(mode==3)a.raw((0x25u<<26)|(8u<<21)|(9u<<16));
        else if(mode==4){a.la(8,0xBF9001A0u);a.la(9,1);a.sh(9,0,8);}
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> image(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),image.begin());iop.reset(image,nullptr,nullptr,false,std::nullopt,{},false,mode!=5);
        iop.run_until(1536);
        if(!mode)require(!iop.stopped() && iop.spu2_sample_count(0)==2 && iop.spu2_last_sample(0)->left==0,
                         "KOFF on all 48 inactive voices preserves the quiet sample clock");
        else {require(iop.stopped(),"KOFF rejects unproved masks, widths, readback, active synthesis and missing reset assumptions");
            if(mode==5)require(iop.stop_reason()->find("KOFF")!=std::string::npos,"inactive voice state requires its explicit assumption even before a core starts");}
    }
    for(unsigned mode=0;mode<5;++mode) {
        m20::Assembler a(kEeResetVector);
        for(unsigned core=0;core<2;++core) {
            a.la(8,0xBF90019Au+core*0x400u);a.la(9,0x8000u);if(mode!=4)a.sh(9,0,8);
            for(unsigned pair=0;pair<6;++pair) {
                a.la(8,0xBF900180u+core*0x400u+pair*4u);a.la(9,0xA5C3u);a.sh(9,0,8);a.la(9,0x96u);a.sh(9,2,8);
                a.raw((0x25u<<26)|(8u<<21)|(9u<<16));a.nop();a.la(10,0xA0000100u+core*0x40u+pair*8u);a.sw(9,0,10);
                a.raw((0x25u<<26)|(8u<<21)|(9u<<16)|2u);a.nop();a.andi(9,9,0xFFu);a.sw(9,4,10);
            }
            a.la(8,0xBF900198u+core*0x400u);a.la(9,0xF30u+core*12u);a.sh(9,0,8);
            a.raw((0x25u<<26)|(8u<<21)|(9u<<16));a.nop();a.la(10,0xA0000130u+core*0x40u);a.sw(9,0,10);
        }
        if(mode==1){a.la(8,0xBF900182u);a.raw((0x25u<<26)|(8u<<21)|(9u<<16));a.nop();a.addiu(9,9,1);}
        else if(mode==2)a.sw(0,0,8);
        else if(mode==3)a.sb(0,0,8);
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> image(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),image.begin());iop.reset(image,nullptr,nullptr,false,std::nullopt,{},false,mode!=4);iop.run_until(1536);
        if(!mode) {
            require(!iop.stopped() && iop.spu2_sample_count(0)==2 && iop.spu2_sample_count(1)==2,"routing inactive sources preserves both quiet sample clocks");
            for(unsigned core=0;core<2;++core) {
                for(unsigned pair=0;pair<6;++pair)require(iop.ee_read(0x100u+core*0x40u+pair*8u,4)==0xA5C3u &&
                    iop.ee_read(0x104u+core*0x40u+pair*8u,4)==0x96u,"all voice bitmap halves remain independent");
                require(iop.ee_read(0x130u+core*0x40u,4)==0xF30u+core*12u,"core input routing retains its own configured mask");
            }
        } else require(iop.stopped(),"reserved readback bits, unsupported widths and unproved inactive sources stay explicit");
    }
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
