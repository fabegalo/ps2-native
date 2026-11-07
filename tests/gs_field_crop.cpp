#include "boot_checks.hpp"
#include "ps2native/gs_registers.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void setup(GsRegisters& gs,unsigned height,unsigned dy=0,unsigned magv=1,bool known=true) {
    gs.reset(0,0x00C7800601A01801ull,known?std::optional<bool>{false}:std::nullopt);
    gs.write(kGsSmode2,8,1,0,{});
    gs.write(kGsPmode,8,1,0,{});
    gs.write(kGsDispfb1,8,1u<<9,0,{});
    gs.write(kGsDisplay1,8,(std::uint64_t{height-1}<<44)|(std::uint64_t{magv-1}<<27)|(std::uint64_t{dy}<<12),0,{});
    auto& vram=const_cast<GsVram&>(gs.vram());
    for(unsigned y=0;y<8;++y)vram.write(0,64,0,0,y,0x80000000u|y);
}
void rows(const Framebuffer& frame,std::initializer_list<unsigned> expected) {
    require(frame.width==1 && frame.height==expected.size(),"native field dimensions");
    unsigned n=0;for(auto y:expected) {
        require(frame.rgba.at(4*n)==y && frame.rgba.at(4*n+1)==0 && frame.rgba.at(4*n+2)==0 && frame.rgba.at(4*n+3)==255,"independent source row and RGB expansion");++n;
    }
}
void next_field(GsRegisters& gs) {const auto c=gs.vertical_clock();gs.advance(c.epoch+c.half_line*7);}
}
int main() try {
    GsRegisters odd;setup(odd,5);rows(odd.scanout(0),{0,2,4});next_field(odd);rows(odd.scanout(0),{1,3});
    GsRegisters even;setup(even,4);rows(even.scanout(0),{0,2});next_field(even);rows(even.scanout(0),{1,3});
    GsRegisters shifted;setup(shifted,5,1);rows(shifted.scanout(0),{0,2});next_field(shifted);rows(shifted.scanout(0),{1,3,5});
    GsRegisters unknown;setup(unknown,5,0,1,false);rejects([&]{(void)unknown.scanout(0);},"field parity is unknown");
    GsRegisters fractional;setup(fractional,5,0,2);rejects([&]{(void)fractional.scanout(0);},"fractional display magnification");
    std::cout<<"GS interlaced crop checks passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
