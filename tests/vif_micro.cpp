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
    {
        VuControl vu;Vif vif;Gif gif;GsRegisters gs;gs.reset(0);program(vu,1);
        vif.write_fifo(0x0200000003000000ull,0,gif,gs,&vu,90);
        vif.write_fifo(0x1400000004000123ull,0,gif,gs,&vu,100);
        vif.write_fifo(0x0700123413000000ull,0,gif,gs,&vu,100);
        for(std::uint64_t cycle=100;cycle<107;++cycle) {
            advance_vpus(cycle,nullptr,&vif,&vu,gif,gs);
            require(vu.micro_running(1) && vif.processing(),"FLUSHA cannot bypass a running VU or its E-bit drain");
        }
        advance_vpus(107,nullptr,&vif,&vu,gif,gs);
        require(!vu.micro_running(1) && !vif.processing() && vif.read_register(0x30)==0x1234,
            "FLUSHA releases the following command on the genuine VU Ready event");
    }
    {
        Iop dma;VuControl vu;Vif v0(Vif::Unit::Vu0),v1;Gif gif;GsRegisters gs;gs.reset(0);dma.reset({},nullptr);
        std::vector<std::uint8_t> ram(0x200);ram[0x103]=0x70;dma.attach_ee_ram(ram); // zero-payload END
        require(!dma.graphics_sync_needed(100,v0,v1,vu),"idle graphics has no hardware event to service");
        dma.advance_graphics(100,v0,v1,gif,gs,vu);
        dma.ee_dma_write(0x1000E000,1);dma.ee_dma_write(0x10009020,0);
        dma.ee_dma_write(0x10009030,0x100);dma.ee_dma_write(0x10009000,0x105);
        dma.advance_graphics(101,v0,v1,gif,gs,vu);
        require((dma.ee_dma_read(0x10009000)&0x100u) && !dma.ee_dma_read(0x1000E010),
            "idle synchronization retains the actual bus anchor rather than accumulating disabled time");
        dma.advance_graphics(102,v0,v1,gif,gs,vu);
        require(!(dma.ee_dma_read(0x10009000)&0x100u) && dma.ee_dma_read(0x1000E010)==2,
            "the first real tag beat completes on its original cycle after idle synchronization");
        dma.ee_dma_write(0x10009030,0x100);dma.ee_dma_write(0x10009000,0x105);
        dma.ee_dma_write(0x10008020,0);dma.ee_dma_write(0x10008030,0x100);dma.ee_dma_write(0x10008000,0x105);
        rejects([&]{dma.advance_graphics(102,v0,v1,gif,gs,vu);},"Concurrent VIF/GIF DMA");
        dma.set_ee_dma_suspended(true);dma.ee_dma_write(0x10008000,0);dma.set_ee_dma_suspended(false);
        ram[0x103]=0x40; // unsupported REFS tag, retained as an actual parser failure
        rejects([&]{dma.advance_graphics(104,v0,v1,gif,gs,vu);},"source chain REFS");
        dma.ee_dma_write(0x1000E000,0);
        require(dma.graphics_sync_needed(104,v0,v1,vu),"a stored graphics refusal remains observable while DMAE is disabled");
        rejects([&]{dma.advance_graphics(104,v0,v1,gif,gs,vu);},"source chain REFS");
    }
    for(unsigned unit=0;unit<2u;++unit) {
        Iop dma;VuControl vu;Vif v0(Vif::Unit::Vu0),v1;Gif gif;GsRegisters gs;gs.reset(0);dma.reset({},nullptr);
        const auto vector=[&](unsigned index,std::uint32_t x,std::uint32_t y) {
            const ObservedQuadword word{{x|(std::uint64_t{y}<<32u),0},{0,0}};
            if(!unit)vu.write_vector(index,word);
            else {vu.write_vector(31,word);vu.write(2,0x400u+index);vu.vector_store_increment(31,2,15);}
        };
        vector(1,0x3F800000,0);vector(2,0,0x40000000);
        const auto base=unit ? 0x11008000u : 0x11000000u;
        const auto divide=0x800003BCu|(1u<<11u)|(2u<<16u)|(1u<<23u);
        vu.write_memory(base,8,{divide|(0x400002FFull<<32u),0});
        vu.write_memory(base+8u,8,{0x000002FF8000033Cull,0});
        vu.start_micro(unit,0,0);dma.advance_graphics(6,v0,v1,gif,gs,vu);
        require(!vu.micro_running(unit) && !dma.graphics_sync_needed(7,v0,v1,vu) &&
            dma.graphics_sync_needed(8,v0,v1,vu),"Q's exact completion remains scheduled after E ends either microprogram");
        dma.advance_graphics(7,v0,v1,gif,gs,vu);
        require((unit ? vu.observe_data(0x4360) : vu.observe(22)).unknown==~0u,
            "early graphics polling does not publish an outstanding FDIV quotient");
        dma.advance_graphics(8,v0,v1,gif,gs,vu);
        const auto quotient=unit ? vu.observe_data(0x4360) : vu.observe(22);
        require(quotient.value==0x3F000000 && !quotient.unknown && !dma.graphics_sync_needed(8,v0,v1,vu),
            "Q0 and Q1 publish the genuine quotient on the same cycle through the graphics fast path");
    }
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
        vif.write_fifo(0x10000000,0,gif,gs,&vu,100);
        require((vif.observe_status().value&7u)==6u && vu.micro_running(unit),"FLUSHE retains its real command in the decoder and reports an E-bit wait");
        for(std::uint64_t cycle=100;cycle<107;++cycle) {
            advance_vpus(cycle,&vif,nullptr,&vu,gif,gs);
            advance_vpus(cycle,&vif,nullptr,&vu,gif,gs);
            require(vu.micro_running(unit) && vif.processing(),"repeated early polls cannot complete FLUSHE before the VU pipeline drains");
        }
        advance_vpus(107,&vif,nullptr,&vu,gif,gs);
        require(!vu.micro_running(unit) && !vif.processing(),"FLUSHE completes only after the true E delay and pipeline drain");
        const auto vi=unit ? vu.observe_data(0x4210) : vu.observe(1);
        require(vi.value==7 && !vi.unknown,"the loaded program produced the actual integer result");
        vu.write_memory(unit ? 0x11008000u : 0x11000000u,8,
            {(unit ? 0x800406BCull : 0x800406BDull)|(0x2FFull<<32u),0});
        vif.write_fifo(0x14000000,0,gif,gs,&vu,110);
        if(unit)require(vif.read_register(0xE0)==5 && vif.read_register(0xC0)==0x3FC && vif.read_register(0)==0,
            "second start alternates the actual buffers back to BASE");
        advance_vpus(117,&vif,nullptr,&vu,gif,gs);
        const auto pointer=unit ? vu.observe_data(0x4240) : vu.observe(4);
        require(!pointer.unknown && pointer.value==(unit ? 5u : 0x23u),
            "XTOP sees the old TOPS latched into TOP and VU0 XITOP zero-extends only its actual low eight bits");
    }
    {
        Iop dma;VuControl vu;Vif v0(Vif::Unit::Vu0),v1;Gif gif;GsRegisters gs;gs.reset(0);dma.reset({},nullptr);program(vu,1);
        std::vector<std::uint8_t> ram(0x200);
        const std::uint32_t words[]{0x03000020,0x02000010,0x04000030,0x14000000};
        for(unsigned n=0;n<4;++n)for(unsigned b=0;b<4;++b)ram[0x100+n*4+b]=static_cast<std::uint8_t>(words[n]>>(8*b));
        dma.attach_ee_ram(ram);dma.ee_dma_write(0x1000E000,1);dma.ee_dma_write(0x10009010,0x100);
        dma.ee_dma_write(0x10009020,1);dma.ee_dma_write(0x10009000,0x101);
        dma.advance_vif_dma(50,v0,v1,gif,gs,&vu);
        require(!vu.micro_running(1) && vu.diagnostic_micro_instructions(1)==3 && vu.observe_data(0x4210).value==7,
            "late host polling starts at the real cycle-two bus beat and advances the genuine engine to cycle fifty");
    }
    for(unsigned unit=0;unit<2u;++unit) {
        VuControl vu;Vif vif(unit ? Vif::Unit::Vu1 : Vif::Unit::Vu0);Gif gif;GsRegisters gs;gs.reset(0);program(vu,unit);
        const auto base=unit ? 0x11008000u : 0x11000000u;
        vu.write_memory(base+24,8,{0x000002FF10010801ull,0}); // VI1=VI1+1
        vu.write_memory(base+32,8,{0x400002FF8000033Cull,0});vu.write_memory(base+40,8,{0x000002FF8000033Cull,0});
        vif.write_fifo(0x1400000004000111ull,0,gif,gs,&vu,0);
        vif.write_fifo(0x040002221700FFFFull,0,gif,gs,&vu,2); // MSCNT ignores IMMEDIATE
        advance_vpus(6,&vif,nullptr,&vu,gif,gs);
        require(vif.read_register(0x90)==0x111 && (vif.observe_status().value&4u),"MSCNT preserves subsequent words and ITOPS until the previous program is Ready");
        advance_vpus(7,&vif,nullptr,&vu,gif,gs);
        require(vu.micro_running(unit) && vu.diagnostic_micro_instructions(unit)==0 && vu.diagnostic_micro_pc(unit)==24 &&
            vif.read_register(0xD0)==0x111 && vif.read_register(0x90)==0x222,"MSCNT resumes the exact termination PC and performs the ITOP handshake before following commands");
        advance_vpus(100,&vif,nullptr,&vu,gif,gs);
        const auto vi=unit ? vu.observe_data(0x4210) : vu.observe(1);
        require(!vu.micro_running(unit) && vi.value==8 && !vi.unknown,"coarse host polling still resumes at cycle seven and executes the real continuation");
        VuControl upload;program(upload,unit);Vif loader(unit ? Vif::Unit::Vu1 : Vif::Unit::Vu0);
        loader.write_fifo(0x14000000,0,gif,gs,&upload,0);
        loader.write_fifo(0x4A01006400000000ull,0x76543210FEDCBA98ull,gif,gs,&upload,2);
        require(upload.read_memory(base+800,8).unknown==~0ull && loader.processing(),"MPG cannot overwrite micro RAM while the target engine runs");
        advance_vpus(7,&loader,nullptr,&upload,gif,gs);
        require(upload.read_memory(base+800,8).value==0x76543210FEDCBA98ull && !loader.processing(),"MPG retains and uploads its exact queued payload after Ready");
    }
    for(unsigned unit=0;unit<2u;++unit) {
        Iop dma;VuControl vu;Vif v0(Vif::Unit::Vu0),v1;auto& vif=unit ? v1 : v0;Gif gif;GsRegisters gs;gs.reset(0);dma.reset({},nullptr);
        const auto base=unit ? 0x11008000u : 0x11000000u,capacity=unit ? 16u : 8u,count=capacity+5u;
        for(unsigned n=0;n<83;++n)vu.write_memory(base+n*8u,8,{(n==81u ? 0x400002FF8000033Cull : 0x000002FF8000033Cull),0});
        std::vector<std::uint8_t> ram(0x400);
        const auto word=[&](unsigned offset,std::uint32_t value){for(unsigned b=0;b<4;++b)ram[offset+b]=static_cast<std::uint8_t>(value>>(8*b));};
        word(0x100,0x14000000);word(0x104,0x10000000);word(0x100+(count-1u)*16u,0x07001234);
        dma.attach_ee_ram(ram);const auto channel=unit ? 0x10009000u : 0x10008000u;
        dma.ee_dma_write(0x1000E000,1);dma.ee_dma_write(channel+0x10,0x100);dma.ee_dma_write(channel+0x20,count);dma.ee_dma_write(channel,0x101);
        dma.advance_vif_dma(2u*(capacity+2u),v0,v1,gif,gs,&vu);
        require(dma.ee_dma_read(channel+0x20)==4 && dma.ee_dma_read(channel+0x10)==0x100+(capacity+1u)*16u &&
            (vif.observe_status().value>>24u)==capacity && (vif.observe_status().value&4u),"a full bounded VIF FIFO stalls the real DMA without advancing MADR/QWC or dropping bytes");
        rejects([&]{vif.write_fifo(0,0,gif,gs,&vu,2u*(capacity+2u));},"FIFO full");
        dma.advance_vif_dma(200,v0,v1,gif,gs,&vu);
        require(!vu.micro_running(unit) && !vif.processing() && vif.read_register(0x30)==0x1234 &&
            !(dma.ee_dma_read(channel)&0x100u) && (dma.ee_dma_read(0x1000E010)&(1u<<unit)),"Ready releases FIFO backpressure and completes all original DMA commands with the actual channel interrupt");
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
    for(unsigned unit=0;unit<2u;++unit) {
        Iop dma;VuControl vu;Vif v0(Vif::Unit::Vu0),v1;auto& vif=unit ? v1 : v0;Gif gif;GsRegisters gs;gs.reset(0);dma.reset({},nullptr);
        const auto base=unit ? 0x11008000u : 0x11000000u,capacity=unit ? 16u : 8u;
        vu.write_memory(base,8,{0x000002FF400007FFull,0});vu.write_memory(base+8,8,{0x000002FF8000033Cull,0});
        vif.write_fifo(0x1000000014000000ull,0,gif,gs,&vu,0);
        for(unsigned n=0;n<capacity;++n)vif.write_fifo(0,0,gif,gs,&vu,0);
        std::vector<std::uint8_t> ram(0x200);ram[0x103]=0x70;dma.attach_ee_ram(ram);
        const auto channel=unit ? 0x10009000u : 0x10008000u;
        dma.ee_dma_write(0x1000E000,1);dma.ee_dma_write(channel+0x20,0);dma.ee_dma_write(channel+0x30,0x100);dma.ee_dma_write(channel,0x105);
        dma.advance_vif_dma(2,v0,v1,gif,gs,&vu);
        require(!(dma.ee_dma_read(channel)&0x100u) && (vif.observe_status().value>>24u)==capacity,
            "a zero-payload END tag without TTE completes without needing space in the VIF FIFO");
        vif.write_register(0x10,1,gif);
        require(!vif.processing() && !(vif.observe_status().value&0x1F000007u) && vu.micro_running(unit),
            "VIF reset drops only its actual FIFO/wait state and does not reset the running VU");
        advance_vpus(10,&v0,&v1,&vu,gif,gs);
        require(vu.diagnostic_micro_instructions(unit)==10,"the independent microprogram keeps executing after VIF reset");
    }
    {
        VuControl vu;Vif v0(Vif::Unit::Vu0),v1;Gif gif;GsRegisters gs;gs.reset(0);
        for(unsigned unit=0;unit<2u;++unit) {
            program(vu,unit);const auto base=unit ? 0x11008000u : 0x11000000u;
            vu.write_memory(base+24,8,{0x000002FF10010801ull,0});vu.write_memory(base+32,8,{0x400002FF8000033Cull,0});
            vu.write_memory(base+40,8,{0x000002FF8000033Cull,0});
            (unit ? v1 : v0).write_fifo(0x14000000,0,gif,gs,&vu,0);
        }
        v0.write_fifo(0x17000000,0,gif,gs,&vu,2);v1.write_fifo(0x17000000,0,gif,gs,&vu,2);
        advance_vpus(100,&v0,&v1,&vu,gif,gs);
        require(!vu.micro_running(0) && !vu.micro_running(1) && vu.observe(1).value==8 && vu.observe_data(0x4210).value==8,
            "the shared scheduler resumes both independent queued VUs on their real Ready edges");
    }
    std::cout<<"MSCAL/MSCNT real execution, exact Ready resume, FIFO backpressure/reset, DMA tags and buffer handshakes passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
