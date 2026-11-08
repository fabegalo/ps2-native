#include "direct_program.hpp"
#include "hle_fixture.hpp"
#include "ps2native/aot.hpp"
#include "iso_image.hpp"
#include "../tools/dev_process.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"
#include <nlohmann/json.hpp>
#include <iostream>

namespace fs = std::filesystem;
using namespace ps2native;
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
std::string text(const fs::path& path) { const auto b=read_file(path);return {b.begin(),b.end()}; }
int main(int argc,char** argv) {
    if(argc!=3 && argc!=11)return 2;
    const auto root=fs::weakly_canonical(fs::absolute(argv[2]));
    if(!fs::create_directory(native_io_path(root)))return 2;
    try {
        auto nested=root / fs::path(u8"ação-日本語");
        for(unsigned i=0;i<4;++i)nested/=std::string(60,'a'+i);
        ps2native::create_directories(nested);
        direct_test::Program p;
        p.segments.push_back({direct_test::text,5,m20::le_words({0x2402002Au,0x03E00008u,0u})});
        const auto bytes=direct_test::elf(p);
        const auto input=nested / "original.elf";
        (void)write_binary_if_changed(input,bytes);
        const auto hash=sha256_file(input);
        auto call=[&](const fs::path& file,const fs::path& output,bool disc,bool success) {
            std::vector<fs::path> args{"prepare-sdk",file,output};if(disc)args.emplace_back("--disc");
            const auto result=dev::process(argv[1],args,root / "prepare.log",30);
            require(!result.timed_out && (result.exit_code==0)==success,text(root / "prepare.log"));
        };
        const auto output=nested / "aot";
        call(input,output,false,true);
        const auto receipt=text(output / "sdk-prepare.json");
        const auto info=nlohmann::json::parse(receipt);
        require(info.at("runtime_ready")==false && info.at("functions").get<unsigned>()>0,"invalid SDK receipt");
        const auto config=text(output / "recompiler.toml");
        require(config.find("patch_cop0 = false")!=std::string::npos &&
            config.find("patch_cache = false")!=std::string::npos,"unsafe patch policy");
        if(argc==11) {
            const auto objects=root / "native-objects", executable=root / "native-fixture.exe";
            ps2native::create_directories(objects);
            const auto generated=root / "native-source";
            // Move this owned fixture directory to a compiler-safe path, then restore it.
            fs::rename(native_io_path(output / "generated"),native_io_path(generated));
            const auto option=[](const char* prefix,const fs::path& path) {
                fs::path result(prefix);result+=path.native();return result;
            };
            std::vector<fs::path> args{"/nologo","/std:c++20","/EHsc","/O2","/utf-8","/bigobj",
                option("/I",argv[4]),option("/I",argv[5]),option("/I",generated),
                option("/I",fs::path(argv[4]).parent_path() / "src" / "lib" / "Kernel"),
                fs::path(argv[6]),generated / "ps2_recompiled_functions.cpp",generated / "register_functions.cpp",
                option("/Fo",objects / ""),option("/Fe",executable)};
            auto result=dev::process(argv[3],args,root / "compile.log",30);
            require(!result.timed_out && !result.exit_code,text(root / "compile.log"));
            result=dev::process(executable,{},root / "native.log",5);
            require(!result.timed_out && !result.exit_code,text(root / "native.log"));
            const auto integrated=[&](const fs::path& image,const fs::path& source,const direct_test::Program& program,const char* mode) {
                const auto baseline=root / "baseline.cpp";
                // Unreachable static edges remain explicit boundaries; both executions are native-only.
                AotOptions options{program.roots,{},true};
                try {(void)write_text_if_changed(baseline,generate_aot_cpp(parse_ee_elf(read_file(image)),options));}
                catch(const std::exception& error){throw std::runtime_error(std::string("Prior native baseline ")+mode+": "+error.what());}
                const auto binary=root / "integrated-fixture.exe";
                std::vector<fs::path> native_args{"/nologo","/std:c++20","/EHsc","/O2","/MD","/utf-8","/bigobj",
                    "/DPS2N_NATIVE_ONLY=1","/DPS2N_SDK_NATIVE_BACKEND=1",
                    option("/I",argv[4]),option("/I",argv[7]),option("/I",source),
                    option("/I",fs::path(argv[10]).parent_path()),fs::path(argv[10]),
                    source / "ps2_recompiled_functions.cpp",source / "register_functions.cpp",baseline,
                    option("/Fo",objects / ""),option("/Fe",binary),fs::path(argv[8]),fs::path(argv[9]),
                    "kernel32.lib","user32.lib","gdi32.lib","winspool.lib","shell32.lib","ole32.lib",
                    "oleaut32.lib","uuid.lib","comdlg32.lib","advapi32.lib"};
                const auto compilation=dev::process(argv[3],native_args,root / "integrated-compile.log",60);
                require(!compilation.timed_out,std::string("SDK host compilation timed out for ")+mode);
                require(!compilation.exit_code,text(root / "integrated-compile.log"));
                const auto execution=dev::process(binary,{image,mode},root / "integrated-run.log",15);
                require(!execution.timed_out && !execution.exit_code,text(root / "integrated-run.log"));
                std::cout<<text(root / "integrated-run.log");
            };
            // Share one fixture and object directory; no second SDK/runtime build.
            integrated(input,generated,p,"cpu");
            fs::rename(native_io_path(generated),native_io_path(output / "generated"));
            for(const auto* mode:{"frame","hle"}) {
                const auto program=std::string_view(mode)=="hle"?hle_test::program():direct_test::frame();
                const auto image=root / (std::string(mode)+".elf"),prepared=root / (std::string(mode)+"-aot");
                (void)write_binary_if_changed(image,direct_test::elf(program));
                call(image,prepared,false,true);
                integrated(image,prepared / "generated",program,mode);
            }
        }
        call(input,output,false,true);
        require(text(output / "sdk-prepare.json")==receipt,"reused output changed");
        auto stale_backend=nlohmann::json::parse(receipt);
        stale_backend["native_backend_sha256"]=std::string(64,'0');
        (void)write_text_if_changed(output / "sdk-prepare.json",stale_backend.dump());
        call(input,output,false,false);
        require(nlohmann::json::parse(text(output / "sdk-prepare.json"))==stale_backend,"stale backend refusal modified the existing preparation");
        (void)write_text_if_changed(output / "sdk-prepare.json",receipt);
        const auto generated=output / "generated" / "ps2_recompiled_functions.cpp";
        const auto tampered=text(generated)+"\n// negative control\n";
        (void)write_text_if_changed(generated,tampered);
        call(input,output,false,false);
        require(text(generated)==tampered,"failed verification destroyed existing output");
        require(sha256_file(input)==hash,"original input changed");

        iso_test::IsoOptions options;options.sectors=options.written=64;
        options.system_cnf="BOOT2 = cdrom0:\\BOOT.ELF;1\r\n";
        auto iso=iso_test::build_iso(options);auto* directory=iso.data()+18*2048;std::size_t at=0;
        while(directory[at])at+=directory[at];
        iso_test::directory_record(directory+at,24,static_cast<std::uint32_t>(bytes.size()),false,"BOOT.ELF;1");
        std::copy(bytes.begin(),bytes.end(),iso.begin()+24*2048);
        const auto disc=root / "original.iso";
        (void)write_binary_if_changed(disc,iso);
        call(disc,root / "disc-aot",true,true);
        require(sha256_file(root / "disc-aot" / "boot.elf")==hash,"disc extraction differs");

        p.segments[0].bytes=m20::le_words({0x00000005u,0x03E00008u,0u});
        const auto unsupported=root / "unsupported.elf", failed=root / "unsupported-aot";
        (void)write_binary_if_changed(unsupported,direct_test::elf(p));
        call(unsupported,failed,false,false);
        require(!fs::exists(native_io_path(failed)),"incomplete output was retained as usable");
        fs::remove_all(native_io_path(root));
        std::cout<<"SDK ELF/disc preparation, Unicode/long paths, reuse, tamper and unsupported-code controls passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';fs::remove_all(native_io_path(root));return 1;}
}
