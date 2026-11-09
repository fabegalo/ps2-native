#pragma once
// Interfaces for the pinned SDK kernel; execution and memory stay canonical.
#include "ps2native/ee.hpp"
#include "ps2native/memory.hpp"
#include <cstdint>
#include <stdexcept>

namespace ps2native {
Memory& sdk_kernel_memory();
void sdk_kernel_return(std::int64_t value);
std::shared_ptr<void>& sdk_kernel_service_slot(unsigned index);
inline std::uint32_t sdk_gpr32(const EEState* context, unsigned index) {
    if(index && (context->unknown_gpr_bits[index] & 0xFFFFFFFFu))
        throw std::runtime_error("PS2Recomp HLE consumed an unknown EE register");
    return context->reg32(index);
}
inline std::uint64_t sdk_gpr64(const EEState* context, unsigned index) {
    if(index && context->unknown_gpr_bits[index])
        throw std::runtime_error("PS2Recomp HLE consumed an unknown EE register");
    return context->reg64(index);
}
class SdkMemory {
public:
    class GsRegister {
    public:
        GsRegister(Memory& memory,std::uint32_t address):memory_(memory),address_(address) {}
        operator std::uint64_t() const {
            const auto value=observe();
            if(value.unknown)throw std::runtime_error("SDK GS interface consumed undefined internal state");
            return value.value;
        }
        GsRegister& operator=(std::uint64_t value){memory_.write64(address_,value);return *this;}
        std::uint64_t operator&(std::uint64_t mask) const {
            const auto value=observe();
            if(value.unknown&mask)throw std::runtime_error("SDK GS interface consumed unknown register bits");
            return value.value&mask;
        }
        GsRegister& operator|=(std::uint64_t bits) {
            auto value=observe();value.value|=bits;value.unknown&=~bits;
            memory_.store64_observed(address_,value);return *this;
        }
    private:
        ObservedDoubleword observe() const {
            const auto value=memory_.gs().latched(address_);
            return value?ObservedDoubleword{*value,0u}:ObservedDoubleword{0u,~0ull};
        }
        Memory& memory_;std::uint32_t address_;
    };
    struct GsView {
        GsRegister pmode,smode2,imr;
        explicit GsView(Memory& memory):pmode(memory,kGsPmode),smode2(memory,kGsSmode2),imr(memory,kGsImr) {}
    };
    explicit SdkMemory(Memory& memory):memory_(memory),gs_(memory) {}
    GsView& gs(){return gs_;}
    void validateCrtMode(int interlaced,int mode,int frame) const {
        if(interlaced!=1 || mode!=2 || (frame!=0 && frame!=1))
            throw std::runtime_error("PS2Recomp native CRT configuration is unsupported");
    }
    bool validateGuestRange(std::uint32_t address,std::size_t size,bool output) const;
    int setSifDma(std::uint32_t address,std::uint32_t count);
    int sifDmaStat(std::uint32_t id);
    void setSifDChain();
    std::uint32_t sifReg(std::uint32_t reg,std::optional<std::uint32_t> value);
    int setIrqCauseEnabled(bool dmac,std::uint32_t cause,bool enabled);
    bool irqCauseEnabled(bool dmac,std::uint32_t cause) const {
        if(cause>=(dmac?10u:15u))return false;
        const auto mask=1u<<(cause+(dmac?16u:0u));
        return (memory_.read32(dmac?0xB000E010u:0xB000F010u)&mask)!=0u;
    }
    void initializeKernelDevices();
    bool hasCanonicalEvent() const {return memory_.iop().hle_events_pending() || memory_.idle_execution_horizon().has_value();}
    void write64(std::uint32_t address,std::uint64_t value){memory_.write64(address,value);syncGuestWrite(address,8u);}
    void write8(std::uint32_t address,std::uint8_t value){memory_.write8(address,value);syncGuestWrite(address,1u);}
    void write32(std::uint32_t address,std::uint32_t value) {memory_.write32(address,value);syncGuestWrite(address,4u);}
private:
    void syncGuestWrite(std::uint32_t address,unsigned bytes);
    Memory& memory_;
    GsView gs_;
};
}
#define SET_GPR_U32(ctx, reg, value) ((ctx)->set_reg32_sign_extended((reg),(value)))
#define SET_GPR_U64(ctx, reg, value) ((ctx)->set_reg64((reg),(value)))
#define GPR_U32(ctx, reg) ps2native::sdk_gpr32((ctx),(reg))
#define GPR_U64(ctx, reg) ps2native::sdk_gpr64((ctx),(reg))
inline std::uint32_t getRegU32(const ps2native::EEState* context,int index) {return GPR_U32(context,index);}
inline std::uint64_t getRegU64(const ps2native::EEState* context,int index) {return GPR_U64(context,index);}
inline void setReturnS32(ps2native::EEState* context,std::int32_t value) {ps2native::sdk_kernel_return(value);SET_GPR_U32(context,2,static_cast<std::uint32_t>(value));}
inline void setReturnU32(ps2native::EEState* context,std::uint32_t value) {ps2native::sdk_kernel_return(value);SET_GPR_U32(context,2,value);}
inline void setReturnU64(ps2native::EEState* context,std::uint64_t value) {ps2native::sdk_kernel_return(static_cast<std::int64_t>(value));SET_GPR_U64(context,2,value);}
inline constexpr std::uint32_t PS2_RAM_SIZE=32u*1024u*1024u;
