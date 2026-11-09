// Original instruction streams and bit-mask assertions; no firmware data.
#include "boot_checks.hpp"
#include "bios_program.hpp"
#include "ee_native_fixture.hpp"
#include "ps2native/vu_float.hpp"
#include "ps2native/boot.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/ee_system.hpp"
#include <chrono>
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
constexpr std::uint32_t math(unsigned fn,unsigned fd,unsigned fs,unsigned ft){return 0x46000000u|(ft<<16u)|(fs<<11u)|(fd<<6u)|fn;}
void min_max(Memory& memory) {
    EEState state;state.set_cp0(Cop0::Status,kStatusCU1);
    std::uint64_t count=0;ExecutionContext ctx{state,memory,count,100,UINT32_MAX};
    constexpr auto maximum=math(0x28,3,1,2),minimum=math(0x29,3,1,2);
    require(decode_mips(0,maximum).opcode==MipsOpcode::MaxS &&
        decode_mips(0,minimum).opcode==MipsOpcode::MinS,"MIN/MAX.S decode to native operations");
    for(const auto pair:std::array<std::array<std::uint32_t,2>,6>{{
        {0xBF800000u,0x3F800000u},{0xC0000000u,0xBF800000u},{0x80000000u,0},
        {1,2},{0x7F800000u,0x7FFFFFFFu},{0xFFFFFFFFu,0xFF800000u}}}) {
        for(const bool reverse:{false,true}) {
            state.fpr[1]=pair[reverse];state.fpr[2]=pair[!reverse];state.fcr31=0x0183C079u;
            execute_ee_system<MipsOpcode::MaxS,maximum>(ctx);
            require(state.fpr[3]==pair[1] && state.fcr31==0x01830079u,"MAX.S raw ordering and sticky flags");
            execute_ee_system<MipsOpcode::MinS,minimum>(ctx);
            require(state.fpr[3]==pair[0] && !state.unknown_fpr_bits[3],"MIN.S raw ordering");
        }
    }
    state.unknown_fpr_bits[1]=1;const auto before=state;const auto retired=count;
    rejects([&]{execute_ee_system<MipsOpcode::MaxS,maximum>(ctx);},"unknown operand bits");
    require(state.fpr[3]==before.fpr[3] && count==retired,"uncertain MIN/MAX refuses before retirement");
    state.unknown_fpr_bits[1]=0;state.set_cp0(Cop0::Status,0);
    rejects([&]{execute_ee_system<MipsOpcode::MinS,minimum>(ctx);},"Status.CU1");
}
void integer_domains(Memory& memory) {
    for(const bool retired_path:{false,true}) {
        EEState s;s.set_cp0(Cop0::Status,kStatusCU1);
        const auto run=[&](std::uint32_t word) {
            s.pc=0x80001000;memory.write32(s.pc,word);std::uint64_t count=0;
            ExecutionContext ctx{s,memory,count,100,0xFFFFFFFFu};
            if(retired_path || word==0x44020000u || word==0x44852000u)compiled_fixture_step(ctx);
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
template<MipsOpcode Op,std::uint32_t Word>
void native_trace_op(ExecutionContext& context) {
    if(aot_enter_checked(context,context.state.pc,Word,false))execute_ee_system<Op,Word>(context);
}
const AotBlock trace_blocks[]{
    {0x80001000u,native_trace_op<MipsOpcode::MulS,0x460208C2u>},
    {0x80001004u,native_trace_op<MipsOpcode::MovS,0x46001906u>}
};
const std::array<std::uint8_t,8> trace_words{0xC2,0x08,0x02,0x46,0x06,0x19,0x00,0x46};
const auto trace_source=sha256_hex(trace_words);
const AotModule trace_module{trace_blocks,2,nullptr,0,"synthetic-words",trace_source.c_str()};
void floating_trace(Memory& memory) {
    EEState state;state.pc=0x80001000;state.set_cp0(Cop0::Status,kStatusCU1|kStatusERL);
    state.fpr[1]=0x3F800001u;state.fpr[2]=0x3F800003u;
    memory.write32(state.pc,math(2,3,1,2));memory.write32(state.pc+4u,math(6,4,3,0));
    BootOptions options;options.max_instructions=2;options.stop_at=state.pc+8u;options.trace=TraceLevel::Full;
    const auto report=run_ee_boot(state,memory,&trace_module,options);
    require(report.retired==2 && state.unknown_fpr_bits[3] && state.fpr[4]==state.fpr[3],
        "a full trace executes the real uncertain product and register copy");
    bool inputs=false,result=false;
    for(const auto& line:report.trace) {
        inputs|=line.find("  F FS=3F800001/?00000000 FT=3F800003/?00000000")!=std::string::npos;
        result|=line.find("  F FS="+hex8(state.fpr[3])+"/?"+hex8(state.unknown_fpr_bits[3]))!=std::string::npos;
    }
    require(inputs && result,"full COP1 records expose original operands and resulting uncertainty without choosing unknown bits");
    state.pc=0x80001000;options.trace=TraceLevel::Events;
    const auto events=run_ee_boot(state,memory,&trace_module,options);
    for(const auto& line:events.trace)require(line.find("  F ")==std::string::npos,"event tracing carries no per-operation FPU records");
}
void scalar_carry_experiment() {
    std::uint32_t flags=0;
    std::uint32_t seed=0x91A2B3C4u;
    for(unsigned coarse=0;coarse<32;++coarse)for(unsigned sample=0;sample<512;++sample) {
        seed=seed*1664525u+1013904223u;
        const auto a=((sample&1u)<<31u)|((sample%4u==0 ? 1u : sample%4u==1 ? 127u : sample%4u==2 ? 254u : 255u)<<23u)|(coarse<<18u);
        const auto b=(seed&0x807FFFFFu)|(((seed>>24u)%255u+1u)<<23u);
        std::uint32_t strict_flags=0x0183C079u,refined_flags=strict_flags;
        const auto strict=ee_fpu_multiply(a,b,strict_flags);
        const auto refined=ee_fpu_multiply(a,b,refined_flags,CoarseEarlyDigits);
        if(strict)require(refined==strict && refined_flags==strict_flags,
            "the scalar experiment preserves certified coarse results and flags, including exponent edges");
    }
    require(ee_fpu_multiply(0x3FC00000u,0x3F800002u,flags)==0x3FC00002u && !flags &&
        ee_fpu_multiply(0xBFC00000u,0x3F800002u,flags)==0xBFC00002u,
        "the established lowest-negative-digit carry class transfers to coarse significands with identical low input columns");
    flags=0xC000u;
    require(!ee_fpu_multiply(0x43A00000u,0x404CCC00u,flags) && flags==0xC000u &&
        !ee_fpu_multiply(0x43A00000u,0x404CCC00u,flags,PowerTwoEarlyDigits),
        "strict and power-only modes retain ambiguous late-column products");
    flags=0x0183C079u;
    require(!ee_fpu_multiply(0xFFC80000u,0x1C16E000u,flags) && flags==0x0183C079u,
        "an unestablished stage-6 correction must not certify an invented low result bit");
    require(ee_fpu_multiply(0xFFC80000u,0x1C16E000u,flags,CoarseEarlyDigits)==0xDC6BBE00u && flags==0x01830079u,
        "the explicitly selected reference class resolves the stage-6 counterexample and preserves flags");
    for(const auto [a,b,expected]:{std::tuple{0x43A00000u,0x404CCC00u,0x447FFF00u},
        std::tuple{0xC3A00000u,0x404CCC00u,0xC47FFF00u},std::tuple{0x3FC00000u,0x3F80CC00u,0x3FC13200u},
        std::tuple{0x3F800000u,0x3F80CC00u,0x3F80CC00u},std::tuple{0x43A00000u,0x3F80FC00u,0x43A13AFFu}}) {
        flags=0;require(ee_fpu_multiply(a,b,flags,CoarseEarlyDigits)==expected && !flags,
            "the opt-in late-column model retains both no-loss and established single-negative loss classes");
    }
    require(!ee_fpu_multiply(0x40400000u,0x3FC90F80u,flags) &&
        !ee_fpu_multiply(0x40400000u,0x3FC90F80u,flags,PowerTwoEarlyDigits),
        "strict and legacy VU experiments keep the other coarse early digits unresolved");
    require(ee_fpu_multiply(0x7FC00000u,0x40000002u,flags)==0x7FFFFFFFu && (flags&0x8010u)==0x8010u,
        "the transferred carry class still saturates overflow with its actual flags");
    flags=0;require(ee_fpu_multiply(0x00C00000u,0x3F000002u,flags)==0u && flags==0x4008u,
        "the transferred carry class retains normal-to-underflow handling");
    flags=0x1234u;rejects([&]{(void)ee_fpu_multiply(0x3F800000u,0x3F800000u,flags,3);},"unknown FPU carry experiment");
    require(flags==0x1234u,"an invalid diagnostic policy changes no numeric flags");
    Memory memory;const auto rom=m20::build_firmware().rom;reset_native_ee_fixture(memory,rom);
    EEState state;state.set_cp0(Cop0::Status,kStatusCU1|kStatusERL);
    const auto multiply=[&](std::uint32_t a,std::uint32_t b) {
        state.fpr[1]=a;state.fpr[2]=b;state.unknown_fpr_bits[1]=state.unknown_fpr_bits[2]=0;
        state.pc=0x80001000;memory.write32(state.pc,math(2,3,1,2));std::uint64_t count=0;
        ExecutionContext context{state,memory,count,1,0xFFFFFFFFu};compiled_fixture_step(context);
    };
    multiply(0x40000000u,0x3FC90F80u);require(state.unknown_fpr_bits[3],"ordinary scalar multiplication keeps the unmeasured early carry unresolved");
    rejects([&]{memory.diagnostic_ee_early_digit_carry();},"precede scalar FPU");
    memory.set_clock(&state.retired_instructions,&state.stall_cycles);
    const auto uncertain_state=checkpoint_state_sha256(state,memory);
    rejects([&]{refine_checkpoint_scalar_carry(state,memory);},"certified numeric history");
    require(checkpoint_state_sha256(state,memory)==uncertain_state,"failed refinement does not mutate the checkpoint");
    state.unknown_fpr_bits.fill(0);state.fpr_affine={};
    rejects([&]{refine_checkpoint_scalar_carry(state,memory);},"certified numeric history");
    memory=Memory{};reset_native_ee_fixture(memory,rom);state=EEState{};state.set_cp0(Cop0::Status,kStatusCU1|kStatusERL);
    memory.set_clock(&state.retired_instructions,&state.stall_cycles);
    multiply(0x40000000u,0x3F800000u);
    rejects([&]{memory.diagnostic_ee_early_digit_carry();},"precede scalar FPU");
    refine_checkpoint_scalar_carry(state,memory);
    multiply(0x40000000u,0x3FC90F80u);
    require(state.fpr[3]==0x40490F7Fu && !state.unknown_fpr_bits[3],
        "certified known arithmetic permits explicit refinement before the first uncertain result");
    reset_native_ee_fixture(memory,rom);memory.diagnostic_ee_early_digit_carry();
    state=EEState{};state.pc=0x80001000;state.set_cp0(Cop0::Status,kStatusCU1|kStatusERL);
    memory.set_clock(&state.retired_instructions,&state.stall_cycles);
    BootOptions stop;stop.stop_at=state.pc;const auto boundary=run_ee_boot(state,memory,nullptr,stop);
    require(boundary.outcome==BootOutcome::StopAddress,"scalar checkpoint did not reach its safe boundary: "+boundary.reason);
    CheckpointContext checkpoint;checkpoint.scenario_sha256=std::string(64,'c');
    const auto path=std::filesystem::temp_directory_path()/("ps2n-ee-carry-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".checkpoint");
    save_checkpoint(path,state,memory,boundary,checkpoint);reset_native_ee_fixture(memory,rom);
    memory.set_clock(&state.retired_instructions,&state.stall_cycles);
    (void)load_checkpoint(path,state,memory,checkpoint);std::filesystem::remove(path);
    multiply(0x40000000u,0x3FC90F80u);
    require(state.fpr[3]==0x40490F7Fu && !state.unknown_fpr_bits[3],
        "the explicit scalar experiment applies the previously exhaustively checked external-reference early-digit class");
    multiply(0x40400000u,0x3FC90F80u);
    require(state.fpr[3]==0x4096CB9Fu && !state.unknown_fpr_bits[3],
        "the scalar experiment also transfers the same low-column hypothesis to a coarse three-halves significand");
    multiply(0x3FC90F80u,0x40000000u);
    require(state.fpr[3]==0x40490F80u && !state.unknown_fpr_bits[3],"the experimental multiplier preserves operand asymmetry");
    multiply(0x333BBD2Eu,0x333BBD2Eu);require(state.unknown_fpr_bits[3],"the scalar experiment does not choose arbitrary unresolved products");
    reset_native_ee_fixture(memory,rom);multiply(0x40000000u,0x3FC90F80u);
    require(state.unknown_fpr_bits[3],"cold reset restores strict scalar arithmetic");
}
void copied_numeric_identity() {
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
    EEState state;state.set_cp0(Cop0::Status,kStatusCU1|kStatusERL);
    const auto step=[&](std::uint32_t word) {
        state.pc=0x80001000;memory.write32(state.pc,word);std::uint64_t count=0;
        ExecutionContext context{state,memory,count,1,0xFFFFFFFFu};compiled_fixture_step(context);
    };
    const auto source=[&](std::uint32_t value,std::uint32_t unknown,std::vector<std::uint32_t> values) {
        state.fpr[1]=value;state.unknown_fpr_bits[1]=unknown;
        state.fpr_affine[1]={memory.allocate_fpu_origin(),0,0,false,value,unknown,values.front(),values.back(),
            std::make_shared<const std::vector<std::uint32_t>>(std::move(values))};
        step(math(6,2,1,0)); // MOV.S preserves the same numeric origin.
    };
    source(0x3F800000u,0x80000000u,{0x3F800000u,0xBF800000u});
    step(math(2,3,1,2));
    require(state.fpr[3]==0x3F800000u && !state.unknown_fpr_bits[3],
        "multiplying copied signed unit values squares the same input, never independent signs");
    state.fpr_affine[2].origin=memory.allocate_fpu_origin();step(math(2,3,1,2));
    require(state.unknown_fpr_bits[3]==0x80000000u,"equal bit hulls with independent origins retain both product signs");
    source(0,0x7F800000u,{0x3F800000u,0x40000000u});
    step(math(3,3,1,2));
    require(state.fpr[3]==0x3F800000u && !state.unknown_fpr_bits[3],
        "division by an unchanged numeric copy evaluates only concrete x/x pairs");
    state.fpr_affine[2].origin=memory.allocate_fpu_origin();step(math(3,3,1,2));
    require(state.unknown_fpr_bits[3],"independent equal domains still admit distinct quotients");
}
void vector_memory_bounds() {
    for(const bool retired_path:{false,true}) {
        Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
        memory.diagnostic_data_cache();memory.set_cache_config(0x10443u);
        EEState state;state.set_cp0(Cop0::Status,kStatusCU1|(1u<<30u));
        state.set_cp0(Cop0::Config,0x10443u);state.set_reg32(1,0x80002000u);
        const auto step=[&](std::uint32_t word) {
            state.pc=0xA0001000;memory.write32(state.pc,word);std::uint64_t count=0;
            ExecutionContext context{state,memory,count,1,0xFFFFFFFFu};
            if(retired_path || (word>>26u)==0x31u)compiled_fixture_step(context);
            else if((word>>26u)==0x3Eu)ee::exec<MipsOpcode::Sqc2>(state,memory,state.pc,word);
            else if((word>>26u)==0x12u)ee::exec<MipsOpcode::Qmtc2>(state,memory,state.pc,word);
            else ee::exec<MipsOpcode::Lqc2>(state,memory,state.pc,word);
        };
        // A real uncertain VU product has a much tighter numeric interval
        // than its bit hull. All four lanes must survive both cache aliases.
        memory.write_vu_vector(1,{{0x3F8000013F800001ull,0},{0x3F8000013F800001ull,0}});
        memory.write_vu_vector(2,{{0x3F8000033F800003ull,0},{0x3F8000033F800003ull,0}});
        memory.vu_product(3,1,2,15,4,false,false);
        const auto source=memory.observe_vu_vector(3);
        const auto bounds=memory.observe_vu_vector_bounds(3,0);
        require(bounds && (*bounds)[1]-(*bounds)[0]==1u && source.low.unknown,"fixture derives a narrow VU product");
        step(0xF8230000u); // SQC2 vf3,0(at)
        step(0xC4240000u); // LWC1 f4,0(at)
        require(state.fpr_affine[4].origin && state.fpr_affine[4].minimum==(*bounds)[0] &&
            state.fpr_affine[4].maximum==(*bounds)[1],"LWC1 receives the proved VU interval from dirty cache");
        require(memory.diagnostic_ram(0x2000,8).unknown==0,"SQC2 does not expose dirty data to DMA before writeback");
        memory.data_cache_operation(0x18,0x80002000u,{});
        state.set_reg32(1,0xA0002000u);step(0xD8240000u); // LQC2 vf4,0(at)
        for(unsigned lane=0;lane<4;++lane)
            require(memory.observe_vu_vector_bounds(4,lane)==bounds,"all four lane bounds survive SQC2/writeback/LQC2");
        const auto copy=memory.observe_vu_vector(4);
        require(copy.low.value==source.low.value && copy.low.unknown==source.low.unknown &&
            copy.high.value==source.high.value && copy.high.unknown==source.high.unknown,"bound transport preserves every raw bit and uncertainty mask");
        memory.store128_observed(0xA0002000u,source);step(0xD8240000u);
        require(!memory.observe_vu_vector_bounds(4,0),"an independent identical raw overwrite cannot revive a VU certificate");
        step(0xD8200000u);require(!memory.observe_vu_vector_bounds(0,0),"LQC2 cannot attach bounds to constant VF00");
        state.set_reg128(2,0,0x123456789ABCDEF0ull);
        state.set_reg64_candidates(2,{0x3F8000033F800003ull,0x3F8000043F800004ull});
        step(0x48A22800u); // QMTC2 vf5,v0
        for(unsigned lane=0;lane<2;++lane)
            require(memory.observe_vu_vector_bounds(5,lane)==FloatBounds{0x3F800003u,0x3F800004u},
                "QMTC2 preserves both finite low-64-bit lanes without guessing a concrete candidate");
        require(memory.observe_vu_vector(5).high.value==0x123456789ABCDEF0ull && !memory.observe_vu_vector_bounds(5,2),
            "QMTC2 keeps the raw upper GPR half without inventing its provenance");
        state.set_reg64_observed(2,state.gpr[2].low,state.unknown_gpr_bits[2]);step(0x48A22800u);
        require(!memory.observe_vu_vector_bounds(5,0),"an opaque identical GPR write invalidates old vector bounds");
    }
}
void profiling_state_equivalence() {
    Memory memories[2];EEState states[2];profiling::Profile profile;
    BootOptions options;options.max_instructions=2;options.tail_instructions=0;
    std::string expected;
    for(unsigned pass=0;pass<2;++pass) {
        auto& memory=memories[pass];auto& state=states[pass];
        reset_native_ee_fixture(memory,m20::build_firmware().rom);
        state.pc=0x80001000u;state.set_cp0(Cop0::Status,kStatusCU1|kStatusERL);
        state.fpr[1]=0x3F800001u;state.fpr[2]=0x3F800003u;
        memory.write32(state.pc,math(2,3,1,2));memory.write32(state.pc+4u,math(6,4,3,0));
        options.profile=pass ? &profile : nullptr;
        if(pass && !profiling::available) {
            const auto before=checkpoint_state_sha256(state,memory);
            rejects([&]{(void)run_ee_boot(state,memory,&trace_module,options);},"PS2N_ENABLE_PROFILING");
            require(checkpoint_state_sha256(state,memory)==before,"a disabled profiler refuses before changing guest state");
            return;
        }
        const auto report=run_ee_boot(state,memory,&trace_module,options);
        require(report.retired==2,"profiled execution retires the same instructions");
        const auto hash=checkpoint_state_sha256(state,memory);
        if(!pass)expected=hash;
        else require(hash==expected,"optional host component timers preserve the complete modeled state");
    }
    std::uint64_t total=0;for(const auto ns:profile.exclusive_ns)total+=ns;
    require(total==profile.inclusive_ns[0] && total>0 && profile.calls[0]==1,
        "exclusive phases partition the session time without double-counting nested work");
    require(profile.calls[static_cast<unsigned>(profiling::Phase::Memory)] &&
        profile.calls[static_cast<unsigned>(profiling::Phase::Bounds)],"profiling exercises memory and numeric-proof scopes");
}
}
void division_ranges() {
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
    EEState state;state.set_cp0(Cop0::Status,kStatusCU1);
    std::uint64_t executed=0;ExecutionContext context{state,memory,executed,100,0xFFFFFFFFu};
    const auto step=[&]{state.pc=0x80001000u;memory.write32(state.pc,math(3,4,1,2));compiled_fixture_step(context);};
    constexpr std::uint32_t first=0x3C05C405u,last=0x3C060536u;
    state.fpr[1]=0x3C040000u;state.unknown_fpr_bits[1]=0x3FFFFu;
    state.fpr_affine[1]={memory.allocate_fpu_origin(),0,120,false,state.fpr[1],state.unknown_fpr_bits[1],first,last,{}};
    auto divisors=std::make_shared<std::vector<std::uint32_t>>();
    for(unsigned i=0;i<17;++i)divisors->push_back(0xC005D36Eu+i*29u);
    state.fpr[2]=0xC005D000u;state.unknown_fpr_bits[2]=0x7FFu;
    state.fpr_affine[2]={memory.allocate_fpu_origin(),0,128,false,state.fpr[2],state.unknown_fpr_bits[2],
        divisors->front(),divisors->back(),divisors};
    state.fcr31=0x0183C079u;step();
    require(state.fpr_affine[4].ranges && state.unknown_fpr_bits[4] && state.fcr31==0x0180C079u,
        "large DIV.S domains retain quotient uncertainty and preserve unrelated FCR31 bits");
    for(std::uint32_t x=first;x<=last;++x)for(const auto y:*divisors) {
        std::uint32_t flags=0;const auto result=ee_fpu_divide(x,y,flags);
        bool contained=false;for(const auto& range:*state.fpr_affine[4].ranges)contained|=result>=range[0] && result<=range[1];
        require(contained && !flags && !((result^state.fpr[4])&~state.unknown_fpr_bits[4]),
            "every scalar SRT quotient is inside the stored range and known-bit mask");
    }
    const auto value=state.fpr[4],unknown=state.unknown_fpr_bits[4],flags=state.fcr31;
    state.fpr_affine[1].maximum=first+65536u;
    rejects(step,"cannot certify");
    require(state.fpr[4]==value && state.unknown_fpr_bits[4]==unknown && state.fcr31==flags,
        "oversized division domains refuse without publishing partial values or flags");
    state.fpr_affine[1].maximum=last;
    state.fpr[2]=0;state.unknown_fpr_bits[2]=0x3F800000u;
    state.fpr_affine[2]={memory.allocate_fpu_origin(),0,0,false,0,0x3F800000u,0,0x3F800000u,
        std::make_shared<const std::vector<std::uint32_t>>(std::initializer_list<std::uint32_t>{0,0x3F800000u})};
    rejects(step,"cannot certify");
    require(state.fpr[4]==value && state.unknown_fpr_bits[4]==unknown && state.fcr31==flags,
        "possible divide-by-zero flags still refuse without changing the destination");
    for(unsigned index=1;index<=2;++index) {
        state.fpr[index]=index==1 ? 0x3F800000u : 0x40000000u;state.unknown_fpr_bits[index]=0x3FFFu;
        state.fpr_affine[index]={memory.allocate_fpu_origin(),0,0,false,state.fpr[index],0x3FFFu,
            state.fpr[index],state.fpr[index]+8192u,{}};
    }
    rejects(step,"cannot certify");
    require(state.fpr[4]==value && state.unknown_fpr_bits[4]==unknown && state.fcr31==flags,
        "the pair-count limit refuses oversized Cartesian products before publishing partial results");
}
void addition_range_classes() {
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
    EEState state;state.set_cp0(Cop0::Status,kStatusCU1);
    auto ranges=std::make_shared<std::vector<FloatBounds>>();
    for(unsigned exponent=100;exponent<=164;++exponent)ranges->push_back({exponent<<23u,exponent<<23u});
    for(unsigned index=1;index<=2;++index) {
        state.fpr[index]=0;state.unknown_fpr_bits[index]=0x7F800000u;
        state.fpr_affine[index]={memory.allocate_fpu_origin(),0,0,false,0,0x7F800000u,
            ranges->front()[0],ranges->back()[1],{},ranges};
    }
    state.pc=0x80001000u;state.fcr31=0x0183C079u;memory.write32(state.pc,math(0,3,1,2));
    std::uint64_t executed=0;ExecutionContext context{state,memory,executed,100,0xFFFFFFFFu};compiled_fixture_step(context);
    require(state.fpr_affine[3].ranges && state.fcr31==0x01830079u,
        "more than 4096 sign/exponent class pairs retain bounded sums and common flags");
    for(const auto& a:*ranges)for(const auto& b:*ranges) {
        std::uint32_t flags=0;const auto value=ee_fpu_add(a[0],b[0],flags);
        bool contained=false;for(const auto& result:*state.fpr_affine[3].ranges)contained|=value>=result[0] && value<=result[1];
        require(contained && !flags && !((value^state.fpr[3])&~state.unknown_fpr_bits[3]),
            "every concrete sum of the 65-by-65 exponent classes remains represented");
    }
}
int main() try {
    division_ranges();
    addition_range_classes();
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
    integer_domains(memory);
    min_max(memory);
    floating_trace(memory);
    scalar_carry_experiment();
    copied_numeric_identity();
    vector_memory_bounds();
    profiling_state_equivalence();
    EEState state;state.set_cp0(Cop0::Status,kStatusCU1);
    std::uint64_t executed=0;ExecutionContext context{state,memory,executed,1000,0xFFFFFFFFu};
    const auto step=[&](std::uint32_t word){state.pc=0x80001000;memory.write32(state.pc,word);compiled_fixture_step(context);};
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
    step(math(3,11,6,2));
    require(state.fpr_affine[11].candidates && state.unknown_fpr_bits[11],
        "DIV.S retains a certified finite quotient domain from an uncertain raw operand");
    const auto conversion_flags=state.fcr31;step(math(0x24,11,6,0));
    require(state.fpr[11]==1 && !state.unknown_fpr_bits[11] && state.fcr31==conversion_flags,
        "CVT.W.S eliminates fraction uncertainty only when every allowed value truncates to the same integer");
    state.fcr31=0x0183C079u;step(math(4,11,0,6));
    require(state.fcr31==0x0180C079u,"bounded SQRT.S clears I/D causes while preserving unrelated flags");
    for(const auto input:*state.fpr_affine[6].candidates) {
        const auto root=ee_fpu_sqrt(input);require(root.has_value(),"concrete SQRT.S reference is available");
        require(!((*root^state.fpr[11])&~state.unknown_fpr_bits[11]),
            "bounded SQRT.S retains every concrete root in the destination known-bit mask");
    }
    for(const auto word : {math(0x1C,11,6,2),math(0x18,0,6,2)}) {
        state.fpr[11]=0x12345678;state.unknown_fpr_bits[11]=0;state.fcr31=0x0183C079;
        rejects([&]{step(word);},word==math(0x1C,11,6,2) ? "not certified" : "unknown FPR bits");
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
    step(0xC4210000);step(math(1,0,4,1));
    require(state.unknown_fpr_bits[1] && state.unknown_fpr_bits[0] && state.fpr_affine[0].ranges,
        "an aliased opaque overwrite loses origin correlation: only a conservative interval union survives, never a fabricated exact cancellation");
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
    const auto finite_register=[&](unsigned index,std::vector<std::uint32_t> values) {
        std::sort(values.begin(),values.end());std::uint32_t all=~0u,any=0;
        for(const auto value:values){all&=value;any|=value;}
        state.fpr[index]=all;state.unknown_fpr_bits[index]=all^any;
        state.fpr_affine[index]={memory.allocate_fpu_origin(),0,0,false,all,all^any,values.front(),values.back(),
            std::make_shared<const std::vector<std::uint32_t>>(std::move(values))};
    };
    std::vector<std::uint32_t> tiny,larger{0};
    for(unsigned n=0;n<30;++n)tiny.push_back(0x0E457440u+n);
    for(unsigned n=0;n<66;++n)larger.push_back(0xAA9CCC40u+n);
    finite_register(1,tiny);finite_register(2,larger);step(math(1,10,1,2));
    std::vector<std::uint32_t> expected;
    for(const auto a:tiny)for(const auto b:larger) {
        std::uint32_t flags=0;expected.push_back(ee_fpu_add(a,b^0x80000000u,flags));
        require(!flags,"every concrete normal residual has the same exception class");
    }
    std::sort(expected.begin(),expected.end());expected.erase(std::unique(expected.begin(),expected.end()),expected.end());
    require(state.fpr_affine[10].candidates && *state.fpr_affine[10].candidates==expected,
        "all 2010 concrete input pairs produce the exact retained set despite repeated outputs, without truncating its candidates");
    for(unsigned n=30;n<65;++n)tiny.push_back(0x0E457440u+n);
    finite_register(1,tiny);state.fcr31=0x0183C079u;step(math(1,10,1,2));
    require(state.fpr_affine[10].ranges && state.fpr_affine[10].ranges->size()==2 && state.fcr31==0x01830079u,
        "larger finite domains use two certified sign/exponent intervals with proven flags instead of truncating candidates");
    for(const auto a:tiny)for(const auto b:larger) {
        std::uint32_t flags=0;const auto value=ee_fpu_add(a,b^0x80000000u,flags);
        bool contained=false;for(const auto& range:*state.fpr_affine[10].ranges)contained|=value>=range[0] && value<=range[1];
        require(contained && !flags,"every concrete result is contained in its certified output interval");
    }
    state.set_reg64(1,0x80002110);step(0xE42A0000u);step(0xC42B0000u);
    require(state.fpr_affine[11].ranges==state.fpr_affine[10].ranges,"SWC1/LWC1 preserves the immutable interval union through RAM");
    step(math(7,11,11,0));step(math(5,11,11,0));
    state.fpr[13]=0x3F800000u;state.unknown_fpr_bits[13]=0;step(math(0,12,11,13));
    require(state.fpr[12]==0x3F800000u && !state.unknown_fpr_bits[12],
        "all interval classes collapse to the same known result under dominated addition after NEG/ABS");
    const auto crossing=vu_float_add({0x3F7FFFFEu,1},{0x33800000u,0});
    const auto exponent_bounds=vu_float_add({0x3F7FFFFEu,1},{0x33800000u,0},{},{},true);
    require(!crossing && exponent_bounds && exponent_bounds->bounds==FloatBounds{0x3F7FFFFFu,0x3F800000u},
        "exponent-spanning addition is opt-in and preserves both sides of the normalization boundary");
    state.fpr[1]=0x3F800000u;state.unknown_fpr_bits[1]=0x1FFu;
    state.fpr_affine[1]={memory.allocate_fpu_origin(),0,0,false,state.fpr[1],state.unknown_fpr_bits[1],0x3F800000u,0x3F800100u,{},
        std::make_shared<const std::vector<FloatBounds>>(std::initializer_list<FloatBounds>{{0x3F800000u,0x3F800100u}})};
    state.fpr[2]=0x3F800080u;state.unknown_fpr_bits[2]=0;state.fcr31=0x0183C079u;
    step(math(1,10,1,2));
    require(state.fpr_affine[10].ranges && state.fcr31==0x01830079u,
        "a normal guard-column quantum proves flags even when the interval crosses exact zero");
    for(unsigned n=0;n<=256;++n) {
        std::uint32_t flags=0;const auto value=ee_fpu_add(0x3F800000u+n,0xBF800080u,flags);
        bool contained=false;for(const auto& range:*state.fpr_affine[10].ranges)contained|=value>=range[0] && value<=range[1];
        require(contained && !flags,"every concrete negative, zero and positive cancellation remains inside the output union");
    }
    state.fpr[1]=0x0B800000u;state.unknown_fpr_bits[1]=0x1FFu;
    state.fpr_affine[1]={memory.allocate_fpu_origin(),0,0,false,state.fpr[1],state.unknown_fpr_bits[1],0x0B800000u,0x0B800100u,{},
        std::make_shared<const std::vector<FloatBounds>>(std::initializer_list<FloatBounds>{{0x0B800000u,0x0B800100u}})};
    state.fpr[2]=0x0B800080u;state.unknown_fpr_bits[2]=0;state.fcr31=0x0183C079u;
    const auto before=state.fpr[10],before_mask=state.unknown_fpr_bits[10];
    rejects([&]{step(math(1,10,1,2));},"cannot certify");
    require(state.fpr[10]==before && state.unknown_fpr_bits[10]==before_mask && state.fcr31==0x0183C079u,
        "a cancellation interval with possible underflow still refuses without publishing a result or flags");
    state.fpr[4]=0x3F800000u;state.unknown_fpr_bits[4]=0x800001FFu;
    state.fpr_affine[4]={memory.allocate_fpu_origin(),0,0,false,state.fpr[4],state.unknown_fpr_bits[4],0x3F800000u,0xBF800101u,{},
        std::make_shared<const std::vector<FloatBounds>>(std::initializer_list<FloatBounds>{{0x3F800000u,0x3F8001FFu},{0xBF800100u,0xBF800101u}})};
    step(math(5,4,4,0));require(state.fpr_affine[4].maximum==0x3F8001FFu && !state.fpr_affine[4].negated,
        "ABS keeps the full outer range when sign folding produces nested intervals, and replaces the signed affine origin");
    state.fpr[12]=0xBF3850F0u;state.unknown_fpr_bits[12]=7;state.fpr_affine[12]={};
    state.fpr[13]=0x30000000u;state.unknown_fpr_bits[13]=0x87FFFF80u;state.fpr_affine[13]={};state.fcr31=0x0183C079u;
    step(math(2,3,12,13));
    require(state.fpr_affine[3].ranges && state.fcr31==0x01830079u,
        "opaque sign/exponent uncertainty is partitioned into bounded normal product classes with common flags");
    for(const auto source:{0x30000000u,0x307FFF80u,0x37800000u,0xB7800000u,0xB7FFFF80u}) {
        for(const auto fraction:{0u,7u}) {
            std::uint32_t flags=0;const auto value=ee_fpu_multiply(0xBF3850F0u|fraction,source,flags);
            if(!value)continue;
            bool contained=false;for(const auto& range:*state.fpr_affine[3].ranges)contained|=*value>=range[0] && *value<=range[1];
            require(contained && !flags,"each concrete certified signed/exponent product remains inside the complete output union");
        }
    }
    std::cout<<"EE floating bounds, dominated addition, raw register/RAM transport, sign operations, consumer refusals and comparison parity passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
