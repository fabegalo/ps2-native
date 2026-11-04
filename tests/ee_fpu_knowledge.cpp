// Original instruction streams and bit-mask assertions; no firmware data.
#include "boot_checks.hpp"
#include "bios_program.hpp"
#include "ps2native/interpreter.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
constexpr std::uint32_t math(unsigned fn,unsigned fd,unsigned fs,unsigned ft){return 0x46000000u|(ft<<16u)|(fs<<11u)|(fd<<6u)|fn;}
}
int main() try {
    Memory memory;memory.reset_for_boot(m20::build_firmware().rom);
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
    std::cout<<"EE floating bounds, dominated addition, raw register/RAM transport, sign operations, consumer refusals and comparison parity passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
