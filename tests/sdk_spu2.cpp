// Original native fixtures; no firmware or interpreted execution.
#include "ps2native/iop_ops.hpp"
#include "ps2native/iop_aot.hpp"
#include "ps2native/sha256.hpp"
#include <iostream>
#include <map>
using namespace ps2native;
void require(bool ok,const std::string& why) { if(!ok)throw std::runtime_error(why); }

template<bool Complete> constexpr auto setup() {
    std::array<std::uint32_t,64> words{};unsigned n=0;
    const auto sh=[&](unsigned address,unsigned value) {
        words[n++]=0x3C08BF90u;words[n++]=0x35080000u|address;
        words[n++]=0x34090000u|value;words[n++]=0xA5090000u;
    };
    sh(0x59A,0x8000);sh(0x764,0);sh(0x766,0);sh(0x19A,0x8080);
    if constexpr(Complete){sh(0x2E0,0);sh(0x2E2,0xFFF8);sh(0x33C,0);}
    words[62]=0x1000FFFFu;
    return words;
}
template<auto Words> auto entries() {
    return []<std::size_t... N>(std::index_sequence<N...>) {
        return std::array{IopAotEntry{0xBFC00000u+4u*static_cast<unsigned>(N),Words[N],
            [](Iop& iop){iop.execute_native<Words[N]>();}}...};
    }(std::make_index_sequence<Words.size()>{});
}
template<bool Complete> void timing() {
    constexpr auto words=setup<Complete>();const auto native=entries<words>();
    std::vector<std::uint8_t> rom(4u*1024u*1024u);
    for(unsigned n=0;n<words.size();++n)for(unsigned b=0;b<4;++b)rom[n*4+b]=static_cast<std::uint8_t>(words[n]>>(b*8));
    const auto source=sha256_hex(rom),identity=iop_aot_identity(source,native);
    Iop missing;missing.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);missing.run_until(1);
    require(missing.stopped() && missing.stop_reason()->find("missing IOP AOT")!=std::string::npos,
        "An absent fixture translation must stop explicitly without an interpreted fallback");
    const IopAotModule module{source,identity,native};const IopAotRegistration registration(module);
    Iop iop;iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);
    iop.run_until(767);
    require(!iop.stopped(),"Driver initialization before the first sample: "+iop.stop_reason().value_or(""));
    iop.run_until(769);
    if constexpr(Complete)require(!iop.stopped() && iop.spu2_sample_count(0)==1,"Configured DSP must produce its first sample");
    else require(iop.stopped() && iop.stop_reason()->find("known workspace")!=std::string::npos,
        "Unknown workspace must fail at the actual DSP edge");
    require(!iop.interpreted_instructions() && iop.native_instructions()>=767,"Timing fixture must execute native instructions only");
}

struct Device : ps2recomp::NativeSpu2<Device,ObservedWord,DeviceRefusal> {
    std::map<std::uint32_t,std::optional<std::uint32_t>> registers;
    std::uint32_t istat_{};
    const std::optional<std::uint32_t>* latch(std::uint32_t address) const {
        const auto found=registers.find(address);return found==registers.end()?nullptr:&found->second;
    }
    std::optional<std::uint16_t> spu2_ram_halfword(std::uint32_t address) const {
        if(address>=spu2_ram_written_.size() || !spu2_ram_written_[address])return {};
        return spu2_ram_[address];
    }
    void synthesis(unsigned filter,bool known,bool irq) {
        constexpr auto base=0x1F900000u;
        registers={{base,0x3FFFu},{base+2,0x3FFFu},{base+4,0x1000u},
            {base+6,0xFu},{base+8,0x1FC0u},{base+0x1C0,0u},{base+0x1C2,0x2800u},
            {base+0x198,0xC00u},{base+0x760,0x3FFFu},{base+0x762,0x3FFFu}};
        for(unsigned r=0x180;r<0x198;r+=2)registers[base+r]=(r==0x188 || r==0x190)?1u:0u;
        spu2_control_[0]=ObservedWord{0x8000,0};spu2_control_[1]=ObservedWord{irq?0x40u:0u,0};
        registers[base+0x59C]=0;registers[base+0x59E]=0x2800;
        spu2_ram_.resize(0x100000);spu2_ram_written_.resize(0x100000);
        spu2_ram_[0x2800]=static_cast<std::uint16_t>(0x0404u|(filter<<4u));
        spu2_ram_[0x2801]=0x7777;spu2_ram_written_[0x2800]=1;spu2_ram_written_[0x2801]=known;
        spu2_keys_[0][0]=1;
        if(!known) {
            try {(void)synthesize_spu2(0,768);}catch(const DeviceRefusal& e) {
                require(std::string(e.what()).find("unknown sound RAM")!=std::string::npos,e.what());return;
            }
            throw std::runtime_error("Unknown sample RAM must not produce invented audio");
        }
        const auto sample=synthesize_spu2(0,768);const auto& voice=spu2_voices_[0][0];
        constexpr int second[]={1792,3472,5012,4536,5208};
        require(voice.decoded[0]==1792 && voice.decoded[1]==second[filter] && voice.envelope==14336,
            "SDK ADPCM predictors and ADSR must match independent integer vectors");
        require(sample.left>0 && sample.left==sample.right && sample.cycle==768,"SDK produces real stereo PCM at the supplied edge");
        require(bool(istat_&0x200u)==irq,"Both cores share the sound RAM IRQ comparator");
    }
};
int main() try {
    timing<false>();timing<true>();
    for(unsigned filter=0;filter<5;++filter){Device device;device.synthesis(filter,true,filter==4);}
    Device unknown;unknown.synthesis(0,false,false);
    std::cout<<"Native SPU2: initialization edge, unknown workspace/RAM, five ADPCM filters, ADSR, PCM and shared IRQ passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
