#include "ps2_runtime.h"
#include "hle_fixture.hpp"
#include "ps2native/hle.hpp"
#include "ps2native/checkpoint.hpp"
#include <iostream>

using namespace ps2native;
void require(bool ok,const char* reason) { if(!ok) throw std::runtime_error(reason); }
int main(int argc,char** argv) try {
    require(argc==2,"Expected HLE ELF");
    PS2Runtime runtime;
    auto* const memory=&runtime.memory();auto* const cpu=&runtime.cpu();
    const auto elf=parse_ee_elf(read_file(argv[1]));const auto fixture=hle_test::program();
    runtime.prepare_hle_elf(elf,&aot_module());
    auto* const kernel=memory->hle_kernel();require(kernel!=nullptr,"SDK owns an attached HLE kernel");
    BootOptions options;options.max_instructions=7;options.stop_at=fixture.stop;options.copy_device_histories=false;
    std::uint64_t total=0;
    for(unsigned slice=0;slice<1000;++slice) {
        const auto report=runtime.run(&aot_module(),options);total+=report.retired;
        require(report.outcome!=BootOutcome::BlockPoint,report.reason.c_str());
        require(!report.interpreted_instructions && report.aot_instructions==report.retired,"HLE guest execution stays native");
        if(report.outcome==BootOutcome::StopAddress) break;
        require(slice<999,"HLE execution exceeded its slice limit");
    }
    require(cpu->pc==fixture.stop && kernel->current_thread_id()==1 && kernel->syscall_count()==15,"SDK-owned HLE threads and services complete");
    require(cpu->cycles()==total && cpu->count()==total,"HLE shares the one console clock");
    require(memory->read32(hle_test::data+0x104)==4 && memory->read32(hle_test::data+0x108)==0,"HLE event delivery preserves guest-visible results");
    bool refused=false;
    try {(void)checkpoint_state_sha256(*cpu,*memory);} catch(const std::exception& error) {
        refused=std::string(error.what()).find("HLE kernel objects")!=std::string::npos;
    }
    require(refused,"Incomplete HLE checkpoints remain explicitly unsupported");
    runtime.prepare_direct_elf(elf,&aot_module());
    require(memory==&runtime.memory() && cpu==&runtime.cpu() && !memory->hle_kernel(),"Fresh preparation detaches HLE in the same SDK-owned console");
    (void)checkpoint_state_sha256(*cpu,*memory);
    std::cout<<"SDK-owned HLE executes fifteen services with native guest code and one clock\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
