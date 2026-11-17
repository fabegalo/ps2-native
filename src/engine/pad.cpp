// Serial command implementation reused from patch 0062; PCSX2 PadDualshock2 is the protocol reference.
#include "ps2native/engine/pad.hpp"
#include <stdexcept>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
namespace ps2native::engine {
void Pad::exchange(std::span<const std::uint8_t> input,std::vector<std::uint8_t>& reply){
    if(input.size()<2)throw std::runtime_error("Truncated DS2 command");
    static unsigned logged;static std::uint16_t last=0xffff;
    if(std::getenv("PS2N_ENGINE_TRACE_INPUT") && logged<128 && (input[1]!=0x42 || buttons!=last || logged<8)){++logged;last=buttons;std::fprintf(stderr,"[pad-wire] command=%02X mode=%02X config=%u buttons=%04X bytes=%zu\n",input[1],mode,config,buttons,input.size());}
    const auto put=[&](unsigned i,std::uint8_t v){if(i<reply.size())reply[i]=v;};
    const auto arg=[&](unsigned i){return i<input.size()?input[i]:std::uint8_t{};};
    put(1,config?0xf3:mode);put(2,0x5a);
    for(unsigned i=3;i<std::min<std::size_t>(reply.size(),9);++i)reply[i]=0;
    switch(input[1]){
    case 0x40:put(5,2);put(8,0x5a);break;
    case 0x41:if(mode!=0x41){put(3,0xff);put(4,0xff);put(5,3);put(8,0x5a);}break;
    case 0x42:{
        put(3,buttons);put(4,buttons>>8);
        const unsigned size=mode==0x79?21:mode==0x73?9:5;
        for(unsigned i=5;i<std::min<std::size_t>(size,reply.size());++i)put(i,i<9?sticks[i-5]:pressure[i-9]);
        for(unsigned i=size;i<reply.size();++i)reply[i]=0xff;
        rumble={};for(unsigned i=0;i<motors.size() && i+3<input.size();++i){if(motors[i]==0)rumble[0]=input[i+3]&1;if(motors[i]==1)rumble[1]=input[i+3];}break;
    }
    case 0x43:config=arg(3)!=0;break;
    case 0x44:if(!config)throw std::runtime_error("DS2 mode change outside configuration");mode=arg(3)?0x73:0x41;break;
    case 0x45:put(3,3);put(4,2);put(5,mode!=0x41);put(6,2);put(7,1);break;
    case 0x46:put(5,1);put(6,arg(3)?1:2);put(7,arg(3)?1:0);put(8,arg(3)?0x14:0xa);break;
    case 0x47:put(5,2);put(7,1);break;
    case 0x4c:put(6,arg(3)?7:4);break;
    case 0x4d:for(unsigned i=0;i<motors.size();++i){put(i+3,motors[i]);if(i+3<input.size())motors[i]=input[i+3];}break;
    case 0x4f:{const auto mask=arg(3)|(std::uint32_t{arg(4)}<<8)|(std::uint32_t{arg(5)}<<16);mode=mask==0x3ffff?0x79:mask==0x3f?0x73:0x41;put(8,0x5a);break;}
    default:throw std::runtime_error("Unsupported DS2 serial command");
    }
}
}
