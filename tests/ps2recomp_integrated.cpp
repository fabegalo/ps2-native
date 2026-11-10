#include "ps2_runtime.h"
#include "hle_fixture.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/hle.hpp"
#include "ps2native/sha256.hpp"
#include <iostream>

using namespace ps2native;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
template<class C> int run(int argc,C** argv)try {
    require(argc==3,"Expected fixture ELF and mode");
    const auto mode=path_utf8(std::filesystem::path(argv[2]));const bool hle=mode=="hle",frame=mode=="frame";
    const auto elf=parse_ee_elf(read_file(std::filesystem::path(argv[1])));
    const auto stop=hle?hle_test::program().stop:frame?direct_test::frame().stop:0xFFFFFFF0u;
    PS2Runtime runtime;
    PS2Runtime referenceRuntime;auto* reference=&referenceRuntime.memory();auto& reference_cpu=referenceRuntime.cpu();
    if(hle)runtime.prepare_hle_elf(elf,&sdk_aot_module());
    else runtime.prepare_direct_elf(elf,&sdk_aot_module());
    if(mode!="sdk-mmi")referenceRuntime.prepare_direct_elf(elf,&aot_module());
    std::unique_ptr<HleKernel> reference_kernel;
    if(hle)reference_kernel=std::make_unique<HleKernel>(referenceRuntime);
    if(!hle && !frame){runtime.cpu().set_reg32(31,stop);reference_cpu.set_reg32(31,stop);}
    if(mode=="sdk-mmi") {
        auto& state=runtime.cpu();
        const auto initialize=[](EEState& state) {
            state.set_reg128_observed(2,0x2222222211111110ull,0x4444444433333331ull,1,2);
            state.set_reg128(6,0x1122334455667788ull,0xDEADBEEFDEADBEEFull);
            state.set_reg64_candidates(6,{0x1122334455667788ull,0x1122334455667789ull});
            state.gpr_candidates[6].bounds[0]=std::array{0x55667788u,0x55667789u};
            state.set_reg64(7,0);
            state.set_reg128_observed(8,0xAABBCCDDCCBBAA00ull,0x8877665544332211ull,0,0x100);
            state.gpr_candidates[8].bounds[2]=std::array{0x44332211u,0x44332311u};
            state.set_reg128(9,0xFFEEDDCCBBAA0098ull,0xAABBCCDD12345678ull);
            state.set_reg64_candidates(10,{0x100000000ull,0x200000000ull});
            state.set_reg128_observed(11,0,0x0123456789ABCDEEull,0,1);
            state.set_reg128(12,42,43);
            state.set_reg64_observed(13,0,1);
        };
        initialize(state);
        BootOptions options;options.max_instructions=1;options.stop_at=stop;options.max_trace_events=0;options.tail_instructions=0;
        for(unsigned n=0;n<10;++n) {
            const auto report=runtime.run(&sdk_aot_module(),options);
            require(report.outcome!=BootOutcome::BlockPoint,report.reason);
            require(report.aot_instructions==report.retired && !report.interpreted_instructions,"Packed SDK code remains AOT");
            if(!n)require(state.gpr[3].low==0x2222222233333331ull && state.gpr[3].high==0x4444444411111110ull &&
                state.unknown_gpr_bits[3]==2 && state.unknown_gpr_high_bits[3]==1,"PEXEW preserves all lane values and unknown masks");
            if(n==1)require(state.gpr[3].low==0x1111111022222222ull && state.gpr[3].high==0x4444444433333331ull &&
                state.unknown_gpr_bits[3]==0x100000000ull && state.unknown_gpr_high_bits[3]==2,"PROT3W supports destination/source aliasing");
            if(n==4)require(state.gpr[8].low==0x1122334455667788ull && state.unknown_gpr_bits[8]==1 &&
                state.gpr[8].high==0x8877665544332211ull && state.unknown_gpr_high_bits[8]==0x100 &&
                state.gpr_candidates[8].bounds[0]==state.gpr_candidates[6].bounds[0] &&
                state.gpr_candidates[8].bounds[2]==std::optional{std::array{0x44332211u,0x44332311u}} &&
                state.reg64_candidates(8) && *state.reg64_candidates(8)==*state.reg64_candidates(6),
                "SDK MOVZ copies low-64 values and certificates while preserving all destination high-64 state");
            if(n==5)require(state.gpr[11].low==0xFFEEDDCCBBAA0098ull && state.gpr[11].high==0x0123456789ABCDEEull &&
                state.unknown_gpr_high_bits[11]==1,"SDK MOVN uses the full 64-bit condition and preserves the upper half");
            if(n==6)require(state.gpr[12].low==42 && state.gpr[12].high==43,"A false SDK conditional move preserves its destination");
        }
        require(state.pc==stop && !state.gpr[4].low && !state.gpr[4].high && !state.gpr[0].low &&
            state.gpr[5].low==0x2222222233333331ull && state.gpr[5].high==0x4444444411111110ull,
            "SDK permutations preserve the zero register and execute in a branch delay slot");
        runtime.prepare_direct_elf(elf,&sdk_aot_module());initialize(state);
        state.set_reg64_observed(7,0,1);options.max_instructions=10;
        const auto unknown=runtime.run(&sdk_aot_module(),options);
        require(unknown.outcome==BootOutcome::BlockPoint && unknown.retired==4 && unknown.pc==elf.entry+16 &&
            state.gpr[8].low==0xAABBCCDDCCBBAA00ull && unknown.reason.find("unknown condition")!=std::string::npos,
            "Ambiguous SDK conditional moves must stop before changing the destination: retired="+std::to_string(unknown.retired)+
            " pc="+std::to_string(state.pc)+" reason="+unknown.reason);
        runtime.prepare_direct_elf(elf,&sdk_aot_module());
        runtime.memory().write32(elf.entry,0xFFFFFFFFu);
        const auto changed=runtime.run(&sdk_aot_module(),options);
        require(changed.outcome==BootOutcome::BlockPoint && !changed.retired && changed.reason.find("code modified")!=std::string::npos,
            "SDK-only instructions must retain code invalidation checks");
        std::cout<<"SDK register emission: packed lanes, aliases, delay slots, MOVZ/MOVN low-64 preservation, uncertainty and code invalidation passed\n";
        return 0;
    }
    if(mode=="sdk-float") {
        auto& state=runtime.cpu();
        state.set_cp0(Cop0::Status,state.cp0(Cop0::Status)|kStatusCU1);
        state.fpr[1]=0x45033000u; // 2099.0f: first previously uncertified table product.
        state.fpr[2]=0x39C90FD8u;
        state.fpr[0]=0x41800000u; // SQRT must read FT, never this FS value.
        BootOptions options;options.max_instructions=1;options.stop_at=stop;
        options.max_trace_events=0;options.tail_instructions=0;
        std::uint64_t instructions=0;
        for(unsigned slice=0;slice<16;++slice) {
            const auto report=runtime.run(&sdk_aot_module(),options);instructions+=report.retired;
            require(report.outcome!=BootOutcome::BlockPoint,report.reason);
            require(report.aot_instructions==report.retired && !report.interpreted_instructions,"SDK float execution must stay native");
            if(report.outcome==BootOutcome::StopAddress)break;
        }
        require(state.pc==stop && instructions==4 && state.fpr[3]==0x3F4E119Du && state.fpr[4]==0x3F65AE7Au,
            "SDK-generated MUL.S/SQRT.S must use canonical registers and SDK arithmetic across one-instruction slices");
        runtime.prepare_direct_elf(elf,&sdk_aot_module());
        runtime.cpu().set_cp0(Cop0::Status,runtime.cpu().cp0(Cop0::Status)|kStatusCU1);
        runtime.cpu().fpr[1]=0x45033000u;runtime.cpu().fpr[2]=0x39C90FD8u;
        runtime.cpu().unknown_fpr_bits[1]=1u;
        const auto unknown=runtime.run(&sdk_aot_module(),options);
        require(unknown.outcome==BootOutcome::BlockPoint && !unknown.retired && runtime.cpu().unknown_fpr_bits[1]==1u,
            "SDK float view must refuse unknown source bits without retiring or erasing them");
        std::cout<<"SDK scalar emission: native MUL/SQRT, canonical state, sliced timing and unknown-input refusal passed\n";
        return 0;
    }
    std::vector<std::string> frames[2];
    BootOptions expected;expected.max_instructions=300000;expected.stop_at=stop;expected.max_trace_events=32;expected.tail_instructions=0;
    expected.on_frame=[&](const Framebuffer& f){frames[0].push_back(sha256_hex(f.rgba));};
    const auto baseline=run_ee_boot(reference_cpu,*reference,&aot_module(),expected);
    if(mode=="partial") {
        const auto actual=runtime.run(&sdk_aot_module(),expected);
        require(baseline.outcome==BootOutcome::BlockPoint && actual.outcome==BootOutcome::BlockPoint &&
            actual.pc==elf.entry+4 && actual.retired==1 && actual.aot_instructions==1 && !actual.interpreted_instructions &&
            runtime.cpu().reg32(2)==42 && find_aot_block(sdk_aot_module(),elf.entry+4)==nullptr &&
            find_aot_block(sdk_aot_module(),elf.entry+8)!=nullptr,
            "Missing translation must stop before the instruction, without skipping to later native code");
        require(checkpoint_state_sha256(runtime.cpu(),runtime.memory())==checkpoint_state_sha256(reference_cpu,*reference),
            "Partial SDK translation must preserve the same complete stop state as the native baseline");
        std::cout<<"SDK partial native module stops exactly at the missing translation; state and retirement agree\n";
        return 0;
    }
    require(baseline.outcome==BootOutcome::StopAddress,baseline.reason);
    auto options=expected;options.max_instructions=17;
    options.on_frame=[&](const Framebuffer& f){frames[1].push_back(sha256_hex(f.rgba));};
    std::uint64_t retired=0;
    for(unsigned slice=0;slice<18000;++slice) {
        const auto actual=runtime.run(&sdk_aot_module(),options);retired+=actual.retired;
        require(actual.outcome!=BootOutcome::BlockPoint,actual.reason);
        require(actual.aot_instructions==actual.retired && !actual.interpreted_instructions,"SDK execution must remain entirely native");
        if(actual.outcome==BootOutcome::StopAddress)break;
        require(slice<17999,"SDK execution exceeded its fixed native budget");
    }
    require(retired==baseline.retired && runtime.cpu().cycles()==reference_cpu.cycles() && runtime.cpu().pc==stop,"SDK instruction counts, clocks and stop boundary differ");
    if(mode=="boundaries")require(retired==134 && runtime.cpu().reg32(2)==42,
        "Branch delay crossing a native chunk and an annulled likely slot must preserve instructions and timing");
    for(unsigned r=0;r<32;++r)require(runtime.cpu().gpr[r].low==reference_cpu.gpr[r].low && runtime.cpu().gpr[r].high==reference_cpu.gpr[r].high,"SDK register state differs");
    if(hle) {
        require(runtime.memory().hle_kernel()->syscall_count()==15 && reference_kernel->syscall_count()==15,"SDK HLE service dispatch differs");
        for(unsigned at=0;at<0x120;at+=4)require(runtime.memory().diagnostic_ram(hle_test::data+at,4).value==reference->diagnostic_ram(hle_test::data+at,4).value,"SDK HLE guest memory differs");
    } else require(checkpoint_state_sha256(runtime.cpu(),runtime.memory())==checkpoint_state_sha256(reference_cpu,*reference),"SDK complete console state differs from the prior native backend");
    if(frame) {
        const auto image=runtime.memory().gs().scanout(runtime.cpu().cycles());
        require(image.width==64 && image.height==32 && sha256_hex(image.rgba)=="402316200a4786b2d6d81a071e89e2adbd98f25d804583790329ae3cb6f436a3","SDK GS golden pixels differ");
        require(!frames[0].empty() && frames[0]==frames[1],"SDK VBLANK presentation differs across native slices");
    }
    runtime.prepare_direct_elf(elf,&sdk_aot_module());
    runtime.memory().write32(elf.entry,0xFFFFFFFFu);
    const auto changed=runtime.run(&sdk_aot_module(),options);
    require(changed.outcome==BootOutcome::BlockPoint && !changed.retired && changed.reason.find("code modified")!=std::string::npos,"SDK must reject modified instruction bytes before execution");
    std::cout<<"SDK integrated "<<mode<<": "<<retired<<" native instructions; shared state/timing, equivalence and invalidation passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return run(argc,argv);}
#else
int main(int argc,char** argv){return run(argc,argv);}
#endif
