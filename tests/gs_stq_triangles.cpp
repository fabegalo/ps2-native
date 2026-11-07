// Original register streams; no BIOS packets or firmware data.
#include "boot_checks.hpp"
#include "ps2native/gs_registers.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void setup(GsRegisters& gs,unsigned type=3,unsigned precision=0) {
    gs.reset(0);if(precision)gs.diagnostic_texture_precision(precision);
    gs.general_write(0,type);gs.general_write(1,0x80201008);
    gs.general_write(0x18,0);gs.general_write(0x1A,1);gs.general_write(0x4C,1u<<16u);
    gs.general_write(0x40,(7ull<<16u)|(7ull<<48u));gs.general_write(0x47,3u<<16u);
    gs.general_write(0x4E,1ull<<32u);gs.general_write(0x22,0);gs.general_write(0x4A,0);
    gs.general_write(0x50,(1ull<<16u)<<32u);gs.general_write(0x51,0);gs.general_write(0x52,(8ull<<32u)|8u);gs.general_write(0x53,0);
    for(unsigned n=0;n<16;++n)gs.image_write(0x8000000080000000ull,0x8000000080000000ull);
}
void vertex(GsRegisters& gs,unsigned x,unsigned y,unsigned z=0,bool draw=true) {
    gs.general_write(draw ? 5u : 0xDu,(x*16u)|(std::uint64_t{y*16u}<<16u)|(std::uint64_t{z}<<32u));
}
}
int main() try {
    for(const unsigned q:{0x3F800000u,0x3F000000u}) {
        GsRegisters gs;setup(gs,3u|16u);
        gs.general_write(0x50,(32ull|(1ull<<16u))<<32u);gs.general_write(0x51,0);gs.general_write(0x52,(8ull<<32u)|8u);gs.general_write(0x53,0);
        for(unsigned n=0;n<16;++n) {
            const auto a=0x80000000u|((n*4u)<<8u),b=a+0x100u,c=a+0x200u,d=a+0x300u;
            gs.image_write(a|(std::uint64_t{b}<<32u),c|(std::uint64_t{d}<<32u));
        }
        gs.general_write(0x3F,0);gs.general_write(6,32ull|(1ull<<14u)|(3ull<<26u)|(3ull<<30u)|(1ull<<34u)|(1ull<<35u));
        gs.general_write(8,0);gs.general_write(0x14,0);
        gs.general_write(1,(std::uint64_t{q}<<32u)|0x80000000u);
        const auto end=q==0x3F800000u ? 0x3F000000u : 0x3E800000u;
        gs.general_write(2,0);vertex(gs,0,0);gs.general_write(2,end);vertex(gs,4,0);gs.general_write(2,std::uint64_t{end}<<32u);vertex(gs,0,4);
        require(gs.vram().read(0,64,0,1,1)==0x80000900u && gs.vram().read(0,64,0,3,0)==0x80000300u,
            "constant-Q STQ triangles reuse exact affine coordinates, actual uploaded texels and DECAL");
    }
    for(const bool unknown:{false,true}) {
        GsRegisters gs;setup(gs,3u|16u);gs.general_write(6,32ull|(1ull<<14u)|(3ull<<26u)|(3ull<<30u));gs.general_write(8,0);gs.general_write(0x14,0);
        gs.general_write(1,0x3F80000080000000ull);gs.general_write(2,0);vertex(gs,0,0);vertex(gs,4,0);
        if(unknown)gs.general_write_observed(2,{0,0x100u});else gs.general_write(1,0x3F00000080000000ull);
        const auto before=gs.vram().pixel_writes();rejects([&]{vertex(gs,0,4);},unknown ? "unknown coordinate" : "varying-Q");
        require(gs.vram().pixel_writes()==before,"unproved STQ triangles refuse before any destination pixel changes");
    }
    std::cout<<"Constant-Q STQ triangles and uncertain-input refusals passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
