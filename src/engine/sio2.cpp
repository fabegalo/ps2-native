// Serial FIFO/packet behavior reused from patches/ps2recomp/0062 and 0065.
// PCSX2 SIO/Sio2.cpp (GPL-3.0-or-later) defines register/absent-device behavior.
#include "ps2native/engine/console.hpp"
#include <algorithm>
#include "iop_cdvd.hpp"
namespace ps2native::engine {
std::uint32_t Console::sio2_read(std::uint32_t address,unsigned bytes){
    if(address==0x1f808264 && bytes==1){
        if(iop_sio2_output.empty())throw std::runtime_error("SIO2 receive FIFO underflow");
        const auto value=iop_sio2_output.front();iop_sio2_output.pop_front();return value;
    }
    if(bytes!=4 || (address&3))throw std::runtime_error("Invalid SIO2 register read");
    if(address<0x1f808260)return iop_sio2_send[(address-0x1f808200)/4];
    switch(address){
    case 0x1f808268:return iop_sio2_control;
    case 0x1f80826c:return iop_sio2_status;
    case 0x1f808270:return 0xf;
    case 0x1f808274:return 0;
    case 0x1f808280:return iop_sio2_interrupt;
    default:throw std::runtime_error("Unsupported SIO2 register read");
    }
}
void Console::sio2_input_byte(std::uint8_t value){
    if(iop_sio2_command>=16)throw std::runtime_error("SIO2 command queue exhausted");
    const auto descriptor=iop_sio2_send[iop_sio2_command];
    const auto length=(descriptor>>8)&0x3ff;
    if(!length)throw std::runtime_error("SIO2 data without a command descriptor");
    if(iop_sio2_block_size && length>iop_sio2_block_size)throw std::runtime_error("SIO2 descriptor exceeds its DMA block");
    if(iop_sio2_received<length)iop_sio2_input.push_back(value);
    if(++iop_sio2_received==(iop_sio2_block_size?iop_sio2_block_size:length)){
        const auto protocol=iop_sio2_input.front();
        if(protocol!=1 && protocol!=0x21 && protocol!=0x61 && protocol!=0x81)throw std::runtime_error("Unsupported SIO2 peripheral protocol");
        const auto receive=(descriptor>>18)&0x3ff;
        const auto wire=std::max(length,receive);
        const auto size=iop_sio2_block_size?((wire+iop_sio2_block_size-1)/iop_sio2_block_size)*iop_sio2_block_size:wire;
        if(size>32768-iop_sio2_output.size())throw std::runtime_error("SIO2 receive FIFO limit reached");
        // No host peripheral is attached yet: an open serial bus returns FF, never a successful pad/card response.
        iop_sio2_output.insert(iop_sio2_output.end(),wire,0xff);
        iop_sio2_output.insert(iop_sio2_output.end(),size-wire,0);
        iop_sio2_status=(iop_sio2_status&0x300)?((iop_sio2_status&~0x300u)|0x200):iop_sio2_status|0x100;
        iop_sio2_status|=0x1000|((descriptor&1) && protocol!=0x21?0x2d000:0x1d000);
        iop_sio2_input.clear();iop_sio2_received=0;++iop_sio2_command;
    }
}
void Console::sio2_write(std::uint32_t address,std::uint32_t value,unsigned bytes){
    if(address==0x1f808260 && bytes==1){sio2_input_byte(static_cast<std::uint8_t>(value));start_sio2_dma(1);return;}
    if(bytes!=4 || (address&3))throw std::runtime_error("Invalid SIO2 register write");
    if(address<0x1f808260){
        iop_sio2_send[(address-0x1f808200)/4]=value;
        if(address==0x1f808200){iop_sio2_command=iop_sio2_received=iop_sio2_block_size=0;iop_sio2_input.clear();iop_sio2_status=0;}
        return;
    }
    if(address==0x1f808280){iop_sio2_interrupt&=~value;if(!iop_sio2_interrupt)iop_pending_irqs&=~(1u<<17);return;}
    if(address==0x1f808268){
        if((value&1) && !iop_sio2_input.empty())throw std::runtime_error("SIO2 transfer has an incomplete serial packet");
        iop_sio2_control=value;
        if((value&1) && !iop_sio2_interrupt){iop_sio2_interrupt=1;iop_pending_irqs|=1u<<17;if(!iop_active)iop_next_tick=ticks+8;}
        return;
    }
    throw std::runtime_error("Unsupported SIO2 register write");
}
// DMA payload/stride adaptation reuses patch 0062; deadlines follow pinned PCSX2 IopDma.cpp.
void Console::start_sio2_dma(unsigned receive){
    const auto base=receive?0x1f801550u:0x1f801540u;const auto chcr=cdvd->dma.at(base+8);
    if(!(chcr&0x1000000)){
        if(iop_sio2_dma_due[receive]!=~0ull)throw std::runtime_error("Stopping active SIO2 DMA is not integrated");return;
    }
    if(iop_sio2_dma_due[receive]!=~0ull || !(cdvd->dma.at(0x1f801570)&(8u<<(receive?20:16))))return;
    if(chcr!=(receive?0x41000200u:0x01000201u))throw std::runtime_error("Unsupported SIO2 slice DMA mode");
    const auto bcr=cdvd->dma.at(base+4),address=cdvd->dma.at(base)&0x1fffff;
    const auto block=(bcr&65535)?(bcr&65535):65536,count=(bcr>>16)?(bcr>>16):65536;
    const auto bytes=std::uint64_t{block}*count*4;
    if((address&3) || bytes>iop_ram.size()-address)throw std::runtime_error("SIO2 DMA exceeds physical IOP RAM");
    if(receive){
        if(bytes>iop_sio2_output.size())return; // The receiver waits for the sender to fill the actual serial FIFO.
        for(std::uint64_t n=0;n<bytes;++n){iop_ram[address+n]=iop_sio2_output.front();iop_sio2_output.pop_front();}
    }else{
        iop_sio2_block_size=block*4;
        for(std::uint64_t n=0;n<bytes;++n)sio2_input_byte(iop_ram[address+n]);
    }
    cdvd->dma.at(base)=address+static_cast<std::uint32_t>(bytes);
    iop_sio2_dma_due[receive]=ticks+std::max<std::uint64_t>(1,bytes/(receive?4:16))*8;
    if(!receive)start_sio2_dma(1);
}
void Console::complete_sio2_dma(unsigned receive){
    const bool idle=!iop_work_pending();const auto base=receive?0x1f801550u:0x1f801540u;
    iop_sio2_dma_due[receive]=~0ull;cdvd->dma.at(base+8)&=~0x1000000u;
    cdvd->dma.at(0x1f801574)|=1u<<(receive?29:28);cdvd->update_dicr();
    if(idle && iop_work_pending())iop_next_tick=std::max(iop_next_tick,ticks+8);
}

}
