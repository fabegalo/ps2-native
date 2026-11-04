// Independent implementation of VU manual 6.0 integer LIW/pipeline behavior.
#include "ps2native/vu_control.hpp"
#include <iomanip>
#include <limits>
#include <sstream>
namespace ps2native {
namespace {
std::string hexword(std::uint32_t value,unsigned width=8) {
    std::ostringstream out;out<<"0x"<<std::hex<<std::uppercase<<std::setw(static_cast<int>(width))<<std::setfill('0')<<value;return out.str();
}
}
void VuControl::reset_micro(unsigned unit) {
    micro_.at(unit)={};
    if(registers_[29])*registers_[29]&=~(unit ? 256u : 1u);
}
void VuControl::start_micro(unsigned unit,std::uint32_t address,std::uint64_t cycle) {
    if(unit>1u)throw DeviceRefusal("VU micro unit must be 0 or 1");
    auto& state=micro_[unit];
    if(state.running)throw DeviceRefusal("VU micro start while the selected engine is busy");
    if(address>=(unit ? 2048u : 512u) || cycle>std::numeric_limits<std::uint64_t>::max()-8u)
        throw DeviceRefusal("VU micro start address/cycle is out of range");
    // Open the next issue slot after delivery. This is functional event
    // scheduling, not a measured VIF-to-VU physical start latency.
    state={};state.running=true;state.pc=address*8u;state.next=cycle+1u;
    *registers_[29]|=unit ? 256u : 1u;
}
ObservedWord VuControl::micro_integer(unsigned unit,unsigned index,bool forwarded) const {
    if(index>=16u)throw DeviceRefusal("VU micro integer register is outside VI00..VI15");
    if(!index)return {};
    const auto& state=micro_[unit];
    // Integer results bypass X/y/z/S to the next T stage. External COP2
    // transfers see the committed S-stage register, not this bypass.
    if(forwarded)for(unsigned n=state.writes;n>0;--n) {
        const auto& pending=state.pending[n-1u];
        if(pending.index==index && pending.ready-3u<=state.next)return pending.word;
    }
    return unit ? vu1_integers_[index] : ObservedWord{*registers_[index],unknown_[index]};
}
void VuControl::retire_micro_integers(unsigned unit) {
    auto& state=micro_[unit];unsigned kept=0;
    for(unsigned n=0;n<state.writes;++n) {
        const auto write=state.pending[n];
        if(write.ready>state.next){state.pending[kept++]=write;continue;}
        if(unit)vu1_integers_[write.index]=write.word;
        else {registers_[write.index]=write.word.value;unknown_[write.index]=write.word.unknown;}
    }
    state.writes=kept;
    kept=0;
    for(unsigned n=0;n<state.vector_writes;++n) {
        const auto write=state.vectors[n];
        if(write.ready>state.next){state.vectors[kept++]=write;continue;}
        auto& vector=unit ? vu1_vectors_[write.index] : vectors_[write.index];
        if(write.index)for(unsigned lane=0;lane<4u;++lane)
            if(write.mask&(8u>>lane))vector[lane]=write.words[lane];
    }
    state.vector_writes=kept;
}
void VuControl::step_micro(unsigned unit) {
    auto& state=micro_[unit];retire_micro_integers(unit);
    if(state.drain) {
        if(state.next>=*state.drain) {
            state.running=false;*registers_[29]&=~(unit ? 256u : 1u);
        }
        ++state.next;return;
    }
    const auto& image=unit ? micro1_ : micro0_;
    const auto low=image.at(state.pc/4u),upper=image.at(state.pc/4u+1u);
    if(low.unknown || upper.unknown)throw DeviceRefusal("instruction fetch consumes unwritten micro memory");
    const auto lo=low.value,hi=upper.value;
    if(hi&0xB8000000u)throw DeviceRefusal("I/M/D/T microinstruction controls are not modeled");
    if((hi&0x7FFu)!=0x2FFu)throw DeviceRefusal("upper instruction is not modeled: "+hexword(hi));
    const bool ending=(hi&0x40000000u)!=0u;
    if(state.end_delay && ending)throw DeviceRefusal("E bit in the termination delay slot is not supported");
    const auto op=lo>>25u,is=(lo>>11u)&31u,it=(lo>>16u)&31u,id=(lo>>6u)&31u;
    std::optional<std::pair<unsigned,std::uint32_t>> result;
    std::optional<MicroVectorWrite> vector_result;
    std::optional<std::uint32_t> branch;
    const auto reg=[&](unsigned r) {
        const auto word=micro_integer(unit,r,true);
        if(word.unknown&0xFFFFu)throw DeviceRefusal("integer operand VI"+std::to_string(r)+" contains unknown bits");
        return word.value&0xFFFFu;
    };
    if(op==8u || op==9u) {
        const auto imm=(lo&0x7FFu)|(((lo>>21u)&15u)<<11u);
        if(it>=16u)throw DeviceRefusal("integer destination is outside VI00..VI15");
        result=std::pair{it,(op==8u ? reg(is)+imm : reg(is)-imm)&0xFFFFu};
    } else if(op==0x20u || op==0x21u) {
        if(state.branch || state.end_delay)throw DeviceRefusal("branch in a branch/E delay slot is not modeled");
        const auto bits=lo&0x7FFu;
        const auto offset=static_cast<std::int32_t>(bits)-((bits&0x400u) ? 2048 : 0);
        const auto slots=unit ? 2048u : 512u;
        branch=((state.pc/8u+1u+static_cast<std::uint32_t>(offset))&(slots-1u))*8u;
        if(op==0x21u) {
            if(it>=16u)throw DeviceRefusal("BAL link register is outside VI00..VI15");
            result=std::pair{it,(state.pc/8u+2u)&(slots-1u)};
        }
    } else if(op==0x40u && (lo&0x7FFu)==0x37Cu) { // LQI, VU manual p.179, 1/4.
        if(state.end_delay)throw DeviceRefusal("VU memory load in the E delay slot is invalid");
        const auto address=reg(is),mask=(lo>>21u)&15u;
        MicroVectorWrite load{it,mask,{},state.next+4u};
        if(mask) {
            if(address>=(unit ? 1024u : 256u))
                throw DeviceRefusal("LQI address outside direct VU data RAM; mirrors/register windows are not modeled");
            const auto& memory=unit ? data1_ : data_;
            for(unsigned lane=0;lane<4u;++lane)if(mask&(8u>>lane))load.words[lane]=memory[address*4u+lane];
        }
        vector_result=load;
        result=std::pair{is,(address+1u)&0xFFFFu};
    } else if(lo==0x8000033Cu) { // MOVE with no fields and VF00 operands: lower NOP
    } else if(op==0x40u && ((lo&63u)==0x30u || (lo&63u)==0x31u || (lo&63u)==0x32u || (lo&63u)==0x34u || (lo&63u)==0x35u)) {
        const auto function=lo&63u;
        if(function==0x32u) {
            const auto immediate=static_cast<std::int32_t>(id)-((id&16u) ? 32 : 0);
            if(it>=16u)throw DeviceRefusal("IADDI destination is outside VI00..VI15");
            result=std::pair{it,(reg(is)+static_cast<std::uint32_t>(immediate))&0xFFFFu};
        } else {
            if(id>=16u)throw DeviceRefusal("integer destination is outside VI00..VI15");
            const auto a=reg(is),b=reg(it);
            result=std::pair{id,(function==0x30u ? a+b : function==0x31u ? a-b : function==0x34u ? a&b : a|b)&0xFFFFu};
        }
    } else throw DeviceRefusal("lower instruction is not modeled: "+hexword(lo));
    // No architectural effects of this pair until both decoders/operands
    // have succeeded. Integer S writeback is four cycles after T issue.
    if(result && result->first && state.writes==state.pending.size())throw DeviceRefusal("VU integer writeback queue is full");
    if(vector_result && vector_result->index && vector_result->mask && state.vector_writes==state.vectors.size())
        throw DeviceRefusal("VU vector writeback queue is full");
    if(result && result->first) {
        state.pending[state.writes++]={result->first,{result->second,0},state.next+4u};
    }
    if(vector_result && vector_result->index && vector_result->mask)state.vectors[state.vector_writes++]=*vector_result;
    const auto next=state.branch.value_or((state.pc+8u)&(unit ? 16383u : 4095u));
    state.branch=branch;state.pc=next;++state.instructions;
    if(state.end_delay)state.drain=state.next+4u; // the delay pair must reach S before Ready
    state.end_delay=ending;
    ++state.next;
}
void VuControl::advance_micro(std::uint64_t cycle) {
    for(;;) {
        unsigned unit=2;
        for(unsigned n=0;n<2u;++n)if(micro_[n].running && micro_[n].next<=cycle &&
            (unit==2u || micro_[n].next<micro_[unit].next))unit=n;
        if(unit==2u)return;
        auto& state=micro_[unit];
        if(state.failure)throw DeviceRefusal(*state.failure);
        try {step_micro(unit);}
        catch(const DeviceRefusal& error) {
            state.failure="VU"+std::to_string(unit)+" micro PC "+hexword(state.pc,4)+" after "+std::to_string(state.instructions)+" instructions: "+error.what();
            throw DeviceRefusal(*state.failure);
        }
    }
}
} // namespace ps2native
