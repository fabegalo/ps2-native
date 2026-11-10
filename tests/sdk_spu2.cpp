// Original native fixtures; no firmware or interpreted execution.
#include "ps2native/iop_ops.hpp"
#include "ps2native/iop_aot.hpp"
#include "ps2native/sha256.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/memory.hpp"
#include <iostream>
#include <map>
using namespace ps2native;
void require(bool ok,const std::string& why) { if(!ok)throw std::runtime_error(why); }

template<bool Complete,bool Audible,bool Irq> constexpr auto setup() {
    std::array<std::uint32_t,64> words{};unsigned n=0;
    const auto sh=[&](unsigned address,unsigned value) {
        words[n++]=0x3C08BF90u;words[n++]=0x35080000u|address;
        words[n++]=0x34090000u|value;words[n++]=0xA5090000u;
    };
    sh(0x59A,0x8000);sh(0x764,Audible?1:0);sh(0x766,0);sh(0x19A,Irq?0x80C0:0x8080);
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
template<unsigned Core,bool UnknownAddress> void transfer_address() {
    constexpr auto words=[] {
        const auto offset=0x1A8u+Core*0x400u;
        return std::array{0x3C08BF90u,0x34090003u,0xA5090000u|offset,0x95090000u|offset,0u,
            UnknownAddress?0x00094902u:0u,0xA5090000u|offset,0x95020000u|offset,0u,0x1000FFFFu,0u};
    }();
    const auto native=entries<words>();std::vector<std::uint8_t> rom(4u*1024u*1024u);
    for(unsigned n=0;n<words.size();++n)for(unsigned b=0;b<4;++b)rom[n*4+b]=static_cast<std::uint8_t>(words[n]>>(b*8));
    const auto source=sha256_hex(rom),identity=iop_aot_identity(source,native);
    const IopAotModule module{source,identity,native};const IopAotRegistration registration(module);
    Iop iop;iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.run_until(30);
    if constexpr(UnknownAddress)require(iop.stopped() && iop.stop_reason()->find("unknown address bits")!=std::string::npos,
        "TSA writes must refuse unknown consumed address bits");
    else require(!iop.stopped() && iop.diagnostic_gpr(2).value==3u && iop.diagnostic_gpr(2).unknown==0xFFF0u,
        "TSA read-modify-write ignores reserved write bits without inventing their readback");
}
template<bool RejectControl> void sdk_addiu() {
    constexpr std::array words{0x3C08BF90u,0x3409000Fu,0xA50901A8u,0x950901A8u,0u,
        0x00094C00u,0x3529849Fu,0x25320200u,0x26520000u,RejectControl?0x02400008u:0u,
        0xA51201AAu,0x950A01AAu,0u,0x1000FFFFu,0u};
    const auto native=entries<words>();std::vector<std::uint8_t> rom(4u*1024u*1024u);
    for(unsigned n=0;n<words.size();++n)for(unsigned b=0;b<4;++b)rom[n*4+b]=static_cast<std::uint8_t>(words[n]>>(b*8));
    const auto source=sha256_hex(rom),identity=iop_aot_identity(source,native);
    const IopAotModule module{source,identity,native};const IopAotRegistration registration(module);
    Iop iop;iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.run_until(30);
    require(iop.diagnostic_gpr(18).value==0xF869Fu && iop.diagnostic_gpr(18).unknown==0xFFF00000u,
        "SDK ADDIU preserves partial operands, signed immediate addition and exact copies without fabricated high bits");
    if constexpr(RejectControl)require(iop.stopped() && iop.stop_reason()->find("unknown operand")!=std::string::npos,
        "An uncertain computed address still refuses an indirect control transfer");
    else require(!iop.stopped() && iop.gpr(10)==0x869Fu && !iop.interpreted_instructions(),
        "Native SDK ADDIU reaches the actual shared SPU2 device with its known low halfword");
}
template<std::uint32_t Trap,unsigned Cause,bool Delay> void sdk_exception() {
    constexpr std::array words{0x3C017FFFu,0x3421FFFFu,0x24020001u,Delay?0x10020001u:0u,Trap};
    const auto native=entries<words>();std::vector<std::uint8_t> rom(4u*1024u*1024u);
    for(unsigned n=0;n<words.size();++n)for(unsigned b=0;b<4;++b)rom[n*4+b]=static_cast<std::uint8_t>(words[n]>>(b*8));
    const auto source=sha256_hex(rom),identity=iop_aot_identity(source,native);
    const IopAotModule module{source,identity,native};const IopAotRegistration registration(module);
    Iop iop;iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.run_until(5);
    require(!iop.stopped() && iop.pc()==0xBFC00180u && iop.native_instructions()==5u &&
        iop.diagnostic_cop0(13)==std::optional{(Delay?0x80000000u:0u)|(Cause<<2u)} &&
        iop.diagnostic_cop0(14)==std::optional{Delay?0xBFC0000Cu:0xBFC00010u},
        "SDK faults preserve exception code, EPC and BD even in a not-taken branch delay slot");
    if constexpr(Cause==4u || Cause==5u)require(iop.diagnostic_cop0(8)==std::optional{1u},"SDK alignment faults preserve BadVAddr");
}
void sdk_integer_edges() {
    constexpr std::array words{0x2401FFF9u,0x24020003u,0x00220018u,0x00001812u,0x00002010u,
        0x0022001Au,0x00002812u,0x00003010u,0x0020001Au,0x00003812u,0x00004010u,
        0x3C018000u,0x2402FFFFu,0x0022001Au,0x00004812u,0x00005010u,
        0x3C01FFFFu,0x3421FFFFu,0x24020002u,0x00220019u,0x00005812u,0x00006010u,
        0x0020001Bu,0x00006812u,0x00007010u,0x1000FFFFu,0u};
    const auto native=entries<words>();std::vector<std::uint8_t> rom(4u*1024u*1024u);
    for(unsigned n=0;n<words.size();++n)for(unsigned b=0;b<4;++b)rom[n*4+b]=static_cast<std::uint8_t>(words[n]>>(b*8));
    const auto source=sha256_hex(rom),identity=iop_aot_identity(source,native);
    const IopAotModule module{source,identity,native};const IopAotRegistration registration(module);
    Iop iop;iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.run_until(30);
    require(!iop.stopped() && iop.gpr(3)==0xFFFFFFEBu && iop.gpr(4)==~0u && iop.gpr(5)==0xFFFFFFFEu &&
        iop.gpr(6)==~0u && iop.gpr(7)==1u && iop.gpr(8)==0xFFFFFFF9u && iop.gpr(9)==0x80000000u &&
        iop.gpr(10)==0u && iop.gpr(11)==0xFFFFFFFEu && iop.gpr(12)==1u && iop.gpr(13)==~0u && iop.gpr(14)==~0u,
        "SDK native signed/unsigned multiplication, division by zero and INT_MIN/-1 match independent architectural results");
}
void sdk_jalr_zero() {
    constexpr std::array words{0x3C01BFC0u,0x34210018u,0x341F0037u,0x00200009u,0x24020001u,
        0x24020063u,0x2403002Au,0x1000FFFFu,0u};
    const auto native=entries<words>();std::vector<std::uint8_t> rom(4u*1024u*1024u);
    for(unsigned n=0;n<words.size();++n)for(unsigned b=0;b<4;++b)rom[n*4+b]=static_cast<std::uint8_t>(words[n]>>(b*8));
    const auto source=sha256_hex(rom),identity=iop_aot_identity(source,native);
    const IopAotModule module{source,identity,native};const IopAotRegistration registration(module);
    Iop iop;iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.run_until(12);
    require(!iop.stopped() && iop.gpr(31)==55u && !iop.gpr(0) && iop.gpr(2)==1u && iop.gpr(3)==42u,
        "Native JALR with rd zero preserves RA and executes its delay instruction before the target");
}
struct SeededIop : Iop {
    void seed() { spu2_ram_.resize(0x100000);spu2_ram_written_.resize(0x100000);spu2_ram_[0x40000]=0x1234;spu2_ram_written_[0x40000]=1; }
    void enable_input() { spu2_adma_[1].enabled=true; }
    std::uint16_t backing() const { return spu2_ram_[0x40000]; }
};
template<bool Complete,bool Audible=false,bool Irq=false,bool Dma=false> void timing() {
    constexpr auto words=setup<Complete,Audible,Irq>();const auto native=entries<words>();
    std::vector<std::uint8_t> rom(4u*1024u*1024u);
    for(unsigned n=0;n<words.size();++n)for(unsigned b=0;b<4;++b)rom[n*4+b]=static_cast<std::uint8_t>(words[n]>>(b*8));
    const auto source=sha256_hex(rom),identity=iop_aot_identity(source,native);
    Iop missing;missing.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);missing.run_until(1);
    require(missing.stopped() && missing.stop_reason()->find("missing IOP AOT")!=std::string::npos,
        "An absent fixture translation must stop explicitly without an interpreted fallback");
    const IopAotModule module{source,identity,native};const IopAotRegistration registration(module);
    SeededIop iop;iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.seed();
    iop.diagnostic_spu2_adma_word_cycles(1);
    iop.run_until(767);
    require(!iop.stopped(),"Driver initialization before the first sample: "+iop.stop_reason().value_or(""));
    if constexpr(Dma)iop.enable_input();
    iop.run_until(769);
    if constexpr(Dma)require(iop.stopped() && iop.stop_reason()->find("share RAM with DMA")!=std::string::npos && !iop.spu2_sample_count(0),
        "An unknown DSP workspace must refuse competing DMA even under a timing experiment");
    else if constexpr(Audible || Irq)require(iop.stopped() && iop.stop_reason()->find(Audible?"zero left and right":"both IRQ comparators")!=std::string::npos && !iop.spu2_sample_count(0),
        "A DMA timing experiment must not permit audible uncomputed effects or drop effect RAM IRQs");
    else if constexpr(Complete)require(!iop.stopped() && iop.spu2_sample_count(0)==1,"Configured DSP must produce its first sample");
    else {
        require(!iop.stopped() && iop.spu2_sample_count(0)==1 && !iop.spu2_ram_halfword(0x40000) && iop.backing()==0x1234,
            "Zero gains and disabled IRQ prove silent output while unknown ESA invalidates RAM knowledge without clearing bytes");
        require(!iop.diagnostic_latch(0x1F9002E0u) && !iop.diagnostic_latch(0x1F9002E2u),
            "A conservative RAM bound must not invent ESA register values");
        iop.seed();iop.run_until(1537);
        require(!iop.stopped() && iop.spu2_sample_count(0)==2 && !iop.spu2_ram_halfword(0x40000) && iop.backing()==0x1234,
            "An unknown DSP workspace must invalidate new RAM knowledge at every sample edge");
    }
    if constexpr(Complete || Audible || Irq || Dma)require(iop.spu2_ram_halfword(0x40000)==0x1234,
        "A disjoint known workspace or a refused DSP access must preserve unrelated sound RAM");
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
    void synthesis(unsigned filter,bool known,bool irq,bool muted=false) {
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
        if(muted){registers[base]=0;registers[base+2]=0;}
        spu2_keys_[0][0]=1;
        if(!known && (!muted || irq)) {
            try {(void)synthesize_spu2(0,768);}catch(const DeviceRefusal& e) {
                require(std::string(e.what()).find("unknown sound RAM")!=std::string::npos,e.what());
                require(bool(istat_&0x200u)==irq,"A known IRQ hit must not be dropped to abstract a muted voice");return;
            }
            throw std::runtime_error("Unknown sample RAM must not produce invented audio");
        }
        const auto sample=synthesize_spu2(0,768);const auto& voice=spu2_voices_[0][0];
        if(muted) {
            require(!sample.left && !sample.right && !voice.modeled && voice.key_ons==1 && (spu2_endx_[0].unknown&1u),
                "Unknown ADPCM can prove zero mixer output while retaining unknown decoder status");
            spu2_keys_[0][0]=0;
            const auto refuses=[&](const char* message) {
                try{(void)synthesize_spu2(0,1536);}catch(const DeviceRefusal& e){require(std::string(e.what()).find(message)!=std::string::npos,e.what());return;}
                throw std::runtime_error("An observable unknown voice must refuse");
            };
            registers[base]=1;refuses("zero mixer gains");registers[base]=0;
            spu2_control_[1]=ObservedWord{0x40,0};refuses("disabled IRQ");spu2_control_[1]=ObservedWord{0,0};
            for(const auto offset:{0u,2u,4u,6u,8u})registers[base+16u+offset]=registers[base+offset];
            registers[base+16u]=0x3FFF;registers[base+18u]=0x3FFF;
            registers[base+0x1CCu]=0;registers[base+0x1CEu]=0x3000;registers[base+0x180u]=2;
            spu2_ram_[0x3000]=0x404;spu2_ram_[0x3001]=0x7777;spu2_ram_written_[0x3000]=spu2_ram_written_[0x3001]=1;
            spu2_keys_[0][0]=2;refuses("pitch modulation");
            registers[base+16u]=0;registers[base+18u]=0;
            spu2_ram_[0x400]=0x1234;spu2_ram_written_[0x400]=1;
            const auto chained=synthesize_spu2(0,1536);
            require(!chained.left && !chained.right && (spu2_symbolic_voices_[0]&3u)==3u &&
                !spu2_ram_halfword(0x400) && spu2_ram_[0x400]==0x1234,
                "Silent pitch-modulation chains keep pre-volume captures unknown without inventing PCM or clearing RAM");
            return;
        }
        constexpr int second[]={1792,3472,5012,4536,5208};
        require(voice.decoded[0]==1792 && voice.decoded[1]==second[filter] && voice.envelope==14336,
            "SDK ADPCM predictors and ADSR must match independent integer vectors");
        require(sample.left>0 && sample.left==sample.right && sample.cycle==768,"SDK produces real stereo PCM at the supplied edge");
        require(bool(istat_&0x200u)==irq,"Both cores share the sound RAM IRQ comparator");
    }
    void loud_mix() {
        // Regression idea from gow-native-pc patch 24: loud simultaneous voices must not wrap polarity.
        synthesis(4,true,false);
        constexpr auto base=0x1F900000u;
        for(unsigned voice=1;voice<24;++voice) {
            for(const auto offset:{0u,2u,4u,6u,8u})registers[base+voice*16u+offset]=registers[base+offset];
            registers[base+0x1C0u+voice*12u]=0;registers[base+0x1C2u+voice*12u]=0x2800;
        }
        registers[base+0x188]=registers[base+0x190]=0xFFFF;
        registers[base+0x18A]=registers[base+0x192]=0xFF;
        spu2_ram_[0x2800]=0x440;
        for(unsigned n=1;n<8;++n){spu2_ram_[0x2800+n]=0x7777;spu2_ram_written_[0x2800+n]=1;}
        spu2_keys_[0][0]=0xFFFF;spu2_keys_[0][1]=0xFF;
        const auto positive=synthesize_spu2(0,1536);
        require(positive.left==32765 && positive.right==32765,"Twenty-four loud voices saturate before master volume without overflow");
        registers[base+0x760]=0x4000;
        const auto negative=synthesize_spu2(0,2304);
        require(negative.left==-32767 && negative.right==32765,"Signed master volume preserves polarity at saturated mix levels");
    }
    void exponential_release() {
        // Patch 48's rounding regression: an exponential decrease from ENVX=1 must reach zero.
        synthesis(0,true,false);spu2_keys_[0][0]=0;
        auto& voice=spu2_voices_[0][0];voice.phase=4;voice.envelope=1;voice.envelope_counter=0;
        registers[0x1F900008u]=0x20;spu2_ram_[0x2802]=0x7777;spu2_ram_written_[0x2802]=1;
        (void)synthesize_spu2(0,1536);
        require(voice.envelope==0 && !voice.phase,"Arithmetic exponential release must not stall above silence");
    }
};
void checkpoint_roundtrip(const std::filesystem::path& file) {
    constexpr auto words=setup<false,false,false>();const auto native=entries<words>();
    std::vector<std::uint8_t> rom(4u*1024u*1024u);
    for(unsigned n=0;n<words.size();++n)for(unsigned b=0;b<4;++b)rom[n*4+b]=static_cast<std::uint8_t>(words[n]>>(b*8));
    const auto hash=sha256_hex(rom),identity=iop_aot_identity(hash,native);
    const IopAotModule module{hash,identity,native};const IopAotRegistration registration(module);
    auto source=std::make_unique<Memory>(),restored=std::make_unique<Memory>();
    DiagnosticAssumptions assumptions;assumptions.spu2_quiescent_reset=true;
    source->reset_for_boot(rom,nullptr,nullptr,&assumptions);restored->reset_for_boot(rom,nullptr,nullptr,&assumptions);
    auto& original_iop=const_cast<Iop&>(source->iop());original_iop.run_until(1537);
    require(!original_iop.stopped() && original_iop.spu2_sample_count(0)==2,"Checkpoint fixture runs actual native IOP sample edges");
    auto original=ee_reset_state(),copy=ee_reset_state();original.stall_cycles=1537u*kEeCyclesPerIopCycle;
    source->set_clock(&original.retired_instructions,&original.stall_cycles);
    restored->set_clock(&copy.retired_instructions,&copy.stall_cycles);
    BootOptions options;options.max_instructions=0;options.max_trace_events=0;options.tail_instructions=0;
    const auto boundary=run_ee_boot(original,*source,nullptr,options);
    require(boundary.outcome==BootOutcome::BudgetExhausted && !boundary.retired,"Serialization boundary: "+boundary.reason);
    unsigned detached_sink_calls=0;
    auto& restored_iop=const_cast<Iop&>(restored->iop());
    restored_iop.set_spu2_sample_sink([&](unsigned,const auto&){++detached_sink_calls;});
    const CheckpointContext context{hash,"SDK SPU2 native state roundtrip"};
    save_checkpoint(file,original,*source,boundary,context);
    (void)load_checkpoint(file,copy,*restored,context);
    require(checkpoint_state_sha256(original,*source)==checkpoint_state_sha256(copy,*restored),
        "SDK-owned device state, latches, clocks and uncertainty survive complete checkpoint serialization");
    auto& continued_iop=const_cast<Iop&>(restored->iop());
    original_iop.run_until(2305);continued_iop.run_until(2305);
    require(!continued_iop.stopped() && continued_iop.spu2_sample_count(0)==3 &&
        continued_iop.spu2_sample_count(0)==original_iop.spu2_sample_count(0) && !detached_sink_calls,
        "Restored SDK state continues at the same sample edge and detaches host sinks");
    std::filesystem::remove(file);
}
int main(int argc,char** argv) try {
    require(argc==2,"Expected owned checkpoint fixture path");
    transfer_address<0,false>();transfer_address<1,false>();transfer_address<0,true>();transfer_address<1,true>();
    sdk_addiu<false>();sdk_addiu<true>();
    sdk_exception<0x00221820u,12,false>();sdk_exception<0x00221820u,12,true>();
    sdk_exception<0x8C030001u,4,true>();sdk_exception<0xAC030001u,5,true>();
    sdk_integer_edges();sdk_jalr_zero();
    timing<false>();timing<true>();timing<true,true>();timing<true,false,true>();timing<false,true>();timing<false,false,true>();timing<false,false,false,true>();
    for(unsigned filter=0;filter<5;++filter){Device device;device.synthesis(filter,true,filter==4);}
    Device unknown;unknown.synthesis(0,false,false);
    Device muted;muted.synthesis(0,false,false,true);
    Device muted_irq;muted_irq.synthesis(0,false,true,true);
    Device loud;loud.loud_mix();Device release;release.exponential_release();
    checkpoint_roundtrip(std::filesystem::path(argv[1]));
    std::cout<<"Native SPU2: sample edges, uncertainty, pitch modulation, PCM/IRQ, loud mixing, exponential release and checkpoint continuity passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
