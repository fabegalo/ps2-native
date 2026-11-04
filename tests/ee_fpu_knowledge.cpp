// Original instruction streams and bit-mask assertions; no firmware data.
#include "boot_checks.hpp"
#include "bios_program.hpp"
#include "ps2native/interpreter.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
constexpr std::uint32_t math(unsigned fn,unsigned fd,unsigned fs,unsigned ft){return 0x46000000u|(ft<<16u)|(fs<<11u)|(fd<<6u)|fn;}
void integer_domains(Memory& memory) {
    for(const bool interpreted:{false,true}) {
        EEState s;s.set_cp0(Cop0::Status,kStatusCU1);
        const auto run=[&](std::uint32_t word) {
            s.pc=0x80001000;memory.write32(s.pc,word);std::uint64_t count=0;
            ExecutionContext ctx{s,memory,count,100,0xFFFFFFFFu};
            if(interpreted || word==0x44020000u || word==0x44852000u)interpret_instruction(ctx);
            else switch(word) {
                case 0x00021DC2u:ee::exec<MipsOpcode::Srl>(s,memory,0,word);break;
                case 0x306300FFu:ee::exec<MipsOpcode::Andi>(s,memory,0,word);break;
                case 0x00831823u:ee::exec<MipsOpcode::Subu>(s,memory,0,word);break;
                case 0x28620009u:ee::exec<MipsOpcode::Slti>(s,memory,0,word);break;
                case 0xAC220000u:ee::exec<MipsOpcode::Sw>(s,memory,0,word);break;
                case 0x8C250000u:ee::exec<MipsOpcode::Lw>(s,memory,0,word);break;
                case 0x9C250000u:ee::exec<MipsOpcode::Lwu>(s,memory,0,word);break;
                default:throw std::runtime_error("unexpected finite-domain test instruction");
            }
        };
        s.set_reg128(3,0,0xAABBCCDDEEFF0011ull);
        s.fpr[0]=0x30222000u;s.unknown_fpr_bits[0]=0x87999F00u;
        s.fpr_affine[0]={memory.allocate_fpu_origin(),0,0,false,s.fpr[0],s.unknown_fpr_bits[0],0x33BBBE00u,0xB4222100u,
            std::make_shared<const std::vector<std::uint32_t>>(std::initializer_list<std::uint32_t>{0x33BBBE00u,0xB4222100u})};
        run(0x44020000u);require(s.reg64_candidates(2) && s.reg64_candidates(2)->size()==2,
            "MFC1 preserves the two raw residuals and their correlated 64-bit sign extension");
        s.set_reg64(1,0x80002200);run(0xAC220000u);run(0x8C250000u);
        require(*s.reg64_candidates(2)==*s.reg64_candidates(5),"SW/LW moves every real word and its finite domain through aliased RAM");
        run(0x9C250000u);require(s.reg64_candidates(5)->back()==0xB4222100ull,"LWU establishes upper zeros for every possible signed source word");
        run(0x44852000u);require(s.fpr_affine[4].candidates && s.fpr_affine[4].candidates->size()==2,
            "MTC1 restores the raw finite domain without converting its integer payload");
        run(0x00021DC2u);run(0x306300FFu);s.set_reg64(4,128);run(0x00831823u);
        require(*s.reg64_candidates(3)==std::vector<std::uint64_t>{24,25} && s.gpr[3].high==0xAABBCCDDEEFF0011ull,
            "shift, mask and subtraction retain exactly both exponents and preserve the upper GPR half");
        run(0x28620009u);require(s.reg64(2)==0,"both finite exponent differences prove the same branch input despite their coarse bit hull");
        s.set_reg64_candidates(3,{8,9});s.set_reg64(2,0xCAFE);
        rejects([&]{run(0x28620009u);},"comparison depends");require(s.reg64(2)==0xCAFE,"different candidate comparisons refuse before changing the destination");
        s.set_reg64_candidates(3,{24,25});s.set_reg64_observed(3,s.gpr[3].low,s.unknown_gpr_bits[3]);
        require(!s.reg64_candidates(3),"a new identical opaque register write discards the old finite restriction");
        s.set_reg64(1,0xA0002200);const auto word=memory.load32_observed(0x80002200);
        memory.store32_observed(0xA0002200,word);run(0x8C250000u);
        require(!s.reg64_candidates(5),"an aliased overwrite of identical RAM bits invalidates the candidate sidecar");
        s.set_reg64_candidates(0,{8,9});require(!s.reg64_candidates(0) && !s.unknown_gpr_bits[0],"the zero register cannot acquire a symbolic domain");
        s.set_reg64_candidates(3,{0xFFFFFFFFull,0x7FFFFFFFull});s.set_reg64(4,1);
        ee::exec<MipsOpcode::Addu>(s,memory,0,(3u<<21u)|(4u<<16u)|(3u<<11u));
        require(*s.reg64_candidates(3)==std::vector<std::uint64_t>{0,0xFFFFFFFF80000000ull},
            "aliased 32-bit addition maps both wrapping/sign-extending candidates exactly");
        ee::exec<MipsOpcode::Xor>(s,memory,0,(3u<<21u)|(3u<<16u)|(3u<<11u));
        require(!s.reg64(3) && !s.reg64_candidates(3),"a same-register XOR proves zero without an impossible cross-product");
        s.set_reg64_candidates(3,{0xFFFFFFFFFFFFFFFFull,0xFFFFFFFFFFFFFFFEull});s.set_reg64(4,0);
        ee::exec<MipsOpcode::Slt>(s,memory,0,(3u<<21u)|(4u<<16u)|(2u<<11u));
        require(s.reg64(2)==1,"signed candidate ordering compares both negative values correctly");
        std::vector<std::uint64_t> large;for(unsigned n=0;n<17;++n)large.push_back(n*16u);
        s.set_reg64_candidates(3,large);s.set_reg64_candidates(4,large);
        ee::exec<MipsOpcode::Addu>(s,memory,0,(3u<<21u)|(4u<<16u)|(2u<<11u));
        require(!s.reg64_candidates(2),"exceeding the pair budget falls back to the sound bit hull rather than silently omitting candidates");
        for(const auto a:large)for(const auto b:large)require(!(((a+b)^s.gpr[2].low)&~s.unknown_gpr_bits[2]),
            "every pair remains represented after the finite-work fallback");
    }
}
}
int main() try {
    Memory memory;memory.reset_for_boot(m20::build_firmware().rom);
    integer_domains(memory);
    EEState state;state.set_cp0(Cop0::Status,kStatusCU1);
    std::uint64_t executed=0;ExecutionContext context{state,memory,executed,1000,0xFFFFFFFFu};
    const auto step=[&](std::uint32_t word){state.pc=0x80001000;memory.write32(state.pc,word);interpret_instruction(context);};
    state.fpr[1]=0x3F800001;state.fpr[2]=0x3F800003;state.fcr31=0x0183C079;
    step(math(2,3,1,2));
    const auto value=state.fpr[3],unknown=state.unknown_fpr_bits[3];
    require(unknown && !(unknown&0xFF800000u) && !(value&unknown) && state.fcr31==0x01830079u,
        "an unresolved product preserves uncertain fraction bits with proven sign/exponent and flags, without selecting an endpoint");
    for(const auto possible : {0x3F800003u,0x3F800004u})require(!((possible^value)&~unknown),"the product hull includes both sides of its retained-mantissa boundary");
    state.fpr[5]=0x5F800000;step(math(0,4,3,5));
    require(state.fpr[4]==0x5F800000 && !state.unknown_fpr_bits[4],"alignment of a dominated uncertain addend proves the entire resulting value");
    step(0x44081800);require(state.gpr[8].low==value && state.unknown_gpr_bits[8]==unknown,"MFC1 transports actual fraction uncertainty into the GPR");
    step(0x44883000);require(state.fpr[6]==value && state.unknown_fpr_bits[6]==unknown,"MTC1 transports raw uncertain bits instead of numerically consuming them");
    state.set_reg64(1,0x80002000);step(0xE4260000);const auto saved=memory.load32_observed(0x80002000);
    require(saved.value==value && saved.unknown==unknown,"SWC1 writes actual value/knownness bytes through shared RAM");
    step(0xC4270000);require(state.fpr[7]==value && state.unknown_fpr_bits[7]==unknown,"LWC1 restores exact stored knownness");
    state.set_reg64_observed(9,0x3F800000,0x80000001u);step(0x44893800);step(0x440A3800);
    require(state.unknown_fpr_bits[7]==0x80000001u && state.unknown_gpr_bits[10]==0xFFFFFFFF80000001ull,
        "an unknown FPR sign bit extends through all high GPR sign-extension bits");
    step(math(5,8,7,0));require(state.fpr[8]==0x3F800000 && state.unknown_fpr_bits[8]==1,"ABS establishes the sign and preserves opaque magnitude bits");
    step(math(7,9,7,0));step(math(6,10,7,0));
    require(state.unknown_fpr_bits[9]==0x80000001u && state.unknown_fpr_bits[10]==0x80000001u,"NEG and MOV preserve unresolved raw payload bits");
    for(const auto word : {math(3,11,6,2),math(4,11,0,6),math(0x24,11,6,0),math(0x1C,11,6,2),math(0x18,0,6,2)}) {
        state.fpr[11]=0x12345678;state.unknown_fpr_bits[11]=0;state.fcr31=0x0183C079;
        rejects([&]{step(word);},"unknown FPR bits");
        require(state.fpr[11]==0x12345678 && !state.unknown_fpr_bits[11] && state.fcr31==0x0183C079,
            "an unsupported uncertain numeric consumer refuses before altering its destination or flags");
    }
    const auto comparison=math(0x32,0,6,2);
    rejects([&]{step(comparison);},"unknown FPR bits");
    rejects([&]{ee::exec<MipsOpcode::CEqualS>(state,memory,0,comparison);},"unknown FPR bits");
    step(math(0x30,0,6,2));require(!(state.fcr31&(1u<<23u)),"C.F.S proves false without consuming unknown numeric operands in either engine");
    memory.write32(0x80002000,0x40000000);step(0xC4270000);
    require(state.fpr[7]==0x40000000 && !state.unknown_fpr_bits[7],"a known load replaces old FPR uncertainty");
    state.fpr[4]=0xBF490000u;state.unknown_fpr_bits[4]=0x1FFFu;
    state.fpr[3]=0x37B54442u;state.unknown_fpr_bits[3]=0;
    step(math(1,0,4,3));require(state.unknown_fpr_bits[0],"subtraction retains the uncertain source fraction");
    state.set_reg64(1,0x80002000);step(0xE4200000);step(0xC4210000);step(math(1,0,4,1));
    require(state.fpr[0]==0x37B50000u && !state.unknown_fpr_bits[0],
        "correlated x-(x-c) cancels the same uncertain mantissa after a real SWC1/LWC1 round trip, with exact EE guard-column loss: "+hex8(state.fpr[0])+" unknown="+hex8(state.unknown_fpr_bits[0]));
    for(unsigned n=0;n<8192;++n) {
        std::uint32_t flags=0;const auto original=0xBF490000u|n;
        const auto reduced=ee_fpu_add(original,0xB7B54442u,flags);
        require(ee_fpu_add(original,reduced^0x80000000u,flags)==0x37B50000u,
            "all 8192 concrete source mantissas independently give the certified cancellation result");
    }
    step(math(1,0,4,3));step(0xE4200000);
    const auto opaque=memory.load32_observed(0x80002000);memory.store32_observed(0xA0002000,opaque);
    step(0xC4210000);rejects([&]{step(math(1,0,4,1));},"cannot certify");
    require(state.unknown_fpr_bits[1],"an ordinary aliased RAM overwrite with identical opaque bits cannot retain a false origin relationship");
    step(math(6,1,4,0));step(math(1,0,4,1));require(!state.fpr[0] && !state.unknown_fpr_bits[0],
        "MOV.S preserves identity so subtraction of the same opaque value proves exact positive zero");
    state.fpr[0]=0x40490F00u;state.unknown_fpr_bits[0]=0xFFu;
    state.fpr_affine[0]={memory.allocate_fpu_origin(),0,128,false,0x40490F00u,0xFFu,0x40490F7Fu,0x40490F80u};
    state.fpr[5]=0x40490FDAu;state.unknown_fpr_bits[5]=0;
    state.fpr[3]=0x37B54442u;state.unknown_fpr_bits[3]=0;
    step(math(1,4,5,0));require(state.fpr_affine[4].candidates && state.fpr_affine[4].candidates->size()==2,
        "two possible adjacent large inputs remain two small cancellation results rather than a spurious 131073-value interval");
    step(math(1,0,4,3));require(state.fpr_affine[0].candidates && state.fpr_affine[0].candidates->size()==2 &&
        (state.unknown_fpr_bits[0]&0x80000000u),"a finite cancellation domain preserves both possible signs without guessing one");
    for(const auto input:{0x40490F7Fu,0x40490F80u}) {
        std::uint32_t flags=0;const auto first=ee_fpu_add(0x40490FDAu,input^0x80000000u,flags);
        const auto residual=ee_fpu_add(first,0xB7B54442u,flags);
        require(std::find(state.fpr_affine[0].candidates->begin(),state.fpr_affine[0].candidates->end(),residual)!=state.fpr_affine[0].candidates->end(),
            "each concrete source reproduces one certified residual and flags");
    }
    step(math(2,1,0,0));require(!(state.unknown_fpr_bits[1]&0x80000000u) && !(state.fpr[1]&0x80000000u),
        "squaring the same finite-domain register preserves correlation and cannot invent a negative cross-product");
    state.set_reg64(1,0x80002100);step(0xE4200000);step(0xC4220000);
    require(state.fpr_affine[2].candidates==state.fpr_affine[0].candidates,"SWC1/LWC1 transports the same immutable candidate domain through actual RAM");
    step(math(5,2,2,0));require(!(state.unknown_fpr_bits[2]&0x80000000u) && state.fpr_affine[2].candidates,
        "ABS preserves both finite magnitude candidates and proves a positive sign");
    state.fpr[8]=0x00800000u;state.unknown_fpr_bits[8]=0x80000000u;
    state.fpr_affine[8]={memory.allocate_fpu_origin(),0,0,false,0x00800000u,0x80000000u,0x00800000u,0x80800000u,
        std::make_shared<const std::vector<std::uint32_t>>(std::initializer_list<std::uint32_t>{0x00800000u,0x80800000u})};
    state.fpr[9]=0x00800001u;state.unknown_fpr_bits[9]=0;state.fcr31=0x0183C079u;
    rejects([&]{step(math(0,10,8,9));},"cannot certify");
    require(state.fcr31==0x0183C079u,"different concrete underflow classes cannot fabricate shared FPU exception flags");
    std::cout<<"EE floating bounds, dominated addition, raw register/RAM transport, sign operations, consumer refusals and comparison parity passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
