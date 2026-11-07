// Independent implementation of VU manual 6.0 integer LIW/pipeline behavior.
#include "ps2native/vu_control.hpp"
#include "ps2native/gif.hpp"
#include "ps2native/vu_micro_ops.hpp"
#include <iomanip>
#include <limits>
#include <sstream>
#include <mutex>
namespace ps2native {
namespace {
struct VuRegistry {
    std::mutex mutex;
    std::vector<const VuAotModule*> modules;
};
VuRegistry& vu_registry() {static VuRegistry registry;return registry;}
std::string hexword(std::uint32_t value,unsigned width=8) {
    std::ostringstream out;out<<"0x"<<std::hex<<std::uppercase<<std::setw(static_cast<int>(width))<<std::setfill('0')<<value;return out.str();
}
}
VuAotRegistration::VuAotRegistration(const VuAotModule& module) {
    if(!module.image || !module.step || module.identity!=vu1_aot_identity(*module.image,module.diagnostic_power_two_carry))
        throw DeviceRefusal("VU1 AOT registration identity mismatch");
    auto& registry=vu_registry();std::lock_guard lock(registry.mutex);
    registry.modules.push_back(&module);
}
VuAotImage VuControl::capture_vu1_micro() const {
    VuAotImage result;
    std::copy(micro1_.begin(),micro1_.end(),result.words.begin());
    return result;
}
bool VuControl::try_vu1_aot(Gif* gif,GsRegisters* gs) {
    if(!vu1_aot_enabled_){vu1_aot_fallback_=VuAotFallback::Disabled;return false;}
    if(!vu1_aot_checked_) {
        vu1_aot_selected_=nullptr;
        auto& registry=vu_registry();std::lock_guard lock(registry.mutex);
        // No artifact linked: avoid hashing or allocating during gameplay.
        if(!registry.modules.empty()) {
            ++vu1_aot_stats_.identity_checks;
            const auto image=capture_vu1_micro();
            const auto identity=vu1_aot_identity(image,diagnostic_power_two_carry_);
            for(const auto* candidate:registry.modules) {
                if(candidate->identity!=identity || candidate->diagnostic_power_two_carry!=diagnostic_power_two_carry_)continue;
                bool equal=true;
                for(unsigned n=0;n<image.words.size();++n)
                    if(image.words[n].value!=candidate->image->words[n].value || image.words[n].unknown!=candidate->image->words[n].unknown){equal=false;break;}
                if(equal){vu1_aot_selected_=candidate;break;}
            }
        }
        vu1_aot_checked_=true;
    }
    if(!vu1_aot_selected_) {
        vu1_aot_fallback_=VuAotFallback::MissingProgram;++vu1_aot_stats_.missing_program;return false;
    }
    if(!vu1_aot_selected_->step(*this,gif,gs)) {
        // Multiple offline units may prove different roots of one image.
        // Their identity includes all words/masks and the semantic mode.
        auto& registry=vu_registry();std::lock_guard lock(registry.mutex);
        bool found=false;
        for(const auto* candidate:registry.modules)
            if(candidate!=vu1_aot_selected_ && candidate->identity==vu1_aot_selected_->identity &&
               candidate->diagnostic_power_two_carry==diagnostic_power_two_carry_ && candidate->step(*this,gif,gs)) {
                vu1_aot_selected_=candidate;found=true;break;
            }
        if(!found){vu1_aot_fallback_=VuAotFallback::UntranslatedPc;++vu1_aot_stats_.untranslated_pc;return false;}
    }
    vu1_aot_fallback_=VuAotFallback::None;return true;
}
void VuControl::reset_micro(unsigned unit) {
    if(unit && path1_)throw DeviceRefusal("VU1 reset during an active PATH1 transfer is not modeled");
    micro_.at(unit)={};
    if(registers_[29])*registers_[29]&=~(unit ? 256u : 1u);
}
void VuControl::start_micro(unsigned unit,std::uint32_t address,std::uint64_t cycle,ObservedWord itop,ObservedWord top) {
    if(unit>1u)throw DeviceRefusal("VU micro unit must be 0 or 1");
    auto& state=micro_[unit];
    if(state.running)throw DeviceRefusal("VU micro start while the selected engine is busy");
    if(address>=(unit ? 2048u : 512u) || cycle>std::numeric_limits<std::uint64_t>::max()-8u)
        throw DeviceRefusal("VU micro start address/cycle is out of range");
    if(unit && vu1_capture_){vu1_capture_(*this,address*8u);vu1_capture_dirty_=false;}
    // Open the next issue slot after delivery. This is functional event
    // scheduling, not a measured VIF-to-VU physical start latency.
    state={};state.running=true;state.pc=address*8u;state.next=cycle+1u;
    const auto mask=unit ? 0x3FFu : 0xFFu;
    state.itop={itop.value&~itop.unknown&mask,itop.unknown&mask};
    state.top={top.value&~top.unknown&0x3FFu,top.unknown&0x3FFu};
    *registers_[29]|=unit ? 256u : 1u;
}
ObservedWord VuControl::micro_integer(unsigned unit,unsigned index,bool forwarded,unsigned bypass_delay) const {
    if(index>=16u)throw DeviceRefusal("VU micro integer register is outside VI00..VI15");
    if(!index)return {};
    const auto& state=micro_[unit];
    // VU manual 3.4.1: integer memory loads interlock dependent uses until
    // S. They do not receive the one-cycle IALU result bypass.
    if(forwarded)for(unsigned n=0;n<state.writes;++n)
        if(state.pending[n].index==index && state.pending[n].memory_load)throw MicroPipelineStall{};
    // Integer results bypass X/y/z/S to the next T stage. External COP2
    // transfers see the committed S-stage register, not this bypass.
    if(forwarded)for(unsigned n=state.writes;n>0;--n) {
        const auto& pending=state.pending[n-1u];
        if(pending.index==index && pending.ready-4u+(pending.flag_check ? 1u : bypass_delay)<=state.next)return pending.word;
    }
    return unit ? vu1_integers_[index] : ObservedWord{*registers_[index],unknown_[index]};
}
void VuControl::retire_micro_integers(unsigned unit) {
    auto& state=micro_[unit];unsigned kept=0;
    for(unsigned n=0;n<state.writes;++n) {
        const auto write=state.pending[n];
        if(write.ready>state.next){state.pending[kept++]=write;continue;}
        if(write.index==18u) {
            if(unit)vu1_controls_[2]=write.word;
            else {registers_[18]=write.word.value;unknown_[18]=write.word.unknown;}
        }else {
            if(write.index>=16u)throw DeviceRefusal("VU pending integer register is outside the modeled range");
            if(unit)vu1_integers_[write.index]=write.word;
            else {registers_[write.index]=write.word.value;unknown_[write.index]=write.word.unknown;}
        }
    }
    state.writes=kept;
    kept=0;
    for(unsigned n=0;n<state.vector_writes;++n) {
        const auto write=state.vectors[n];
        if(write.ready>state.next){state.vectors[kept++]=write;continue;}
        auto& vector=write.index==32u ? (unit ? accumulator1_ : accumulator_) :
            (unit ? vu1_vectors_[write.index] : vectors_[write.index]);
        if(write.index)for(unsigned lane=0;lane<4u;++lane)
            if(write.mask&(8u>>lane)){
                vector[lane]=write.words[lane];
                if(unit)vu1_vector_bounds_[write.index][lane]=write.bounds[lane];
                else if(write.index==32u)accumulator_bounds_[lane]=write.bounds[lane];
                else vector_bounds_[write.index][lane]=write.bounds[lane];
            }
    }
    state.vector_writes=kept;
    kept=0;
    for(unsigned n=0;n<state.flag_writes;++n) {
        const auto write=state.flags[n];
        if(write.ready>state.next){state.flags[kept++]=write;continue;}
        if(unit) {
            vu1_controls_[1]={write.mac,0};
            auto& status=vu1_controls_[0];
            status.value=(status.value&~15u)|write.status|(write.status<<6u);
            status.unknown&=~(15u|(write.status<<6u));
        } else {
            registers_[17]=write.mac;unknown_[17]=0;
            registers_[16]=(*registers_[16]&~15u)|write.status|(write.status<<6u);
            unknown_[16]&=~(15u|(write.status<<6u));
        }
    }
    state.flag_writes=kept;
    kept=0;
    for(unsigned n=0;n<state.store_writes;++n) {
        const auto write=state.stores[n];
        if(write.ready>state.next){state.stores[kept++]=write;continue;}
        auto& memory=unit ? data1_ : data_;
        for(unsigned lane=0;lane<4u;++lane)if(write.mask&(8u>>lane)){memory[write.index*4u+lane]=write.words[lane];(unit ? data1_bounds_ : data_bounds_)[write.index*4u+lane]=write.bounds[lane];}
    }
    state.store_writes=kept;
}
VuControl::MicroFmac VuControl::evaluate_micro_upper(unsigned unit,std::uint32_t instruction) const {
    return evaluate_micro_upper_shared<-1,-1>(unit,instruction);
}
void VuControl::step_micro(unsigned unit,Gif* gif,GsRegisters* gs) {
    issue_micro_shared<-1,-1,-1>(unit,0,0,gif,gs);
}
void VuControl::step_path1(Gif* gif,GsRegisters* gs) {
    auto& transfer=*path1_;
    if(transfer.failure)throw DeviceRefusal(*transfer.failure);
    try {
        if(!gif || !gs)throw DeviceRefusal("active PATH1 requires the shared GIF/GS devices");
        if(!gif->path1_requested())throw DeviceRefusal("GIF reset interrupted the active VU1 PATH1 transfer");
        if(!gif->path1_ready()){transfer.next+=kEeCyclesPerBusCycle;return;}
        std::array<ObservedWord,4> words{};
        for(unsigned lane=0;lane<4u;++lane) {
            words[lane]=data1_[transfer.address*4u+lane];
        }
        gif->write_path1_observed({words[0].value|(std::uint64_t{words[1].value}<<32u),
                                  words[0].unknown|(std::uint64_t{words[1].unknown}<<32u)},
                                 {words[2].value|(std::uint64_t{words[3].value}<<32u),
                                  words[2].unknown|(std::uint64_t{words[3].unknown}<<32u)},*gs);
        ++transfer.qwords;transfer.address=(transfer.address+1u)&1023u;transfer.next+=kEeCyclesPerBusCycle;
        if(!gif->path1_requested()) {
            if(queued_path1_) {
                // Open the next bus beat for the latched second packet.
                // Never drain either packet inline or snapshot future data.
                gif->request_path1();queued_path1_->next=transfer.next;
                path1_=*queued_path1_;queued_path1_.reset();
            } else path1_.reset();
        }
    } catch(const DeviceRefusal& error) {
        transfer.failure="VU1 PATH1 at data address "+hexword(transfer.address*16u,4)+" after "+
            std::to_string(transfer.qwords)+" qwords: "+error.what();
        throw DeviceRefusal(*transfer.failure);
    }
}
void VuControl::advance_micro_q(std::uint64_t cycle) {
    advance_q(cycle);
    if(!pending_q1_ || pending_q1_->ready>cycle)return;
    vu1_controls_[6]={pending_q1_->value,pending_q1_->unknown};
    const auto flags=pending_q1_->flags;auto& status=vu1_controls_[0];
    status.value=(status.value&~0x30u)|flags|(flags<<6u);
    status.unknown&=~(0x30u|(flags<<6u));pending_q1_.reset();
}
void VuControl::advance_micro(std::uint64_t cycle,Gif* gif,GsRegisters* gs) {
    // No architectural state can change before the earliest micro/PATH1/Q
    // deadline. Include both FDIV pipelines even after their issuing program
    // has stopped; Q publication is independent of the micro running bits.
    if(!micro_event_due(cycle)) return;
    profiling::Scope profile(profiling::Phase::Vu);
    for(;;) {
        unsigned unit=2;
        for(unsigned n=0;n<2u;++n)if(micro_[n].running && micro_[n].next<=cycle &&
            (unit==2u || micro_[n].next<micro_[unit].next))unit=n;
        if(path1_ && path1_->next<=cycle && (unit==2u || path1_->next<micro_[unit].next)) {
            advance_micro_q(path1_->next);
            step_path1(gif,gs);continue;
        }
        if(unit==2u){advance_micro_q(cycle);return;}
        auto& state=micro_[unit];
        advance_micro_q(state.next);
        if(state.failure)throw DeviceRefusal(*state.failure);
        try {
            // MPG/aperture writes can change an active program without a new
            // launch. Optional exports observe the complete image at its next
            // issue boundary, including with native execution disabled.
            if(unit && vu1_capture_dirty_ && vu1_capture_ && !state.drain && !queued_path1_) {
                vu1_capture_(*this,state.pc);vu1_capture_dirty_=false;
            }
            const auto before=state.instructions;
            // Idle/drain/interlock cycles remain in the common scheduler.
            const bool native=unit && !state.drain && !queued_path1_ && try_vu1_aot(gif,gs);
            if(!native)step_micro(unit,gif,gs);
            if(unit)(native ? vu1_aot_stats_.native_instructions : vu1_aot_stats_.interpreted_instructions)+=state.instructions-before;
        }
        catch(const MicroPipelineStall&) {++state.next;}
        catch(const DeviceRefusal& error) {
            state.failure="VU"+std::to_string(unit)+" micro PC "+hexword(state.pc,4)+" after "+std::to_string(state.instructions)+" instructions: "+error.what();
            throw DeviceRefusal(*state.failure);
        }
    }
}
} // namespace ps2native
