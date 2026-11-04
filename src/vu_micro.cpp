// Independent implementation of VU manual 6.0 integer LIW/pipeline behavior.
#include "ps2native/vu_control.hpp"
#include "ps2native/gif.hpp"
#include <iomanip>
#include <limits>
#include <sstream>
namespace ps2native {
namespace {
struct MicroPipelineStall {};
std::string hexword(std::uint32_t value,unsigned width=8) {
    std::ostringstream out;out<<"0x"<<std::hex<<std::uppercase<<std::setw(static_cast<int>(width))<<std::setfill('0')<<value;return out.str();
}
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
    bool accumulator=false,accumulate=false,comparison=false,maximum=false,addition=false,subtract=false,fixed=false;unsigned broadcast=4u;
    if((extended&~3u)==0x17Cu)fixed=true;
    else if((extended&~3u)==0x1BCu || (extended&~3u)==0xBCu) {
        accumulator=true;accumulate=(extended&~3u)==0xBCu;broadcast=extended&3u;
    } else if(extended==0x2BEu || extended==0x2BFu) {
        accumulator=true;accumulate=extended==0x2BFu;
    } else if((function&~3u)==0u || (function&~3u)==4u) {
        addition=true;subtract=(function&~3u)==4u;broadcast=function&3u;
    } else if(function==0x28u || function==0x2Cu) {
        addition=true;subtract=function==0x2Cu;
    } else if(function==0x20u || function==0x24u) {
        addition=true;subtract=function==0x24u;broadcast=5u;
        if((code>>16u)&31u)throw DeviceRefusal("ADDq/SUBq reserved FT field is nonzero");
    } else if((function&~3u)==0x10u || (function&~3u)==0x14u) {
        comparison=true;maximum=(function&~3u)==0x10u;broadcast=function&3u;
    } else if(function==0x2Bu || function==0x2Fu) {
        comparison=true;maximum=function==0x2Bu;
    } else if((function&~3u)==0x18u || (function&~3u)==8u) {
        accumulate=(function&~3u)==8u;broadcast=function&3u;
    } else if(function==0x1Cu || function==0x21u) {
        accumulate=function==0x21u;broadcast=5u;
        if((code>>16u)&31u)throw DeviceRefusal("MULq/MADDq reserved FT field is nonzero");
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
        if(pending(fs,lane) || (!fixed && broadcast!=5u && pending(ft,broadcast<4u ? broadcast : lane))) {
            result.hazard=true;return result;
        }
    }
    result.active=true;result.changes_flags=!comparison && !fixed;result.output={fixed ? ft : accumulator ? 32u : fd,mask,{},state.next+4u};
    for(unsigned lane=0;lane<4u;++lane) {
        const auto bit=8u>>lane;if(!(mask&bit))continue;
        const auto a=vectors[fs][lane];
        if(fixed) {
            if(a.unknown)throw DeviceRefusal("FTOI consumes unknown VF operand bits");
            // VU manual 2.1-2.3: signed 32-bit fixed point, truncate toward
            // zero and clamp conversion overflow without changing flags.
            // Exponent zero is zero; exponent 255 remains a finite PS2 input.
            constexpr unsigned fractions[]{0,4,12,15};
            const auto exponent=(a.value>>23u)&255u;
            const int scaled=static_cast<int>(exponent)-127+static_cast<int>(fractions[extended&3u]);
            std::uint32_t value=0;
            if(exponent && scaled>=0) {
                if(scaled>=31)value=(a.value&0x80000000u) ? 0x80000000u : 0x7FFFFFFFu;
                else {
                    const auto mantissa=0x800000u|(a.value&0x7FFFFFu);
                    const auto magnitude=scaled>=23 ? mantissa<<static_cast<unsigned>(scaled-23) : mantissa>>static_cast<unsigned>(23-scaled);
                    value=(a.value&0x80000000u) ? 0u-magnitude : magnitude;
                }
            }
            result.output.words[lane]={value,0};continue;
        }
        const auto b=broadcast==5u ? (unit ? vu1_controls_[6] : observe(22)) : vectors[ft][broadcast<4u ? broadcast : lane];
        if(a.unknown || b.unknown)throw DeviceRefusal("FMAC consumes unknown VF operand bits");
        if(comparison) {
            const auto am=a.value&0x7FFFFFFFu,bm=b.value&0x7FFFFFFFu;
            if((am && !(am&0x7F800000u)) || (bm && !(bm&0x7F800000u)))
                throw DeviceRefusal("MAX/MINI denormal input selection is not established");
            const bool an=(a.value>>31u)!=0u,bn=(b.value>>31u)!=0u;
            // Exponent 255 is a finite PS2 number, not a host NaN. Compare
            // sign/magnitude directly; the manual selects FT on equal values,
            // including signed zeros. These instructions change no flags.
            const bool unequal=am || bm;
            const bool greater=unequal && (an!=bn ? !an : an ? am<bm : am>bm);
            const bool less=unequal && (an!=bn ? an : an ? am>bm : am<bm);
            result.output.words[lane]=(maximum ? greater : less) ? a : b;
            continue;
        }
        std::uint32_t flags=0,value=0;
        if(addition)value=ee_fpu_add(a.value,b.value^(subtract ? 0x80000000u : 0u),flags);
        else {
            const auto product=ee_fpu_multiply(a.value,b.value,flags,diagnostic_power_two_carry_);
            if(!product)throw DeviceRefusal("FMAC multiply has ambiguous low-column Booth rounding");
            if(flags&0xC000u)throw DeviceRefusal("FMAC exceptional product/ACC metadata is not modeled");
            value=*product;
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
        }
        result.output.words[lane]={value,0};
        if(!(value&0x7FFFFFFFu)){result.mac|=bit;result.status|=1u;}
        if(value&0x80000000u){result.mac|=bit<<4u;result.status|=2u;}
        if(flags&0x4000u){result.mac|=bit<<8u;result.status|=4u;}
        if(flags&0x8000u){result.mac|=bit<<12u;result.status|=8u;}
    }
    return result;
}
void VuControl::step_micro(unsigned unit,Gif* gif,GsRegisters* gs) {
    auto& state=micro_[unit];retire_micro_integers(unit);
    // VU manual 3.4.9: the second XGKICK's upper half executes normally.
    // Its following LIW stalls until the preceding PATH1 transfer ends;
    // already issued FMAC/integer/store writebacks keep progressing.
    if(unit && queued_path1_){++state.next;return;}
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
    // Resource/operand interlocks precede evaluation of either half. WAITQ
    // can therefore supply the newly completed Q to an upper MULq in this LIW.
    const auto primary=lo>>25u,extended=lo&0x7FFu;
    if(primary==0x40u && (extended==0x3BCu || extended==0x3BFu) && (unit ? pending_q1_ : pending_q_)) {
        ++state.next;return;
    }
    if(primary==0x40u && extended==0x3BCu) {
        const auto fs=(lo>>11u)&31u,ft=(lo>>16u)&31u,fsf=(lo>>21u)&3u,ftf=(lo>>23u)&3u;
        for(unsigned n=0;n<state.vector_writes;++n) {
            const auto& write=state.vectors[n];
            if((fs && write.index==fs && (write.mask&(8u>>fsf))) || (ft && write.index==ft && (write.mask&(8u>>ftf)))) {
                ++state.next;return;
            }
        }
    }
    const auto fmac=evaluate_micro_upper(unit,hi);
    if(fmac.hazard){++state.next;return;}
    const bool ending=(hi&0x40000000u)!=0u;
    if(state.end_delay && ending)throw DeviceRefusal("E bit in the termination delay slot is not supported");
    const auto op=lo>>25u,is=(lo>>11u)&31u,it=(lo>>16u)&31u,id=(lo>>6u)&31u;
    std::optional<std::pair<unsigned,std::uint32_t>> result;
    std::uint32_t integer_unknown=0;
    bool integer_load=false,flag_check=false;
    std::optional<MicroVectorWrite> vector_result;
    std::optional<MicroVectorWrite> store_result;
    std::optional<std::uint32_t> kick;
    std::optional<PendingQ> q_result;
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
    } else if(op==0x18u || op==0x1Au || op==0x1Bu) { // FMEQ/FMAND/FMOR, 1/1.
        if(it>=16u || is>=16u || (lo&0x01E007FFu))throw DeviceRefusal("MAC flag check register/reserved fields are invalid");
        const auto a=unit ? vu1_controls_[1] : observe(17),b=micro_integer(unit,is,true);
        const auto av=a.value&~a.unknown,bv=b.value&~b.unknown;
        std::uint32_t value=0,unknown=0;
        if(op==0x1Au) {
            value=av&bv;unknown=(a.unknown&(bv|b.unknown))|(b.unknown&av);
        } else if(op==0x1Bu) {
            unknown=(a.unknown&~bv)|(b.unknown&~av);value=(av|bv)&~unknown;
        } else {
            const auto uncertain=(a.unknown|b.unknown)&0xFFFFu;
            if(!((av^bv)&~uncertain&0xFFFFu)) {unknown=uncertain ? 1u : 0u;value=uncertain ? 0u : 1u;}
        }
        result=std::pair{it,value&0xFFFFu};integer_unknown=unknown&0xFFFFu;flag_check=true;
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
    } else if(op==0x28u || op==0x29u || (op>=0x2Cu && op<=0x2Fu)) {
        if(state.branch || state.end_delay)throw DeviceRefusal("branch in a branch/E delay slot is not modeled");
        if((lo&0x01E00000u) || (op>=0x2Cu && it))throw DeviceRefusal("conditional branch reserved fields are nonzero");
        const auto bits=lo&0x7FFu,slots=unit ? 2048u : 512u;
        const auto offset=static_cast<std::int32_t>(bits)-((bits&0x400u) ? 2048 : 0);
        const auto source=reg(is,2);
        const bool negative=(source&0x8000u)!=0u;
        const bool take=op==0x28u ? source==reg(it,2) : op==0x29u ? source!=reg(it,2) :
            op==0x2Cu ? negative : op==0x2Du ? !negative && source!=0u : op==0x2Eu ? negative || source==0u : !negative;
        // Even an untaken conditional branch has one delay instruction.
        branch=((state.pc/8u+(take ? 1u+static_cast<std::uint32_t>(offset) : 2u))&(slots-1u))*8u;
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
    } else if(op==0u || (op==0x40u && (lo&0x7FFu)==0x37Cu)) { // LQ/LQI, 1/4.
        if(state.end_delay)throw DeviceRefusal("VU memory load in the E delay slot is invalid");
        const bool increment=op!=0u;
        const auto mask=(lo>>21u)&15u;
        if(is>=16u)throw DeviceRefusal("LQ/LQI address requires VI00..VI15");
        if(mask || increment) {
            const auto bits=lo&0x7FFu;
            const auto offset=increment ? 0 : static_cast<std::int32_t>(bits)-((bits&0x400u) ? 2048 : 0);
            const auto address=(reg(is)+static_cast<std::uint32_t>(offset))&0xFFFFu;
            MicroVectorWrite load{it,mask,{},state.next+4u};
            if(mask) {
                if(address>=(unit ? 1024u : 256u))
                    throw DeviceRefusal("LQ/LQI address outside direct VU data RAM; mirrors/register windows are not modeled");
                const auto& memory=unit ? data1_ : data_;
                for(unsigned lane=0;lane<4u;++lane)if(mask&(8u>>lane))load.words[lane]=memory[address*4u+lane];
            }
            vector_result=load;
            if(increment)result=std::pair{is,(address+1u)&0xFFFFu};
        }
    } else if(op==1u || (op==0x40u && (lo&0x7FFu)==0x37Du)) { // SQ/SQI, 1/4.
        if(state.end_delay)throw DeviceRefusal("VU memory store in the E delay slot is invalid");
        const bool increment=op!=1u;
        const auto mask=(lo>>21u)&15u;
        if(it>=16u)throw DeviceRefusal("SQ/SQI address requires VI00..VI15");
        if(mask || increment) {
            const auto bits=lo&0x7FFu;
            const auto offset=increment ? 0 : static_cast<std::int32_t>(bits)-((bits&0x400u) ? 2048 : 0);
            const auto address=(reg(it)+static_cast<std::uint32_t>(offset))&0xFFFFu;
            if(mask) {
                if(address>=(unit ? 1024u : 256u))
                    throw DeviceRefusal("SQ/SQI address outside direct VU data RAM; mirrors/register windows are not modeled");
                for(unsigned n=0;n<state.vector_writes;++n)
                    if(is && state.vectors[n].index==is && (state.vectors[n].mask&mask)) {++state.next;return;}
                store_result=MicroVectorWrite{address,mask,unit ? vu1_vectors_[is] : vectors_[is],state.next+4u};
            }
            if(increment)result=std::pair{it,(address+1u)&0xFFFFu};
        }
    } else if(op==0x40u && extended==0x3BCu) {
        const auto fsf=(lo>>21u)&3u,ftf=(lo>>23u)&3u;
        const auto& vectors=unit ? vu1_vectors_ : vectors_;
        const auto a=vectors[is][fsf],b=vectors[it][ftf];
        if(a.unknown || b.unknown)throw DeviceRefusal("DIV Q consumes unknown VF operand bits");
        // Same integer FDIV precision model as the shared VU macro path.
        std::uint32_t flags=0;const auto value=ee_fpu_divide(a.value,b.value,flags);
        flags=((flags>>13u)&0x10u)|((flags>>11u)&0x20u);
        q_result=PendingQ{state.next+7u,value,flags};
    } else if(lo==0x800003BFu) { // WAITQ, no implicit Q dependency in other instructions.
    } else if(op==4u || (op==0x40u && (lo&0x7FFu)==0x3FEu)) {
        if(state.end_delay)throw DeviceRefusal("integer memory load in the E delay slot is invalid");
        const auto mask=(lo>>21u)&15u;
        if(it>=16u || !mask || (mask&(mask-1u)))throw DeviceRefusal("ILW/ILWR requires one selected field and a VI00..VI15 destination");
        const auto bits=lo&0x7FFu;
        const auto offset=op==4u ? static_cast<std::int32_t>(bits)-((bits&0x400u) ? 2048 : 0) : 0;
        const auto address=(reg(is)+static_cast<std::uint32_t>(offset))&0xFFFFu;
        if(address>=(unit ? 1024u : 256u))throw DeviceRefusal("integer load outside direct VU RAM; mirrors/register windows are not modeled");
        const auto lane=mask==8u ? 0u : mask==4u ? 1u : mask==2u ? 2u : 3u;
        const auto word=(unit ? data1_ : data_)[address*4u+lane];
        result=std::pair{it,word.value&0xFFFFu};integer_unknown=word.unknown&0xFFFFu;integer_load=true;
    } else if(op==0x40u && ((lo&0x7FFu)==0x6BCu || (lo&0x7FFu)==0x6BDu)) {
        const auto xtop=(lo&0x7FFu)==0x6BCu;
        if(xtop && !unit)throw DeviceRefusal("XTOP exists only on VU1");
        if(it>=16u || is || (lo&0x01E00000u))throw DeviceRefusal("XTOP/XITOP destination/reserved fields are invalid");
        if(state.end_delay)throw DeviceRefusal("XTOP/XITOP in the E delay slot is invalid");
        const auto word=xtop ? state.top : state.itop;
        result=std::pair{it,word.value};integer_unknown=word.unknown;
    } else if(op==0x40u && (lo&0x7FFu)==0x3FCu) { // MTIR, VU manual p.184, 1/1.
        if(it>=16u || (lo&0x01800000u))throw DeviceRefusal("MTIR destination/reserved fields are invalid");
        const auto lane=(lo>>21u)&3u;
        for(unsigned n=0;n<state.vector_writes;++n)
            if(is && state.vectors[n].index==is && (state.vectors[n].mask&(8u>>lane))) {++state.next;return;}
        const auto word=(unit ? vu1_vectors_ : vectors_)[is][lane];
        result=std::pair{it,word.value&0xFFFFu};integer_unknown=word.unknown&0xFFFFu;
    } else if(op==0x40u && extended==0x3FDu) { // MFIR, 1/4, signed VI raw-bit transfer.
        if(is>=16u)throw DeviceRefusal("MFIR source requires VI00..VI15");
        const auto mask=(lo>>21u)&15u;
        if(mask) {
            const auto word=micro_integer(unit,is,true);
            const auto unknown=(word.unknown&0xFFFFu)|((word.unknown&0x8000u) ? 0xFFFF0000u : 0u);
            const auto value=((word.value&0xFFFFu)|((word.value&0x8000u) ? 0xFFFF0000u : 0u))&~unknown;
            MicroVectorWrite transfer{it,mask,{},state.next+4u};transfer.words.fill({value,unknown});vector_result=transfer;
        }
    } else if(op==0x40u && (extended==0x33Cu || extended==0x33Du)) { // MOVE/MR32, 1/4.
        const auto mask=(lo>>21u)&15u;
        if(mask) {
            const auto rotate=extended==0x33Du;
            for(unsigned lane=0;lane<4u;++lane)if(mask&(8u>>lane))
                for(unsigned n=0;n<state.vector_writes;++n)
                    if(is && state.vectors[n].index==is && (state.vectors[n].mask&(8u>>((lane+(rotate ? 1u : 0u))&3u)))) {++state.next;return;}
            const auto& source=(unit ? vu1_vectors_ : vectors_)[is];
            MicroVectorWrite transfer{it,mask,{},state.next+4u};
            for(unsigned lane=0;lane<4u;++lane)if(mask&(8u>>lane))transfer.words[lane]=source[(lane+(rotate ? 1u : 0u))&3u];
            vector_result=transfer;
        }
    } else if(op==0x40u && (lo&0x7FFu)==0x6FCu) {
        if(!unit)throw DeviceRefusal("XGKICK exists only on VU1");
        if(state.end_delay)throw DeviceRefusal("XGKICK in the E delay slot is invalid");
        if((lo&0x01E00000u) || it)throw DeviceRefusal("XGKICK reserved fields are nonzero");
        if(!gif || !gs)throw DeviceRefusal("XGKICK requires the shared GIF/GS devices");
        kick=reg(is)&1023u;
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
    if(fmac.changes_flags && state.flag_writes==state.flags.size())throw DeviceRefusal("VU flag writeback queue is full");
    if(store_result && state.store_writes==state.stores.size())throw DeviceRefusal("VU memory store queue is full");
    if(kick) {
        // PATH1 transfers 128 bits per BUSCLK (EE manual p.148). As with
        // the other bus engines, delivery opens the next functional bus slot;
        // this phase is not a measured pin-level XGKICK latency.
        const auto request=Path1{*kick,state.next+kEeCyclesPerBusCycle,0,ending,std::nullopt};
        if(path1_)queued_path1_=request;
        else {gif->request_path1();path1_=request;}
    }
    if(q_result)(unit ? pending_q1_ : pending_q_)=*q_result;
    if(result && result->first) {
        state.pending[state.writes++]={result->first,{result->second,integer_unknown},state.next+4u,integer_load,flag_check};
    }
    if(lower_write)state.vectors[state.vector_writes++]=*vector_result;
    if(upper_write)state.vectors[state.vector_writes++]=fmac.output;
    if(fmac.changes_flags)state.flags[state.flag_writes++]={fmac.mac,fmac.status,state.next+4u};
    if(store_result)state.stores[state.store_writes++]=*store_result;
    const auto next=state.branch.value_or((state.pc+8u)&(unit ? 16383u : 4095u));
    state.branch=branch;state.pc=next;++state.instructions;
    if(state.end_delay)state.drain=state.next+4u; // the delay pair must reach S before Ready
    state.end_delay=ending;
    ++state.next;
}
void VuControl::step_path1(Gif* gif,GsRegisters* gs) {
    auto& transfer=*path1_;
    if(transfer.failure)throw DeviceRefusal(*transfer.failure);
    try {
        if(!gif || !gs)throw DeviceRefusal("active PATH1 requires the shared GIF/GS devices");
        if(!gif->path1_requested())throw DeviceRefusal("GIF reset interrupted the active VU1 PATH1 transfer");
        if(!gif->path1_ready()){transfer.next+=kEeCyclesPerBusCycle;return;}
        std::array<std::uint32_t,4> words{};
        for(unsigned lane=0;lane<4u;++lane) {
            const auto word=data1_[transfer.address*4u+lane];
            if(word.unknown)throw DeviceRefusal("PATH1 consumes unknown VU1 packet bits");
            words[lane]=word.value;
        }
        gif->write_path1(words[0]|(std::uint64_t{words[1]}<<32u),words[2]|(std::uint64_t{words[3]}<<32u),*gs);
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
    vu1_controls_[6]={pending_q1_->value,0};
    const auto flags=pending_q1_->flags;auto& status=vu1_controls_[0];
    status.value=(status.value&~0x30u)|flags|(flags<<6u);
    status.unknown&=~(0x30u|(flags<<6u));pending_q1_.reset();
}
void VuControl::advance_micro(std::uint64_t cycle,Gif* gif,GsRegisters* gs) {
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
        try {step_micro(unit,gif,gs);}
        catch(const MicroPipelineStall&) {++state.next;}
        catch(const DeviceRefusal& error) {
            state.failure="VU"+std::to_string(unit)+" micro PC "+hexword(state.pc,4)+" after "+std::to_string(state.instructions)+" instructions: "+error.what();
            throw DeviceRefusal(*state.failure);
        }
    }
}
} // namespace ps2native
