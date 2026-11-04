#include "boot_checks.hpp"
#include "direct_program.hpp"
#include "iso_image.hpp"
#include "../tools/dev_profile.hpp"
#include "../tools/dev_process.hpp"
#include <iostream>

namespace {
using namespace ps2native;
using namespace ps2native::dev;
using namespace boot_checks;
std::vector<std::uint8_t> disc_image(const std::vector<std::uint8_t>& elf) {
    iso_test::IsoOptions options;options.sectors=options.written=64;options.system_cnf="BOOT2 = cdrom0:\\BOOT.ELF;1\r\n";
    auto bytes=iso_test::build_iso(options);auto root=bytes.data()+18*2048;std::size_t at=0;
    while(root[at])at+=root[at];
    iso_test::directory_record(root+at,24,static_cast<std::uint32_t>(elf.size()),false,"BOOT.ELF;1");
    std::copy(elf.begin(),elf.end(),bytes.begin()+24*2048);return bytes;
}
std::string cmake_path_literal(const fs::path& path) {
    const auto bytes=path.generic_u8string();const std::string text(bytes.begin(),bytes.end());
    std::string eq;while(text.find("]"+eq+"]")!=std::string::npos)eq+='=';
    return "["+eq+"["+text+"]"+eq+"]";
}
void content_build(const fs::path& root,const fs::path& cmake,const fs::path& repository,
                   const std::string& generator,const fs::path& compiler,const fs::path& make,const std::string& arch) {
    const auto project=root / "content-project",build=root / "content-build";
    const auto source=project / "probe.cpp",header=project / "include/value.hpp";
    save_text(source,"#include \"value.hpp\"\nint main(){return VALUE;}\n");
    save_text(header,"#define VALUE 11\n");
    save_text(project / "CMakeLists.txt","cmake_minimum_required(VERSION 3.21)\nproject(content_probe LANGUAGES CXX)\n"
        "add_executable(probe probe.cpp)\ntarget_include_directories(probe PRIVATE \"${CMAKE_CURRENT_SOURCE_DIR}/include\")\n"
        "set(PS2N_DEV_CONTENT_TRACKING ON)\ninclude("+cmake_path_literal(repository / "cmake/DevContent.cmake")+")\n");
    std::vector<fs::path> configure{"-S",project,"-B",build,"-G",cli::utf8_path(generator),"-DCMAKE_BUILD_TYPE=Release","-DCMAKE_TRY_COMPILE_CONFIGURATION=Release"};
    if(generator.find("Visual Studio")!=std::string::npos){configure.emplace_back("-A");configure.push_back(cli::utf8_path(arch));}
    else{configure.emplace_back("-DCMAKE_CXX_COMPILER="+path_utf8(compiler));configure.emplace_back("-DCMAKE_MAKE_PROGRAM="+path_utf8(make));}
    auto call=[&](const std::string& name,const std::vector<fs::path>& args){
        const auto log=root / (name+".log");const auto r=process(cmake,args,log,90);
        require(r.exit_code==0,name+": "+read_text(log));
    };
    call("content-configure",configure);
    const std::vector<fs::path> compile{"--build",build,"--config","Release","--parallel","2"};
    call("content-first-build",compile);
    auto probe=build / "probe";
#ifdef _WIN32
    probe+=".exe";
#endif
    if(file_exists(build / "Release" / probe.filename()))probe=build / "Release" / probe.filename();
    require(process(probe,{},root / "content-run.log",15).exit_code==11,"initial synthetic value");
    const auto stamp=file_write_time(probe);
    call("content-unchanged",compile);require(file_write_time(probe)==stamp,"unchanged build preserves executable mtime");
    const auto header_time=file_write_time(header);
    save_text(header,"#define VALUE 13\n");fs::last_write_time(native_io_path(header),header_time);
    call("content-header-configure",configure);call("content-header-build",compile);
    require(process(probe,{},root / "content-run.log",15).exit_code==13,"changed header content rebuilds despite restored mtime");
    const auto source_time=file_write_time(source);
    save_text(source,"#include \"value.hpp\"\nint main(){return VALUE+2;}\n");fs::last_write_time(native_io_path(source),source_time);
    call("content-source-configure",configure);call("content-source-build",compile);
    require(process(probe,{},root / "content-run.log",15).exit_code==15,"changed source content rebuilds despite restored mtime");
}
template<class C> int test_main(int argc,C** argv) {
    if(argc==2 && path_utf8(fs::path(argv[1]))=="--sleep") {std::this_thread::sleep_for(std::chrono::seconds(15));return 0;}
    require(argc==10,"dev, debug, root, source, cmake, generator, compiler, make program, architecture");
    const auto tool=fs::absolute(fs::path(argv[1])),debug=fs::absolute(fs::path(argv[2])),root=fs::absolute(fs::path(argv[3]));
    ps2native::create_directories(root);
    const auto self=fs::absolute(fs::path(argv[0]));
    const auto timed=process(self,{"--sleep"},root / "timeout.log",1);
    require(timed.timed_out && timed.exit_code==124 && timed.milliseconds<6000,"bounded child timeout kills and reaps process");
    content_build(root,fs::path(argv[5]),fs::absolute(fs::path(argv[4])),path_utf8(fs::path(argv[6])),fs::path(argv[7]),fs::path(argv[8]),path_utf8(fs::path(argv[9])));
    const auto elf=root / fs::path(u8"fixture ação & literal.elf");
    // Small fixture reaches a real unsupported MMI encoding after four GPR ops.
    m20::Assembler a(direct_test::text);a.addiu(m20::t0,m20::zero,17);a.addiu(m20::t1,m20::t0,3);a.raw(0x7000003Fu);
    direct_test::Program program{{{direct_test::text,5,m20::le_words(a.finish())}},0,{},{}};
    const auto bytes=direct_test::elf(program);(void)write_binary_if_changed(elf,bytes);
    const auto hash=sha256_file(elf);
    const auto file=root / "game.ps2n";
    auto invoke=[&](const std::string& name,std::vector<fs::path> args,int expected) {
        const auto r=process(tool,args,root / (name+".log"),300);
        if(r.exit_code!=expected)throw std::runtime_error(name+" exit="+std::to_string(r.exit_code)+"\n"+read_text(root / (name+".log")));
        return r.milliseconds;
    };
    if(!file_exists(file))invoke("init",{"init",elf,file},0);
    auto profile=Profile::load(file);profile.values["source"]=path_utf8(fs::absolute(fs::path(argv[4])));
    profile.values["cmake"]=path_utf8(fs::path(argv[5]));profile.values["build"]=path_utf8(root / "build");
    profile.values["generator"]=path_utf8(fs::path(argv[6]));profile.values["timeout"]="240";
    profile.values["stream_bytes"]="1048576";
    profile.values["name"]=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    profile.values["input"]=path_utf8(elf);profile.values["input_sha256"]=hash;profile.values["kind"]="elf";
    profile.cmake_args.clear();profile.runtime_args={"--budget=1000000"};
    if(profile.get("generator").find("Visual Studio")!=std::string::npos)profile.values["architecture"]=path_utf8(fs::path(argv[9]));
    else {
        profile.cmake_args.push_back("-DCMAKE_CXX_COMPILER="+path_utf8(fs::path(argv[7])));
        profile.cmake_args.push_back("-DCMAKE_MAKE_PROGRAM="+path_utf8(fs::path(argv[8])));
        profile.cmake_args.push_back("-DCMAKE_TRY_COMPILE_CONFIGURATION=Release");
    }
    save_text(file,profile.serialize());
    const auto cold=invoke("cold",{"run",file},1);
    const auto first=cli::utf8_path(read_text(profile.path("build") / "ps2n-dev/last.txt"));
    const auto runtime=read_text(first / "runtime.sha256");
    require(read_text(first / "manifest.json").find("\"aot_reused\":false")!=std::string::npos,"cold run generates AOT");
    const auto warm=invoke("warm",{"replay",file},1);
    const auto second=cli::utf8_path(read_text(profile.path("build") / "ps2n-dev/last.txt"));
    require(second!=first && read_text(second / "runtime.sha256")==runtime,"incremental replay retains exact executable");
    require(read_text(second / "manifest.json").find("\"aot_reused\":true")!=std::string::npos,"warm replay verifies/reuses AOT");
    require(read_text(second / "analysis-reused.txt")=="true","warm replay verifies/reuses static analysis");
    require(read_text(second / "build-runtime.log").find("Building CXX")==std::string::npos,"warm build does not recompile runtime");
    const auto compare=process(debug,{"compare",first / "diagnostic/full-events.ndjson",second / "diagnostic/full-events.ndjson"},root / "compare.log",15);
    require(compare.exit_code==0,"complete event streams match across replay");
    require(sha256_file(elf)==hash,"input is never modified");
    // Changed content, even with unchanged path/size, cannot replay under the old identity.
    auto changed=bytes;changed.back()^=1;(void)write_binary_if_changed(elf,changed);
    invoke("identity-change",{"replay",file},2);(void)write_binary_if_changed(elf,bytes);
    auto invalid=profile;invalid.cmake_args.push_back("--build");save_text(root / "invalid.ps2n",invalid.serialize());
    invoke("invalid-profile",{"run",root / "invalid.ps2n"},2);
    // Corrupted generated content must be regenerated from the original input.
    const auto translation=cli::utf8_path(read_text(first / "analysis-reference.txt")).parent_path() / "module.cpp";
    require(file_exists(translation),"active profile AOT cache exists");const auto original=sha256_file(translation);
    save_text(translation,"// corrupted synthetic cache\n");
    invoke("cache-repair",{"replay",file},1);require(sha256_file(translation)==original,"cache hash catches corruption");
    // Exercise disc identification, BOOT2 AOT and ps2n-run --disc using same build.
    const auto disc=root / "fixture.iso";(void)write_binary_if_changed(disc,disc_image(bytes));
    profile.values["input"]=path_utf8(disc);profile.values["input_sha256"]=sha256_file(disc);profile.values["kind"]="disc";save_text(file,profile.serialize());
    invoke("disc",{"run",file},1);
    const auto disc_run=cli::utf8_path(read_text(profile.path("build") / "ps2n-dev/last.txt"));
    require(file_exists(disc_run / "identify-disc.log") && file_exists(disc_run / "diagnostic/report.json"),"existing disc CLI and runtime integrated");
    // BIOS uses the same core/build with firmware provenance checks.
    const auto bios=root / "synthetic-rom0.bin";(void)write_binary_if_changed(bios,m20::build_firmware().rom);
    profile.values["input"]=path_utf8(bios);profile.values["input_sha256"]=sha256_file(bios);profile.values["kind"]="bios";
    profile.runtime_args={"--max-instructions=1000"};save_text(file,profile.serialize());invoke("bios",{"run",file},0);
    const auto bios_run=cli::utf8_path(read_text(profile.path("build") / "ps2n-dev/last.txt"));
    require(file_exists(bios_run / "diagnostic/report.json"),"boot runner structured diagnosis");
    std::cout<<"dev pipeline cold_ms="<<cold<<" warm_ms="<<warm<<" comparison_ms="<<compare.milliseconds<<'\n';
    return 0;
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){try{return test_main(argc,argv);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#else
int main(int argc,char** argv){try{return test_main(argc,argv);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
