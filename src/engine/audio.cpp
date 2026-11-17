// SPU2 device bridge reuses ps2-native's approved PCSX2Audio/kzspu2 contracts.
#include "ps2native/engine/console.hpp"
#include "engine_audio.hpp"
#include "iop_cdvd.hpp"
#include "kzspu2.h"
#include <algorithm>
namespace ps2native::engine {
void Console::initialize_audio(){
    if(audio)return;
    audio=std::make_unique<EngineAudio>(audio_output);
    audio->reset(iop_ram.data(),static_cast<std::uint32_t>(iop_ram.size()),ticks/8);
    audio_sample_due=(ticks/8+768)*8;
}
std::uint64_t Console::audio_next_tick() const {
    if(!audio)return ~0ull;
    const auto dma=audio->nextEvent();
    return std::min(audio_sample_due,dma==~0ull?~0ull:std::max(ticks,dma*8));
}
void Console::advance_audio(){
    if(!audio)return;
    const bool idle=!iop_work_pending();audio->advance(ticks/8);
    const auto events=audio->takeEvents();
    if(events&1)iop_pending_irqs|=1u<<9;
    for(unsigned core=0;core<2;++core){
        const auto base=core?0x1f801500u:0x1f8010c0u;
        if(audio->dmaBusy(core) || (events&(2u<<core)))cdvd->dma.at(base)=audio->madr(core);
        if(events&(2u<<core)){
            cdvd->dma.at(base+8)&=~0x1000000u;cdvd->dma.at(base+4)&=0xffff;
            cdvd->dma.at(core?0x1f801574:0x1f8010f4)|=1u<<(24+(core?0:4));cdvd->update_dicr();
        }
    }
    if(idle && iop_work_pending())iop_next_tick=std::max(iop_next_tick,ticks+8);
    if(audio_sample_due<=ticks)audio_sample_due+=((ticks-audio_sample_due)/6144+1)*6144;
}
std::uint32_t Console::audio_read(std::uint32_t a,unsigned bytes){
    if((bytes!=2 && bytes!=4) || (a&1) || (a&0xffff)+bytes>0x10000)throw std::runtime_error("Invalid SPU2 register access");
    initialize_audio();auto value=std::uint32_t{audio->read16(a,ticks/8)};
    if(bytes==4)value|=std::uint32_t{audio->read16(a+2,ticks/8)}<<16;
    advance_audio();return value;
}
void Console::audio_write(std::uint32_t a,std::uint32_t value,unsigned bytes){
    if((bytes!=2 && bytes!=4) || (a&1) || (a&0xffff)+bytes>0x10000)throw std::runtime_error("Invalid SPU2 register access");
    initialize_audio();audio->write16(a,static_cast<std::uint16_t>(value),ticks/8);
    if(bytes==4)audio->write16(a+2,static_cast<std::uint16_t>(value>>16),ticks/8);
    advance_audio();
}
void Console::start_audio_dma(unsigned core){
    const auto base=core?0x1f801500u:0x1f8010c0u;
    const auto chcr=cdvd->dma.at(base+8);
    if(!(chcr&0x1000000)){
        if(audio && audio->dmaBusy(core))throw std::runtime_error("Stopping active SPU2 DMA is not integrated");
        return;
    }
    const auto priority=cdvd->dma.at(core?0x1f801570:0x1f8010f0);
    if(!(priority&(8u<<(core?0:16))))return;
    initialize_audio();if(audio->dmaBusy(core))return;
    const auto bcr=cdvd->dma.at(base+4),address=cdvd->dma.at(base)&0x1fffff;
    const auto words=std::uint64_t{(bcr&65535)?(bcr&65535):65536}*((bcr>>16)?(bcr>>16):65536);
    if((address&3) || words>(iop_ram.size()-address)/4)throw std::runtime_error("SPU2 DMA crosses the contiguous IOP RAM span");
    if((chcr&0x600)!=0x200)throw std::runtime_error("SPU2 DMA mode is not integrated");
    audio->dma(core,reinterpret_cast<std::uint16_t*>(iop_ram.data()+address),static_cast<std::uint32_t>(words*2),cdvd->dma.at(base),chcr&1,ticks/8);
    advance_audio();
}
std::uint32_t Console::pending_iop_irq() const {
    if(!iop_intr_enabled)return ~0u;
    const auto device=cdvd->irq_line();if(device!=~0u)return device;
    for(unsigned cause=0,pending=iop_pending_irqs;pending;++cause,pending>>=1){if(!(pending&1))continue;const auto it=iop_irqs.find(cause);if(it!=iop_irqs.end() && it->second.enabled)return cause;}
    return ~0u;
}
}
