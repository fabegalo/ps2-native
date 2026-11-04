// Original UNPACK vectors and independent examples from EE manual 6.3.4-6.3.7.
#include "boot_checks.hpp"
#include "ps2native/vu_control.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void feed(Vif& vif,VuControl& vu,Gif& gif,GsRegisters& gs,std::vector<std::uint32_t> words) {
    while(words.size()%4u)words.push_back(0);
    for(std::size_t n=0;n<words.size();n+=4u)vif.write_fifo(std::uint64_t{words[n]}|(std::uint64_t{words[n+1]}<<32),
        std::uint64_t{words[n+2]}|(std::uint64_t{words[n+3]}<<32),gif,gs,&vu);
}
}
int main() try {
    {
        Memory memory;require(memory.diagnostic_vu_memory(0x11008000,8).unknown==~0ull,"a host snapshot preserves unknown cold micro memory without executing a bus access");
        rejects([&]{(void)memory.diagnostic_vu_memory(0x11008000,16);},"snapshot width");
    }
    for(const auto unit:{Vif::Unit::Vu0,Vif::Unit::Vu1})for(unsigned dimensions=1;dimensions<=4u;++dimensions) {
        Vif vif(unit);VuControl vu;Gif gif;GsRegisters gs;gs.reset(0);
        const auto base=unit==Vif::Unit::Vu0 ? 0x11004000u : 0x1100C000u;
        for(unsigned n=0;n<12u;++n)vu.write_memory(base+n*4u,4,{0xDEADBEEFu,0});
        std::vector<std::uint32_t> words{0x01000101,0x05000000,((0x60u+(dimensions-1u)*4u)<<24)|(3u<<16)};
        for(unsigned n=0;n<3u;++n)for(unsigned c=0;c<dimensions;++c)words.push_back(0x80000000u+n*16u+c);
        feed(vif,vu,gif,gs,words);require(!vif.processing() && !vif.read_register(0x60),"NUM counts complete output vectors");
        for(unsigned n=0;n<3u;++n)for(unsigned c=0;c<4u;++c) {
            const auto got=vu.read_memory(base+n*16u+c*4u,4);
            if(dimensions==1u || c<dimensions)require(!got.unknown && got.value==0x80000000u+n*16u+(dimensions==1u ? 0u : c),
                "known S/V2/V3/V4 input words reach their documented lanes");
            else require(got.unknown==0xFFFFFFFFu,"indeterminate V2 Z/W and V3 W overwrite knowledge instead of inventing duplicated input");
        }
    }
    for(const auto unit:{Vif::Unit::Vu0,Vif::Unit::Vu1}) {
        Vif vif(unit);VuControl vu;Gif gif;GsRegisters gs;gs.reset(0);
        const auto base=unit==Vif::Unit::Vu0 ? 0x11004000u : 0x1100C000u;
        const auto limit=unit==Vif::Unit::Vu0 ? 256u : 1024u,start=limit-3u;
        const std::array<unsigned,6> addresses{limit-3u,limit-2u,limit-1u,0,1,3};
        for(const auto at:addresses)for(unsigned lane=0;lane<4u;++lane)vu.write_memory(base+at*16u+lane*4u,4,{0x70000000u+lane,0});
        std::vector<std::uint32_t> words{0x01000506,0x05000002,0x20000000,0xB14E1BE4,
            0x30000000,10,20,30,40,0x31000000,100,200,300,400,0x7C060000u|start};
        for(unsigned n=0;n<6u;++n)for(unsigned lane=0;lane<4u;++lane)words.push_back(lane+1u);
        feed(vif,vu,gif,gs,words);
        constexpr std::uint32_t expected[6][4]{{11,20,100,0x70000003},{0x70000000,200,30,44},
            {300,0x70000001,33,44},{11,22,0x70000002,400},{11,24,0x70000002,400},{12,24,100,0x70000003}};
        for(unsigned n=0;n<6u;++n)for(unsigned lane=0;lane<4u;++lane) {
            const auto got=vu.read_memory(base+addresses[n]*16u+lane*4u,4);
            require(!got.unknown && got.value==expected[n][lane],"cycle mask, ROW/COL replacement, protected lanes and difference updates follow the manual examples");
        }
        require(vu.read_memory(base+2u*16u,4).unknown==0xFFFFFFFFu,"CL>WL skips storage without consuming extra vectors");
        for(unsigned lane=0;lane<4u;++lane)require(vif.read_register(0x100u+lane*16u)==std::array<unsigned,4>{12,24,33,44}[lane],
            "difference mode updates ROW only for input-selected lanes");
        if(unit==Vif::Unit::Vu1)feed(vif,vu,gif,gs,{0x01000101,0x05000000,0x030003FE,0x02000007,0x6C018003,1,2,3,4});
        else feed(vif,vu,gif,gs,{0x01000101,0x05000000,0x6C018003,1,2,3,4});
        const auto q=unit==Vif::Unit::Vu1 ? 1u : 3u;
        require(vu.read_memory(base+q*16u,8).value==0x200000001ull,"UNPACK FLG adds TOPS on VIF1 and does not affect VIF0 addressing");
    }
    {
        Vif vif;VuControl vu;Gif gif;GsRegisters gs;gs.reset(0);
        feed(vif,vu,gif,gs,{0x01000101,0x05000000,0x6C020000,11});
        require(vif.read_register(0x60)==2 && vu.read_memory(0x1100C000,8).unknown==~0ull,"partial vector cannot create a full VU write or decrement NUM");
        vif.write_fifo(12ull|(13ull<<32),14ull|(21ull<<32),gif,gs,&vu);
        require(vif.read_register(0x60)==1 && vu.read_memory(0x1100C000,8).value==0xC0000000Bull,"the fourth word commits the genuine first vector");
        vif.write_register(0x10,1,gif);require(!vif.processing() && vu.read_memory(0x1100C010,8).unknown==~0ull &&
            !vu.read_memory(0x1100C000,8).unknown,"VIF reset drops only the incomplete vector and preserves completed data");
    }
    {
        Vif vif;VuControl vu;Gif gif;GsRegisters gs;gs.reset(0);
        std::vector<std::uint32_t> words{0x01000101,0x05000000,0x60000384};
        for(unsigned n=0;n<256u;++n)words.push_back(n|0x80000000u);
        feed(vif,vu,gif,gs,words);require(!vif.processing(),"NUM zero transfers 256 actual vectors");
        for(unsigned n=0;n<256u;++n)require(vu.read_memory(0x1100C000u+((900u+n)&1023u)*16u,8).value==
            ((std::uint64_t{n|0x80000000u}<<32)|(n|0x80000000u)),"scalar NUM zero transfer wraps and broadcasts all 256 values");
        feed(vif,vu,gif,gs,{0x20000000,0x000000D0,0x70010000,0x55});
        require(vu.read_memory(0x1100C000,4).value==0x55 && vu.read_memory(0x1100C008,4).unknown==0xFFFFFFFFu,
            "selecting an uninitialized ROW keeps its bits unknown, while independent input lanes transfer");
    }
    {
        Vif vif;VuControl vu;Gif gif;GsRegisters gs;gs.reset(0);
        rejects([&]{vif.write_fifo(0x6C010000,0,gif,gs,&vu);},"known CYCLE and MODE");
        feed(vif,vu,gif,gs,{0x01000101,0x05000000});
        rejects([&]{vif.write_fifo(0x7C010000,0,gif,gs,&vu);},"known MASK");
        rejects([&]{vif.write_fifo(0x6C018000,0,gif,gs,&vu);},"known TOPS");
        rejects([&]{vif.write_fifo(0x6D010000,0,gif,gs,&vu);},"packed");
        rejects([&]{vif.write_fifo(0x6C010000,0,gif,gs);},"shared VU");
        feed(vif,vu,gif,gs,{0x01000201});rejects([&]{vif.write_fifo(0x6C010000,0,gif,gs,&vu);},"filling/zero");
    }
    {
        Iop dma;VuControl vu;Vif v0(Vif::Unit::Vu0),v1;Gif gif;GsRegisters gs;gs.reset(0);dma.reset({},nullptr);
        std::vector<std::uint8_t> ram(0x200);const std::uint32_t words[]{0x01000101,0x05000000,0x6C010002,11,12,13,14,0};
        for(unsigned n=0;n<8u;++n)for(unsigned b=0;b<4u;++b)ram[0x100+n*4u+b]=static_cast<std::uint8_t>(words[n]>>(8u*b));
        dma.attach_ee_ram(ram);dma.ee_dma_write(0x1000E000,1);dma.ee_dma_write(0x10009010,0x100);
        dma.ee_dma_write(0x10009020,2);dma.ee_dma_write(0x10009000,0x101);
        dma.advance_vif_dma(2,v0,v1,gif,gs,&vu);require(vu.read_memory(0x1100C020,8).unknown==~0ull && v1.read_register(0x60)==1,
            "one DMA beat supplies only part of the vector and cannot fake completion");
        dma.advance_vif_dma(4,v0,v1,gif,gs,&vu);
        require(vu.read_memory(0x1100C020,8).value==0xC0000000Bull && vu.read_memory(0x1100C028,8).value==0xE0000000Dull &&
            !vu.read_memory(0x1100C028,8).unknown && !v1.processing() && (dma.ee_dma_read(0x1000E010)&2u),
            "real DMA data reaches the shared VU aperture before the completion interrupt");
        require(!gs.vram().pixel_writes() && gif.packets().empty(),"UNPACK alone does not fabricate VU execution or graphics");
    }
    std::cout<<"32-bit UNPACK lane knowledge, masks, cycles, ROW/COL, NUM, wrap and real DMA passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
