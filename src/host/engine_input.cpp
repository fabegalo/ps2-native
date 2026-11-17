// SDL3 is the approved host input boundary; DS2 packets remain owned by Console/SIO2.
#include "engine_input.hpp"
#include "gs/gs.hpp"
#include "gs/gpu/gpu.hpp"
#include <SDL3/SDL_metal.h>
#include <algorithm>
#include "ps2native/io.hpp"
#include <sstream>
#include <cstdio>
using ps2native::engine::Pad;
namespace {
constexpr SDL_GamepadButton buttons[16]={SDL_GAMEPAD_BUTTON_BACK,SDL_GAMEPAD_BUTTON_LEFT_STICK,SDL_GAMEPAD_BUTTON_RIGHT_STICK,SDL_GAMEPAD_BUTTON_START,SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,SDL_GAMEPAD_BUTTON_DPAD_DOWN,SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_INVALID,SDL_GAMEPAD_BUTTON_INVALID,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,SDL_GAMEPAD_BUTTON_NORTH,SDL_GAMEPAD_BUTTON_EAST,SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_WEST};
constexpr unsigned pressure_bits[]={5,7,4,6,12,13,14,15,10,11,8,9};
constexpr SDL_Scancode keys[]={SDL_SCANCODE_TAB,SDL_SCANCODE_UNKNOWN,SDL_SCANCODE_UNKNOWN,SDL_SCANCODE_RETURN,SDL_SCANCODE_UP,SDL_SCANCODE_RIGHT,SDL_SCANCODE_DOWN,SDL_SCANCODE_LEFT,SDL_SCANCODE_LSHIFT,SDL_SCANCODE_RSHIFT,SDL_SCANCODE_Q,SDL_SCANCODE_E,SDL_SCANCODE_V,SDL_SCANCODE_C,SDL_SCANCODE_X,SDL_SCANCODE_Z};
[[noreturn]] void fail(){throw std::runtime_error(SDL_GetError());}
}
EngineInput::EngineInput(ps2native::engine::Console& c,bool window,const std::filesystem::path& script):console_(c){
    if(!SDL_InitSubSystem(SDL_INIT_GAMEPAD|SDL_INIT_EVENTS|(window?SDL_INIT_VIDEO:0)))fail();
    try{
        if(window){SDL_WindowFlags flags=SDL_WINDOW_RESIZABLE;
#if defined(SDL_PLATFORM_MACOS)
            flags|=SDL_WINDOW_METAL;
#endif
            window_=SDL_CreateWindow("PS2Native Engine - DS2: arrows, X/C/Z/V, Enter/Tab",640,448,flags);if(!window_)fail();}
        if(!script.empty()){
            ps2native::InputFile source(script);
            if(source.size()>65536)throw std::runtime_error("Input script exceeds 64 KiB");
            std::vector<std::uint8_t> bytes(source.size());if(source.read_at(0,bytes)!=bytes.size())throw std::runtime_error("Truncated input script");
            std::istringstream file(std::string(bytes.begin(),bytes.end()));std::uint64_t frame;unsigned mask;
            while(file>>std::dec>>frame){if(!(file>>std::hex>>mask))throw std::runtime_error("Incomplete input event");if(mask>65535 || script_.size()>=4096 || (!script_.empty() && frame<script_.back().frame))throw std::runtime_error("Invalid input event order/mask");script_.push_back({frame,static_cast<std::uint16_t>(mask)});}
            if(!file.eof() || script_.empty())throw std::runtime_error("Input script requires frame and hexadecimal pressed-button mask pairs");
            SDL_VirtualJoystickDesc d;SDL_INIT_INTERFACE(&d);d.type=SDL_JOYSTICK_TYPE_GAMEPAD;d.name="PS2Native explicit SDL input replay";d.naxes=SDL_GAMEPAD_AXIS_COUNT;d.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;d.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;d.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
            virtual_id_=SDL_AttachVirtualJoystick(&d);if(!virtual_id_)fail();virtual_=SDL_OpenJoystick(virtual_id_);if(!virtual_)fail();
        }
        console_.input_poll=[this]{poll();};poll();
    }catch(...){if(attached_)ps2::gs::gpu::detach_surface();for(auto* d:devices_)if(d)SDL_CloseGamepad(d);if(metal_view_)SDL_Metal_DestroyView(metal_view_);if(virtual_)SDL_CloseJoystick(virtual_);if(virtual_id_)SDL_DetachVirtualJoystick(virtual_id_);if(window_)SDL_DestroyWindow(window_);SDL_QuitSubSystem(SDL_INIT_GAMEPAD|SDL_INIT_EVENTS|(window?SDL_INIT_VIDEO:0));console_.input_poll={};throw;}
}
EngineInput::~EngineInput(){console_.input_poll={};if(attached_)ps2::gs::gpu::detach_surface();for(auto* d:devices_)if(d)SDL_CloseGamepad(d);if(virtual_)SDL_CloseJoystick(virtual_);if(virtual_id_)SDL_DetachVirtualJoystick(virtual_id_);if(metal_view_)SDL_Metal_DestroyView(metal_view_);if(window_)SDL_DestroyWindow(window_);SDL_QuitSubSystem(SDL_INIT_GAMEPAD|SDL_INIT_EVENTS|(window_?SDL_INIT_VIDEO:0));}
void EngineInput::poll(){
    while(next_<script_.size() && script_[next_].frame<=console_.graphics_frames){const auto e=script_[next_++];
        for(unsigned i=0;i<16;++i)if(buttons[i]!=SDL_GAMEPAD_BUTTON_INVALID && !SDL_SetJoystickVirtualButton(virtual_,buttons[i],(e.buttons&(1u<<i))!=0))fail();
        if(!SDL_SetJoystickVirtualAxis(virtual_,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,e.buttons&0x100?32767:-32768) || !SDL_SetJoystickVirtualAxis(virtual_,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,e.buttons&0x200?32767:-32768))fail();
        std::fprintf(stderr,"[input-replay] frame=%llu pressed=%04X via SDL virtual joystick\n",static_cast<unsigned long long>(e.frame),e.buttons);
    }
    SDL_Event event;while(SDL_PollEvent(&event))if(event.type==SDL_EVENT_QUIT)throw std::runtime_error("Host input window closed");SDL_UpdateGamepads();
    for(unsigned port=0;port<2;++port)if(devices_[port] && !SDL_GamepadConnected(devices_[port])){SDL_CloseGamepad(devices_[port]);devices_[port]=nullptr;bindings_[port].clear();console_.pads[port].disconnect();std::fprintf(stderr,"[input] port %u disconnected\n",port);}
    int count;auto* ids=SDL_GetGamepads(&count);if(!ids)fail();
    // Explicit replay owns port zero; physical devices retain their normal order otherwise.
    if(virtual_id_){auto* found=std::find(ids,ids+count,virtual_id_);if(found!=ids+count)std::rotate(ids,found,found+1);}
    for(int i=0;i<count;++i){if(std::any_of(std::begin(devices_),std::end(devices_),[&](auto* d){return d && SDL_GetGamepadID(d)==ids[i];}))continue;
        for(unsigned port=0;port<2;++port)if(!devices_[port]){auto* d=SDL_OpenGamepad(ids[i]);if(!d)fail();devices_[port]=d;console_.pads[port].disconnect();int n=0;auto** b=SDL_GetGamepadBindings(d,&n);if(b){for(int j=0;j<n;++j)bindings_[port].push_back(*b[j]);SDL_free(b);}std::fprintf(stderr,"[input] port %u SDL %s\n",port,SDL_GetGamepadName(d));break;}
    }SDL_free(ids);
    for(unsigned port=0;port<2;++port){auto& p=console_.pads[port];auto* d=devices_[port];const bool keyboard=!port && window_;if(!d && !keyboard)continue;
        p.connected=true;p.buttons=0xffff;p.sticks={128,128,128,128};p.pressure={};p.analog_pressure_mask=0;
        const bool focus=window_ && (SDL_GetWindowFlags(window_)&SDL_WINDOW_INPUT_FOCUS);const auto* state=SDL_GetKeyboardState(nullptr);
        for(unsigned bit=0;bit<16;++bit){const bool down=(d && buttons[bit]!=SDL_GAMEPAD_BUTTON_INVALID && SDL_GetGamepadButton(d,buttons[bit])) || (keyboard && focus && keys[bit]!=SDL_SCANCODE_UNKNOWN && state[keys[bit]]);if(down)p.buttons&=~(1u<<bit);}
        if(d){
            constexpr SDL_GamepadAxis axes[]={SDL_GAMEPAD_AXIS_RIGHTX,SDL_GAMEPAD_AXIS_RIGHTY,SDL_GAMEPAD_AXIS_LEFTX,SDL_GAMEPAD_AXIS_LEFTY};
            for(unsigned i=0;i<4;++i)if(SDL_GamepadHasAxis(d,axes[i]))p.sticks[i]=static_cast<std::uint8_t>((static_cast<int>(SDL_GetGamepadAxis(d,axes[i]))+32768)/256);
            for(unsigned i=0;i<2;++i){const auto axis=i?SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:SDL_GAMEPAD_AXIS_LEFT_TRIGGER;if(SDL_GamepadHasAxis(d,axis)){const auto value=std::max<int>(0,SDL_GetGamepadAxis(d,axis))*255/32767;p.pressure[10+i]=value;p.analog_pressure_mask|=1u<<(10+i);if(value)p.buttons&=~(1u<<(8+i));}}
        }
        for(unsigned i=0;i<12;++i)if(!(p.analog_pressure_mask&(1u<<i)))p.pressure[i]=(p.buttons&(1u<<pressure_bits[i]))?0:255;
        if(d)for(const auto& b:bindings_[port])if(b.input_type==SDL_GAMEPAD_BINDTYPE_AXIS && b.output_type==SDL_GAMEPAD_BINDTYPE_BUTTON){for(unsigned i=0;i<12;++i)if(buttons[pressure_bits[i]]==b.output.button){const auto lo=b.input.axis.axis_min,hi=b.input.axis.axis_max;if(lo!=hi){const auto raw=SDL_GetJoystickAxis(SDL_GetGamepadJoystick(d),b.input.axis.axis);p.pressure[i]=std::clamp((static_cast<int>(raw)-lo)*255/(hi-lo),0,255);p.analog_pressure_mask|=1u<<i;}}}
        // SDL HIDAPI PS3 exposes ten real pressure axes after the six standard axes.
        if(d && SDL_GetGamepadType(d)==SDL_GAMEPAD_TYPE_PS3 && SDL_GetNumJoystickAxes(SDL_GetGamepadJoystick(d))>=16){
            constexpr unsigned pressure_axes[]={15,14,12,13,9,7,6,8,10,11,4,5};
            for(unsigned i=0;i<12;++i){p.pressure[i]=(static_cast<int>(SDL_GetJoystickAxis(SDL_GetGamepadJoystick(d),pressure_axes[i]))+32768)/256;p.analog_pressure_mask|=1u<<i;}
        }
        if(keyboard && focus)for(unsigned i=0;i<12;++i)if(keys[pressure_bits[i]]!=SDL_SCANCODE_UNKNOWN && state[keys[pressure_bits[i]]])p.pressure[i]=255;
        if(d && SDL_GetBooleanProperty(SDL_GetGamepadProperties(d),SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN,false))if(!SDL_RumbleGamepad(d,p.rumble[1]*257,p.rumble[0]?65535:0,100))fail();
    }
    if(window_){const auto now=SDL_GetTicksNS();if(!sample_wall_){sample_wall_=now;sample_ticks_=console_.ticks;sample_frames_=console_.graphics_frames;}
        if(now-sample_wall_>=1000000000ull){const double seconds=double(now-sample_wall_)/1e9;char title[160];std::snprintf(title,sizeof(title),"PS2Native Engine | %.2fx | %.1f VBlank/s | Enter: Start, X: Cross",double(console_.ticks-sample_ticks_)/294912000/seconds,double(console_.graphics_frames-sample_frames_)/seconds);if(!SDL_SetWindowTitle(window_,title))fail();sample_wall_=now;sample_ticks_=console_.ticks;sample_frames_=console_.graphics_frames;}}
    if(window_ && console_.graphics_initialized){
        int w,h;if(!SDL_GetWindowSizeInPixels(window_,&w,&h))fail();if(w<=0 || h<=0)return;
        if(!attached_){const auto props=SDL_GetWindowProperties(window_);bool ok=false;
#if defined(SDL_PLATFORM_WINDOWS)
            ok=ps2::gs::attach_window(3,SDL_GetPointerProperty(props,SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER,nullptr),0,SDL_GetPointerProperty(props,SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr),nullptr,w,h);
#elif defined(SDL_PLATFORM_LINUX)
            if(auto* display=SDL_GetPointerProperty(props,SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER,nullptr))ok=ps2::gs::attach_window(1,display,0,SDL_GetPointerProperty(props,SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER,nullptr),nullptr,w,h);
            else ok=ps2::gs::attach_window(0,SDL_GetPointerProperty(props,SDL_PROP_WINDOW_X11_DISPLAY_POINTER,nullptr),SDL_GetNumberProperty(props,SDL_PROP_WINDOW_X11_WINDOW_NUMBER,0),nullptr,nullptr,w,h);
#elif defined(SDL_PLATFORM_MACOS)
            metal_view_=SDL_Metal_CreateView(window_);if(!metal_view_)fail();
            ok=ps2::gs::attach_window(4,nullptr,0,SDL_Metal_GetLayer(metal_view_),nullptr,w,h);
#else
            throw std::runtime_error("SDL native presentation surface is not connected for this host");
#endif
            if(!ok)throw std::runtime_error("SDL/WebGPU surface attachment failed");attached_=true;
        }ps2::gs::resize_window(w,h);if(!ps2::gs::present())throw std::runtime_error("SDL/WebGPU presentation failed");
    }
}
