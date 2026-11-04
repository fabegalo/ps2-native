#include "ps2native/vif.hpp"
#include <string>

namespace ps2native {
std::uint32_t Vif1::read_register(std::uint32_t offset) const {
    // EE User's Manual v6, pp.136 and 143-146. With synchronous FIFO
    // acceptance, DIRECT can wait for data but no qwords remain queued.
    if(offset==0) return ((direct_left_ || pending_!=Pending::None) ? 1u : 0u)|(mark_detected_ ? 0x40u : 0u);
    if(offset==0x10u) throw DeviceRefusal("VIF1_FBRST is write-only");
    if(offset==0x60u) return direct_left_&0xFFu; // VIF1_NUM, EE manual p.138.
    const auto* value=offset==0x20u ? &error_mask_ : offset==0x30u ? &mark_ : offset==0x80u ? &code_ :
        (offset==0x40u || offset==0x50u || offset==0x70u || (offset>=0x90u && offset<=0xE0u) ||
         (offset>=0x100u && offset<=0x170u)) ? &programmed_.at(offset/16u) : nullptr;
    if(!value) throw DeviceRefusal("VIF1 register is not modeled");
    if(!*value) throw DeviceRefusal("VIF1 register read before write/command; reset value is not verified");
    return **value;
}
void Vif1::write_register(std::uint32_t offset,std::uint32_t value,Gif& gif) {
    if(offset==0x10u) {
        if(value&~15u) throw DeviceRefusal("VIF1_FBRST reserved bits set");
        if(value&6u) throw DeviceRefusal("VIF1 force break/stop needs FIFO stall/backpressure");
        if(value&1u) {reset();gif.set_vif_path3_mask(false);} // Clear VIF state, preserve GIF packets and GS.
        // STC clears stall/IRQ/error flags. None can be raised by the currently
        // accepted command subset; interrupt-bearing commands remain refused.
        return;
    }
    if(offset==0) {
        if(value) throw DeviceRefusal("VIF1_STAT reverse FIFO/read-only write is not modeled");
        return; // Explicit FDR=0; no guest-to-EE readback engine is claimed.
    }
    if(offset==0x20u) {
        if(value&~7u) throw DeviceRefusal("VIF1_ERR reserved bits set");
        error_mask_=value;return;
    }
    if(offset==0x30u) {mark_=value&0xFFFFu;mark_detected_=false;return;}
    throw DeviceRefusal("VIF1 register write is not modeled");
}
void Vif1::write_fifo(std::uint64_t low, std::uint64_t high, Gif& gif, GsRegisters& gs) {
    if (direct_left_) {
        gif.write_path2(low, high, gs);
        --direct_left_;
        return;
    }
    const std::array<std::uint32_t,4> words{static_cast<std::uint32_t>(low), static_cast<std::uint32_t>(low >> 32),
        static_cast<std::uint32_t>(high), static_cast<std::uint32_t>(high >> 32)};
    for (unsigned i = 0; i < 4u; ++i) {
        const auto command = words[i];
        if(pending_!=Pending::None) {
            const auto base=pending_==Pending::Mask ? 7u : pending_==Pending::Row ? 16u : 20u;
            programmed_[base+pending_index_]=command;
            if(++pending_index_==(pending_==Pending::Mask ? 1u : 4u)) {pending_=Pending::None;pending_index_=0;}
            continue;
        }
        const auto opcode=command>>24u,immediate=command&0xFFFFu;
        if(command&0x80000000u) throw DeviceRefusal("VIF1 interrupt-bearing command is not modeled");
        switch(opcode) {
            case 0: break; // NOP ignores NUM and IMMEDIATE.
            case 1: programmed_[4]=immediate;break;
            case 2: programmed_[11]=immediate&0x3FFu;programmed_[12]=programmed_[10];break;
            case 3: programmed_[10]=immediate&0x3FFu;break;
            case 4: programmed_[9]=immediate&0x3FFu;break; // ITOPS, copied to ITOP only by a real start.
            case 5:
                if((immediate&3u)==3u) throw DeviceRefusal("VIF1 STMOD mode 3 is undefined");
                programmed_[5]=immediate&3u;break;
            case 6: gif.set_vif_path3_mask((immediate&0x8000u)!=0u);break;
            case 7: mark_=immediate;mark_detected_=true;break;
            case 0x20: pending_=Pending::Mask;break;
            case 0x30: pending_=Pending::Row;break;
            case 0x31: pending_=Pending::Col;break;
            case 0x50:
                direct_left_=immediate ? immediate : 65536u;
                code_=command;
                return; // Remaining bytes before qword alignment are padding.
            default: throw DeviceRefusal("VIF1 command unsupported: " + std::to_string(opcode));
        }
        code_=command;
    }
}
} // namespace ps2native
