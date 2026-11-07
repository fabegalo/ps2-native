// Hardware layout and transmission fixtures from GS manual 4.1.5/8.3.2.
#include "boot_checks.hpp"
#include <array>
#include <iostream>
#include <set>
using namespace ps2native;
using namespace boot_checks;
namespace {
void setup(GsRegisters& gs,unsigned psm,unsigned width,unsigned height,unsigned x=0,unsigned y=0) {
    gs.general_write(0x50,(1ull<<48)|(std::uint64_t{psm}<<56));
    gs.general_write(0x51,(std::uint64_t{x}<<32)|(std::uint64_t{y}<<48));
    gs.general_write(0x52,width|(std::uint64_t{height}<<32));gs.general_write(0x53,0);
}
}
int main() try {
    constexpr std::array<std::array<unsigned,3>,17> common{{
        {0,0,0},{1,0,4},{2,0,16},{4,0,32},{8,0,2},{0,1,8},{0,2,64},{0,4,128},
        {16,0,512},{0,8,256},{63,63,8190},{64,0,8192},{0,64,16384},{64,64,24576},
        {15,0,54},{0,7,200},{15,7,254}
    }};
    for(unsigned psm:{2u,10u}) {
        GsVram vram;
        for(const auto& v:common)require(GsVram::address(0,128,psm,v[0],v[1])==v[2],"16-bit page/column diagram vector");
        require(GsVram::address(0,64,psm,32,0)==(psm==2u ? 2048u : 4096u) &&
            GsVram::address(0,64,psm,0,16)==(psm==2u ? 1024u : 2048u) &&
            GsVram::address(0,64,psm,0,32)==(psm==2u ? 4096u : 1024u),"16S has its independently specified block order");
        std::set<unsigned> locations;
        for(unsigned y=0;y<128u;++y)for(unsigned x=0;x<128u;++x) {
            require(locations.insert(GsVram::address(0,128,psm,x,y)).second,"four-page halfword mapping is bijective");
            vram.write(0,128,psm,x,y,(y<<8u)|x);
        }
        for(unsigned y=0;y<128u;++y)for(unsigned x=0;x<128u;++x)
            require(vram.read(0,128,psm,x,y)==((y<<8u)|x),"every packed halfword preserves its neighbors");
        vram.invalidate();vram.write(0,64,psm,0,0,0x1234);
        rejects([&]{(void)vram.read(0,64,psm,8,0);},"unwritten bits");
        vram.write(0,64,psm,8,0,0xABCD);require(vram.word_at(0)==0xABCD1234,"X bit 3 selects the upper halfword");
        vram.write(0,64,psm,0,0,0xFFFF,0xFF00);require(vram.word_at(0)==0xABCD12FF,"raw halfword preservation mask leaves the adjacent pixel intact");
        vram.write(0,64,psm,0,0,0x12FF,0,0,true);require(vram.read(0,64,psm,0,0)==0x12FF,"equal conditional writes retain knowledge");
        vram.write(0,64,psm,0,0,0x12FE,0,0,true);rejects([&]{(void)vram.read(0,64,psm,0,0);},"unwritten bits");
        require(vram.read(0,64,psm,8,0)==0xABCD,"conditional uncertainty is confined to its halfword");
        vram.write(0,64,psm,0,0,0,0,0x8000);rejects([&]{(void)vram.read(0,64,psm,0,0);},"unwritten bits");
        require(GsVram::address(GsVram::kBytes-256,64,psm,0,8)==0,"16-bit addresses wrap at four MiB");
        require(GsVram::address(256,64,psm,8,0)==258,"non-page-aligned buffer bases retain halfword lanes");
        GsRegisters gs;gs.reset(0);setup(gs,psm,4,3,2047,2047);
        gs.image_write(0x0004000300020001ull,0x0008000700060005ull);
        gs.image_write(0x000C000B000A0009ull,~0ull);
        for(unsigned n=0;n<12u;++n)require(gs.vram().read(0,64,psm,(2047+n%4)&2047u,(2047+n/4)&2047u)==n+1,
            "real IMAGE words pack four halfwords per doubleword and wrap coordinates");
        const auto count=gs.vram().pixel_writes();gs.general_write(0x54,~0ull);
        require(count==12u && gs.vram().pixel_writes()==count,"final quadword padding and later inactive HWREG writes are discarded");
        rejects([&]{setup(gs,psm,2,1);},"hardware alignment");
        setup(gs,psm,8,1);gs.image_write(0x0004000300020001ull,0x0008000700060005ull);
        gs.general_write(0x50,(1ull<<16)|(std::uint64_t{psm}<<24)|(1ull<<48)|(std::uint64_t{psm}<<56));
        gs.general_write(0x51,1ull|(2ull<<32)|(2ull<<59));gs.general_write(0x52,4ull|(1ull<<32));gs.general_write(0x53,2);
        for(unsigned n=0;n<4u;++n)require(gs.vram().read(0,64,psm,n+2,0)==n+2,"right-to-left local copy preserves overlapping packed pixels");
    }
    for(unsigned psm:{2u,10u}) {
        GsRegisters gs;gs.reset(0);setup(gs,psm,4,2,3,4);
        gs.image_write(0x0000000003E0001Full,0x000000007FFF7C00ull);
        gs.write(kGsPmode,8,0xFF25,0,{});gs.write(kGsSmode2,8,0,0,{});
        gs.write(kGsDispfb1,8,(1u<<9)|(std::uint64_t{psm}<<15)|(3ull<<32)|(4ull<<43),0,{});
        gs.write(kGsDisplay1,8,(1ull<<32)|(1ull<<44),0,{});
        const std::vector<std::uint8_t> expected{248,0,0,255,0,248,0,255,0,0,248,255,248,248,248,255};
        auto& vram=const_cast<GsVram&>(gs.vram());vram.write(0,64,psm,3,4,0x1F,0,0x8000);
        require(gs.scanout(12).rgba==expected,"CT16/16S scanout uses real swizzled RGB5, source offset and zero-bit expansion without consuming alpha");
        vram.write(0,64,psm,3,4,0x1F,0,1u);
        rejects([&]{(void)gs.scanout(12);},"unwritten bits");
    }
    std::cout<<"PSMCT16/16S swizzle, halfword knowledge, real IMAGE packing, wrap, overlap and RGB scanout passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
