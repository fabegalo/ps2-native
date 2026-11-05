// Bounded original workload. Its throughput is never a commercial-game FPS.
#include "boot_checks.hpp"
#include "vu_aot_fixture.hpp"
#include "ps2native/vu_aot.hpp"
#include "ps2native/aot_runtime.hpp"
#include "ps2native/checkpoint.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>

using namespace ps2native;
using namespace boot_checks;
using Clock=std::chrono::steady_clock;
namespace {
double seconds(Clock::time_point start){return std::chrono::duration<double>(Clock::now()-start).count();}
void isolated(bool native,unsigned repeats) {
    VuControl vu;Vif vif;Gif gif;GsRegisters gs;gs.reset(0);vu.enable_vu1_aot(native);
    vu_aot_fixture::upload(vu,vif,gif,gs);vu_aot_fixture::seed(vu);
    const auto start=Clock::now();
    for(unsigned round=0;round<repeats;++round) {
        vu_aot_fixture::seed_integer(vu,1,2000);vu_aot_fixture::seed_integer(vu,2,0);
        vu.start_micro(1,vu_aot_fixture::loop_entry,std::uint64_t{round}*100000u);
        vu.advance_micro(std::uint64_t{round+1u}*100000u,&gif,&gs);
        require(!vu.micro_running(1) && vu.observe_data(0x4210).value==0 && vu.observe_data(0x4220).value==2000,
            "bounded benchmark loop finishes with actual results");
    }
    const auto elapsed=seconds(start);const auto stats=vu.vu1_aot_stats();
    const auto instructions=stats.native_instructions+stats.interpreted_instructions;
    require(native ? stats.native_instructions==instructions && instructions>0 : !stats.native_instructions && instructions>0,
        "benchmark engine coverage is explicit");
    std::cout<<(native ? "vu1-native" : "vu1-interpreter")<<','<<elapsed<<','<<instructions<<','<<instructions/elapsed<<','
        <<elapsed/repeats<<','<<stats.native_instructions<<','<<stats.interpreted_instructions<<'\n';
}
std::string integrated(unsigned engine) {
    auto memory=std::make_unique<Memory>();EEState state;vu_aot_fixture::initialize(*memory,state,engine==2,vu_aot_fixture::draw_entry);
    BootOptions options;options.max_instructions=100;options.tail_instructions=0;options.copy_device_histories=false;
    const auto start=Clock::now();
    const auto draw=run_ee_boot(state,*memory,engine ? &aot_module() : nullptr,options);
    require(draw.outcome==BootOutcome::BudgetExhausted,"integrated graphics workload");
    vu_aot_fixture::MemoryVu adapter{*memory};vu_aot_fixture::seed_integer(adapter,1,2000);vu_aot_fixture::seed_integer(adapter,2,0);
    memory->write128(0xB0005000u,0x14000000u|vu_aot_fixture::loop_entry,0);
    options.max_instructions=20000;
    const auto report=run_ee_boot(state,*memory,engine ? &aot_module() : nullptr,options);
    const auto elapsed=seconds(start);const auto stats=memory->vu1_aot_stats();
    require(report.outcome==BootOutcome::BudgetExhausted,"integrated loop workload: "+report.reason);
    require(!engine || report.interpreted_instructions==0,"EE AOT baseline retains full proven coverage");
    const auto frame=memory->gs().scanout(state.cycles());
    require(frame.rgba.size()==256 && frame.rgba[0]==0x10,"integrated native VU generated real pixels");
    const auto vu_instructions=stats.native_instructions+stats.interpreted_instructions;
    require(engine!=2 || stats.native_instructions==vu_instructions,"integrated VU native coverage");
    const char* label=engine==0 ? "ee-interpreter-vu1-interpreter-gs" : engine==1 ? "ee-aot-vu1-interpreter-gs" : "ee-aot-vu1-native-gs";
    std::cout<<label<<','<<elapsed<<','<<report.retired+draw.retired<<','<<(report.retired+draw.retired)/elapsed<<','
        <<elapsed<<','<<stats.native_instructions<<','<<stats.interpreted_instructions<<'\n';
    return checkpoint_state_sha256(state,*memory);
}
}
int main() try {
    std::cout<<std::setprecision(12)<<"path,seconds,instructions,instructions_per_second,seconds_per_workload,native_vu_liw,interpreted_vu_liw\n";
    std::string integrated_reference;
    // Alternating order makes the paired samples visible; no affinity changes
    // or platform-specific SIMD are required by this portable executable.
    for(unsigned round=0;round<3;++round) {
        isolated((round&1u)!=0u,40);isolated((round&1u)==0u,40);
        for(unsigned engine=0;engine<3;++engine) {
            const auto hash=integrated(engine);if(integrated_reference.empty())integrated_reference=hash;
            else require(hash==integrated_reference,"all benchmark paths preserve complete machine state");
        }
    }
    const auto image=parse_vu1_aot_image(vu_aot_fixture::micro_image());VuAotOptions options;
    options.entries={0,vu_aot_fixture::draw_entry*8u,vu_aot_fixture::indirect_entry*8u,vu_aot_fixture::loop_entry*8u};
    const auto start=Clock::now();const auto output=generate_vu1_aot_cpp(image,options);
    std::cout<<"generation_seconds="<<seconds(start)<<" source_bytes="<<output.size()<<" state_sha256="<<integrated_reference<<'\n';
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
