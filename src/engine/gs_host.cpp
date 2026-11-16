#include "ps2native/engine/console.hpp"
#include "ps2/hitch.hpp"
#include "gs/gpu/gpu.hpp"
#include "ps2native/io.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
namespace ps2 {
GsHostOptions& gs_options(){return ps2native::engine::active_console().gs_options;}
std::uint64_t now(){return ps2native::engine::active_console().now();}
std::uint64_t frame_count(){return ps2native::engine::active_console().graphics_frames;}
std::uint64_t frames_run(){return frame_count();}
std::uint64_t scheduled_at(int kind){auto& c=ps2native::engine::active_console();const auto it=c.graphics_events.find(kind);return it==c.graphics_events.end() ? ~0ull : it->second.first;}
void schedule(int kind,std::uint64_t at,std::function<void()> action){ps2native::engine::active_console().graphics_events[kind]={at,[action=std::move(action)]{ps2::gs::gpu::wait_idle();action();}};}
std::uint32_t video_options(){return ps2native::engine::active_console().host_video_options;}
void set_video_options(std::uint32_t value){ps2native::engine::active_console().host_video_options=value;}
}
namespace ps2::gs {
State& state_registers(){return ps2native::engine::active_console().gs_state;}
std::uint8_t* host_vram(){return ps2native::engine::active_console().gs_vram.data();}
}
namespace ps2::hw {
std::uint32_t crt_mode(){return ps2native::engine::active_console().gs_crt_mode;}
void intc_raise(unsigned cause){ps2native::engine::active_console().intc_status|=1u<<cause;}
}
namespace ps2::vu {
std::uint32_t trace_vu1_entry(){return ps2native::engine::active_console().vu[1].entry;}
void trace_dump(const char* path){const auto& c=ps2native::engine::active_console();ps2native::write_file_atomically(std::filesystem::path(path),c.vu1_code);}
}
namespace ps2::hitch {
double thread_cpu_ms(){
#ifdef _WIN32
FILETIME created,exited,kernel,user;if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))return 0;const auto ticks=[](FILETIME t){return (static_cast<std::uint64_t>(t.dwHighDateTime)<<32)|t.dwLowDateTime;};return (ticks(kernel)+ticks(user))/1e4;
#else
timespec t{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t))return 0;return t.tv_sec*1000.0+t.tv_nsec/1e6;
#endif
}
}

namespace ps2::vif {
std::array<Unit,2>& unit_storage(){return ps2native::engine::active_console().vif_state;}
std::uint8_t* vu_memory(int unit){auto& c=ps2native::engine::active_console();return unit ? c.vu1_memory.data() : c.vu0_memory.data();}
std::uint8_t* vu_code(int unit){auto& c=ps2native::engine::active_console();return unit ? c.vu1_code.data() : c.vu0_code.data();}
}
namespace ps2::gif {
std::array<Path,4>& path_storage(){return ps2native::engine::active_console().gif_paths;}
std::array<Path,4>& scan_storage(){return ps2native::engine::active_console().gif_scans;}
std::uint32_t& status_storage(){return ps2native::engine::active_console().gif_status;}
Held& held_storage(){return ps2native::engine::active_console().gif_held;}
}
namespace ps2::vu {
void wait(int unit){ps2native::engine::active_console().wait_vu(unit);}
void run(int unit,std::uint32_t address){auto& c=ps2native::engine::active_console();c.wait_vu(unit);if(unit==0)c.vu[0].unit.itop=c.vif_state[0].itop;c.start_vu(unit,address);}
std::uint32_t tpc(int unit){auto& c=ps2native::engine::active_console();return unit ? c.vu1_regs.tpc : c.ee.vu0.tpc;}
void set_tops(std::uint32_t top,std::uint32_t itop){auto& c=ps2native::engine::active_console();c.vu[1].unit.top=top;c.vu[1].unit.itop=itop;}
void code_changed(int unit){ps2native::engine::active_console().invalidate_vu(unit);}
}
namespace ps2::dmac {
std::uint32_t read(std::uint32_t address){return ps2native::engine::active_console().dma_registers[address];}
void resume(int unit){ps2native::engine::active_console().graphics_dma_resume(unit);}
}

namespace ps2::gs {
std::array<std::uint64_t,32>& private_registers(){return ps2native::engine::active_console().gs_private;}
std::array<std::uint64_t,2>& display_registers(){return ps2native::engine::active_console().gs_display;}
std::array<std::uint64_t,2>& display_buffers(){return ps2native::engine::active_console().gs_buffers;}
void ensure_initialized(){ps2native::engine::active_console().initialize_graphics();}
}

namespace ps2::gs {
void write_privileged32(std::uint32_t address,std::uint32_t value) {
    auto& c=ps2native::engine::active_console();const auto offset=address&0x1ff0;
    std::uint64_t old=0;
    if(offset<=0x60)old=c.gs_private[offset/16];
    else if(offset==0x70 || offset==0x90)old=c.gs_buffers[offset==0x90];
    else if(offset==0x80 || offset==0xa0)old=c.gs_display[offset==0xa0];
    else if(offset>=0xb0 && offset<=0xe0)old=c.gs_private[7+(offset-0xb0)/16];
    else if(offset==0x1080)old=c.gs_private[14];
    else {if(address&4)return;write_privileged(address,value);return;}
    const auto shift=(address&4)*8;write_privileged(address&~7u,(old&~(0xffffffffull<<shift))|(static_cast<std::uint64_t>(value)<<shift));
}
}

namespace ps2::gs {
std::uint32_t& finish_pending(){return ps2native::engine::active_console().gs_finish_pending;}
std::uint32_t& finish_queued(){return ps2native::engine::active_console().gs_finish_queued;}
bool& odd_field(){return ps2native::engine::active_console().gs_odd_field;}
}
