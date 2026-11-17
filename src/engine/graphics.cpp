// Console adapter; DMA tag contracts follow exoteric33 a50471a dmac.cpp (MIT).
#include "ps2native/engine/console.hpp"
#include "gs/gpu/gpu.hpp"
#include <algorithm>
#include <cstring>
#include <format>
namespace ps2native::engine {
namespace {
constexpr std::uint32_t bases[]={0x10008000,0x10009000,0x1000a000,0x1000b000,0x1000b400,0x1000c000,0x1000c400,0x1000c800,0x1000d000,0x1000d400};
std::span<std::uint8_t> dma_memory(Console& c,std::uint32_t address,std::size_t bytes) {
    if(address&0x80000000u) {
        const auto offset=address&0x3fff;
        if(bytes>c.scratchpad.size()-offset)throw std::runtime_error("DMA span crosses scratchpad");
        return std::span(c.scratchpad).subspan(offset,bytes);
    }
    address&=0x1fffffff;
    if(address>c.ram.size() || bytes>c.ram.size()-address)throw std::runtime_error("DMA span outside EE RAM");
    return std::span(c.ram).subspan(address,bytes);
}
}
void Console::initialize_graphics() {
    if(graphics_initialized)return;
    const auto csr=gs_private[11],imr=gs_private[12];const auto field=gs_odd_field;
    ps2::gs::reset();
    gs_private[11]=csr;gs_private[12]=imr;gs_odd_field=field;
    if(ps2::gs::gpu::mode()!=ps2::gs::gpu::Mode::primary)throw std::runtime_error("GS compute unavailable");
    std::vector<std::uint32_t> zero(1u<<20),clut(512);
    ps2::gs::gpu::write_state(zero,1,clut);
    graphics_initialized=true;
}
// Same deterministic NTSC/PAL timing profile as the pinned GS host; not hardware cycle certification.
void Console::graphics_vblank() {
    iop_vblank_edge(0);++graphics_frames;intc_status|=1u<<2;ps2::gs::vblank(true);
    if(input_poll)input_poll();
    const auto period=294912000u/(gs_options.pal ? 50u : 60u);
    const auto hblank=period*2/(gs_options.pal ? 625u : 525u);
    graphics_events[3]={ticks+hblank*22,[this]{iop_vblank_edge(1);intc_status|=1u<<3;ps2::gs::vblank(false);}};
    graphics_events[2]={ticks+period,[this]{graphics_vblank();}};
}
void Console::graphics_dma(unsigned channel) {
    if(channel>2 && channel!=8 && channel!=9)throw std::runtime_error("Unsupported graphics DMA channel");
    graphic_dma[channel]={};
    const auto base=bases[channel],chcr=dma_registers[base],id=chcr>>28&7;
    graphic_dma[channel].tag_end=((chcr>>2)&3)==1 && dma_registers[base+0x20] && (id==0 || id==7 || ((chcr&0x80) && (chcr&0x80000000)));
    graphics_dma_resume(channel);
}
void Console::graphics_dma_resume(unsigned channel) {
    if(channel>2 && channel!=8 && channel!=9)throw std::runtime_error("Unsupported graphics DMA resume");
    const auto base=bases[channel];auto& chcr=dma_registers[base];
    if(!(chcr&0x100) || !(dma_registers[0x1000e000]&1) || (dma_registers[0x1000f520]&0x10000))return;
    const auto mode=(chcr>>2)&3;
    if(mode==3 || (mode==2 && channel<8) || (dma_registers[0x1000e000]&0xfc))throw std::runtime_error("DMA interleave/MFIFO/stall control is not integrated");
    if(channel==1 && !(chcr&1) && mode!=0)throw std::runtime_error("Reverse VIF1 chain DMA is not integrated");
    if(channel==2)initialize_graphics();
    auto& state=graphic_dma[channel];auto& madr=dma_registers[base+0x10];auto& qwc=dma_registers[base+0x20];auto& tadr=dma_registers[base+0x30];
    unsigned interleave_count=0;
    const auto quantum=(dma_registers[0x1000e030]>>16)&0xff,skip=dma_registers[0x1000e030]&0xff;
    for(unsigned guard=0;guard<1u<<20;++guard) {
        if(channel<2 && ps2::vif::stalled(channel)){state.paused=true;return;}
        state.paused=false;
        if(qwc) {
            auto bytes=dma_memory(*this,madr,16);
            if(channel==8 || channel==9) {
                auto& sadr=dma_registers[base+0x80];const auto at=sadr&0x3fff;
                if(channel==9)std::memcpy(scratchpad.data()+at,bytes.data(),16);else std::memcpy(bytes.data(),scratchpad.data()+at,16);
                sadr=(sadr+16)&0x3fff;
            } else if(channel==1 && !(chcr&1)) {
                initialize_graphics();ps2::gs::read_image(bytes.data(),1);
            } else if(channel<2) ps2::vif::transfer(channel,bytes.data(),1);
            else ps2::gif::transfer(3,bytes.data(),1);
            madr+=16;--qwc;
            if(mode==2 && ((quantum && ++interleave_count==quantum) || !qwc)) {madr+=skip*16;interleave_count=0;}
            continue;
        }
        if(mode!=1 || state.tag_end) {
            chcr&=~0x100u;dmac_status|=1u<<channel;
            if(channel==1)ps2::gif::vif1_idle();
            return;
        }
        if(channel==8)throw std::runtime_error("SPR destination-chain DMA is not integrated");
        const auto tag=dma_memory(*this,tadr,16);
        const auto control=ps2::load<std::uint32_t>(tag.data()),address=ps2::load<std::uint32_t>(tag.data()+4)&0xfffffff0;
        qwc=control&0xffff;const auto id=(control>>28)&7;
        chcr=(chcr&0xffff)|((control&0xffff0000));
        auto next=tadr+16;auto data=address;state.tag_end=(id==0 || id==7 || ((control&0x80000000) && (chcr&0x80)));
        if(id==1 || id==2 || id>=5)data=tadr+16;
        switch(id) {
        case 0:case 3:case 4:break;
        case 1:next=data+qwc*16;break;
        case 7:next=tadr;break;
        case 2:next=address;break;
        case 5: {
            const auto asp=(chcr>>4)&3;if(asp>=2)throw std::runtime_error("DMA CALL stack overflow");
            dma_registers[base+0x40+asp*16]=data+qwc*16;chcr=(chcr&~0x30u)|((asp+1)<<4);next=address;break;
        }
        case 6: {
            const auto asp=(chcr>>4)&3;
            if(!asp){state.tag_end=true;next=tadr;}
            else {next=dma_registers[base+0x40+(asp-1)*16];chcr=(chcr&~0x30u)|((asp-1)<<4);}break;
        }
        }
        madr=data;tadr=next;
        if((chcr&0x40) && channel<2)ps2::vif::transfer_words(channel,reinterpret_cast<const std::uint32_t*>(tag.data()+8),2);
        else if((chcr&0x40) && channel==9) {
            auto& sadr=dma_registers[base+0x80];std::memcpy(scratchpad.data()+(sadr&0x3ff0),tag.data(),16);sadr=(sadr+16)&0x3ff0;
        }
    }
    throw std::runtime_error("DMA chain/transfer iteration limit reached");
}
}
