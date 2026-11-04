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
ObservedWord VuControl::micro_integer(unsigned unit,unsigned index,bool forwarded,unsigned bypass_delay) const {
    if(index>=16u)throw DeviceRefusal("VU micro integer register is outside VI00..VI15");
    if(!index)return {};
    const auto& state=micro_[unit];
    // Integer results bypass X/y/z/S to the next T stage. External COP2
    // transfers see the committed S-stage register, not this bypass.
    if(forwarded)for(unsigned n=state.writes;n>0;--n) {
        const auto& pending=state.pending[n-1u];
        if(pending.index==index && pending.ready-4u+bypass_delay<=state.next)return pending.word;
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
        auto& vector=write.index==32u ? (unit ? accumulator1_ : accumulator_) :
            (unit ? vu1_vectors_[write.index] : vectors_[write.index]);
        if(write.index)for(unsigned lane=0;lane<4u;++lane)
            if(write.mask&(8u>>lane))vector[lane]=write.words[lane];
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
        for(unsigned lane=0;lane<4u;++lane)if(write.mask&(8u>>lane))memory[write.index*4u+lane]=write.words[lane];
    }
    state.store_writes=kept;
}
VuControl::MicroFmac VuControl::evaluate_micro_upper(unsigned unit,std::uint32_t instruction) const {
    MicroFmac result;
    const auto code=instruction&0x07FFFFFFu;
    if(code==0x2FFu)return result;
    const auto function=code&63u,extended=code&0x7FFu;
    bool accumulator=false,accumulate=false;unsigned broadcast=4u;
    if((extended&~3u)==0x1BCu || (extended&~3u)==0xBCu) {
        accumulator=true;accumulate=(extended&~3u)==0xBCu;broadcast=extended&3u;
    } else if(extended==0x2BEu || extended==0x2BFu) {
        accumulator=true;accumulate=extended==0x2BFu;
    } else if((function&~3u)==0x18u || (function&~3u)==8u) {
        accumulate=(function&~3u)==8u;broadcast=function&3u;
    } else if(function==0x2Au || function==0x29u) {
        accumulate=function==0x29u;
    } else throw DeviceRefusal("upper instruction is not modeled: "+hexword(instruction));
    const auto mask=(code>>21u)&15u,fs=(code>>11u)&31u,ft=(code>>16u)&31u,fd=(code>>6u)&31u;
    const auto& state=micro_[unit];const auto& vectors=unit ? vu1_vectors_ : vectors_;
    const auto pending=[&](unsigned reg,unsigned lane) {
        if(!reg)return false;
        for(unsigned n=0;n<state.vector_writes;++n)
            if(state.vectors[n].index==reg && (state.vectors[n].mask&(8u>>lane)))return true;
        return false;
    };
    // FMAC source dependencies stall the entire LIW. A lower instruction in
    // this same pair reads the old values; neither half has committed yet.
    for(unsigned lane=0;lane<4u;++lane)if(mask&(8u>>lane)) {
        if(pending(fs,lane) || pending(ft,broadcast<4u ? broadcast : lane)) {
            result.hazard=true;return result;
        }
    }
    result.active=true;result.output={accumulator ? 32u : fd,mask,{},state.next+4u};
    for(unsigned lane=0;lane<4u;++lane) {
        const auto bit=8u>>lane;if(!(mask&bit))continue;
        const auto a=vectors[fs][lane],b=vectors[ft][broadcast<4u ? broadcast : lane];
        if(a.unknown || b.unknown)throw DeviceRefusal("FMAC consumes unknown VF operand bits");
        std::uint32_t flags=0;const auto product=ee_fpu_multiply(a.value,b.value,flags);
        if(!product)throw DeviceRefusal("FMAC multiply has ambiguous low-column Booth rounding");
        if(flags&0xC000u)throw DeviceRefusal("FMAC exceptional product/ACC metadata is not modeled");
        auto value=*product;
        if(accumulate) {
            auto acc=(unit ? accumulator1_ : accumulator_)[lane];
            // Consecutive MULA/MADDA/MADD share the accumulator bypass;
            // vector destinations and flags still commit only at S.
            for(unsigned n=state.vector_writes;n>0;--n) {
                const auto& previous=state.vectors[n-1u];
                if(previous.index==32u && (previous.mask&bit)){acc=previous.words[lane];break;}
            }
            if(acc.unknown)throw DeviceRefusal("FMAC accumulation consumes unknown ACC bits");
            value=ee_fpu_add(acc.value,value,flags);
            if(flags&0xC000u)throw DeviceRefusal("FMAC exceptional accumulation/ACC metadata is not modeled");
        }
        result.output.words[lane]={value,0};
        if(!(value&0x7FFFFFFFu)){result.mac|=bit;result.status|=1u;}
        if(value&0x80000000u){result.mac|=bit<<4u;result.status|=2u;}
    }
    return result;
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
    const auto fmac=evaluate_micro_upper(unit,hi);
    if(fmac.hazard){++state.next;return;}
    const bool ending=(hi&0x40000000u)!=0u;
    if(state.end_delay && ending)throw DeviceRefusal("E bit in the termination delay slot is not supported");
    const auto op=lo>>25u,is=(lo>>11u)&31u,it=(lo>>16u)&31u,id=(lo>>6u)&31u;
    std::optional<std::pair<unsigned,std::uint32_t>> result;
    std::optional<MicroVectorWrite> vector_result;
    std::optional<MicroVectorWrite> store_result;
    std::optional<std::uint32_t> branch;
    const auto reg=[&](unsigned r,unsigned bypass_delay=1) {
        const auto word=micro_integer(unit,r,true,bypass_delay);
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
    } else if(op==0x24u || op==0x25u) {
        if(state.branch || state.end_delay)throw DeviceRefusal("branch in a branch/E delay slot is not modeled");
        if((lo&0x01E007FFu) || (op==0x24u && it))throw DeviceRefusal("JR/JALR reserved fields are nonzero");
        // Branch operands have the documented one-instruction integer gap.
        // In particular, a preceding IALU result cannot replace the old target.
        branch=(reg(is,2)&(unit ? 2047u : 511u))*8u;
        if(op==0x25u) {
            if(it>=16u)throw DeviceRefusal("JALR link register is outside VI00..VI15");
            result=std::pair{it,(state.pc/8u+2u)&(unit ? 2047u : 511u)};
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
    } else if(op==0x40u && (lo&0x7FFu)==0x37Du) { // SQI, VU manual p.192, 1/4.
        if(state.end_delay)throw DeviceRefusal("VU memory store in the E delay slot is invalid");
        const auto address=reg(it),mask=(lo>>21u)&15u;
        if(mask) {
            if(address>=(unit ? 1024u : 256u))
                throw DeviceRefusal("SQI address outside direct VU data RAM; mirrors/register windows are not modeled");
            for(unsigned n=0;n<state.vector_writes;++n)
                if(is && state.vectors[n].index==is && (state.vectors[n].mask&mask)) {++state.next;return;}
            store_result=MicroVectorWrite{address,mask,unit ? vu1_vectors_[is] : vectors_[is],state.next+4u};
        }
        result=std::pair{it,(address+1u)&0xFFFFu};
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
    // Upper wins the whole VF destination, including fields it did not
    // select. LQI still performs its independent VI post-increment.
    if(fmac.active && fmac.output.mask && vector_result && fmac.output.index==vector_result->index)vector_result.reset();
    // No architectural effects of this pair until both decoders/operands
    // have succeeded. Integer S writeback is four cycles after T issue.
    if(result && result->first && state.writes==state.pending.size())throw DeviceRefusal("VU integer writeback queue is full");
    const auto lower_write=vector_result && vector_result->index && vector_result->mask;
    const auto upper_write=fmac.active && fmac.output.index && fmac.output.mask;
    if(state.vector_writes+static_cast<unsigned>(lower_write)+static_cast<unsigned>(upper_write)>state.vectors.size())
        throw DeviceRefusal("VU vector writeback queue is full");
    if(fmac.active && state.flag_writes==state.flags.size())throw DeviceRefusal("VU flag writeback queue is full");
    if(store_result && state.store_writes==state.stores.size())throw DeviceRefusal("VU memory store queue is full");
    if(result && result->first) {
        state.pending[state.writes++]={result->first,{result->second,0},state.next+4u};
    }
    if(lower_write)state.vectors[state.vector_writes++]=*vector_result;
    if(upper_write)state.vectors[state.vector_writes++]=fmac.output;
    if(fmac.active)state.flags[state.flag_writes++]={fmac.mac,fmac.status,state.next+4u};
    if(store_result)state.stores[state.store_writes++]=*store_result;
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
