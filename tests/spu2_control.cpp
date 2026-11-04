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
        if(!mode || mode==3)require(!iop.stopped() && iop.spu2_sample_count(0)==2 && iop.spu2_last_sample(0)->left==0,
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
    for(unsigned mode=0;mode<4;++mode) {
        m20::Assembler a(kEeResetVector);
        for(unsigned core=0;core<2;++core) {
            a.la(8,0xBF90019Au+core*0x400u);if(mode!=2)a.sh(0,0,8);
            a.la(8,0xBF9002E0u+core*0x400u);a.la(9,core+1u);a.sh(9,0,8);a.la(9,0xF012u+core);a.sh(9,2,8);
            a.la(8,0xBF90033Cu+core*0x400u);a.la(9,14u-core);if(mode!=1)a.sh(9,0,8);
            a.raw((0x25u<<26)|(8u<<21)|(9u<<16));a.nop();if(mode!=3)a.andi(9,9,0xFu);else a.addiu(9,9,1);
            a.la(10,0xA0000100u+core*4u);a.sw(9,0,10);
            a.la(8,0xBF9002E0u+core*0x400u);
            a.raw((0x25u<<26)|(8u<<21)|(9u<<16));a.nop();a.andi(9,9,0xFu);
            a.la(10,0xA0000110u+core*8u);a.sw(9,0,10);
            a.raw((0x25u<<26)|(8u<<21)|(9u<<16)|2u);a.nop();a.sw(9,4,10);
        }
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> image(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),image.begin());iop.reset(image,nullptr);iop.run_until(80);
        if(!mode)require(!iop.stopped() && iop.ee_read(0x100,4)==14 && iop.ee_read(0x104,4)==13,
                         "disabled effects retain independent workspace end addresses without requiring an invented reset state");
        if(!mode)for(unsigned core=0;core<2;++core)require(iop.ee_read(0x110u+core*8u,4)==core+1u &&
            iop.ee_read(0x114u+core*8u,4)==0xF012u+core,"effect workspace start halves retain word-address bits without clearing RAM");
        if(mode)require(iop.stopped(),"unknown workspace reset, effect-enable state and reserved readback bits stay explicit");
    }
    for(unsigned mode=0;mode<6;++mode) {
        m20::Assembler a(kEeResetVector);a.la(10,0xA0000100u);
        for(unsigned core=0;core<2;++core) {
            const auto base=0xBF900000u+core*0x400u;
            for(unsigned field=0;field<5;++field) {
                a.la(8,base+23u*16u+field*2u);a.la(9,field==2 ? 0x3FFFu : field==3 ? 0x1234u : 0u);
                if(mode==2 && field==0)a.la(9,0x8000u);
                if(mode!=5)a.sh(9,0,8);
                a.lhu(9,0,8);a.nop();a.sw(9,core*32u+field*4u,10);
            }
            a.la(8,base+0x1C0u);a.la(9,0xAu);a.sh(9,0,8);a.la(9,0x2803u);a.sh(9,2,8);
            // Copy unknown reserved readback bits into another SSA: only
            // the documented address bits are consumed by the destination.
            a.la(11,base+0x1CCu);a.lhu(9,0,8);a.nop();a.sh(9,0,11);
            a.andi(9,9,0xFu);a.sw(9,core*32u+20u,10);
            a.lhu(9,2,8);a.nop();a.sh(9,2,11);a.andi(9,9,0xFFF8u);a.sw(9,core*32u+24u,10);
            if(mode==3){a.la(8,base+0xAu);a.sh(0,0,8);}
            if(mode==4){a.la(8,base+0x1A0u);a.la(9,1);a.sh(9,0,8);}
        }
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> image(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),image.begin());iop.reset(image,nullptr,nullptr,false,std::nullopt,{},false,mode!=1);iop.run_until(400);
        if(!mode)for(unsigned core=0;core<2;++core)require(!iop.stopped() && iop.ee_read(0x108u+core*32u,4)==0x3FFFu &&
            iop.ee_read(0x10Cu+core*32u,4)==0x1234u && iop.ee_read(0x114u+core*32u,4)==0xAu &&
            iop.ee_read(0x118u+core*32u,4)==0x2800u,"inactive voices retain independent pitch, envelope configuration and aligned start addresses");
        else require(iop.stopped(),"voice startup, envelope processing, sweeps, unknown reset and active configuration remain explicit");
    }
    for(unsigned mode=0;mode<8u;++mode) {
        m20::Assembler a(kEeResetVector);
        const auto sh=[&](std::uint32_t address,std::uint32_t value){a.la(8,address);a.la(9,value);a.sh(9,0,8);};
        for(unsigned core=0;core<2u;++core) {
            if(mode!=7u || !core)sh(0xBF90019Au+core*0x400u,0x8000u);
            for(unsigned voice=0;voice<24u;++voice) {
                sh(0xBF900000u+core*0x400u+voice*16u,mode==1u ? 1u : 0u);
                sh(0xBF900002u+core*0x400u+voice*16u,0u);
            }
        }
        // Position command writes away from a synthesis edge, then copy
        // both halves as a driver may do. Reserved bits remain unknown.
        a.label("keys");sh(0xBF9005A0u,0xFFFFu);
        a.lhu(9,0,8);a.nop();a.la(8,0xBF9001A0u);a.sh(9,0,8);
        sh(0xBF9005A2u,0xFFu);a.lhu(9,0,8);a.nop();a.la(8,0xBF9001A2u);a.sh(9,0,8);
        a.label("pending");a.la(10,0xA0000100u);
        a.la(8,0xBF9001A0u);a.lhu(9,0,8);a.nop();a.sw(9,0,10);
        for(unsigned n=0;n<800u;++n)a.nop();
        a.lhu(9,0,8);a.nop();a.sw(9,4,10);
        if(mode==2u)sh(0xBF900000u,1u);
        if(mode==3u){sh(0xBF9001A4u,0xFFFFu);sh(0xBF900000u,1u);}
        if(mode==4u)sh(0xBF90059Au,0x40u); // disabled core still compares shared RAM addresses
        if(mode==5u){a.la(8,0xBF90000Au);a.lhu(9,0,8);}
        if(mode==6u){a.la(8,0xBF900340u);a.lhu(9,0,8);}
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> image(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),image.begin());iop.reset(image,nullptr,nullptr,false,std::nullopt,{},false,true);
        iop.run_until(2304u);
        if(!mode)require(!iop.stopped() && iop.ee_read(0x100u,4)==0xFFFFu && iop.ee_read(0x104u,4)==0u &&
            iop.spu2_sample_count(0)==3u && iop.spu2_last_sample(1)->right==0,
            "zero gains prove the mixer output while key commands clear at the sample clock; envelopes stay unknown");
        else require(iop.stopped(),"symbolic voices refuse audible gain, release guesses, IRQ enable, unknown core IRQ and invented ENVX/ENDX");
    }
    for(const bool active:{false,true}) {
        m20::Assembler a(kEeResetVector);a.la(10,0xA0000100u);
        const auto sh=[&](std::uint32_t address,std::uint32_t value){a.la(8,address);a.la(9,value);a.sh(9,0,8);};
        for(unsigned core=0;core<2u;++core)sh(0xBF90019Au+core*0x400u,0x8000u);
        if(active){sh(0xBF900000u,0u);sh(0xBF900002u,0u);sh(0xBF9001A0u,1u);}
        for(unsigned core=0;core<2u;++core)for(unsigned bank=0;bank<2u;++bank) {
            sh(0xBF900340u+core*0x400u+bank*2u,0xFFFFu);
            a.lhu(9,0,8);a.nop();a.sh(9,0,8); // clear ignores unknown reserved source bits
            a.andi(9,9,bank ? 0xFFu : 0xFFFFu);
            a.sw(9,core*8u+bank*4u,10);
        }
        for(unsigned n=0;n<800u;++n)a.nop();
        a.la(8,0xBF900340u);a.lhu(9,0,8);a.nop();a.sw(9,16,10);
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> image(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),image.begin());iop.reset(image,nullptr,nullptr,false,std::nullopt,{},false,true);
        iop.run_until(1600u);
        if(active)require(iop.stopped() && iop.stop_reason()->find("ENDX depends")!=std::string::npos,
                          "ENDX clear cannot invent later decoder flags for a symbolic voice");
        else {
            require(!iop.stopped(),"ENDX clear stays known without an active decoder");
            for(unsigned offset=0;offset<=16u;offset+=4u)require(iop.ee_read(0x100u+offset,4)==0u,
                "writes of any data clear the selected ENDX bank on either core");
        }
    }
    {
        m20::Assembler a(kEeResetVector);a.la(10,0xA0000100u);
        for(unsigned core=0;core<2u;++core) {
            a.la(8,0xBF90033Cu+core*0x400u);a.lhu(9,0,8);a.nop();a.addiu(9,9,1);a.sw(9,core*4u,10);
        }
        a.label("configured");a.la(8,0xBF90019Au);a.sh(0,0,8);
        a.la(8,0xBF90033Cu);a.la(9,3u);a.sh(9,0,8);a.lhu(9,0,8);a.nop();a.addiu(9,9,1);
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> image(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),image.begin());
        for(unsigned pass=0;pass<3u;++pass) {
            iop.reset(image,nullptr);
            if(pass==1u)iop.diagnostic_spu2_effect_end_reset({0xBu,0xCu});
            const auto boundary=(a.at("configured")-kEeResetVector)/4u;
            iop.run_until(boundary);
            if(pass!=1u){require(iop.stopped(),"initial EEA stays unknown by default and after reset");continue;}
            require(!iop.stopped() && iop.ee_read(0x100,4)==0xCu && iop.ee_read(0x104,4)==0xDu,
                    "explicit EEA experiment supplies only the selected initial readbacks");
            bool rejected=false;try{iop.diagnostic_spu2_effect_end_reset({1,2});}catch(const DeviceRefusal&){rejected=true;}
            require(rejected,"initial EEA cannot be replaced after execution started");
            iop.run_until(100);
            require(iop.stopped() && iop.stop_reason()->find("reserved readback")!=std::string::npos,
                    "guest EEA writes supersede the initial experiment; reserved bits are not fabricated thereafter");
        }
        iop.reset(image,nullptr);bool rejected=false;
        try{iop.diagnostic_spu2_effect_end_reset({0x10,0});}catch(const DeviceRefusal&){rejected=true;}
        require(rejected,"EEA experiment rejects values outside the address field");
    }
    {
        m20::Assembler a(kEeResetVector);a.la(10,0xA0000100u);
        for(unsigned core=0;core<2u;++core) {
            const auto base=0xBF900000u+core*0x400u;
            a.la(8,base+0x19Au);a.sh(0,0,8);
            a.la(8,base+0x33Cu);a.la(9,14u+core);a.sh(9,0,8);a.lhu(9,0,8);a.nop();
            a.r(0,9,9,17,0x00u);a.la(11,0x1FFFFu);a.r(9,11,9,0,0x25u);
            a.la(11,14u);a.r(9,11,9,0,0x23u);
            a.la(8,base+0x2E0u);a.r(0,9,11,17,0x02u);a.sh(11,0,8);
            a.r(0,9,11,1,0x02u);a.sh(11,2,8);
            a.lhu(9,0,8);a.nop();a.andi(9,9,0xFu);a.sw(9,core*8u,10);
            a.lhu(9,2,8);a.nop();a.sw(9,core*8u+4u,10);
        }
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> image(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),image.begin());iop.reset(image,nullptr);iop.run_until(200);
        for(unsigned core=0;core<2u;++core)require(!iop.stopped() && iop.ee_read(0x100u+core*8u,4)==14u+core &&
            iop.ee_read(0x104u+core*8u,4)==0xFFF8u,"EEA byte-address arithmetic and ESA writes consume only proven address bits");
    }
    for(unsigned mode=0;mode<4u;++mode) {
        m20::Assembler a(kEeResetVector);a.la(10,0xA0000100u);
        for(unsigned core=0;core<2u;++core) {
            a.la(8,0xBF90019Au+core*0x400u);if(mode!=1u)a.sh(0,0,8);
            const auto base=0xBF900760u+core*0x28u;
            a.la(8,base);a.la(9,0x1234u);a.sh(9,0,8);
            a.la(8,base+0x10u);a.la(9,0xFFFFu);a.sh(9,0,8);a.sh(9,2,8);
            if(mode==2u)a.lhu(9,0,8);
            if(mode==3u)a.sw(9,0,8);
            a.la(8,base);a.lhu(9,0,8);a.nop();a.sw(9,core*32u,10);
            for(unsigned field=0x14u;field<0x28u;field+=2u) {
                a.la(8,base+field);a.la(9,0xF000u+field+core);a.sh(9,0,8);
                a.lhu(9,0,8);a.nop();a.sw(9,core*32u+4u,10);
            }
        }
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> image(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),image.begin());iop.reset(image,nullptr);iop.run_until(300u);
        if(!mode)for(unsigned core=0;core<2u;++core)require(!iop.stopped() && iop.ee_read(0x100u+core*32u,4)==0x1234u &&
            iop.ee_read(0x104u+core*32u,4)==0xF026u+core,"read-only MVOLX writes do not change gain; disabled effects retain all coefficients independently");
        else require(iop.stopped(),"MVOLX readback, unsupported widths and unproved disabled effects remain explicit");
    }
    for(unsigned synth_core=0;synth_core<2u;++synth_core)for(unsigned mode=0;mode<8u;++mode) {
        m20::Assembler a(kEeResetVector);unsigned padding=0;
        const auto pad=[&](unsigned cycle){const auto name="pad"+std::to_string(padding++);a.label(name);
            for(auto current=(a.at(name)-kEeResetVector)/4u;current<cycle;++current)a.nop();};
        const auto sh=[&](std::uint32_t address,std::uint32_t value){a.la(8,address);a.la(9,value);a.sh(9,0,8);};
        for(unsigned core=0;core<2u;++core)sh(0xBF90019Au+core*0x400u,0x8000u);
        const auto voice=0xBF900170u+synth_core*0x400u; // voice 23, checks the upper command bank
        sh(voice,0);sh(voice+2u,0);sh(voice+8u,mode==2u ? 0x20u : mode==3u ? 12u : 0u);
        sh(0xBF9001A2u+synth_core*0x400u,0x80u);pad(780u);sh(0xBF9001A6u+synth_core*0x400u,0x80u);
        pad(1540u);
        if(mode==4u)sh(voice+8u,0x20u);
        if(mode!=1u)pad(2310u);
        a.la(8,voice+0xAu);a.lhu(9,0,8);a.nop();a.la(10,0xA0000100u);a.sw(9,0,10);
        if(mode==5u || mode==6u)sh(voice,1u);
        if(mode==5u || mode==7u)sh(0xBF9001A2u+synth_core*0x400u,0x80u);
        if(mode==7u){a.la(8,voice+0xAu);a.lhu(9,0,8);}
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> image(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),image.begin());iop.reset(image,nullptr,nullptr,false,std::nullopt,{},false,true);
        iop.run_until(3072u);
        if(mode==0u || mode==6u)require(!iop.stopped() && iop.ee_read(0x100,4)==0u && iop.spu2_last_sample(synth_core)->left==0,
            "two linear release steps prove ENVX zero and permit gain changes without inventing decoded audio");
        else require(iop.stopped(),"early ENVX, exponential/slow/changed release and a new attack cannot reuse a silence proof");
    }
    for(unsigned effect_core=0;effect_core<2u;++effect_core)for(unsigned mode=0;mode<20u;++mode) {
        m20::Assembler a(kEeResetVector);
        const auto sh=[&](std::uint32_t address,std::uint32_t value){a.la(8,address);a.la(9,value);a.sh(9,0,8);};
        const auto base=0xBF900000u+effect_core*0x400u,volume=0xBF900764u+effect_core*0x28u;
        for(unsigned core=0;core<2u;++core)sh(0xBF90019Au+core*0x400u,0x8000u);
        sh(base+0x1A8u,0);sh(base+0x1AAu,0xFFF8u);sh(base+0x1ACu,0xABCDu);sh(base+0x19Au,0x8010u);
        for(unsigned n=0;n<20u;++n)a.nop();sh(base+0x19Au,0x8000u);
        sh(base+0x1A8u,1);sh(base+0x1AAu,0xFFF8u);sh(base+0x1ACu,0x5678u);sh(base+0x19Au,0x8010u);
        for(unsigned n=0;n<20u;++n)a.nop();sh(base+0x19Au,0x8000u);
        if(mode<9u || mode==10u || mode>=13u)sh(base+0x2E0u,0);
        if(mode<9u || mode>=13u)sh(base+0x2E2u,mode==5u ? 0x2000u : 0xFFF8u);
        if(mode!=3u)sh(base+0x33Cu,0);
        sh(volume,mode==1u ? 1u : 0u);sh(volume+2u,0);
        if(mode==4u)sh(0xBF90019Au+(effect_core^1u)*0x400u,0x40u);
        a.label("enable");sh(base+0x19Au,0x8080u);
        if(mode==2u)sh(volume+2u,1u);
        if(mode==6u)sh(base+0x19Au,0x8010u);
        if(mode==7u)sh(0xBF90019Au+(effect_core^1u)*0x400u,0x40u);
        if(mode==8u)sh(base+0x19Au,0x8000u);
        if(mode==13u)sh(base+0x33Cu,1u);
        if(mode==14u)sh(base+0x2E2u,0x2000u);
        if(mode==15u)sh(base+0x2E0u,1u);
        if(mode==16u)sh(base+0x2E2u,0xFFF0u);
        if(mode>=17u) {
            for(unsigned offset=0x2E4u;offset<=0x33Au;offset+=2u)sh(base+offset,0xFFFFu-offset);
            for(unsigned offset=0x14u;offset<0x28u;offset+=2u)sh(volume-4u+offset,0x8000u+offset);
            if(mode==18u){a.la(8,base+0x2E4u);a.lhu(9,0,8);a.nop();}
            if(mode==19u){a.la(8,base+0x2E4u);a.sw(0,0,8);}
        }
        if(mode==12u){a.la(8,base+0x2E0u);a.lhu(9,0,8);a.nop();}
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> image(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),image.begin());iop.reset(image,nullptr,nullptr,false,std::nullopt,{},false,true);
        if(mode>=9u && mode<13u) {
            iop.diagnostic_spu2_effect_start_reset(mode==10u ? std::array<std::uint32_t,2>{0xFFFF8u,0xFFFF8u} :
                                                                           std::array<std::uint32_t,2>{0xFFF8u,0xFFF8u});
            if(mode==11u)iop.reset(image,nullptr,nullptr,false,std::nullopt,{},false,true);
        }
        iop.diagnostic_spu2_pio_word_cycles(3u);iop.run_until((a.at("enable")-kEeResetVector)/4u);
        require(!iop.stopped() && iop.spu2_ram_halfword(0xFFF8u)==0xABCDu,"fixture uploads real RAM before effects start");
        iop.run_until(1536u);
        if(mode==13u)require(!iop.spu2_ram_halfword(0x1FFF8u),"extending active EEA invalidates newly reachable RAM");
        else require(iop.spu2_ram_halfword(0x1FFF8u)==0x5678u,"rejected or disjoint workspace writes preserve unrelated RAM");
        if(mode==0u || mode==8u || mode==9u || mode==10u || mode==13u || mode==16u || mode==17u)require(!iop.stopped() && !iop.spu2_ram_halfword(0xFFF8u) &&
            iop.spu2_last_sample(effect_core)->left==0 && iop.spu2_last_sample(effect_core)->right==0,
            "zero effect gains prove output while workspace bytes become unknown, including after disable");
        else {
            require(iop.stopped(),"unknown effects refuse audible gain, absent workspace, IRQ, dynamic-buffer overlap and transfer arbitration");
            if(mode==1u || mode==3u || mode==4u || mode==5u)
                require(iop.spu2_ram_halfword(0xFFF8u)==0xABCDu,"rejected effect start leaves prior RAM knowledge untouched");
        }
        bool late=false;try{iop.diagnostic_spu2_effect_start_reset({0xFFF8u,0xFFF8u});}catch(const DeviceRefusal&){late=true;}
        require(late,"initial ESA experiment cannot be changed after guest execution");
    }
    for(const auto bad: {0x100000u,0xFFF9u}) {
        std::vector<std::uint8_t> image(4u*1024u*1024u);iop.reset(image,nullptr);
        bool rejected=false;try{iop.diagnostic_spu2_effect_start_reset({bad,0u});}catch(const DeviceRefusal&){rejected=true;}
        require(rejected,"initial ESA experiment rejects out-of-range or unaligned addresses");
    }
    for(unsigned core=0;core<2u;++core)for(const auto reg:{0x2E0u,0x33Cu})for(unsigned mode=0;mode<5u;++mode) {
        m20::Assembler a(kEeResetVector);const auto base=0xBF900000u+core*0x400u;
        a.la(8,base+0x19Au);a.sh(0,0,8);a.la(8,base+(mode==4u ? 0x1C0u : reg));
        a.la(9,0xABCDu);if(mode!=3u)a.sh(9,0,8);a.lhu(9,0,8);a.nop();a.addiu(9,9,1);
        a.la(10,0xA0000100u);a.sw(9,0,10);a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> image(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),image.begin());iop.reset(image,nullptr,nullptr,false,std::nullopt,{},false,true);
        if(mode)iop.diagnostic_spu2_effect_address_reserved_zero();
        if(mode==2u)iop.reset(image,nullptr,nullptr,false,std::nullopt,{},false,true);
        iop.run_until(100u);
        if(mode==1u)require(!iop.stopped() && iop.ee_read(0x100u,4)==14u,
            "explicit reserved-zero effect readback hypothesis retains the written address field on either core");
        else require(iop.stopped(),"default/reset, absent address writes and unrelated voice addresses remain unknown");
        bool late=false;try{iop.diagnostic_spu2_effect_address_reserved_zero();}catch(const DeviceRefusal&){late=true;}
        require(late,"readback experiment cannot be enabled after guest execution");
    }
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
