// Original accumulator vectors; no commercial instruction stream.
#include "boot_checks.hpp"
#include "bios_program.hpp"
#include "ee_native_fixture.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
int main() try {
    constexpr auto instruction=0x4602081Au; // MULA.S F1,F2; FD is reserved.
    require(try_decode_r5900(0,instruction)->opcode==MipsOpcode::MulaS &&
        !try_decode_r5900(0,instruction|(1u<<6u)),"MULA.S validates the reserved destination field");
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
    EEState state;state.set_cp0(Cop0::Status,kStatusCU1);
    std::uint64_t retired=0;ExecutionContext context{state,memory,retired,100,0xFFFFFFFFu};
    const auto step=[&]{state.pc=0x80001000u;memory.write32(state.pc,instruction);compiled_fixture_step(context);};
    const std::uint32_t vectors[][4]{{0x40000000u,0x40400000u,0x40C00000u,0},
        {0x80000000u,0x40400000u,0x80000000u,0},{0x00800000u,0x00800000u,0,0x4008u},
        {0x7FFFFFFFu,0x40000000u,0x7FFFFFFFu,0x8010u}};
    for(const auto& row:vectors) {
        state.fpr[1]=row[0];state.fpr[2]=row[1];state.fpu_acc=0xDEADBEEFu;state.fcr31=0x0183C079u;
        const auto registers=state.fpr;step();
        require(state.fpu_acc==row[2] && state.fcr31==((0x0183C079u&~0xC000u)|row[3]) && state.fpr==registers,
            "MULA.S writes only ACC and O/U flags, preserving sources, FPRs and sticky history");
    }
    state.fpr[1]=0x3F800001u;state.fpr[2]=0x3F800003u;
    state.fpu_acc=0x12345678u;state.fcr31=0x0183C079u;
    rejects(step,"not certified");
    require(state.fpu_acc==0x12345678u && state.fcr31==0x0183C079u,
        "uncertified multiplication cannot publish an arbitrary accumulator or flags");
    state.unknown_fpr_bits[1]=1;rejects(step,"unknown FPR bits");
    state.set_cp0(Cop0::Status,0);rejects(step,"Status.CU1 = 0");
    require(state.fpu_acc==0x12345678u && state.fcr31==0x0183C079u,
        "unknown operands and disabled COP1 preserve accumulator state");
    state.set_cp0(Cop0::Status,kStatusCU1);state.unknown_fpr_bits[1]=0;
    state.fpr[1]=0x40000000u;state.fpr[2]=0x40400000u;state.fpu_acc=0x41000000u;
    const auto registers=state.fpr;
    state.pc=0x80001000u;memory.write32(state.pc,0x4602081Fu);compiled_fixture_step(context);
    require(state.fpu_acc==0x40000000u && state.fpr==registers && state.fcr31==0x01830079u,
        "MSUBA.S computes 8 - 2*3 in ACC and preserves every FPR");
    require(try_decode_r5900(0,0x4602081Fu)->opcode==MipsOpcode::MsubaS &&
        !try_decode_r5900(0,0x4602085Fu),"MSUBA.S validates the reserved destination field");
    state.pc=0x80001000u;memory.write32(state.pc,0x4602085Cu);compiled_fixture_step(context);
    require(state.fpr[1]==0x41000000u && state.fpu_acc==0x40000000u,
        "MADD.S computes 2 + 2*3 with a source/destination alias without changing ACC");
    state.fpr[1]=0x40000000u;const auto add_sources=state.fpr;
    state.pc=0x80001000u;memory.write32(state.pc,0x4602081Eu);compiled_fixture_step(context);
    require(state.fpu_acc==0x41000000u && state.fpr==add_sources,
        "MADDA.S computes 2 + 2*3 in ACC without changing its FPR sources");
    require(try_decode_r5900(0,0x4602081Eu)->opcode==MipsOpcode::MaddaS &&
        !try_decode_r5900(0,0x4602085Eu),"MADDA.S validates the reserved destination field");
    state.pc=0x80001000u;memory.write32(state.pc,0x460208DDu);compiled_fixture_step(context);
    require(state.fpr[3]==0x40000000u && state.fpu_acc==0x41000000u,
        "MSUB.S computes 8 - 2*3 in its FPR destination while preserving ACC");
    const std::uint32_t refused[][3]{{0x7FFFFFFFu,0x40000000u,0x40000000u},
        {0x00800000u,0x3F800000u,0x00800001u},{0xFF000000u,0x3F800000u,0x7F7FFFFFu},
        {0x3F800001u,0x3F800003u,0x3F800000u}};
    for(unsigned index=0;index<4;++index) {
        state.fpr[1]=refused[index][0];state.fpr[2]=refused[index][1];
        state.fpu_acc=refused[index][2];state.fcr31=0x0183C079u;
        const auto before=state.fpr;
        const auto run=[&]{state.pc=0x80001000u;memory.write32(state.pc,0x4602081Fu);compiled_fixture_step(context);};
        rejects(run,index==3 ? "not certified" : "nonnormal");
        require(state.fpr==before && state.fpu_acc==refused[index][2] && state.fcr31==0x0183C079u,
            "uncertified products, intermediate overflow and nonnormal sums preserve all architectural destinations");
    }
    state.fpr[1]=0x3F800000u;state.unknown_fpr_bits[1]=1;state.fpr_affine[1]={};
    state.fpr[2]=0x3F800000u;state.fpu_acc=0x5F800000u;state.fcr31=0x0183C079u;
    state.pc=0x80001000u;memory.write32(state.pc,0x4602081Eu);compiled_fixture_step(context);
    require(state.fpu_acc==0x5F800000u && state.fcr31==0x01830079u && state.unknown_fpr_bits[1]==1,
        "MADDA.S proves a dominated uncertain product without selecting an operand or losing its uncertainty");
    state.unknown_fpr_bits[1]=0;state.fpr[1]=0x80000000u;state.fpr[2]=0x80000000u;state.fpu_acc=0x80000000u;
    state.pc=0x80001000u;memory.write32(state.pc,0x460208DCu);compiled_fixture_step(context);
    require(state.fpr[3]==0 && state.fpu_acc==0x80000000u,
        "MADD.S (-0) + (-0)*(-0) produces +0, matching the measured EE zero-sign case");
    state.fpr[1]=0;
    state.pc=0x80001000u;memory.write32(state.pc,0x460208DCu);compiled_fixture_step(context);
    require(state.fpr[3]==0x80000000u,"MADD.S (-0) + (+0)*(-0) preserves a negative zero result");
    std::cout<<"EE multiply/accumulate decoding, accumulator, flags, finite proofs and explicit refusals passed\n";
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
