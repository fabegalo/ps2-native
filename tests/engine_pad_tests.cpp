#include "engine_input.hpp"
#include <cstdio>
#include <stdexcept>
using namespace ps2native::engine;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static std::vector<std::uint8_t> packet(Console& c,std::initializer_list<std::uint8_t> data,unsigned size=21){
    c.sio2_write(0x1f808200,(data.size()<<8)|(size<<18),4);
    for(auto b:data)c.sio2_write(0x1f808260,b,1);
    std::vector<std::uint8_t> result;while(size--)result.push_back(c.sio2_read(0x1f808264,1));return result;
}
static unsigned low,high;
static bool SDLCALL rumble(void*,Uint16 l,Uint16 h){low=l;high=h;return true;}
int main(){try{
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1209/0x0001");
    Console c;c.audio_output=false;
    check(packet(c,{1,0x42,0,0,0})[1]==0xff,"Disconnected DS2 must leave an open bus");
    EngineInput host(c,false);
    SDL_VirtualJoystickDesc d;SDL_INIT_INTERFACE(&d);d.type=SDL_JOYSTICK_TYPE_GAMEPAD;d.name="DS2 host regression";
    d.vendor_id=0x1209;d.product_id=1;d.naxes=SDL_GAMEPAD_AXIS_COUNT;d.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;d.axis_mask=(1u<<d.naxes)-1;d.button_mask=(1u<<d.nbuttons)-1;d.Rumble=rumble;
    auto id=SDL_AttachVirtualJoystick(&d);check(id,"SDL virtual joystick attach");auto* joystick=SDL_OpenJoystick(id);check(joystick,"SDL virtual joystick open");
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768);SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-32768);
    host.poll();check(c.pads[0].connected,"SDL hotplug must connect the same SIO2 port");
    auto r=packet(c,{1,0x42,0,0,0});check(r[1]==0x41 && r[2]==0x5a && r[3]==255 && r[4]==255,"Digital DS2 read and neutral buttons");
    SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true);SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_START,true);
    SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,32767);SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTY,-32768);SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,0);host.poll();
    r=packet(c,{1,0x43,0,1,0});check(r[1]==0x41 && c.pads[0].config,"Enter configuration retains previous wire identity");
    packet(c,{1,0x44,0,1,3});
    r=packet(c,{1,0x46,0,0});check(r[5]==1 && r[6]==2 && r[7]==0 && r[8]==10,"First actuator query follows DS2 protocol");
    r=packet(c,{1,0x46,0,1});check(r[5]==1 && r[6]==1 && r[7]==1 && r[8]==20,"Second actuator query follows DS2 protocol");
    packet(c,{1,0x4f,0,255,255,3,0,0,0x5a});packet(c,{1,0x4d,0,0,1,255,255,255,255});packet(c,{1,0x43,0,0,0});
    r=packet(c,{1,0x42,0,1,128,0,0,0,0});check(r[1]==0x79 && r[3]==0xf7 && r[4]==0xbe,"Pressure mode preserves Start/Cross/L2 active-low button bits");
    check(r[5]==128 && r[6]==0 && r[7]==255 && r[8]==128,"Stick order/range/neutral center");
    check(r[15]==255 && r[19]>=127 && r[19]<=128,"Digital buttons use endpoints; real trigger pressure survives");
    check(c.pads[0].analog_pressure_mask==0xc00,"Absent pressure axes cannot become fabricated analog capabilities");
    host.poll();check(low==128*257 && high==65535,"Negotiated guest motors must reach SDL rumble");
    c.iop_write(0xbf808200,(9u<<8)|(21u<<18),4);
    for(unsigned i=0;i<32;++i)c.iop_write(0x6400+i,i==0?1:i==1?0x42:0,1);
    c.iop_write(0xbf801550,0x6500,4);c.iop_write(0xbf801554,0x10008,4);c.iop_write(0xbf801558,0x41000200,4);
    c.iop_write(0xbf801540,0x6400,4);c.iop_write(0xbf801544,0x10008,4);c.iop_write(0xbf801548,0x01000201,4);
    c.iop_write(0xbf801570,0x880000,4);
    check(c.iop_read(0x6501,1)==0x79 && c.iop_read(0x6503,1)==0xf7 && c.iop_read(0x6504,1)==0xbe,"Connected DS2 serial DMA copies real input into shared IOP RAM");
    check(c.iop_sio2_dma_due[1]>c.ticks,"Input DMA completion stays on the Console clock");
    for(unsigned i=21;i<32;++i)check(c.iop_read(0x6500+i,1)==0,"DMA padding cannot become controller data");
    c.advance_to(c.ticks+64);
    check(!(c.iop_read(0xbf801558,4)&0x1000000),"Input DMA completes at its real scheduled deadline");
    SDL_CloseJoystick(joystick);check(SDL_DetachVirtualJoystick(id),"SDL virtual joystick detach");host.poll();
    check(!c.pads[0].connected && packet(c,{1,0x42,0,0,0})[1]==255,"Hot unplug restores the actual absent-device serial response");
    d.naxes=16;d.axis_mask=63;auto pressure_id=SDL_AttachVirtualJoystick(&d);check(pressure_id,"Pressure-capable SDL virtual attach");
    auto* pressure_joystick=SDL_OpenJoystick(pressure_id);check(pressure_joystick,"Pressure joystick open");
    check(SDL_SetGamepadMapping(pressure_id,"*,DS2 pressure regression,type:PS3,a:b0,b:b1,x:b2,y:b3,back:b4,start:b6,leftshoulder:b9,rightshoulder:b10,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5"),"PS3 SDL mapping");
    for(unsigned i=4;i<16;++i)SDL_SetJoystickVirtualAxis(pressure_joystick,i,-32768);
    SDL_SetJoystickVirtualAxis(pressure_joystick,6,0);SDL_SetJoystickVirtualAxis(pressure_joystick,4,32767);host.poll();SDL_SetJoystickVirtualAxis(pressure_joystick,4,-32511);host.poll();
    check(c.pads[0].analog_pressure_mask==0xfff && c.pads[0].pressure[6]==128 && c.pads[0].pressure[10]==1,"SDL PS3 pressure axes retain intermediate values without synthetic pressure");
    SDL_CloseJoystick(pressure_joystick);SDL_DetachVirtualJoystick(pressure_id);host.poll();
    std::puts("engine-pad: SDL hotplug, DS2 configuration, buttons, axes, pressure and rumble PASS");
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
