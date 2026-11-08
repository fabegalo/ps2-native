// Shared source for offline native compilation and the Qt session fixture.
#pragma once
#include "direct_program.hpp"
#include "ps2native/console.hpp"
#include "ps2native/boot.hpp"
namespace experimental_test {
inline std::vector<std::uint8_t> firmware() {
    const auto* hardware=ps2native::find_console_hardware("SCPH-70004:GH-032-11");
    constexpr auto kEeResetVector=ps2native::kEeResetVector;
        auto raw=m20::build_firmware().rom;
        const auto ver=std::search(raw.begin(),raw.end(),std::begin(m20::kRomver),std::end(m20::kRomver)-1);
        if(ver==raw.end())throw std::runtime_error("synthetic ROMVER");std::copy(hardware->romver.begin(),hardware->romver.end(),ver);
        m20::Assembler a(kEeResetVector);
        const auto sh=[&](unsigned address,unsigned value){a.la(8,address);a.la(9,value);a.sh(9,0,8);};
        const auto sw=[&](unsigned address,unsigned value){a.la(8,address);a.la(9,value);a.sw(9,0,8);};
        sh(0xBF90019A,0);sh(0xBF90059A,0);
        const unsigned base=0xBF900400;
        sh(base+0x1A8,0);sh(base+0x1AA,0x2800);
        sh(base+0x1AC,0x0404);for(unsigned i=0;i<7;++i)sh(base+0x1AC,0x7777);
        sh(base+0x1AC,0x0304);for(unsigned i=0;i<7;++i)sh(base+0x1AC,0x9999);
        sh(base+0x19A,0x8010);for(unsigned i=0;i<40;++i)a.nop();sh(base+0x19A,0x8000);
        for(unsigned reg=0x180;reg<0x198;reg+=2)sh(base+reg,(reg==0x188 || reg==0x190)?1:0);
        sh(base+0x198,0xC00);sh(0xBF900788,0x3FFF);sh(0xBF90078A,0x3FFF);
        sh(base,0x3FFF);sh(base+2,0x3FFF);sh(base+4,0x1000);sh(base+6,0xF);sh(base+8,0x1FC0);
        sh(base+0x1C0,0);sh(base+0x1C2,0x2800);sh(base+0x1A0,1);
        sw(0xBF801070,0);sw(0xBF801078,0);
        a.label("poll");sw(0xBF808268,0x3BC);sw(0xBF808200,(5u<<18)|(5u<<8)|0x40);
        a.la(8,0xBF808260);
        for(unsigned i=0;i<5;++i){a.la(9,i==0?1:i==1?0x42:0);a.sb(9,0,8);}
        sw(0xBF808204,0);sw(0xBF808268,0x3B1);
        for(unsigned i=0;i<80;++i)a.nop();
        a.la(8,0xBF808264);a.la(10,0xA0000300);
        for(unsigned i=0;i<5;++i){a.lbu(9,0,8);a.nop();a.sb(9,i,10);}
        sw(0xBF808280,1);a.beq(0,0,"poll");a.nop();
        const auto code=m20::le_words(a.finish());if(code.size()>=m20::kResetSize)throw std::runtime_error("synthetic code exceeds RESET");
        std::copy(code.begin(),code.end(),raw.begin());
    return raw;
}
inline std::vector<std::uint8_t> ee_elf() {
    direct_test::Program p;
    p.segments.push_back({0x10000,5,m20::le_words({0x1000FFFFu,0,0xDu})});
    auto bytes=direct_test::elf(p);
    direct_test::put(bytes,24,0x10000);
    return bytes;
}
}
