// Original BSD-3-Clause synthesis in the existing IOP/SPU2 device.
// Register/ADPCM/envelope facts: psx-spx SPU documentation and ps2sdk.
// PCSX2 SPU2 is a behavioral reference only; no GPL implementation is used.
#include "ps2native/iop.hpp"
#include <algorithm>
#include <sstream>

namespace ps2native {
namespace {
constexpr std::int16_t gaussian[512]={
#include "spu_gaussian.inc"
};
std::int16_t saturate(std::int32_t x) { return static_cast<std::int16_t>(std::clamp(x,-32768,32767)); }
std::int32_t gain(std::int32_t x,std::int32_t volume) { return (x*volume)>>15; }
std::int32_t direct_volume(std::uint16_t reg) {
    if(reg&0x8000u)throw DeviceRefusal("SPU2 volume sweep is not modeled");
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(reg<<1u));
}
void envelope(Iop::Spu2VoiceState& v,std::uint16_t a,std::uint16_t b) {
    if(!v.phase)return;
    unsigned shift{},step_bits{};bool decrease=false,exponential=false;
    switch(v.phase) {
    case 1:shift=(a>>10u)&31u;step_bits=(a>>8u)&3u;exponential=(a&0x8000u)!=0;break;
    case 2:shift=(a>>4u)&15u;decrease=exponential=true;break;
    case 3:shift=(b>>8u)&31u;step_bits=(b>>6u)&3u;decrease=(b&0x4000u)!=0;exponential=(b&0x8000u)!=0;break;
    case 4:shift=b&31u;decrease=true;exponential=(b&0x20u)!=0;break;
    default:throw DeviceRefusal("SPU2 invalid envelope phase");
    }
    std::int32_t step=decrease ? -8+static_cast<int>(step_bits) : 7-static_cast<int>(step_bits);
    step*=1<<std::max(0,11-static_cast<int>(shift));
    auto increment=0x8000u>>std::max(0,static_cast<int>(shift)-11);
    if(exponential) {
        if(decrease)step=(step*v.envelope)>>15;
        else if(v.envelope>0x6000) {
            if(shift<10)step>>=2;
            else if(shift==10){step>>=1;increment>>=1;}
            else increment>>=2;
        }
    }
    const bool infinite=(v.phase==4 ? shift==31 : v.phase==3 || v.phase==1 ? shift==31 && step_bits==3 : false);
    if(!infinite)increment=std::max(1u,increment);
    v.envelope_counter+=increment;
    if(v.envelope_counter<0x8000u)return;
    v.envelope_counter=0;
    v.envelope=std::clamp(v.envelope+step,0,32767);
    if(v.phase==1 && v.envelope==32767){v.phase=2;v.envelope_counter=0;}
    else if(v.phase==2 && v.envelope<=std::min(32767,((a&15)+1)*2048)){v.phase=3;v.envelope_counter=0;}
    else if(v.phase==4 && !v.envelope)v.phase=0;
}
}

std::uint16_t Iop::spu2_known_register(std::uint32_t address) const {
    const auto x=latch(address);
    if(!x || !*x){std::ostringstream message;message<<"SPU2 synthesis requires known register 0x"<<std::hex<<address;throw DeviceRefusal(message.str());}
    return static_cast<std::uint16_t>(**x);
}
bool Iop::spu2_voice_configured(unsigned core,unsigned voice) const {
    const auto base=0x1F900000u+core*0x400u;
    for(unsigned reg=0;reg<=8;reg+=2){const auto x=latch(base+voice*16u+reg);if(!x || !*x)return false;}
    for(const auto reg:{0x1C0u,0x1C2u}){const auto x=latch(base+voice*12u+reg);if(!x || !*x)return false;}
    return true;
}
void Iop::start_spu2_voice(unsigned core,unsigned voice) {
    const auto base=0x1F900000u+core*0x400u;
    const auto address=(std::uint32_t{spu2_known_register(base+0x1C0u+voice*12u)}<<16u)|spu2_known_register(base+0x1C2u+voice*12u);
    auto& v=spu2_voices_[core][voice];const auto keys=v.key_ons+1;const auto previous_loop=v.loop;
    v={};v.modeled=true;v.phase=1;v.next=address|1u;v.key_ons=keys;v.loop=previous_loop;
    const auto hi=latch(base+0x1C4u+voice*12u),lo=latch(base+0x1C6u+voice*12u);
    if(hi && *hi && lo && *lo)v.loop=((**hi&15u)<<16u)|(**lo&0xFFF8u);
    spu2_symbolic_voices_[core]&=~(1u<<voice);
    spu2_release_pending_[core]&=~(1u<<voice);
    spu2_endx_[core].value&=~(1u<<voice);spu2_endx_[core].unknown&=~(1u<<voice);
}
void Iop::spu2_ram_irq(std::uint32_t address) {
    for(unsigned core=0;core<2;++core) {
        const auto attr=spu2_control_[core];
        if(!attr || (attr->unknown&0x40u))throw DeviceRefusal("SPU2 RAM access with unknown IRQ comparator enable");
        if(!(attr->value&0x40u))continue;
        const auto base=0x1F90019Cu+core*0x400u;
        const auto irq=(std::uint32_t{spu2_known_register(base)&15u}<<16u)|spu2_known_register(base+2u);
        if(irq==address && !spu2_irq_pending_[core]){spu2_irq_pending_[core]=true;istat_|=1u<<9u;}
    }
}
Iop::Spu2Sample Iop::synthesize_spu2(unsigned core,std::uint64_t sample_cycle) {
    const auto base=0x1F900000u+core*0x400u;
    const auto read_ram=[&](std::uint32_t address) {
        const auto data=spu2_ram_halfword(address);
        if(!data){std::ostringstream message;message<<"SPU2 ADPCM reads unknown sound RAM at 0x"<<std::hex<<address;throw DeviceRefusal(message.str());}
        spu2_ram_irq(address);return *data;
    };
    const auto capture=[&](std::uint32_t address,std::int16_t value) {
        spu2_ram_irq(address);
        if(spu2_ram_.empty()){spu2_ram_.resize(0x100000u);spu2_ram_written_.resize(0x100000u);}
        spu2_ram_[address]=static_cast<std::uint16_t>(value);spu2_ram_written_[address]=1;
    };
    const auto on=spu2_keys_[core][0].value_or(0)|(std::uint32_t{spu2_keys_[core][1].value_or(0)}<<16u);
    const auto off=spu2_keys_[core][2].value_or(0)|(std::uint32_t{spu2_keys_[core][3].value_or(0)}<<16u);
    std::int32_t left=0,right=0,wet_left=0,wet_right=0,previous_output=0;
    bool any=false;
    const auto route=[&](unsigned reg,unsigned voice){return (spu2_known_register(base+reg+(voice>=16?2u:0u))>>(voice&15u))&1u;};
    for(unsigned voice=0;voice<24;++voice) {
        auto& v=spu2_voices_[core][voice];
        if((on&(1u<<voice)) && spu2_voice_configured(core,voice))start_spu2_voice(core,voice);
        if(!v.modeled){previous_output=0;continue;}
        any=true;
        if((off&~on&(1u<<voice)) && v.phase && v.phase!=4){v.phase=4;v.envelope_counter=0;}
        const auto param=base+voice*16u;
        const auto pitch=std::min(0x3FFFu,std::uint32_t{spu2_known_register(param+4u)});
        if(route(0x184u,voice))throw DeviceRefusal("SPU2 noise source synthesis is not modeled");
        // One compressed halfword supplies four samples per 48 kHz edge.
        // The ring preserves the hardware decoder's bounded lookahead.
        const auto header=read_ram(v.next&0xFFFF8u);
        if((header&0x400u) && !v.manual_loop)v.loop=v.next&0xFFFF8u;
        if(v.write-v.read<=12) {
            const auto packed=read_ram(v.next);
            const auto filter=(header>>4u)&15u,shift=header&15u;
            if(filter>4 || shift>12)throw DeviceRefusal("SPU2 ADPCM reserved predictor/shift is not established");
            constexpr int factors[5][2]={{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
            for(unsigned nibble=0;nibble<4;++nibble) {
                const int code=(packed>>(nibble*4u))&15u;
                const auto residual=((code<8?code:code-16)*4096)>>shift;
                const auto sample=saturate(residual+((v.previous*factors[filter][0]+v.older*factors[filter][1]+32)>>6));
                v.older=v.previous;v.previous=sample;v.decoded[v.write++&31u]=sample;++v.decoded_samples;
            }
            v.next=(v.next+1u)&0xFFFFFu;
            if(!(v.next&7u)) {
                if(header&0x100u) {
                    spu2_endx_[core].value|=1u<<voice;spu2_endx_[core].unknown&=~(1u<<voice);
                    if(!v.loop)throw DeviceRefusal("SPU2 ADPCM end reached before a known loop address");
                    v.next=*v.loop;
                    if(!(header&0x200u)){v.phase=0;v.envelope=0;}
                }
                v.next=(v.next+1u)&0xFFFFFu;
            }
        }
        const auto phase=(v.pitch_counter&0xFF0u)>>4u;
        const unsigned indices[4]={255u-phase,511u-phase,256u+phase,phase};
        std::int32_t interpolated=0;
        for(unsigned tap=0;tap<4;++tap)interpolated+=(v.decoded[(v.read+tap)&31u]*gaussian[indices[tap]])>>15;
        envelope(v,spu2_known_register(param+6u),spu2_known_register(param+8u));
        v.output=v.phase ? gain(interpolated,v.envelope) : 0;
        if(v.output)++v.nonzero_samples;
        auto step=pitch;
        if(voice && route(0x180u,voice))step=std::min(0x3FFFu,static_cast<std::uint32_t>((pitch*(32768+previous_output))>>15));
        previous_output=v.output;
        v.pitch_counter+=step;v.read+=v.pitch_counter>>12u;v.pitch_counter&=0xFFFu;
        const auto l=gain(v.output,direct_volume(spu2_known_register(param)));
        const auto r=gain(v.output,direct_volume(spu2_known_register(param+2u)));
        if(route(0x188u,voice))left+=l;
        if(route(0x190u,voice))right+=r;
        if(route(0x18Cu,voice))wet_left+=l;
        if(route(0x194u,voice))wet_right+=r;
        if(voice==1 || voice==3)capture(0x400u+core*0x800u+(voice==3?0x200u:0u)+static_cast<unsigned>((sample_cycle/768u-1u)&511u),static_cast<std::int16_t>(v.output));
    }
    const bool external=core && spu2_last_sample_[0] && spu2_last_sample_[0]->cycle==sample_cycle &&
        (spu2_last_sample_[0]->left || spu2_last_sample_[0]->right);
    if(!any && !external)return {sample_cycle,0,0}; // established inactive/symbolically zero sources
    const auto pos=static_cast<unsigned>((sample_cycle/768u-1u)&511u);
    capture(0x1000u+core*0x800u+pos,saturate(left));capture(0x1200u+core*0x800u+pos,saturate(right));
    capture(0x1400u+core*0x800u+pos,saturate(wet_left));capture(0x1600u+core*0x800u+pos,saturate(wet_right));
    const auto mix=spu2_known_register(base+0x198u);
    left=(mix&0x800u)?saturate(left):0;right=(mix&0x400u)?saturate(right):0;
    // Existing effects/ADMA validation refuses any audible uncomputed source.
    if(core && spu2_last_sample_[0] && spu2_last_sample_[0]->cycle==sample_cycle) {
        if(mix&0x8u)left+=gain(spu2_last_sample_[0]->left,static_cast<std::int16_t>(spu2_known_register(0x1F900790u)));
        if(mix&0x4u)right+=gain(spu2_last_sample_[0]->right,static_cast<std::int16_t>(spu2_known_register(0x1F900792u)));
    }
    return {sample_cycle,saturate(gain(saturate(left),direct_volume(spu2_known_register(0x1F900760u+core*0x28u)))),
                         saturate(gain(saturate(right),direct_volume(spu2_known_register(0x1F900762u+core*0x28u))))};
}
} // namespace ps2native
