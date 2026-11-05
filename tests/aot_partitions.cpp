// Verify native execution and real Ninja object reuse, not just emitter text.
#include "ps2native/core.hpp"
#include "ps2native/aot_sources.hpp"
#include "boot_checks.hpp"
#include "io_process.hpp"
#include <iostream>
#include <map>
#include <sstream>

namespace {
using namespace ps2native;
using boot_checks::require;
using boot_checks::rejects;
namespace fs=std::filesystem;
std::string text(const fs::path& path) {const auto b=read_file(path);return {b.begin(),b.end()};}
std::string bracket(const fs::path& path) {
    const auto u8=path.generic_u8string();const std::string value(u8.begin(),u8.end());
    std::string eq;while(value.find("]"+eq+"]")!=std::string::npos)eq+='=';
    return "["+eq+"["+value+"]"+eq+"]";
}
fs::path option(const char* prefix,const fs::path& value) {auto p=fs::path(prefix);p+=value.native();return p;}
using Stamps=std::map<std::string,fs::file_time_type>;
struct OwnedDirectory {
    fs::path path;
    ~OwnedDirectory() {try{if(!path.empty())remove_directory_tree(path);}catch(...) {}}
};
Stamps objects(const fs::path& path) {
    std::istringstream lines(text(path));std::string line;Stamps result;
    while(std::getline(lines,line)) {
        if(!line.empty() && line.back()=='\r')line.pop_back();
        if(!line.empty()) {
            const fs::path object(std::u8string(line.begin(),line.end()));
            result.emplace(path_utf8(object.filename()),file_write_time(object));
        }
    }
    return result;
}
}
template<class Char> int run_test(int argc,Char** argv) try {
    require(argc==9,"fixture root repo cmake core compiler ninja config");
    if(!file_exists(argv[7]))return 77;
    const fs::path elf_path(argv[1]),root(argv[2]),repo(argv[3]),cmake(argv[4]);
    require(root.is_absolute() && !file_exists(root),"unused absolute partition test directory");
    OwnedDirectory cleanup{root};
    const auto source=root / fs::path(u8"aot ação 日本") / "module.cpp";
    const auto elf=parse_ee_elf(read_file(elf_path));
    const auto initial=generate_aot_sources(elf,{},"fixture",64);
    require(initial.partitions.size()>=2,"fixture crosses stable address partitions");
    require(write_aot_sources(source,initial),"cold source set written");
    const auto fingerprint=aot_sources_sha256(source);
    const auto stamp=file_write_time(source);
    require(!write_aot_sources(source,generate_aot_sources(elf,{},"fixture",64)) &&
        aot_sources_sha256(source)==fingerprint && file_write_time(source)==stamp,"deterministic no-op generation");
    rejects([&]{(void)generate_aot_sources(elf,{},"fixture",63);},"power of two");
    const auto partition=source.parent_path()/initial.partitions.front().name;
    (void)write_text_if_changed(partition,"// corrupt unit\n");
    rejects([&]{(void)aot_sources_sha256(source);},"identity mismatch");
    (void)write_aot_sources(source,initial);
    const auto project=root/"project",build=root/"build";
    std::string script=
        "cmake_minimum_required(VERSION 3.21)\nproject(aot_partitions LANGUAGES CXX)\n"
        "set(CMAKE_CXX_STANDARD 20)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
        "include("+bracket(repo/"cmake/AotSource.cmake")+")\n"
        "add_library(aot_object OBJECT)\nps2n_target_aot_source(aot_object "+bracket(source)+")\n"
        "target_include_directories(aot_object PRIVATE "+bracket(repo/"include")+")\n"
        "add_library(core STATIC IMPORTED)\nset_target_properties(core PROPERTIES IMPORTED_LOCATION "+bracket(fs::path(argv[5]))+")\n"
        "add_executable(partition-probe "+bracket(repo/"tests/aot_smoke.cpp")+" $<TARGET_OBJECTS:aot_object>)\n"
        "target_include_directories(partition-probe PRIVATE "+bracket(repo/"include")+")\ntarget_link_libraries(partition-probe PRIVATE core)\n"
        "file(GENERATE OUTPUT \"${CMAKE_BINARY_DIR}/objects.txt\" CONTENT \"$<JOIN:$<TARGET_OBJECTS:aot_object>,\\n>\")\n";
#if defined(PS2N_ENABLE_PROFILING)
    script+="target_compile_definitions(aot_object PRIVATE PS2N_ENABLE_PROFILING=1)\n"
            "target_compile_definitions(partition-probe PRIVATE PS2N_ENABLE_PROFILING=1)\n";
#endif
    (void)write_text_if_changed(project/"CMakeLists.txt",script);
    const auto configure=[&]{io_test::run(cmake,{"-S",project,"-B",build,"-G","Ninja",
        option("-DCMAKE_CXX_COMPILER:FILEPATH=",fs::path(argv[6])),option("-DCMAKE_MAKE_PROGRAM:FILEPATH=",fs::path(argv[7])),
        option("-DCMAKE_BUILD_TYPE:STRING=",fs::path(argv[8])),"-DCMAKE_TRY_COMPILE_CONFIGURATION=Release"},90);};
    configure();
    const auto compile=[&]{io_test::run(cmake,{"--build",build,"--parallel","2"},90);};
    const auto executable=build /
#ifdef _WIN32
        "partition-probe.exe";
#else
        "partition-probe";
#endif
    compile();io_test::run(executable,{elf_path});
    const auto before=objects(build/"objects.txt");
    compile();require(objects(build/"objects.txt")==before,"no-op build reuses every object");
    // Change a NOP to SLL $zero,$zero,1: same guest effects, different code word.
    auto changed=elf;const auto graph=analyze_control_flow(elf,{});
    bool mutated=false;
    for(const auto& [pc,instruction]:graph.instructions)if(instruction.word==0) {
        const auto& segment=elf.code_segment_at(pc);
        changed.bytes.at(segment.file_offset+pc-segment.vaddr)=0x40;mutated=true;break;
    }
    require(mutated,"fixture has a no-effect instruction to change");
    const auto updated=generate_aot_sources(changed,{},"fixture",64);
    std::size_t reused=0;
    for(const auto& old:initial.partitions)for(const auto& unit:updated.partitions)reused+=old.name==unit.name;
    require(reused+1==initial.partitions.size(),"one code change invalidates exactly its partition");
    require(write_aot_sources(source,updated),"changed source set written");
    (void)write_binary_if_changed(root/"changed.elf",changed.bytes);
    compile();io_test::run(executable,{root/"changed.elf"});
    const auto after=objects(build/"objects.txt");std::size_t retained=0;
    for(const auto& [name,time]:before)if(name.find("ps2n_aot_")!=std::string::npos && after.contains(name)) {
        require(after.at(name)==time,"unchanged native partition object retained");++retained;
    }
    require(retained==reused,"incremental build recompiles only dispatcher and changed partition");
    // Both consumers reject corrupt or injected manifest paths before compiling.
    (void)write_text_if_changed(partition,"// corrupt unit\n");
    // Use an active unit even if the first partition was the one replaced.
    const auto active=source.parent_path()/updated.partitions.back().name;
    (void)write_text_if_changed(active,"// corrupt active unit\n");
    rejects(configure,"Test tool failed");
    (void)write_aot_sources(source,updated);
    auto manifest=source;manifest+=".sources";
    (void)write_text_if_changed(manifest,text(manifest)+"../foreign.cpp invalid\n");
    rejects([&]{(void)aot_sources_sha256(source);},"identity mismatch");
    rejects(configure,"Test tool failed");
    remove_directory_tree(root);
    std::cout<<"PASS partitioned native execution, exact state/IRQs/delay slots, corruption checks and incremental object reuse\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<"aot-partitions: "<<error.what()<<'\n';return 1;}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return run_test(argc,argv);}
#else
int main(int argc,char** argv){return run_test(argc,argv);}
#endif
