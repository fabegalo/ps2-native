#include "hle_fixture.hpp"
#include "boot_checks.hpp"
#include "ps2native/boot.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/hle.hpp"
#include "ps2native/ee_system.hpp"
#include "ps2native/sha256.hpp"
#include <iostream>

using namespace ps2native;
using boot_checks::require;
template<class F> void rejects(F&& operation,const char* text) {
    try{operation();}catch(const std::exception& e){require(std::string(e.what()).find(text)!=std::string::npos,e.what());return;}
    throw std::runtime_error(std::string("Expected refusal: ")+text);
}
template<class C> int run(int argc,C** argv)try {
    require(argc==3,"ELF and scratch directory required");
    const auto fixture=hle_test::program();const auto image=parse_ee_elf(read_file(argv[1]));
    auto memory=std::make_unique<Memory>();EEState state;
#ifdef PS2N_NATIVE_ONLY
    const auto* module=&aot_module();
#else
    const AotModule* module=nullptr;
#endif
    prepare_direct_elf(image,module,state,*memory);
    {
        HleKernel kernel(state,*memory);
        rejects([&]{HleKernel duplicate(state,*memory);},"already owns");
        state.set_reg32(4,0x1234);const auto pc=state.pc;
        rejects([&]{prepare_direct_elf(image,module,state,*memory);},"Detach HLE");
        require(state.pc==pc && state.reg32(4)==0x1234,"refused reset preserves the live CPU");
    }
    prepare_direct_elf(image,module,state,*memory);
    std::uint64_t total=0,aot=0,interpreted=0;
    {
        HleKernel kernel(state,*memory);
        BootOptions options;options.max_instructions=7;options.max_trace_events=16;options.tail_instructions=4;
        options.stop_at=fixture.stop;options.copy_device_histories=false;
        for(unsigned slice=0;slice<1000;++slice) {
            const auto report=run_ee_boot(state,*memory,module,options);
            total+=report.retired;aot+=report.aot_instructions;interpreted+=report.interpreted_instructions;
            require(report.outcome!=BootOutcome::BlockPoint,report.reason);
            if(report.outcome==BootOutcome::StopAddress)break;
            require(slice<999,"bounded HLE execution");
        }
        require(state.pc==fixture.stop && kernel.current_thread_id()==1,"main resumes after child exits");
        require(kernel.syscall_count()==15,"all kernel services executed, including blocking calls");
        require(memory->read32(hle_test::data+0x8C)==1,"semaphore records the waiting child");
        require(memory->read32(hle_test::data+0x100)==1,"blocked semaphore resumes with its ID");
        require(memory->read32(hle_test::data+0x104)==4 && memory->read32(hle_test::data+0x108)==0,"event wakeup publishes bits and success");
        require(memory->read32(hle_test::data+0x10C)==static_cast<std::uint32_t>(-419),"empty semaphore error preserved");
        require(memory->read32(hle_test::data+0x118)==static_cast<std::uint32_t>(-421) && memory->read32(hle_test::data+0x110)==0xDEADBEEF,"event clear and failed poll preserve output");
        require(memory->read32(hle_test::data+0x140)==16,"exited child is dormant");
        require(memory->read32(hle_test::data+0x114)==Memory::kMainRamBytes,"GetMemorySize returns actual memory capacity");
        require(state.cycles()==total && state.count()==total,"thread switches preserve the one console clock and Count");
        const std::filesystem::path path=std::filesystem::path(argv[2])/"unsupported.ps2ncp";
        BootReport boundary;boundary.pc=state.pc;boundary.outcome=BootOutcome::StopAddress;
        rejects([&]{save_checkpoint(path,state,*memory,boundary,{});},"HLE kernel objects");
        require(!file_exists(path),"incomplete kernel state is never published as a checkpoint");
        rejects([&]{(void)load_checkpoint(path,state,*memory,{});},"HLE kernel objects");
        rejects([&]{(void)checkpoint_state_sha256(state,*memory);},"HLE kernel objects");
    }
    require(!memory->hle_kernel(),"detach restores ordinary runtime ownership");
    rejects([&]{(void)checkpoint_state_sha256(state,*memory);},"HLE kernel objects");
    std::vector<std::uint8_t> data(0x3000);
    for(unsigned i=0;i<data.size();++i)data[i]=memory->read8(hle_test::data+i);
    std::cout<<"HLE_GUEST_RESULT "<<sha256_hex(data)<<" pc="<<state.pc<<" cycles="<<state.cycles()<<'\n';
#ifdef PS2N_NATIVE_ONLY
    require(total==aot && interpreted==0,"all guest instructions used static AOT");
#else
    require(total==interpreted && aot==0,"independent reference execution is interpreted");
#endif
    // Unsupported services and symbolic descriptors must fail before publication.
    prepare_direct_elf(image,module,state,*memory);
    {
        HleKernel kernel(state,*memory);std::uint64_t executed=0;
        ExecutionContext ctx{state,*memory,executed,1,UINT32_MAX};
        state.set_reg32(3,0x777);const auto before=state;
        rejects([&]{execute_ee_system<MipsOpcode::Syscall>(ctx,12);},"unsupported PS2Recomp HLE syscall");
        require(state.pc==before.pc && state.retired_instructions==before.retired_instructions && kernel.syscall_count()==0,"unsupported syscall is transactional");
        state.set_reg32(3,0x40);state.set_reg32(4,hle_test::data);
        memory->store32_observed(hle_test::data+8,{0,1});
        rejects([&]{execute_ee_system<MipsOpcode::Syscall>(ctx,12);},"unknown bits");
        require(kernel.syscall_count()==0 && executed==0,"unknown descriptor is not consumed");
        memory->write32(hle_test::data+8,0);
        const auto call=[&](int number,std::uint32_t a=0,std::uint32_t b=0,std::uint32_t c=0,std::uint32_t d=0){
            state.set_reg32(3,static_cast<std::uint32_t>(number));state.set_reg32(4,a);state.set_reg32(5,b);
            state.set_reg32(6,c);state.set_reg32(7,d);execute_ee_system<MipsOpcode::Syscall>(ctx,12);
            return static_cast<std::int32_t>(state.reg32(2));
        };
        require(call(0x40,0)==-1,"bad semaphore descriptor returns the guest error");
        memory->store32_observed(hle_test::data,{0,UINT32_MAX});
        require(call(0x40,hle_test::data)==1,"failed descriptor did not allocate an object");
        require(call(0x45,1)==-419 && call(0x42,1)==1 && call(0x42,1)==-420 && call(-0x46,1)==1,
            "semaphore empty/overflow errors and interrupt-safe poll");
        require(call(0x50,hle_test::data+0x20)==1,"event creation");
        require(call(0x57,1,0,1)==-423 && call(0x57,1,4,0x80)==-405,"event pattern/mode validation");
        require(call(0x52,1,4)==0 && call(0x57,1,4,0x11,hle_test::data+0x104)==0 &&
                memory->read32(hle_test::data+0x104)==4 && call(0x57,1,4,1)==-421,"event consumption");
        require(call(0x3D,0x10000,0x10000)==0x10000 && call(0x3E)==0x20000,"bounded heap initialization");
        require(call(0x3D,UINT32_MAX,1)==-1 && call(0x3E)==0x20000,"invalid heap preserves prior allocation range");
        require(call(0x3C,0x1234,hle_test::data+0x1000,0x1000)==static_cast<int>(hle_test::data+0x2000) &&
                state.reg32(28)==0x1234,"SetupThread returns aligned stack and establishes GP");
        state.set_cp0(Cop0::Status,0);const auto retired=executed;
        rejects([&]{execute_ee_system<MipsOpcode::Mtc1>(ctx,0x44800000);},"Status.CU1");
        require(executed==retired,"guest disabling COP1 still receives the shared unusable fault");
        state.delay_pending=true;state.set_reg32(3,0x7F);
        rejects([&]{execute_ee_system<MipsOpcode::Syscall>(ctx,12);},"branch delay slot");state.delay_pending=false;
        rejects([&]{(void)call(0x44,1);},"no runnable thread");
        require(kernel.current_thread_id()==0,"a blocked thread is not silently resumed");
        BootOptions stopped;stopped.max_instructions=1;
        require(run_ee_boot(state,*memory,module,stopped).retired==0,"retry does not execute a blocked thread");
    }
    prepare_direct_elf(image,module,state,*memory);
    {
        memory->diagnostic_data_cache();memory->set_cache_config(0x10443u);
        state.set_cp0(Cop0::Config,0x10443u);
        memory->write32(0xA0004000u,0x11223344);
        require(memory->read32(0x80004000u)==0x11223344,"cache refill");
        memory->write32(0x80004000u,0xAABBCCDD);
        require(memory->read32(0xA0004000u)==0x11223344,"dirty CPU data is not backing RAM yet");
        HleKernel kernel(state,*memory);std::uint64_t executed=0;
        ExecutionContext ctx{state,*memory,executed,10,UINT32_MAX};
        state.set_reg32(3,0x64);state.set_reg32(4,0);execute_ee_system<MipsOpcode::Syscall>(ctx,12);
        require(memory->read32(0xA0004000u)==0xAABBCCDD,"FlushCache writes dirty data through the shared coherence path");
        memory->write32(0x80004000u,0xDEADBEEF);
        state.set_reg32(4,1);execute_ee_system<MipsOpcode::Syscall>(ctx,12);
        require(memory->read32(0x80004000u)==0xAABBCCDD,"invalidate discards dirty cache without inventing writeback");
        state.set_reg32(4,2);execute_ee_system<MipsOpcode::Syscall>(ctx,12);
        state.set_reg32(4,99);const auto before=executed;
        rejects([&]{execute_ee_system<MipsOpcode::Syscall>(ctx,12);},"unsupported HLE FlushCache");
        require(executed==before,"unsupported cache operation does not report success");
    }
    prepare_direct_elf(image,module,state,*memory);
    {
        HleKernel kernel(state,*memory);state.pc=0;
        BootOptions options;options.max_instructions=10;
        const auto report=run_ee_boot(state,*memory,module,options);
        require(report.outcome==BootOutcome::BlockPoint && report.retired==0 &&
            report.reason.find("return trampoline")!=std::string::npos,"thread return is explicit, not a zero-page instruction loop");
    }
    std::cout<<"PS2Recomp HLE threads, semaphore/event waits, state isolation and explicit refusals passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return run(argc,argv);}
#else
int main(int argc,char** argv){return run(argc,argv);}
#endif
