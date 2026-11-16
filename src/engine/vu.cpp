#include "ps2native/engine/console.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"
#include "ps2/os.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <format>

namespace ps2native::engine {
void register_vu_programs(const VuProgram* programs,std::size_t count) {
    auto& c=active_console();
    for(std::size_t i=0;i<count;++i) {
        const auto& p=programs[i];
        if(p.unit>1 || p.bytes!=(p.unit ? 16384u : 4096u) || !p.image || !p.pairs)throw std::runtime_error("Invalid native VU program");
        c.vu_programs.push_back(&p);
    }
}
void Console::invalidate_vu(unsigned n) {vu_selected.at(n)=nullptr;vu[n].reported=false;}
void Console::start_vu(unsigned n,std::uint32_t address) {
    if(n>1 || (address&7))throw std::runtime_error("Invalid VU start");
    account();
    if(vu[n].busy)throw std::runtime_error("VU start while already running");
    const auto cmsar=vu[n].cmsar;vu[n]={};auto& v=vu[n];v.cmsar=cmsar;
    auto& r=n ? vu1_regs : ee.vu0;
    v.unit={&r,n ? vu1_code.data() : vu0_code.data(),n ? vu1_memory.data() : vu0_memory.data(),
        n ? 16383u : 4095u,n ? 1024u : 256u,0,0,n,&v.pipe};
    v.pc=address&v.unit.code_mask;v.entry=v.pc;v.npc=(v.pc+8)&v.unit.code_mask;
    v.busy=true;v.next_tick=ticks+1;v.pipe.start(r);vu_selected[n]=nullptr;
    for(auto* program:vu_programs)if(program->unit==n && std::equal(program->image,program->image+program->bytes,v.unit.code)) {vu_selected[n]=program;break;}
}
void Console::step_vu(unsigned n) {
    auto& v=vu[n];auto& u=v.unit;
    if(v.pairs>=10000000)throw std::runtime_error("VU pair limit reached");
    if(const auto* native=vu_selected[n]) {
        native->pairs[v.pc/8](v);++v.aot_pairs;++vu_aot_pairs[n];
    } else {
        if(!v.reported) {
            v.reported=true;
            std::fprintf(stderr,"[engine-vu] explicit interpreter fallback unit=%u entry=%04X\n",n,v.pc);
            if(const auto* root=std::getenv("PS2N_ENGINE_VU_CAPTURE_DIR")) {
                const auto bytes=std::span(u.code,u.code_mask+1);
                const auto directory=ps2::os::path(root);ps2native::create_directories(directory);
                const auto identity=ps2native::sha256_hex(bytes);
                const auto output=directory/std::format("vu{}-{}.bin",n,identity);
                std::size_t count=0;for(const auto& file:std::filesystem::directory_iterator(directory))if(file.path().extension()==".bin")++count;
                if(!std::filesystem::exists(output) && count>=32)throw std::runtime_error("VU capture limit reached");
                ps2native::write_binary_if_changed(output,bytes);
            }
        }
        const auto lo=ps2::load<std::uint32_t>(u.code+v.pc),hi=ps2::load<std::uint32_t>(u.code+v.pc+4);
        execute_vu_pair(v,lo,hi);++v.interpreted_pairs;++vu_interpreted_pairs[n];
    }
    if(v.busy) {
        const auto lo=ps2::load<std::uint32_t>(u.code+v.pc),hi=ps2::load<std::uint32_t>(u.code+v.pc+4);
        const auto use=ps2::vu::vf_use(lo,hi);
        const auto issue=ps2::vu::issue_cycle(use,v.pipe.t+1,v.vf_ready,v.vi_ready,v.pipe.q_ready);
        v.next_tick=ticks+issue-v.pipe.t;
    }
}
void Console::wait_vu(unsigned n) {
    account();
    while(vu.at(n).busy)advance_to(vu[n].next_tick);
}
}
namespace ps2::vu {
void kick(int unit,std::uint32_t address) {throw std::runtime_error(std::format("VU{} GIF XGKICK at {:04X}: GPU packet consumer is not integrated",unit,address));}
std::uint32_t top() {return ps2native::engine::active_console().vu[1].unit.top;}
std::uint32_t itop() {return itop(1);}
std::uint32_t itop(int unit) {return ps2native::engine::active_console().vu.at(unit).unit.itop;}
}
