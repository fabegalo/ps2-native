// Original micro-RAM byte patterns: transferred as data, never executed.
#include "boot_checks.hpp"
#include "ps2native/vu_control.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
int main() try {
    for(const auto unit:{Vif::Unit::Vu0,Vif::Unit::Vu1}) {
        const auto base=unit==Vif::Unit::Vu0 ? 0x11000000u : 0x11008000u;
        const auto bytes=unit==Vif::Unit::Vu0 ? 4096u : 16384u;
        VuControl vu;Vif vif(unit);Gif gif;GsRegisters gs;gs.reset(0);
        vif.write_fifo(0x4A02000100000000ull,0xFEDCBA9876543210ull,gif,gs,&vu);
        require(vu.read_memory(base+8,8).value==0xFEDCBA9876543210ull && !vu.read_memory(base+8,8).unknown &&
            vu.read_memory(base+16,8).unknown==~0ull,"the first genuine 64-bit instruction is stored, the absent second remains unknown");
        require(vif.processing() && vif.read_register(0x60)==1,"NUM decrements only after a whole instruction arrives");
        vif.write_fifo(0x8123456789ABCDEFull,0,gif,gs,&vu);
        require(!vif.processing() && vif.read_register(0x60)==0 && vu.read_memory(base+16,8).value==0x8123456789ABCDEFull,
            "instruction bits including high interrupt/opcode bits are payload, followed by ordinary NOP parsing");
        require(gif.packets().empty() && !gs.vram().pixel_writes() && vu.read(29)==0,"a microcode upload never fabricates execution, graphics or VU busy state");
        vif.write_dma_tag(std::uint64_t{0x4A000000u|(bytes/8u-1u)}<<32,gif,gs,&vu);
        require(vif.read_register(0x60)==0 && vif.processing(),"NUM zero denotes 256 pending instructions");
        for(unsigned n=0;n<128u;++n)vif.write_fifo(0x100000000ull+n*2u,0x100000001ull+n*2u,gif,gs,&vu);
        require(!vif.processing() && vu.read_memory(base+bytes-8u,8).value==0x100000000ull &&
            vu.read_memory(base+254u*8u,8).value==0x1000000FFull,"MPG instruction-address wrap stays inside the selected shared micro RAM");
        vif.write_dma_tag(0x4A02000000000000ull,gif,gs,&vu);vif.write_register(0x10,1,gif);
        require(!vif.processing() && !vu.read_memory(base,8).unknown,"VIF reset cancels a partial upload while preserving already transferred VU bytes");
        rejects([&]{vif.write_fifo(0x4A010000u,0,gif,gs,&vu);},"64-bit alignment");
        vif.reset();rejects([&]{vif.write_fifo(std::uint64_t{0x4A010000u|bytes/8u}<<32,0,gif,gs,&vu);},"load address");
        vif.reset();rejects([&]{vif.write_fifo(0x4A01000000000000ull,0,gif,gs);},"shared VU");
    }
    {
        Iop dma;VuControl vu;Vif v0(Vif::Unit::Vu0),v1;Gif gif;GsRegisters gs;gs.reset(0);dma.reset({},nullptr);
        std::vector<std::uint8_t> ram(0x200);const std::uint32_t words[]{0,0x4A030001,1,0x80000001,2,0x80000002,3,0x80000003};
        for(unsigned n=0;n<8u;++n)for(unsigned b=0;b<4u;++b)ram[0x100+n*4u+b]=static_cast<std::uint8_t>(words[n]>>(8u*b));
        dma.attach_ee_ram(ram);dma.ee_dma_write(0x1000E000,1);dma.ee_dma_write(0x10009010,0x100);
        dma.ee_dma_write(0x10009020,2);dma.ee_dma_write(0x10009000,0x101);
        dma.advance_vif_dma(1,v0,v1,gif,gs,&vu);require(vu.read_memory(0x11008008,8).unknown==~0ull,"MPG DMA has no immediate memory side effect");
        dma.advance_vif_dma(2,v0,v1,gif,gs,&vu);
        require(vu.read_memory(0x11008008,8).value==0x8000000100000001ull && v1.read_register(0x60)==2 &&
            dma.ee_dma_read(0x10009020)==1,"one actual DMA bus beat loads one instruction and leaves the remaining payload queued");
        dma.advance_vif_dma(4,v0,v1,gif,gs,&vu);
        require(vu.read_memory(0x11008018,8).value==0x8000000300000003ull && !v1.processing() &&
            !(dma.ee_dma_read(0x10009000)&0x100u) && (dma.ee_dma_read(0x1000E010)&2u),"DMA completion follows all real microprogram bytes");
    }
    {
        Iop dma;VuControl vu;Vif v0(Vif::Unit::Vu0),v1;Gif gif;GsRegisters gs;gs.reset(0);dma.reset({},nullptr);
        std::vector<std::uint8_t> ram(0x200);const std::uint32_t words[]{0x70000002u,0,0,0x4A030000u,11,12,13,14,15,16,0,0};
        for(unsigned n=0;n<12u;++n)for(unsigned b=0;b<4u;++b)ram[0x80+n*4u+b]=static_cast<std::uint8_t>(words[n]>>(8u*b));
        dma.attach_ee_ram(ram);dma.ee_dma_write(0x1000E000,1);dma.ee_dma_write(0x10008020,0);
        dma.ee_dma_write(0x10008030,0x80);dma.ee_dma_write(0x10008000,0x145);
        dma.advance_vif_dma(2,v0,v1,gif,gs,&vu);
        require(v0.processing() && v0.read_register(0x60)==3 && vu.read_memory(0x11000000,8).unknown==~0ull,
            "TTE transports the real MPG command in upper tag lanes without treating tag control as microcode");
        dma.advance_vif_dma(4,v0,v1,gif,gs,&vu);require(v0.read_register(0x60)==1,"one payload beat loads two complete microinstructions");
        dma.advance_vif_dma(6,v0,v1,gif,gs,&vu);
        require(!v0.processing() && vu.read_memory(0x11000010,8).value==0x100000000Full &&
            !vu.read_memory(0x11000010,8).unknown && (dma.ee_dma_read(0x1000E010)&1u),
            "VIF0 chained TTE upload ends after exact payload bytes, not at tag fetch");
    }
    {
        EeRegisters regs;regs.reset(nullptr,{});regs.write128(0x10005000,0x4A01000000000000ull,0xC001CAFE12345678ull);
        const auto data=regs.read_vu_memory(0x11008000,8);
        require(data.value==0xC001CAFE12345678ull && !data.unknown,"CPU FIFO upload and EE micro-memory aperture share the same VU storage");
    }
    std::cout<<"MPG exact bytes, NUM, alignment, wrap, reset, DMA timing and shared VU memory passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
