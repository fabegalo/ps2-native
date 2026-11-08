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
    auto reference=std::make_unique<Memory>();EEState reference_cpu;
    if(hle)runtime.prepare_hle_elf(elf,&sdk_aot_module());
    else runtime.prepare_direct_elf(elf,&sdk_aot_module());
    prepare_direct_elf(elf,&aot_module(),reference_cpu,*reference);
    std::unique_ptr<HleKernel> reference_kernel;
    if(hle)reference_kernel=std::make_unique<HleKernel>(reference_cpu,*reference);
    if(!hle && !frame){runtime.cpu().set_reg32(31,stop);reference_cpu.set_reg32(31,stop);}
    std::vector<std::string> frames[2];
    BootOptions expected;expected.max_instructions=300000;expected.stop_at=stop;expected.max_trace_events=32;expected.tail_instructions=0;
    expected.on_frame=[&](const Framebuffer& f){frames[0].push_back(sha256_hex(f.rgba));};
    const auto baseline=run_ee_boot(reference_cpu,*reference,&aot_module(),expected);
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
