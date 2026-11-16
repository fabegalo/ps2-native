// Serial FIFO/packet behavior reused from patches/ps2recomp/0062 and 0065.
// PCSX2 SIO/Sio2.cpp (GPL-3.0-or-later) defines register/absent-device behavior.
#include "ps2native/engine/console.hpp"
#include <algorithm>
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
void Console::sio2_write(std::uint32_t address,std::uint32_t value,unsigned bytes){
    if(address==0x1f808260 && bytes==1){
        if(iop_sio2_command>=16)throw std::runtime_error("SIO2 command queue exhausted");
        const auto descriptor=iop_sio2_send[iop_sio2_command];
        const auto length=(descriptor>>8)&0x3ff;
        if(!length)throw std::runtime_error("SIO2 data without a command descriptor");
        iop_sio2_input.push_back(static_cast<std::uint8_t>(value));
        if(iop_sio2_input.size()==length){
            const auto protocol=iop_sio2_input.front();
            if(protocol!=1 && protocol!=0x21 && protocol!=0x61 && protocol!=0x81)throw std::runtime_error("Unsupported SIO2 peripheral protocol");
            const auto receive=(descriptor>>18)&0x3ff;
            const auto size=std::max(length,receive);
            if(size>32768-iop_sio2_output.size())throw std::runtime_error("SIO2 receive FIFO limit reached");
            // No host peripheral is attached yet: an open serial bus returns FF, never a successful pad/card response.
            iop_sio2_output.insert(iop_sio2_output.end(),size,0xff);
            iop_sio2_status=(iop_sio2_status&0x300)?((iop_sio2_status&~0x300u)|0x200):iop_sio2_status|0x100;
            iop_sio2_status|=0x1000|((descriptor&1) && protocol!=0x21?0x2d000:0x1d000);
            iop_sio2_input.clear();++iop_sio2_command;
        }
        return;
    }
    if(bytes!=4 || (address&3))throw std::runtime_error("Invalid SIO2 register write");
    if(address<0x1f808260){
        iop_sio2_send[(address-0x1f808200)/4]=value;
        if(address==0x1f808200){iop_sio2_command=0;iop_sio2_input.clear();iop_sio2_status=0;}
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
}
