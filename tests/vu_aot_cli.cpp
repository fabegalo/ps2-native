// Original fixture: exercise the actual optional CLI capture/export workflow.
#include "boot_checks.hpp"
#include "vu_aot_fixture.hpp"
#include "../tools/vu_aot_cli.hpp"
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>

using namespace ps2native;
using namespace boot_checks;
namespace {
struct Machine {
    std::unique_ptr<Memory> memory=std::make_unique<Memory>();
    EEState state;
    Machine() {
        prepare_direct_elf(parse_ee_elf(vu_aot_fixture::ee_image()),nullptr,state,*memory);
        for(unsigned entry:{0u,2u,4u}) {
            memory->write64(0xB1008000u+entry*8u,std::uint64_t{vu_aot_fixture::addiu(1,0,17)}|
                (std::uint64_t{vu_aot_fixture::upper_nop|0x40000000u}<<32u));
            memory->write64(0xB1008008u+entry*8u,std::uint64_t{vu_aot_fixture::nop}|
                (std::uint64_t{vu_aot_fixture::upper_nop}<<32u));
        }
    }
    void launch(unsigned entry=0) {memory->write128(0xB0005000u,0x14000000u|entry,0);}
    void run(unsigned budget=16) {
        BootOptions options;options.max_instructions=budget;options.tail_instructions=0;
        const auto report=run_ee_boot(state,*memory,nullptr,options);
        require(report.outcome==BootOutcome::BudgetExhausted,"CLI capture fixture executes: "+report.reason);
    }
};
std::string text_file(const std::filesystem::path& path) {
    const auto bytes=read_file(path);return {bytes.begin(),bytes.end()};
}
std::filesystem::path capture_path(const std::filesystem::path& directory,const std::string& identity,const char* extension) {
    return directory/("vu1_"+identity+extension);
}
void parser_contract(const std::filesystem::path& directory) {
    cli::VuAotOptions options;
    const auto unicode=directory/std::filesystem::path(u8"captura-açúcar-向量");
    const auto argument=std::filesystem::path(std::filesystem::path("--vu1-capture=").native()+unicode.native());
    require(options.parse(path_utf8(argument),argument.c_str()) && options.directory==unicode,
        "capture parser preserves the native Unicode path (UTF-16 on Windows)");
    require(options.parse("--vu1-interpreter","--vu1-interpreter") && options.interpreter,"explicit interpreter parser option");
    require(!options.parse("--unknown","--unknown"),"unrelated options remain for the caller to reject");
    rejects([&]{(void)options.parse("--vu1-capture=","--vu1-capture=");},"requires a directory");
}
void deduplication(const std::filesystem::path& directory) {
    Machine machine;cli::VuAotOptions options;options.directory=directory;options.interpreter=true;
    options.attach(*machine.memory,{});
    const auto identity=vu1_aot_identity(machine.memory->capture_vu1_micro());
    const auto capture=capture_path(directory,identity,".vu1"),metadata=capture_path(directory,identity,".entries");
    machine.launch();
    require(file_exists(capture) && options.captured.size()==1 && options.captured.at(identity).contains(0),
        "launch exports the real complete image before its first issue");
    require(vu1_aot_identity(parse_vu1_aot_image(read_file(capture)))==identity,"export preserves unknown memory masks and identity");
    const auto image_time=file_write_time(capture),entry_time=file_write_time(metadata);
    machine.run();machine.launch();machine.run();
    require(file_write_time(capture)==image_time && file_write_time(metadata)==entry_time,
        "identical image/root capture reuses both files without changing mtimes");
    machine.launch(2);machine.run();
    require(file_write_time(capture)==image_time && options.captured.at(identity)==std::set<std::uint32_t>{0,16},
        "new entry merges metadata without duplicating the image");
    require(text_file(metadata).find("entry 0\nentry 16\n")!=std::string::npos,"metadata stores deterministic sorted byte PCs");
    Machine restarted;cli::VuAotOptions resumed;resumed.directory=directory;resumed.attach(*restarted.memory,{});
    restarted.launch(4);restarted.run();
    require(text_file(metadata).find("entry 0\nentry 16\nentry 32\n")!=std::string::npos && file_write_time(capture)==image_time,
        "a fresh capture session preserves prior roots and the original image");
    const auto stats=machine.memory->vu1_aot_stats();
    require(stats.native_instructions==0 && stats.interpreted_instructions==6 && stats.identity_checks==0,
        "--vu1-interpreter executes all real LIWs in the reference engine");
    std::ostringstream printed;
    auto* original=std::cout.rdbuf(printed.rdbuf());options.print(*machine.memory);std::cout.rdbuf(original);
    require(printed.str().find("native=0 interpreted=6")!=std::string::npos &&
        printed.str().find("mode=interpreter")!=std::string::npos,"CLI reports actual forced-interpreter counters");
}
void protected_outputs(const std::filesystem::path& root) {
    for(unsigned test=0;test<4;++test) {
        Machine machine;cli::VuAotOptions options;options.directory=root/std::to_string(test);
        const auto identity=vu1_aot_identity(machine.memory->capture_vu1_micro());
        const auto capture=capture_path(options.directory,identity,".vu1"),metadata=capture_path(options.directory,identity,".entries");
        const auto protected_path=test==1u ? metadata : capture;
        if(test==1u || test==3u)(void)write_binary_if_changed(capture,encode_vu1_aot_image(machine.memory->capture_vu1_micro()));
        const auto sentinel=std::string{"private original bytes\n"};
        (void)write_text_if_changed(test==3u ? metadata : protected_path,sentinel);
        const std::array inputs{protected_path};
        options.attach(*machine.memory,test<2u ? std::span<const std::filesystem::path>(inputs) : std::span<const std::filesystem::path>{});
        rejects([&]{machine.launch();},test<2u ? "must not overwrite an input or checkpoint" :
            test==2u ? "capture disagrees with its identity" : "metadata is invalid");
        require(text_file(test==3u ? metadata : protected_path)==sentinel,
            "input/checkpoint conflicts and inconsistent existing exports preserve their original contents");
    }
}
void active_mutation(const std::filesystem::path& directory) {
    Machine machine;cli::VuAotOptions options;options.directory=directory;options.interpreter=true;options.attach(*machine.memory,{});
    const auto old_identity=vu1_aot_identity(machine.memory->capture_vu1_micro());
    machine.launch();machine.run(1);
    require(machine.memory->vu1_aot_stats().interpreted_instructions==1,"first pair issues before active mutation");
    const auto patch=vu_aot_fixture::addiu(3,0,99);
    machine.memory->write32(0xB1008008u,patch);
    const auto changed_identity=vu1_aot_identity(machine.memory->capture_vu1_micro());
    require(changed_identity!=old_identity && options.captured.size()==1,"mutation remains guest-visible and capture waits for issue");
    machine.run(1);
    require(options.captured.size()==2 && options.captured.at(changed_identity).contains(8),
        "modified running microcode is captured at its actual next byte PC before issue");
    const auto changed=parse_vu1_aot_image(read_file(capture_path(directory,changed_identity,".vu1")));
    require(changed.words[2].value==patch && changed.words[2].unknown==0,
        "active mutation export contains the guest's exact replacement instruction");
    machine.run();
    require(machine.memory->vu1_aot_stats().interpreted_instructions==2,"capture does not duplicate or suppress the E delay instruction");
}
}
template<class Character> int test_main(int argc,Character** argv) try {
    if(argc!=2)return 2;
    const auto directory=std::filesystem::absolute(std::filesystem::path(argv[1]));
    require(directory.filename()=="vu-aot-cli-owned","test output is an explicitly owned fixture directory");
    if(file_exists(directory))remove_directory_tree(directory);
    ps2native::create_directories(directory);
    parser_contract(directory);deduplication(directory/"dedup");protected_outputs(directory/"protected");active_mutation(directory/"active");
    std::cout<<"VU1 CLI native paths, capture dedup/roots, protected exports, forced interpreter and active mutation capture passed\n";
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return test_main(argc,argv);}
#else
int main(int argc,char** argv){return test_main(argc,argv);}
#endif
