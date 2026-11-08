#include "boot_checks.hpp"
#include "ps2native/boot.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/io.hpp"
#include <iostream>
#include <memory>
using namespace ps2native;
using namespace boot_checks;
template<class C> int run(int argc,C** argv) try {
    if(argc!=2)return 2;
    const auto image=parse_ee_elf(read_file(std::filesystem::path(argv[1])));
    auto memory=std::make_unique<Memory>();EEState state;memory->load(image);state.pc=image.entry|0x80000000u;
    BootOptions options;options.max_instructions=100;options.stop_at=0x80100024u;
    const auto report=run_ee_boot(state,*memory,&aot_module(),options);
    require(report.aot_instructions==13 && report.interpreted_instructions==0,"all EE system instructions executed compiled AOT");
    require(report.outcome==BootOutcome::StopAddress && report.retired==13,"native system path reaches stop after syscall/ERET");
    require(state.reg32(2)==0 && state.reg32(4)==0x1234 && state.reg32(5)==~0u && state.reg32(6)==0 && state.reg32(7)==0x1000,
        "independent expected Count, COP0 transfer, signed arithmetic and PREF results");
    require(state.cp0(Cop0::Epc)==0x80100014u && !(state.cp0(Cop0::Status)&kStatusEXL) &&
        (state.cp0(Cop0::Cause)&0x7Cu)==32u,"exception EPC/Cause and ERET restore original context");
    std::cout<<"EE_SYSTEM_STATE "<<checkpoint_state_sha256(state,*memory)<<'\n';
    memory->load_direct_elf(image);
    std::uint64_t clock=8;memory->set_clock(&clock);
    memory->sync_iop();
    require(memory->iop().cycle()==0 && memory->iop().pc()==0xBFC00000u,
        "bare-metal EE execution leaves an omitted IOP dormant");
    const std::array<std::uint8_t,4> iop_code{};
    auto& iop=const_cast<Iop&>(memory->iop());iop.load_program(iop_code,0,0);
    iop.run_until(1);
    bool refused=false;
    try {memory->sync_iop();}
    catch(const DeviceRefusal& error){refused=std::string(error.what()).find("missing IOP AOT")!=std::string::npos;}
    require(refused && iop.cycle()==0 && iop.native_instructions()==0 && iop.interpreted_instructions()==0,
        "native synchronization reports absent IOP translation before further guest effects");
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return run(argc,argv);}
#else
int main(int argc,char** argv){return run(argc,argv);}
#endif
