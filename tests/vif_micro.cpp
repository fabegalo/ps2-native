// Original short integer programs exercise the real VIF/VU handshake.
#include "boot_checks.hpp"
#include "ps2native/vu_control.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void program(VuControl& vu,unsigned unit) {
    const auto base=unit ? 0x11008000u : 0x11000000u;
    vu.write_memory(base,8,{0x000002FF10010007ull,0}); // IADDIU VI1,VI0,7
    vu.write_memory(base+8,8,{0x400002FF8000033Cull,0});
    vu.write_memory(base+16,8,{0x000002FF8000033Cull,0});
}
}
int main() try {
    for(unsigned unit=0;unit<2;++unit) {
        VuControl vu;Vif vif(unit ? Vif::Unit::Vu1 : Vif::Unit::Vu0);Gif gif;GsRegisters gs;gs.reset(0);program(vu,unit);
        if(unit) {
            require(vif.observe_status().unknown==0x80,"DBF is unknown before an actual OFFSET command");
            rejects([&]{(void)vif.read_register(0);},"unknown double-buffer phase");
            vif.write_fifo(0x02000009030003FCull,0,gif,gs,&vu,90);
        }
        vif.write_fifo(0x1400000004000123ull,0,gif,gs,&vu,100);
        require(vu.micro_running(unit) && vu.diagnostic_micro_instructions(unit)==0 && vif.read_register(0xD0)==0x123,
            "MSCAL starts the genuine engine without inline execution and latches ITOPS");
        if(unit)require(vif.read_register(0xE0)==0x3FC && vif.read_register(0xC0)==5 && vif.read_register(0)==0x80,
            "first start copies old TOPS to TOP, reverses DBF and wraps BASE+OFFSET");
        rejects([&]{vif.write_fifo(0x10000000,0,gif,gs,&vu,100);},"FLUSHE waits");
        rejects([&]{vif.write_fifo(0x4A01000000000000ull,0,gif,gs,&vu,100);},"MPG waits");
        vu.advance_micro(107);require(!vu.micro_running(unit),"the true E delay and pipeline finish independently of VIF");
        const auto vi=unit ? vu.observe_data(0x4210) : vu.observe(1);
        require(vi.value==7 && !vi.unknown,"the loaded program produced the actual integer result");
        vif.write_fifo(0x14000000,0,gif,gs,&vu,110);
        if(unit)require(vif.read_register(0xE0)==5 && vif.read_register(0xC0)==0x3FC && vif.read_register(0)==0,
            "second start alternates the actual buffers back to BASE");
    }
    {
        Iop dma;VuControl vu;Vif v0(Vif::Unit::Vu0),v1;Gif gif;GsRegisters gs;gs.reset(0);dma.reset({},nullptr);program(vu,1);
        std::vector<std::uint8_t> ram(0x200);
        const std::uint32_t words[]{0x03000020,0x02000010,0x04000030,0x14000000};
        for(unsigned n=0;n<4;++n)for(unsigned b=0;b<4;++b)ram[0x100+n*4+b]=static_cast<std::uint8_t>(words[n]>>(8*b));
        dma.attach_ee_ram(ram);dma.ee_dma_write(0x1000E000,1);dma.ee_dma_write(0x10009010,0x100);
        dma.ee_dma_write(0x10009020,1);dma.ee_dma_write(0x10009000,0x101);
        dma.advance_vif_dma(50,v0,v1,gif,gs,&vu);
        require(vu.micro_running(1) && vu.diagnostic_micro_instructions(1)==0,"late host polling still timestamps MSCAL at the real DMA bus beat");
        vu.advance_micro(3);require(vu.diagnostic_micro_instructions(1)==1,"the first instruction issues after cycle-two DMA delivery, not after cycle-fifty polling");
        vu.advance_micro(9);require(!vu.micro_running(1) && vu.observe_data(0x4210).value==7,"normal DMA launches the same shared integer engine");
    }
    {
        EeRegisters regs;std::uint64_t cycle=200;regs.reset();regs.set_clock(&cycle);
        regs.write_vu_memory(0x11008000,8,{0x400002FF10010007ull,0});
        regs.write_vu_memory(0x11008008,8,{0x000002FF8000033Cull,0});
        regs.write128(0x10005000,0x14000000,0);
        require(regs.read_vu_control(29)==256,"CPU FIFO starts VU1 at the attached EE clock");
        cycle=206;require(regs.read_vu_control(29)==0 && regs.observe_vu_data(0x4210).value==7,
            "EE status polling advances the actual engine and observes its completed result");
    }
    std::cout<<"MSCAL real VU execution, DBF/TOP/ITOP, unknown reset phase and DMA timestamp ordering passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
