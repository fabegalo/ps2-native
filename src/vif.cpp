#include "ps2native/vif.hpp"
#include "ps2native/vu_control.hpp"
#include <algorithm>
#include <string>

namespace ps2native {
std::uint32_t Vif::read_register(std::uint32_t offset) const {
    // EE User's Manual v6, pp.136 and 143-146. With synchronous FIFO
    // acceptance, DIRECT can wait for data but no qwords remain queued.
    if(offset==0) {
        const auto status=observe_status();
        if(status.unknown)refuse(" STAT consumes unknown double-buffer phase; OFFSET has not established it");
        return status.value;
    }
    if(offset&15u) refuse(" registers require aligned 32-bit accesses");
    if(unit_==Unit::Vu0 && (offset==0xA0u || offset==0xB0u || offset==0xC0u || offset==0xE0u))
        refuse(" register is reserved (double buffering exists only on VIF1)");
    if(offset==0x10u) refuse("_FBRST is write-only");
    if(offset==0x60u) return (mpg_left_ ? mpg_left_ : unpack_left_ ? unpack_left_ : direct_left_)&0xFFu; // VIFn_NUM, EE manual pp.137-138.
    const auto* value=offset==0x20u ? &error_mask_ : offset==0x30u ? &mark_ : offset==0x80u ? &code_ :
        (offset==0x40u || offset==0x50u || offset==0x70u || (offset>=0x90u && offset<=0xE0u) ||
         (offset>=0x100u && offset<=0x170u)) ? &programmed_.at(offset/16u) : nullptr;
    if(!value) refuse(" register is not modeled");
    if(!*value) refuse(" register read before write/command; reset value is not verified");
    return **value;
}
void Vif::write_register(std::uint32_t offset,std::uint32_t value,Gif& gif) {
    if(offset==0x10u) {
        if(value&~15u) refuse("_FBRST reserved bits set");
        if(value&6u) refuse(" force break/stop needs FIFO stall/backpressure");
        if(value&1u) {
            reset();
            if(unit_==Unit::Vu1) gif.set_vif_path3_mask(false);
        } // Clear only this VIF, preserving GIF packets and GS.
        // STC clears stall/IRQ/error flags. None can be raised by the currently
        // accepted command subset; interrupt-bearing commands remain refused.
        return;
    }
    if(offset==0) {
        if(unit_==Unit::Vu0) refuse("_STAT is read-only");
        if(value) refuse("_STAT reverse FIFO/read-only write is not modeled");
        return; // Explicit FDR=0; no guest-to-EE readback engine is claimed.
    }
    if(offset==0x20u) {
        if(value&~7u) refuse("_ERR reserved bits set");
        error_mask_=value;return;
    }
    if(offset==0x30u) {mark_=value&0xFFFFu;mark_detected_=false;return;}
    refuse(" register write is not modeled");
}
void Vif::start_unpack(std::uint32_t command,VuControl* vu) {
    if(!vu)refuse(" UNPACK requires the shared VU data memory");
    if(command&0x03000000u)refuse(" UNPACK packed 8/16/5-bit formats are not modeled");
    if(!programmed_[4] || !programmed_[5])refuse(" UNPACK requires known CYCLE and MODE settings");
    const auto cycle=*programmed_[4],cl=cycle&255u,wl=(cycle>>8u)&255u;
    if(!cl || !wl || cl<wl)refuse(" UNPACK filling/zero write cycles are not modeled");
    if((command&0x10000000u) && !programmed_[7])refuse(" masked UNPACK requires a known MASK register");
    unsigned address=command&0x3FFu;
    if(unit_==Unit::Vu1 && (command&0x8000u)) {
        if(!programmed_[12])refuse(" UNPACK relative address requires known TOPS");
        address+=*programmed_[12];
    }
    unpack_components_=((command>>26u)&3u)+1u;
    unpack_left_=(command>>16u)&255u;if(!unpack_left_)unpack_left_=256u;
    unpack_address_=address;unpack_cl_=cl;unpack_wl_=wl;unpack_mode_=*programmed_[5];
    unpack_mask_=command&0x10000000u ? *programmed_[7] : 0u;
    unpack_index_=unpack_cycle_=0;
}
void Vif::unpack_word(std::uint32_t word,VuControl& vu) {
    unpack_data_[unpack_index_++]=word;
    if(unpack_index_!=unpack_components_)return;
    const auto base=unit_==Unit::Vu0 ? 0x11004000u : 0x1100C000u;
    const auto address=(unpack_address_&(unit_==Unit::Vu0 ? 255u : 1023u))*16u;
    const auto row=std::min(unpack_cycle_,3u);
    for(unsigned lane=0;lane<4u;++lane) {
        const auto select=(unpack_mask_>>(row*8u+lane*2u))&3u;
        if(select==3u)continue; // write protect retains the actual old bits
        ObservedWord data{0,~0u};
        if(select==1u || select==2u) {
            const auto& reg=programmed_[(select==1u ? 16u+lane : 20u+row)];
            if(reg)data={*reg,0};
        } else {
            // EE manual 6.3.4: S is broadcast. V2 Z/W and V3 W are
            // indeterminate, not duplicated input or an invented zero.
            if(unpack_components_==1u)data={unpack_data_[0],0};
            else if(lane<unpack_components_)data={unpack_data_[lane],0};
            if(unpack_mode_) {
                auto& reg=programmed_[16u+lane];
                data=reg && !data.unknown ? ObservedWord{data.value+*reg,0} : ObservedWord{0,~0u};
                if(unpack_mode_==2u)reg=data.unknown ? std::nullopt : std::optional<std::uint32_t>{data.value};
            }
        }
        vu.write_memory(base+address+lane*4u,4,{data.value,data.unknown});
    }
    unpack_index_=0;--unpack_left_;++unpack_address_;
    if(++unpack_cycle_==unpack_wl_){unpack_address_+=unpack_cl_-unpack_wl_;unpack_cycle_=0;}
}
void Vif::write_fifo(std::uint64_t low, std::uint64_t high, Gif& gif, GsRegisters& gs,VuControl* vu,std::uint64_t cycle) {
    const std::array<std::uint32_t,4> words{static_cast<std::uint32_t>(low), static_cast<std::uint32_t>(low >> 32),
        static_cast<std::uint32_t>(high), static_cast<std::uint32_t>(high >> 32)};
    write_words(words,0,gif,gs,vu,cycle);
}
void Vif::write_dma_tag(std::uint64_t data,Gif& gif,GsRegisters& gs,VuControl* vu,std::uint64_t cycle) {
    const std::array<std::uint32_t,2> words{static_cast<std::uint32_t>(data),static_cast<std::uint32_t>(data>>32)};
    write_words(words,2,gif,gs,vu,cycle);
}
void advance_vpus(std::uint64_t cycle,Vif* vif0,Vif* vif1,VuControl* vu,Gif& gif,GsRegisters& gs) {
    const auto drain=[&](std::uint64_t at) {
        if(vif0)vif0->drain(at,vu,gif,gs);
        if(vif1)vif1->drain(at,vu,gif,gs);
    };
    if(vu) {
        while((vif0 && vif0->waiting_) || (vif1 && vif1->waiting_)) {
            const auto next=vu->next_micro_cycle();
            if(!next || *next>cycle)break;
            vu->advance_micro(*next);drain(*next);
        }
        vu->advance_micro(cycle);
    }
    drain(cycle);
}
void Vif::write_words(std::span<const std::uint32_t> words,unsigned first_lane,Gif& gif,GsRegisters& gs,VuControl* vu,std::uint64_t cycle) {
    advance_vpus(cycle,this,nullptr,vu,gif,gs);
    if(!can_accept_dma())refuse(" FIFO full: CPU store backpressure is not modeled");
    Input incoming;incoming.first=first_lane;incoming.count=static_cast<unsigned>(words.size());
    std::copy(words.begin(),words.end(),incoming.words.begin());fifo_.push_back(incoming);
    drain(cycle,vu,gif,gs);
}
void Vif::drain(std::uint64_t cycle,VuControl* vu,Gif& gif,GsRegisters& gs) {
    waiting_=false;
    for(;;) {
        if(input_index_==input_count_) {
            if(fifo_.empty())return;
            input_=fifo_.front();fifo_.pop_front();input_index_=0;input_count_=input_.count;
        }
        const auto lane=input_.first+input_index_,command=input_.words[input_index_];
        if(padding_) {if(++input_index_==input_count_)padding_=false;continue;}
        const bool payload=direct_left_ || mpg_left_ || unpack_left_ || pending_!=Pending::None;
        const auto op=command>>24u;
        if(!payload && !(command&0x80000000u) && vu && vu->micro_running(unit_==Unit::Vu1) &&
            (op==0x10u || (op==0x11u && unit_==Unit::Vu1) || op==0x14u || op==0x17u || op==0x4Au)) {
            code_=command;waiting_=true;return;
        }
        ++input_index_;
        if(direct_left_) {
            direct_buffer_[direct_words_]=command;
            if(direct_words_==3u) {
                gif.write_path2(std::uint64_t{direct_buffer_[0]}|(std::uint64_t{direct_buffer_[1]}<<32),
                                std::uint64_t{direct_buffer_[2]}|(std::uint64_t{direct_buffer_[3]}<<32),gs);
                direct_words_=0;--direct_left_;
            } else ++direct_words_;
            continue;
        }
        if(mpg_left_) {
            if(!vu)refuse(" MPG requires the shared VU micro memory");
            if(!mpg_low_)mpg_low_=command;
            else {
                const auto bytes=unit_==Unit::Vu0 ? 4096u : 16384u;
                const auto base=unit_==Unit::Vu0 ? 0x11000000u : 0x11008000u;
                vu->write_memory(base+mpg_address_,8,{std::uint64_t{*mpg_low_}|(std::uint64_t{command}<<32),0});
                mpg_address_=(mpg_address_+8u)&(bytes-1u);mpg_low_.reset();--mpg_left_;
            }
            continue;
        }
        if(unpack_left_) {
            if(!vu)refuse(" UNPACK requires the shared VU data memory");
            unpack_word(command,*vu);continue;
        }
        if(pending_!=Pending::None) {
            const auto base=pending_==Pending::Mask ? 7u : pending_==Pending::Row ? 16u : 20u;
            programmed_[base+pending_index_]=command;
            if(++pending_index_==(pending_==Pending::Mask ? 1u : 4u)) {pending_=Pending::None;pending_index_=0;}
            continue;
        }
        const auto opcode=command>>24u,immediate=command&0xFFFFu;
        if(command&0x80000000u) refuse(" interrupt-bearing command is not modeled");
        if((opcode&0x60u)==0x60u){start_unpack(command,vu);code_=command;continue;}
        if(unit_==Unit::Vu0 && (opcode==2u || opcode==3u || opcode==6u || opcode==0x11u || opcode==0x50u || opcode==0x51u))
            refuse(" received a VIF1-only command: "+std::to_string(opcode));
        switch(opcode) {
            case 0: break; // NOP ignores NUM and IMMEDIATE.
            case 1: programmed_[4]=immediate;break;
            case 2: programmed_[11]=immediate&0x3FFu;programmed_[12]=programmed_[10];dbf_=false;break;
            case 3: programmed_[10]=immediate&0x3FFu;break;
            case 4: programmed_[9]=immediate&0x3FFu;break; // ITOPS, copied to ITOP only by a real start.
            case 5:
                if((immediate&3u)==3u) refuse(" STMOD mode 3 is undefined");
                programmed_[5]=immediate&3u;break;
            case 6: gif.set_vif_path3_mask((immediate&0x8000u)!=0u);break;
            case 7: mark_=immediate;mark_detected_=true;break;
            case 0x10: // The FIFO decoder waited for the actual Ready event.
                break;
            case 0x11:
                // EE manual: FLUSH waits for VU1 and PATH1/2, not PATH3.
                if(((gif.read_stat()>>10u)&3u)==2u)
                    refuse(" FLUSH waits for an incomplete GIF PATH2 transfer; stall is not modeled");
                break;
            case 0x14:
            case 0x17: {
                if(!vu)refuse(" MSCAL requires the shared VU micro engine");
                const auto unit=unit_==Unit::Vu1 ? 1u : 0u;
                if(opcode==0x17u)vu->resume_micro(unit,cycle);
                else vu->start_micro(unit,immediate,cycle);
                programmed_[13]=programmed_[9]; // Unknown ITOPS remains unknown.
                if(unit) {
                    programmed_[14]=programmed_[12];
                    if(dbf_)dbf_=!*dbf_;
                    programmed_[12]=dbf_ && programmed_[10] && (!*dbf_ || programmed_[11]) ?
                        std::optional<std::uint32_t>{(*programmed_[10]+(*dbf_ ? *programmed_[11] : 0u))&0x3FFu} : std::nullopt;
                }
                break;
            }
            case 0x20: pending_=Pending::Mask;break;
            case 0x30: pending_=Pending::Row;break;
            case 0x31: pending_=Pending::Col;break;
            case 0x4A: {
                // EE manual 6.3.3/MPG: the following instructions are
                // aligned 64-bit data, not VIF commands.
                if(!vu)refuse(" MPG requires the shared VU micro memory");
                if((lane+1u)&1u)refuse(" MPG payload requires 64-bit alignment");
                const auto bytes=unit_==Unit::Vu0 ? 4096u : 16384u;
                if(immediate>=bytes/8u)refuse(" MPG load address exceeds this VU micro memory");
                mpg_address_=immediate*8u;mpg_left_=(command>>16u)&255u;
                if(!mpg_left_)mpg_left_=256u;
                mpg_low_.reset();break;
            }
            case 0x50:
            case 0x51:
                // DIRECTHL cannot interrupt a PATH3 IMAGE transfer. This
                // synchronous parser accepts the idle path only; concurrent
                // graphics DMA is also refused by the shared DMA engine.
                if(opcode==0x51u && ((gif.read_stat()>>10u)&3u)==3u)
                    refuse(" DIRECTHL waits for GIF PATH3; stall is not modeled");
                direct_left_=immediate ? immediate : 65536u;
                code_=command;
                padding_=input_index_!=input_count_; // discard only this physical qword's remaining words
                break;
            default: {
                constexpr char hex[]="0123456789ABCDEF";
                auto reason=std::string(" command unsupported: 0x");
                reason+=hex[(opcode>>4u)&15u];reason+=hex[opcode&15u];
                refuse(reason+" (immediate="+std::to_string(immediate)+")");
            }
        }
        code_=command;
    }
}
} // namespace ps2native
