// Own DMA/GIF image upload; exact source pixels, no pre-rendered framebuffer.
#include "boot_checks.hpp"
#include "ps2native/iop.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void qword(std::vector<std::uint8_t>& ram,unsigned at,std::uint64_t low,std::uint64_t high) {
    for(unsigned b=0;b<8;++b){ram.at(at+b)=static_cast<std::uint8_t>(low>>(8*b));ram.at(at+8+b)=static_cast<std::uint8_t>(high>>(8*b));}
}
void arm(Iop& dma,std::vector<std::uint8_t>& ram,unsigned low=0u) {
    dma.attach_ee_ram(ram);dma.ee_dma_write(0x1000E000u,1);
    dma.ee_dma_write(0x1000A010u,0x100u|low);dma.ee_dma_write(0x1000A020u,7);
    dma.ee_dma_write(0x1000A000u,0x101u);
}
}
int main() try {
    std::vector<std::uint8_t> ram(0x1000);
    qword(ram,0x100,4ull|(1ull<<60),0xE);
    qword(ram,0x110,1ull<<48,0x50); // PSMCT32, DBW=64 pixels.
    qword(ram,0x120,0,0x51);qword(ram,0x130,2ull|(2ull<<32),0x52);
    qword(ram,0x140,0,0x53);qword(ram,0x150,1ull|(1ull<<15)|(2ull<<58),0);
    qword(ram,0x160,0x5566778811223344ull,0xDDEEFF0099AABBCCull);
    Iop dma;Gif gif;GsRegisters gs;dma.reset({},nullptr);gif.reset();gs.reset(0);arm(dma,ram);
    dma.advance_gif_dma(1,gif,gs);
    require(dma.ee_dma_read(0x1000A020u)==7 && gif.packets().empty(),"DMA does not complete at submission");
    dma.advance_gif_dma(12,gif,gs);
    require(gs.vram().pixel_writes()==0 && gif.packets().empty() && !(dma.ee_dma_read(0x1000E010u)&4u),
            "setup and IMAGE tag cannot fabricate pixels or completion");
    dma.advance_gif_dma(14,gif,gs);
    require(gs.vram().pixel_writes()==4 && gif.packets().size()==1,"real source data completes the packet and writes four pixels");
    const std::uint32_t expected[]{0x11223344u,0x55667788u,0x99AABBCCu,0xDDEEFF00u};
    for(unsigned p=0;p<4;++p)require(gs.vram().read(0,64,0,p%2,p/2)==expected[p],"DMA preserves source pixels exactly");
    require(dma.ee_dma_read(0x1000A010u)==0x170u && !dma.ee_dma_read(0x1000A020u) &&
            !(dma.ee_dma_read(0x1000A000u)&0x100u) && (dma.ee_dma_read(0x1000E010u)&4u),"MADR/QWC/STR and D_STAT reflect the actual transfer");
    for(unsigned low=1u;low<16u;++low) {
        dma.reset({},nullptr);gif.reset();gs.reset(0);arm(dma,ram,low);
        require(dma.ee_dma_read(0x1000A010u)==0x100u,"MADR lower bits cannot select a byte lane");
        dma.advance_gif_dma(14,gif,gs);
        for(unsigned p=0;p<4;++p)require(gs.vram().read(0,64,0,p%2,p/2)==expected[p],
            "all fifteen low-bit aliases transfer bytes from the actual aligned source");
        require(dma.ee_dma_read(0x1000A010u)==0x170u && !dma.ee_dma_read(0x1000A020u) &&
            (dma.ee_dma_read(0x1000E010u)&4u),"low-bit aliases preserve actual byte count and completion timing");
    }
    dma.reset({},nullptr);gif.reset();gs.reset(0);arm(dma,ram);dma.set_ee_dma_suspended(true);
    dma.advance_gif_dma(100,gif,gs);require(dma.ee_dma_read(0x1000A020u)==7,"CPND stalls without consuming data");
    dma.set_ee_dma_suspended(false);dma.advance_gif_dma(114,gif,gs);require(gif.packets().size()==1,"resume transfers original bytes");
    for(const bool priority_gate:{false,true}) {
        dma.reset({},nullptr);gif.reset();gs.reset(0);arm(dma,ram);
        dma.ee_dma_write(priority_gate ? 0x1000E020u : 0x1000E000u,priority_gate ? 0x80000000u : 0u);
        dma.advance_gif_dma(100,gif,gs);
        require(dma.ee_dma_read(0x1000A020u)==7 && gif.packets().empty(),"DMAE and channel priority gate the actual transfer");
        dma.ee_dma_write(priority_gate ? 0x1000E020u : 0x1000E000u,priority_gate ? 0x80040000u : 1u);
        dma.advance_gif_dma(114,gif,gs);require(gif.packets().size()==1,"reenabling does not accumulate disabled cycles");
    }
    dma.reset({},nullptr);gif.reset();gs.reset(0);arm(dma,ram);
    std::unordered_map<std::uint32_t,std::uint32_t> unknown{{0x160u,1u}};dma.attach_ee_ram(ram,&unknown);
    rejects([&]{dma.advance_gif_dma(14,gif,gs);},"unknown RAM bits");
    require(!gs.vram().pixel_writes() && !(dma.ee_dma_read(0x1000E010u)&4u),"unknown source cannot create pixels or completion");
    // One packet crosses a referenced payload and the final inline END.
    qword(ram,0x300,(0x100ull<<32)|0x30000006u,~0ull);
    qword(ram,0x310,0x70000001u,~0ull);
    qword(ram,0x320,0x5566778811223344ull,0xDDEEFF0099AABBCCull);
    const auto chain=[&](unsigned at,unsigned control=0x105u) {
        dma.reset({},nullptr);gif.reset();gs.reset(0);dma.attach_ee_ram(ram);
        dma.ee_dma_write(0x1000E000u,1);dma.ee_dma_write(0x1000A020u,0);
        dma.ee_dma_write(0x1000A030u,at);dma.ee_dma_write(0x1000A000u,control);
    };
    chain(0x300);dma.advance_gif_dma(14,gif,gs);
    require(!gs.vram().pixel_writes() && (dma.ee_dma_read(0x1000A000u)&0x100u) &&
            dma.ee_dma_read(0x1000A030u)==0x310u,"REF sends real setup data but does not terminate chain");
    dma.advance_gif_dma(16,gif,gs);
    require(dma.ee_dma_read(0x1000A020u)==1 && !gs.vram().pixel_writes(),"END tag consumes its own bus beat");
    dma.advance_gif_dma(18,gif,gs);
    for(unsigned p=0;p<4;++p)require(gs.vram().read(0,64,0,p%2,p/2)==expected[p],"chained GIF exact source pixels");
    require(gif.packets().size()==1 && (dma.ee_dma_read(0x1000E010u)&4u) &&
            dma.ee_dma_read(0x1000A030u)==0x310u,"END completion and preserved TADR after actual pixels");
    for(unsigned low=1u;low<16u;++low) {
        chain(0x300u|low);require(dma.ee_dma_read(0x1000A030u)==0x300u,"TADR low bits cannot select a different tag");
        dma.advance_gif_dma(18,gif,gs);
        for(unsigned p=0;p<4;++p)require(gs.vram().read(0,64,0,p%2,p/2)==expected[p],"unaligned tag-register stores fetch the documented qword");
        require(dma.ee_dma_read(0x1000A030u)==0x310u && (dma.ee_dma_read(0x1000E010u)&4u),"tag aliases retain the true final tag and interrupt");
    }
    qword(ram,0x400,0x90000007u,~0ull);
    std::copy(ram.begin()+0x100,ram.begin()+0x170,ram.begin()+0x410);
    chain(0x400,0x185);dma.advance_gif_dma(16,gif,gs);
    require(gif.packets().size()==1 && !(dma.ee_dma_read(0x1000A000u)&0x100u) &&
            dma.ee_dma_read(0x1000A030u)==0x480u,"CNT IRQ/TIE terminates only after all payload qwords");
    qword(ram,0x500,0x70000000u,~0ull);chain(0x500);dma.advance_gif_dma(1,gif,gs);
    require(dma.ee_dma_read(0x1000A000u)&0x100u,"empty END is not immediate completion");
    dma.advance_gif_dma(2,gif,gs);require(gif.packets().empty() && (dma.ee_dma_read(0x1000E010u)&4u),
        "empty END fetch completes without sending tag garbage to GIF");
    std::cout<<"Normal GIF DMA preserves source pixels, timing boundary, suspension and unknown data\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
