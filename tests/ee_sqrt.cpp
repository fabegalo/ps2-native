// Original COP1 encodings and independent exact/SRT result vectors.
#include "boot_checks.hpp"
#include "bios_program.hpp"
#include "ee_native_fixture.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
constexpr std::uint32_t code(unsigned fd,unsigned ft){return 0x46000004u|(ft<<16u)|(fd<<6u);}
}
int main() try {
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
    EEState state;state.set_cp0(Cop0::Status,kStatusCU1);
    std::uint64_t executed=0;ExecutionContext context{state,memory,executed,1000,0xFFFFFFFFu};
    const std::uint32_t roots[][2]{{0,0},{0x80000000,0x80000000},{1,0},{0x80000001,0x80000000},
        {0x3F800000,0x3F800000},{0x40800000,0x40000000},{0x40000000,0x3FB504F3},
        {0x41100000,0x40400000},{0x7F800000,0x5F800000},{0xC0800000,0x40000000}};
    for(const auto& row:roots)for(const unsigned fd : {2u,12u}) {
        state.pc=0x80001000;state.fpr[0]=0xFFFFFFFF;state.fpr[12]=row[0];state.fcr31=0x0183C079;
        memory.write32(state.pc,code(fd,12));compiled_fixture_step(context);
        const auto expected_flags=(0x0183C079u&~0x30000u)|((row[0]&0x80000000u) && (row[0]&0x7F800000u) ? 0x20040u : 0u);
        require(state.fpr[fd]==row[1] && state.fcr31==expected_flags,"SQRT.S reads FT, supports destination aliasing, signed zero/denormals, finite exponent 255 and exact/inexact roots with I/D and sticky semantics");
        require(state.fpr[0]==0xFFFFFFFF,"SQRT.S cannot consume or modify the reserved FS register");
    }
    require(try_decode_r5900(0,code(31,31))->opcode==MipsOpcode::SqrtS && !try_decode_r5900(0,code(2,12)|(1u<<11u)),
        "SQRT.S accepts FT/FD fields and rejects a nonzero reserved FS field");
    state.pc=0x80001000;state.set_cp0(Cop0::Status,0);state.fpr[2]=0x12345678;state.fcr31=0x0183C079;
    memory.write32(state.pc,code(2,12));rejects([&]{compiled_fixture_step(context);},"Status.CU1 = 0");
    require(state.fpr[2]==0x12345678 && state.fcr31==0x0183C079,"disabled COP1 refuses before changing root or flags");
    std::cout<<"EE SQRT.S FT/FD, reserved FS, SRT roots, signed zero, negative input, flags and CU1 passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
