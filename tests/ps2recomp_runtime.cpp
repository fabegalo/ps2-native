#include "ps2_runtime.h"
#include "ps2native/aot_runtime.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/sha256.hpp"
#include <iostream>
#include <type_traits>

using namespace ps2native;
namespace fs = std::filesystem;
static_assert(!std::is_copy_constructible_v<PS2Runtime> && !std::is_move_constructible_v<PS2Runtime>);
void require(bool ok,const char* reason) { if(!ok) throw std::runtime_error(reason); }
template<class F> void rejects(F&& operation,const char* reason) {
    try { operation(); } catch(const std::exception& error) {
        require(std::string(error.what()).find(reason)!=std::string::npos,error.what()); return;
    }
    throw std::runtime_error(std::string("Expected refusal: ")+reason);
}
struct Scratch {
    fs::path path;
    explicit Scratch(fs::path p):path(fs::weakly_canonical(fs::absolute(p))) {
        require(!fs::exists(native_io_path(path)),"Runtime test scratch already exists");
        ps2native::create_directories(path);
    }
    ~Scratch() { std::error_code error;fs::remove_all(native_io_path(path),error); }
};
int main(int argc,char** argv) try {
    require(argc==3,"Expected ELF and scratch directory");
    const auto elf=parse_ee_elf(read_file(argv[1]));
    Scratch scratch(argv[2]);
    PS2Runtime runtime;
    auto* const cpu=&runtime.cpu();auto* const memory=&runtime.memory();
    const auto* const iop=&memory->iop();const auto* const gs=&memory->gs();
    BootOptions options;options.max_instructions=7;options.stop_at=0xFFFFFFF0u;
    rejects([&]{(void)runtime.run(&aot_module(),options);},"not prepared");
    runtime.prepare_direct_elf(elf,&aot_module());
    cpu->set_reg32_sign_extended(31,*options.stop_at);
    auto reference=std::make_unique<Memory>();EEState reference_cpu;
    prepare_direct_elf(elf,&aot_module(),reference_cpu,*reference);
    reference_cpu.set_reg32_sign_extended(31,*options.stop_at);
    const auto actual=runtime.run(&aot_module(),options);
    const auto expected=run_ee_boot(reference_cpu,*reference,&aot_module(),options);
    require(actual.outcome==expected.outcome && actual.retired==7 && actual.aot_instructions==7 && !actual.interpreted_instructions,
            "SDK owner executes only the verified native slice");
    const auto hash=checkpoint_state_sha256(*cpu,*memory);
    require(hash==checkpoint_state_sha256(reference_cpu,*reference),"Native owner preserves complete state against the prior execution boundary");
    const auto file=scratch.path / "owner.ps2ncp";
    CheckpointContext context{sha256_hex(elf.bytes),"SDK native owner"};
    save_checkpoint(file,*cpu,*memory,actual,context);
    require(regular_file_size(file)<100u*1024u*1024u,"Checkpoint diagnostic budget exceeded");
    cpu->set_reg32(2,0x1234);memory->write32(0x40,0x1234);
    (void)load_checkpoint(file,*cpu,*memory,context);
    require(hash==checkpoint_state_sha256(*cpu,*memory),"Checkpoint restores the canonical SDK-owned state");
    require(cpu==&runtime.cpu() && memory==&runtime.memory() && iop==&memory->iop() && gs==&memory->gs(),
            "Checkpoint preserves the console and device object identities");
    options.max_instructions=100;
    const auto resumed=runtime.run(&aot_module(),options);
    (void)run_ee_boot(reference_cpu,*reference,&aot_module(),options);
    require(resumed.outcome==BootOutcome::StopAddress && cpu->reg32(2)==37 && memory->read32(0x40)==37 && !resumed.interpreted_instructions,
            "SDK owner resumes native code after checkpoint restoration");
    require(checkpoint_state_sha256(*cpu,*memory)==checkpoint_state_sha256(reference_cpu,*reference),"Restored execution agrees with the uninterrupted reference");
    runtime.prepare_direct_elf(elf,nullptr);cpu->set_reg32(31,*options.stop_at);
    const auto absent=runtime.run(nullptr,options);
    require(absent.outcome==BootOutcome::BlockPoint && !absent.retired && !absent.interpreted_instructions && absent.reason.find("missing EE AOT")!=std::string::npos,
            "Absent translation stops explicitly without an interpreted fallback");
    runtime.invalidate_preparation();
    rejects([&]{(void)runtime.run(nullptr,options);},"not prepared");
    std::cout<<"SDK-owned CPU/memory/devices, native state equivalence, checkpoint continuation and missing-AOT control passed\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
