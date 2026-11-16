#pragma once
#include "ps2/vu_semantics.hpp"
#include <array>
namespace ps2native::engine {
struct VuExecution {
    vu_detail::Unit unit{};
    ps2::vuops::FlagPipe pipe{};
    vu_detail::BranchView branch{};
    std::uint32_t vf_ready[32][4]{},vi_ready[16]{};
    std::uint32_t pc{},npc{},entry{},cmsar{};
    std::uint64_t next_tick{},pairs{},aot_pairs{},interpreted_pairs{};
    bool busy{},ending{},reported{};
};
using VuPairFn=void(*)(VuExecution&);
struct VuProgram {
    unsigned unit;
    const std::uint8_t* image;
    std::size_t bytes;
    const VuPairFn* pairs;
};
void register_vu_programs(const VuProgram* programs,std::size_t count);
// AOT instantiates constants; the same semantics remain the explicit interpreter fallback.
PS2_VU_INLINE void execute_vu_pair(VuExecution& v,std::uint32_t lo,std::uint32_t hi) {
    auto& u=v.unit;auto& r=*u.regs;
    if(hi&0x18000000u) throw std::runtime_error("VU D/T interrupt delivery is not integrated");
    const auto use=ps2::vu::vf_use(lo,hi);
    v.pipe.t=ps2::vu::issue_cycle(use,v.pipe.t+1,v.vf_ready,v.vi_ready,v.pipe.q_ready);
    ps2::vu::retire(use,v.pipe.t,v.vf_ready,v.vi_ready);
    if(v.pipe.q_pending && ps2::vu::reads_q(hi)) {
        const auto kind=ps2::vu::q_read_kind(u.code,u.code_mask,v.pc);
        if(kind==ps2::vu::kQNew)v.pipe.flush_q(r);
        else if(kind==ps2::vu::kQDynamic)v.pipe.sync_q(r);
    }
    const auto pc=v.pc;v.pc=v.npc;v.npc=(v.npc+8)&u.code_mask;
    auto out=vu_detail::upper(r,hi);
    std::uint16_t was[16];v.branch.snapshot(r,was);v.branch.arm(u,r);
    if(hi&0x80000000u) r.i=lo;
    else {bool branch=false;vu_detail::lower(u,lo,pc,v.npc,branch);}
    u.branch_vi=nullptr;v.branch.commit(was,lo,hi);
    vu_detail::commit(r,out);
    if(out.target>=0 && out.flags)v.pipe.set_mac(r.mac,r.status);
    if(u.fsset>=0) {r.status=(r.status&0x3f)|static_cast<std::uint32_t>(u.fsset);v.pipe.force_mac(r.mac,r.status);u.fsset=-1;}
    if(out.has_clip) {r.clip=out.clip;v.pipe.set_clip(out.clip,v.pipe.t);}
    r.vf[0]={};r.vf[0].w[3]=ps2::fp::kOne;r.vi[0]=0;
    if(v.ending) {v.pipe.flush_q(r);v.busy=false;r.tpc=v.pc;}
    else if(hi&0x40000000u)v.ending=true;
    ++v.pairs;
}
template<std::uint32_t Lo,std::uint32_t Hi> void native_vu_pair(VuExecution& v) {execute_vu_pair(v,Lo,Hi);}
}
