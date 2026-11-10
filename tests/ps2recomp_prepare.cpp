#include "direct_program.hpp"
#include "hle_fixture.hpp"
#include "ps2native/aot.hpp"
#include "ps2native/aot_sources.hpp"
#include "iso_image.hpp"
#include "../tools/dev_process.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"
#include "ps2native/frontend/native_module.hpp"
#include "ps2_runtime.h"
#include "ps2recomp/recompiler_reporter.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;
using namespace ps2native;
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
std::string text(const fs::path& path) { const auto b=read_file(path);return {b.begin(),b.end()}; }
int main(int argc,char** argv) {
    if(argc!=3 && argc!=11)return 2;
    {
        ps2recomp::RecompilerReporter report;
        report.recordUnhandledInstruction("fixture",direct_test::text,5u,"strict-stop control");
        require(report.hasErrors() && report.counters().unhandledInstructions==1 && !report.counters().nonInstructionErrors,
                "Standalone instruction errors remain visible and distinguishable");
        report.error("codegen","unrelated failure control");
        require(report.counters().nonInstructionErrors==1,"Non-instruction generation errors must remain fatal");
    }
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
                try {
                    if(std::string_view(mode)=="sdk-mmi")
                        (void)write_text_if_changed(baseline,"#include <ps2native/aot_runtime.hpp>\nnamespace ps2native { const AotModule& aot_module(){ static const AotModule m{}; return m; } }\n");
                    else (void)write_text_if_changed(baseline,generate_aot_cpp(parse_ee_elf(read_file(image)),options));
                }
                catch(const std::exception& error){throw std::runtime_error(std::string("Prior native baseline ")+mode+": "+error.what());}
                const auto binary=root / "integrated-fixture.exe";
                std::vector<fs::path> native_args{"/nologo","/std:c++20","/EHsc","/O2","/Ob0","/MD","/utf-8","/bigobj",
                    "/DPS2N_NATIVE_ONLY=1","/DPS2N_SDK_NATIVE_BACKEND=1",
                    option("/I",argv[4]),option("/I",argv[7]),option("/I",source),
                    option("/I",fs::path(argv[10]).parent_path()),fs::path(argv[10]),
                    source / "ps2_recompiled_functions.cpp",source / "register_functions.cpp",baseline,
                    option("/Fo",objects / ""),option("/Fe",binary),fs::path(argv[8]),fs::path(argv[9]),
                    "kernel32.lib","user32.lib","gdi32.lib","winspool.lib","shell32.lib","ole32.lib",
                    "oleaut32.lib","uuid.lib","comdlg32.lib","advapi32.lib"};
                const auto dispatcher=source/"ps2_recompiled_functions.cpp";
                (void)aot_sources_sha256(dispatcher);
                std::istringstream manifest(text(source/"ps2_recompiled_functions.cpp.sources"));
                std::string version,hash,name;manifest>>version>>hash;unsigned partitions=0;
                while(manifest>>name>>hash){native_args.emplace_back(source/name);++partitions;}
                require(partitions>0 && (std::string_view(mode)!="boundaries" || partitions>1),
                    "Native SDK compilation must include every verified address partition");
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
            auto partial=p;
            partial.segments[0].bytes=m20::le_words({0x2402002Au,0x4BE43190u,0x24020063u,0x03E00008u,0u});
            const auto image=root/"partial.elf",prepared=root/"partial-aot";
            (void)write_binary_if_changed(image,direct_test::elf(partial));
            call(image,prepared,false,true);
            const auto metadata=nlohmann::json::parse(text(prepared/"sdk-prepare.json"));
            require(metadata.at("native_unsupported").size()==1 &&
                metadata.at("native_unsupported")[0].at("pc")==direct_test::text+4,
                "Partial preparation must identify the exact missing native instruction");
            integrated(image,prepared/"generated",partial,"partial");
            partial.segments[0].bytes=m20::le_words({0x2402002Au,0x00000005u,0x24020063u,0x03E00008u,0u});
            const auto unhandled_image=root/"unhandled.elf",unhandled_output=root/"unhandled-aot";
            (void)write_binary_if_changed(unhandled_image,direct_test::elf(partial));
            call(unhandled_image,unhandled_output,false,true);
            const auto unhandled=nlohmann::json::parse(text(unhandled_output/"sdk-prepare.json"));
            require(unhandled.at("sdk_unhandled_instructions").get<unsigned>()>0 &&
                unhandled.at("native_unsupported").size()==1 &&
                unhandled.at("native_unsupported")[0].at("pc")==direct_test::text+4,
                "Standalone emission errors must become exact native boundaries, never skipped instructions");
            integrated(unhandled_image,unhandled_output/"generated",partial,"partial");
            auto boundaries=p;std::vector<std::uint32_t> words(127,0u);
            for(const auto word:{0x10000002u,0x2402002Au,0x24020063u,0x54000001u,0x24020063u,0x03E00008u,0u})words.push_back(word);
            boundaries.segments[0].bytes=m20::le_words({0x08043F80u,0u});
            boundaries.segments.push_back({direct_test::text+65536-512,5,m20::le_words(words)});
            boundaries.roots={direct_test::text+65536-512};
            const auto boundary_image=root/"boundaries.elf",boundary_output=root/"boundaries-aot";
            (void)write_binary_if_changed(boundary_image,direct_test::elf(boundaries));
            call(boundary_image,boundary_output,false,true);
            integrated(boundary_image,boundary_output/"generated",boundaries,"boundaries");
            auto scalar=p;
            scalar.segments[0].bytes=m20::le_words({0x460208C2u,0x46030104u,0x03E00008u,0u});
            const auto scalar_image=root/"scalar.elf",scalar_output=root/"scalar-aot";
            (void)write_binary_if_changed(scalar_image,direct_test::elf(scalar));
            call(scalar_image,scalar_output,false,true);
            bool sdk_emission=false;
            for(const auto& entry:fs::directory_iterator(scalar_output/"generated"))if(entry.path().extension()==".cpp") {
                const auto source=text(entry.path());
                if(source.find("FPU_MUL_S(ctx->f[1], ctx->f[2])")!=std::string::npos &&
                   source.find("FPU_SQRT_S(ctx->f[3])")!=std::string::npos)sdk_emission=true;
            }
            require(sdk_emission,"SDK scalar translation must reach generated output without local arithmetic substitution");
            integrated(scalar_image,scalar_output/"generated",scalar,"sdk-float");
            auto packed=p;
            packed.segments[0].bytes=m20::le_words({0x70021F89u,0x70031FC9u,0x70020789u,0x70002789u,0x03E00008u,0u});
            const auto packed_image=root/"packed.elf",packed_output=root/"packed-aot";
            (void)write_binary_if_changed(packed_image,direct_test::elf(packed));
            call(packed_image,packed_output,false,true);
            const auto packed_info=nlohmann::json::parse(text(packed_output/"sdk-prepare.json"));
            require(packed_info.at("native_unsupported").empty(),"SDK packed translations must not be gated by the local decoder");
            integrated(packed_image,packed_output/"generated",packed,"sdk-mmi");
        }
        call(input,output,false,true);
        require(text(output / "sdk-prepare.json")==receipt,"reused output changed");
        const auto relocated=root/"relocated.elf";(void)write_binary_if_changed(relocated,bytes);
        call(relocated,output,false,true);
        require(text(output/"sdk-prepare.json")==receipt,"Content-addressed source reuse must preserve the verified translation");
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

        const auto unsupported=root / "malformed.elf", failed=root / "malformed-aot";
        (void)write_binary_if_changed(unsupported,std::vector<std::uint8_t>{0x7F,'E','L','F'});
        call(unsupported,failed,false,false);
        require(!fs::exists(native_io_path(failed)),"incomplete output was retained as usable");
        const auto native_log=root/"native-prepare.log",native_cache=root/"native-cache";
        auto launcher=fs::path(argv[1]).parent_path()/"ps2-native";
#ifdef _WIN32
        launcher+=".exe";
#endif
        const auto launcher_exists=file_exists(launcher);
        const auto launcher_stamp=launcher_exists ? fs::last_write_time(launcher) : fs::file_time_type{};
#ifdef _WIN32
        // Match the desktop launcher: preparation must work without a parent console.
        FreeConsole();
#endif
        const auto ticket=root/"prepare.cancel",cancel_input=root/"cancel.elf";
        auto cancel_program=p;std::vector<std::uint32_t> cancel_words(32768,0u);
        cancel_words.push_back(0x03E00008u);cancel_words.push_back(0u);
        cancel_program.segments[0].bytes=m20::le_words(cancel_words);
        (void)write_binary_if_changed(cancel_input,direct_test::elf(cancel_program));
        const auto identity=sha256_file(cancel_input)+info.at("native_backend_sha256").get<std::string>()+
            info.at("sdk_lock_sha256").get<std::string>()+"elf";
        const auto source_key=sha256_hex({reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()});
        const auto pending=native_cache/"translations"/(source_key+".pending");
        (void)write_text_if_changed(ticket,"0");bool signalled=false;
        auto cancel_arg=fs::path("--cancel-file=");cancel_arg+=ticket.native();
        const auto interrupted=dev::process(argv[1],{"prepare-native",cancel_input,native_cache,cancel_arg},
            native_log,30,16u*1024u*1024u,[&]{
                if(!signalled && file_exists(pending/"sdk/recompiler.toml")){
                    (void)write_text_if_changed(ticket,"1");signalled=true;
                }
                return false;
            });
        require(signalled && !interrupted.timed_out && interrupted.exit_code!=0 &&
            text(native_log).find("Native preparation cancelled")!=std::string::npos && !fs::exists(native_io_path(pending)),
            "SDK-stage cancellation must remove only its staging and release the preparation lock: "+text(native_log));
        const auto prepare=[&] {
            const auto result=dev::process(argv[1],{"prepare-native",input,native_cache},native_log,100,16u*1024u*1024u);
            require(!result.timed_out && !result.exit_code,text(native_log));
        };
        prepare();
        const auto native_text=text(native_log);const auto marker=native_text.rfind("native_receipt=");
        require(marker!=std::string::npos,"Native preparation did not publish a receipt");
        const auto begin=marker+15,end=native_text.find_first_of("\r\n",begin);
        const auto native_receipt=fs::u8path(native_text.substr(begin,end-begin));
        const auto native_info=nlohmann::json::parse(text(native_receipt));
        const auto native_binary=native_receipt.parent_path()/fs::u8path(native_info.at("binary").get<std::string>());
        const auto binary_stamp=fs::last_write_time(native_binary);
        {
            frontend::LoadedNativeModule module(native_receipt,hash,hash);PS2Runtime runtime;
            runtime.prepare_direct_elf(parse_ee_elf(bytes),&module.module());runtime.cpu().set_reg32_sign_extended(31,0xFFFFFFF0u);
            BootOptions budget;budget.max_instructions=100;budget.stop_at=0xFFFFFFF0u;
            const auto report=runtime.run(&module.module(),budget);
            require(report.outcome==BootOutcome::StopAddress && report.retired==3 && report.aot_instructions==3 &&
                !report.interpreted_instructions && runtime.cpu().reg32(2)==42,"Cold native preparation must execute its generated module");
        }
        prepare();
        require(fs::last_write_time(native_binary)==binary_stamp && text(native_log).find("Native module reused")!=std::string::npos,
            "Verified native module reuse must not recompile the content");
        auto discovery_info=nlohmann::json::parse(text(native_receipt));
        require(discovery_info.at("discovery").is_object() && discovery_info.at("discovery").at("runtime_sha256").is_string() &&
            discovery_info.at("discovery").at("diagnostic_spu2_effect_end_reset").is_null(),
            "Discovery publishes a keyed object, including explicit absent reset inputs");
        discovery_info["discovery"]=nlohmann::json::array({"legacy malformed discovery"});
        (void)write_text_if_changed(native_receipt,discovery_info.dump(2));
        prepare();
        require(fs::last_write_time(native_binary)==binary_stamp &&
            nlohmann::json::parse(text(native_receipt)).at("discovery").is_object(),
            "Malformed legacy discovery metadata must be recomputed without discarding a verified native binary");
        require(!launcher_exists || fs::last_write_time(launcher)==launcher_stamp,"Preparing content must not rebuild the launcher");
        {
        const auto dynamic_input=root/"dynamic.elf";
        constexpr auto dynamic_alias=0x80000000u;
        const auto dynamic_program=direct_test::build("dynamic",dynamic_alias);
        const auto dynamic_bytes=direct_test::elf(dynamic_program);
        (void)write_binary_if_changed(dynamic_input,dynamic_bytes);
        const auto dynamic_result=dev::process(argv[1],{"prepare-native",dynamic_input,native_cache},native_log,150,16u*1024u*1024u);
        require(!dynamic_result.timed_out && !dynamic_result.exit_code,text(native_log));
        const auto dynamic_text=text(native_log);const auto dynamic_marker=dynamic_text.rfind("native_receipt=");
        require(dynamic_marker!=std::string::npos,"Dynamic preparation did not publish its parent receipt");
        const auto line_end=dynamic_text.find_first_of("\r\n",dynamic_marker+15);
        const auto dynamic_receipt=fs::u8path(dynamic_text.substr(dynamic_marker+15,line_end-dynamic_marker-15));
        const auto dynamic_info=nlohmann::json::parse(text(dynamic_receipt));
        require(dynamic_info.at("overlays").size()==1,"Preparation discovers one witnessed copied-code region");
        const auto dynamic_hash=sha256_hex(dynamic_bytes);
        frontend::LoadedNativeModule dynamic_module(dynamic_receipt,dynamic_hash,dynamic_hash);
        PS2Runtime dynamic_runtime;dynamic_runtime.prepare_hle_elf(parse_ee_elf(dynamic_bytes),&dynamic_module.module());
        BootOptions dynamic_options;dynamic_options.max_instructions=1000;
        dynamic_options.stop_at=dynamic_program.stop+16u;
        const auto copied=dynamic_runtime.run(&dynamic_module.module(),dynamic_options);
        require(copied.outcome==BootOutcome::BlockPoint && copied.pc==(direct_test::dynamic_address|dynamic_alias) && !copied.interpreted_instructions,
            "Play stops strictly before entering a copied function");
        dynamic_runtime.memory().write32(direct_test::dynamic_address+4u,0);
        require(!dynamic_module.activate_overlays(dynamic_runtime.memory(),copied.pc,dynamic_options),
            "A matching first instruction cannot activate an overlay with different later bytes");
        dynamic_runtime.memory().write32(direct_test::dynamic_address+4u,0x03E00008u);
        require(dynamic_module.activate_overlays(dynamic_runtime.memory(),copied.pc,dynamic_options),"Verified offline overlay activates in the same runtime");
        const auto dynamic_run=dynamic_runtime.run(&dynamic_module.module(),dynamic_options);
        require(dynamic_run.outcome==BootOutcome::StopAddress && dynamic_runtime.cpu().reg32(2)==42 &&
            !dynamic_run.interpreted_instructions,"Copied guest function returns through native AOT without compiler or interpreter in Play");
        const auto region=native_cache/"regions"/(dynamic_info.at("overlays")[0].at("image_sha256").get<std::string>()+".elf");
        auto region_bytes=read_file(region);region_bytes.back()^=1;(void)write_binary_if_changed(region,region_bytes);
        bool refused=false;
        try{frontend::LoadedNativeModule changed(dynamic_receipt,dynamic_hash,dynamic_hash);}
        catch(const std::exception& error){refused=std::string(error.what()).find("SHA-256 mismatch")!=std::string::npos;}
        require(refused,"Tampered overlay input is refused before activation");
        region_bytes.back()^=1;(void)write_binary_if_changed(region,region_bytes);
        } // Windows DLL handles must close before removing the owned fixture cache.
        fs::remove_all(native_io_path(root));
        std::cout<<"SDK ELF/disc preparation, Unicode/long paths, reuse, tamper and unsupported-code controls passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';fs::remove_all(native_io_path(root));return 1;}
}
