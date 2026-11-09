#include "dev_profile.hpp"
#include "ee_alias_cli.hpp"
#include "dev_process.hpp"
#include "ps2native/bios.hpp"
#include "ps2native/hle.hpp"
#include "ps2native/iop_aot.hpp"
#include "sdk/iop_module_interface.hpp"
#include "sdk/console_interface.hpp"
#include "dvd_geometry_cli.hpp"
#include "ps2native/disc.hpp"
#include "ps2native/analysis.hpp"
#include "ps2native/aot_sources.hpp"
#include "ps2native/frontend/native_module.hpp"
#include "ps2native/native_module_abi.hpp"
#include "ps2_runtime.h"
#include "ps2native/frontend/content.hpp"
#include "ps2recomp/config_manager.h"
#include <nlohmann/json.hpp>
#ifdef _MSC_VER
#include "native_toolchain.hpp"
#endif
#include <algorithm>
#include <chrono>
#include <iostream>

namespace ps2native::dev {
int prepare_ps2recomp(const std::filesystem::path&,const std::filesystem::path&,bool,std::uint32_t=0);
}

namespace {
using namespace ps2native;
using namespace ps2native::dev;
fs::path option(std::string_view prefix, const fs::path& path) { auto out = cli::utf8_path(prefix); out += path.native(); return out; }
fs::path executable(const fs::path& build, const std::string& config, std::string name) {
#ifdef _WIN32
    name += ".exe";
#endif
    if (file_exists(build / config / name)) return build / config / name;
    if (file_exists(build / name)) return build / name;
    throw std::runtime_error("build did not produce " + name);
}
std::string source_identity(const fs::path& source) {
    std::vector<fs::path> paths;
    for (const auto* directory : {"include","src","tools","cmake"})
        for (const auto& e : fs::recursive_directory_iterator(native_io_path(source / directory)))
            if (e.is_regular_file()) paths.push_back(display_path(e.path()));
    paths.push_back(source / "CMakeLists.txt");
    std::sort(paths.begin(),paths.end());
    std::string entries;
    for (const auto& path : paths) entries += path_utf8(path.lexically_relative(source))+'\n'+sha256_file(path)+'\n';
    return digest(entries);
}
std::string identify(const fs::path& input, const std::string& requested) {
    if (requested != "auto" && requested != "elf" && requested != "bios" && requested != "disc")
        throw std::runtime_error("kind must be auto, elf, bios or disc");
    InputFile file(input); std::array<std::uint8_t,4> magic{}; (void)file.read_at(0,magic);
    if(requested=="auto" && magic!=std::array<std::uint8_t,4>{0x7f,'E','L','F'} && file.size()==Memory::kBiosRomBytes) {
        std::string rom_error;
        try{(void)load_bios_rom0(read_file(input));return "bios";}catch(const std::exception& e){rom_error=e.what();}
        try{(void)DiscImage(input).boot_elf();return "disc";}catch(const std::exception& e){
            throw std::runtime_error("unrecognized 4-MiB input; ROM0: "+rom_error+"; disc: "+e.what());
        }
    }
    const std::string kind = requested != "auto" ? requested :
        magic == std::array<std::uint8_t,4>{0x7f,'E','L','F'} ? "elf" : file.size()==Memory::kBiosRomBytes ? "bios" : "disc";
    if (kind == "elf") (void)parse_ee_elf(read_file(input));
    else if (kind == "bios") (void)load_bios_rom0(read_file(input));
    else (void)DiscImage(input).boot_elf();
    return kind;
}
int prepare_iop(const fs::path& input_path,const fs::path& cache_root,const fs::path& cancel) {
    using nlohmann::json;
    const auto input=fs::absolute(input_path);
    if(regular_file_size(input)>2u*1024u*1024u+16u)throw std::runtime_error("IOP executable capture exceeds its 1-MiB memory limit");
    const auto bytes=read_file(input);
    const std::array<std::uint8_t,8> magic{'P','S','2','N','I','O','P',1};
    if(bytes.size()<16u || !std::equal(magic.begin(),magic.end(),bytes.begin()))throw std::runtime_error("IOP preparation needs a verified loaded-image capture");
    const auto word=[&](unsigned at){std::uint32_t value=0;for(unsigned b=0;b<4u;++b)value|=std::uint32_t{bytes.at(at+b)}<<(b*8u);return value;};
    const auto base=word(8),count=word(12);
    if(!count || count>262144u || (base&3u) || base>=kIopRamBytes || std::uint64_t{count}*4u>kIopRamBytes-base ||
       bytes.size()!=16ull+std::uint64_t{count}*8u)throw std::runtime_error("Invalid loaded IOP executable bounds");
    const auto hash=sha256_hex(bytes);const auto root=fs::absolute(cache_root);
    const auto cache=root/native_iop_cache_key(hash,native_runtime_abi()),receipt=cache/"native-iop-prepare.json";
    ps2native::create_directories(cache);
    if(file_exists(receipt)){frontend::NativeIopCode verified(receipt,hash);std::cout<<"native_iop_receipt="<<path_utf8(receipt)<<"\nNative IOP module reused\n";return 0;}
    BuildLock lock(fs::path(PS2N_DEV_BUILD));
    const auto cancelled=[&]() noexcept {
        if(cancel.empty())return false;
        try {InputFile ticket(cancel);std::array<std::uint8_t,1> state{};return ticket.read_at(0,state)!=1 || state[0]=='1';}
        catch(...){return true;}
    };
    if(cancelled())throw std::runtime_error("Native preparation cancelled");
    std::vector<std::uint32_t> entries;entries.reserve(count);
    for(unsigned n=0;n<count;++n) {
        if(word(20u+n*8u))throw std::runtime_error("IOP executable capture contains unknown bits");
        entries.push_back(base+n*4u);
    }
    const auto source=cache/"generated"/"iop.cpp";
    const auto generated=generate_iop_aot_sources(bytes,base,entries,"native-iop",4096u);
    (void)write_aot_sources(source,generated);
#ifdef _MSC_VER
    _wputenv_s(L"INCLUDE",fs::u8path(PS2N_COMPILER_INCLUDE).c_str());
    _wputenv_s(L"LIB",fs::u8path(PS2N_COMPILER_LIB).c_str());
    _wputenv_s(L"LIBPATH",fs::u8path(PS2N_COMPILER_LIBPATH).c_str());
    _wputenv_s(L"PATH",fs::u8path(PS2N_COMPILER_PATH).c_str());
#endif
    const auto invoke=[&](const std::vector<fs::path>& args,const char* log,unsigned seconds,bool allow_cancel=true) {
        const auto result=process(fs::path(PS2N_DEV_CMAKE),args,cache/log,seconds,4u*1024u*1024u,
            allow_cancel?std::function<bool()>(cancelled):std::function<bool()>{});
        if(result.cancelled)throw std::runtime_error("Native preparation cancelled");
        if(result.exit_code || result.timed_out)throw std::runtime_error("Native IOP preparation failed; see "+path_utf8(cache/log));
    };
    const auto cmake_path=[](const char* prefix,const fs::path& path){const auto bytes=path.generic_u8string();return cli::utf8_path(std::string(prefix)+std::string(bytes.begin(),bytes.end()));};
    const auto reset=[&]{invoke({"-S",fs::path(PS2N_DEV_SOURCE),"-B",fs::path(PS2N_DEV_BUILD),
        "-DPS2N_IOP_CONTENT_SOURCE=","-DPS2N_IOP_CONTENT_OUTPUT="},"reset.log",120,false);};
    try {
        invoke({"-S",fs::path(PS2N_DEV_SOURCE),"-B",fs::path(PS2N_DEV_BUILD),
            cmake_path("-DPS2N_IOP_CONTENT_SOURCE:FILEPATH=",source),cmake_path("-DPS2N_IOP_CONTENT_OUTPUT:PATH=",cache)},"configure.log",120);
        invoke({"--build",fs::path(PS2N_DEV_BUILD),"--target","ps2n_iop_content_module","--config",PS2N_DEV_CONFIG,"--parallel","4"},"build.log",600);
    }catch(...){reset();throw;}
    reset();
    if(cancelled())throw std::runtime_error("Native preparation cancelled");
    if(sha256_file(input)!=hash)throw std::runtime_error("IOP executable capture changed during preparation");
    const auto binary=cache/(std::string("native-iop")+PS2N_MODULE_SUFFIX);
    json info{{"schema","ps2n-native-iop-module-v1"},{"runtime_abi",native_runtime_abi()},{"image_sha256",hash},
        {"base",base},{"bytes",count*4u},{"binary",path_utf8(binary.filename())},{"binary_sha256",sha256_file(binary)}};
    const auto text=info.dump(2)+"\n";
    write_file_atomically(receipt,{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});
    try {frontend::NativeIopCode verified(receipt,hash);}catch(...){(void)ps2native::remove_file(receipt);throw;}
    std::cout<<"native_iop_receipt="<<path_utf8(receipt)<<"\nNative IOP module prepared offline\n";return 0;
}
int prepare_native(const fs::path& input_path,const fs::path& cache_root,bool disc,const fs::path& cancel_file,std::uint32_t alias=0) {
    using nlohmann::json;
    if(std::string_view(native_runtime_abi())!=PS2N_HOST_NATIVE_ABI)
        throw std::runtime_error("Host executable and shared runtime ABI differ; rebuild the launcher and tools");
    const auto cancelled=[&]() noexcept {
        if(cancel_file.empty())return false;
        try {InputFile file(cancel_file);std::array<std::uint8_t,1> state{};return file.read_at(0,state)!=0 && state[0]=='1';}
        catch(...) {return true;}
    };
    const auto input=fs::absolute(input_path);
    const auto input_hash=sha256_file(input);
    const auto image=disc ? DiscImage(input).boot_elf() : parse_ee_elf(read_file(input));
    if(alias && disc)throw std::runtime_error("execution alias requires a stripped ELF");
    if(alias)(void)ps2native::ee_execution_alias(image,alias);
    const auto alias_key=alias?":"+std::to_string(alias):"";
    const auto elf_hash=sha256_hex(image.bytes);
    const auto root=fs::absolute(cache_root);
    const auto cache=root/digest(input_hash+native_runtime_abi()+alias_key);
    ps2native::create_directories(cache);
    const auto receipt=cache/"native-prepare.json";
    if(file_exists(receipt)) {
        frontend::LoadedNativeModule verified(receipt,input_hash,elf_hash,alias);
        std::cout<<"native_receipt="<<path_utf8(receipt)<<"\nNative module reused; compatibility remains unverified\n";
        return 0;
    }
    BuildLock lock(fs::path(PS2N_DEV_BUILD));
    if(cancelled())throw std::runtime_error("Native preparation cancelled");
    const auto sdk=root/"translations"/digest(input_hash+PS2N_PS2RECOMP_BACKEND_SHA256+PS2N_PS2RECOMP_LOCK_SHA256+(disc?"disc":"elf")+alias_key);
#ifdef _MSC_VER
    // Reuse the configured toolchain when launched from the desktop, outside vcvars.
    _wputenv_s(L"INCLUDE",fs::u8path(PS2N_COMPILER_INCLUDE).c_str());
    _wputenv_s(L"LIB",fs::u8path(PS2N_COMPILER_LIB).c_str());
    _wputenv_s(L"LIBPATH",fs::u8path(PS2N_COMPILER_LIBPATH).c_str());
    _wputenv_s(L"PATH",fs::u8path(PS2N_COMPILER_PATH).c_str());
#endif
    const auto invoke=[&](const fs::path& program,const std::vector<fs::path>& arguments,const char* log,unsigned seconds) {
        const auto result=process(program,arguments,cache/log,seconds,64u*1024u*1024u,cancelled);
        if(result.cancelled)throw std::runtime_error("Native preparation cancelled");
        if(result.exit_code || result.timed_out) {
            std::string tail;
            try {
                InputFile file(cache/log);std::vector<std::uint8_t> bytes(static_cast<std::size_t>(std::min<std::uint64_t>(file.size(),4096)));
                (void)file.read_at(file.size()-bytes.size(),bytes);tail.assign(bytes.begin(),bytes.end());
            }catch(const std::exception&) {}
            throw std::runtime_error((result.timed_out ? "Native preparation exceeded "+std::to_string(seconds)+" seconds" : "Native preparation failed")+
                "; see "+path_utf8(cache/log)+"\n"+tail);
        }
    };
    if(fs::exists(native_io_path(sdk))) {
        (void)ps2native::dev::prepare_ps2recomp(input,sdk,disc,alias);
    } else {
        ps2native::create_directories(sdk.parent_path());
        auto staging=sdk;staging+=".pending";
        if(!fs::create_directory(native_io_path(staging)))throw std::runtime_error("Native source staging already exists; verify the previous preparation has stopped");
        struct Cleanup {
            fs::path path;
            ~Cleanup(){std::error_code error;fs::remove_all(native_io_path(path),error);}
        } cleanup{staging};
        const auto staged=staging/"sdk";
        std::vector<fs::path> args{"prepare-sdk",input,staged};if(disc)args.emplace_back("--disc");
        if(alias)args.emplace_back("--execution-alias="+std::to_string(alias));
        invoke(executable(fs::path(PS2N_DEV_BUILD),PS2N_DEV_CONFIG,"ps2n-dev"),args,"sdk.log",600);
        const auto bytes=read_file(staged/"sdk-prepare.json");auto source=json::parse(bytes.begin(),bytes.end());
        if(source.at("input_sha256")!=input_hash || source.at("elf_sha256")!=elf_hash || source.value("execution_alias",0u)!=alias)
            throw std::runtime_error("Content changed during native source preparation");
        // Publish stable paths with the verified sources; originals remain in place.
        ps2recomp::ConfigManager manager(path_utf8(native_io_path(staged/"recompiler.toml")));
        auto config=manager.loadConfig();
        config.inputPath=path_utf8(native_io_path(disc ? sdk/"boot.elf" : input));
        config.outputPath=path_utf8(native_io_path(sdk/"generated"));manager.saveConfig(config);
        source["files"]["recompiler.toml"]=sha256_file(staged/"recompiler.toml");
        (void)write_text_if_changed(staged/"sdk-prepare.json",source.dump(2)+"\n");
        fs::rename(native_io_path(staged),native_io_path(sdk));
    }
    if(cancelled())throw std::runtime_error("Native preparation cancelled");
    const auto reset_configuration=[&] {
        const auto result=process(fs::path(PS2N_DEV_CMAKE),{"-S",fs::path(PS2N_DEV_SOURCE),"-B",fs::path(PS2N_DEV_BUILD),
            "-DPS2N_CONTENT_RECEIPT=","-DPS2N_CONTENT_OUTPUT="},cache/"reset.log",120,64u*1024u*1024u);
        if(result.exit_code || result.timed_out)throw std::runtime_error("Cannot reset the transient native module build selection");
    };
    const auto cmake_path=[](const char* prefix,const fs::path& path) {
        const auto bytes=path.generic_u8string();return cli::utf8_path(std::string(prefix)+std::string(bytes.begin(),bytes.end()));
    };
    try {
        invoke(fs::path(PS2N_DEV_CMAKE),{"-S",fs::path(PS2N_DEV_SOURCE),"-B",fs::path(PS2N_DEV_BUILD),
            cmake_path("-DPS2N_CONTENT_RECEIPT:FILEPATH=",sdk/"sdk-prepare.json"),cmake_path("-DPS2N_CONTENT_OUTPUT:PATH=",cache)},"configure.log",120);
        invoke(fs::path(PS2N_DEV_CMAKE),{"--build",fs::path(PS2N_DEV_BUILD),"--target","ps2n_content_module","--config",PS2N_DEV_CONFIG,"--parallel","6"},"build.log",600);
    } catch(...) {reset_configuration();throw;}
    reset_configuration();
    // Recheck immutable inputs and generated sources after the compiler has read them.
    (void)ps2native::dev::prepare_ps2recomp(input,sdk,disc,alias);
    const auto binary=cache/(std::string("native-game")+PS2N_MODULE_SUFFIX);
    const auto sdk_bytes=read_file(sdk/"sdk-prepare.json");
    const auto sdk_info=json::parse(sdk_bytes.begin(),sdk_bytes.end());
    json info{{"schema","ps2n-native-module-v1"},{"runtime_abi",native_runtime_abi()},
        {"input_sha256",input_hash},{"elf_sha256",elf_hash},{"execution_alias",alias},{"binary",path_utf8(binary.filename())},
        {"binary_sha256",sha256_file(binary)},{"translation_receipt",path_utf8(sdk/"sdk-prepare.json")},
        {"native_unsupported_count",sdk_info.at("native_unsupported").size()}};
    if(sha256_file(input)!=input_hash)throw std::runtime_error("Content changed while preparing its native module");
    const auto text=info.dump(2)+"\n";
    write_file_atomically(receipt,std::span(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));
    try {frontend::LoadedNativeModule verified(receipt,input_hash,elf_hash,alias);}
    catch(...) {std::error_code error;fs::remove(receipt,error);throw;}
    std::cout<<"native_receipt="<<path_utf8(receipt)<<"\nNative module prepared; compatibility remains unverified\n";
    return 0;
}
std::uint64_t disk_bytes(const fs::path& root) {
    std::uint64_t n = 0;
    for (const auto& e : fs::recursive_directory_iterator(native_io_path(root)))
        if (e.is_regular_file() && !e.is_symlink()) n += e.file_size();
    return n;
}
int discover_native(const fs::path& input_path,const fs::path& cache_root,bool disc,const fs::path& cancel,
                    const fs::path& rom0={},const std::string& rom0_hash={},const std::string& profile={},const fs::path& nvm={},const std::string& nvm_hash={},
                    std::optional<DiscImage::DiagnosticDvdGeometry> geometry={},bool diagnostic_cache_uncached_hits=false,bool spu2_7ca_latch=false,bool spu2_quiescent_reset=false,std::uint32_t spu2_pio_word_cycles=0,bool ee_early_digit_carry=false,std::uint64_t discovery_budget=50000000u,bool spu2_reserved_latches=false,std::optional<std::array<std::uint16_t,2>> spu2_effect_end={}) {
    using nlohmann::json;
    const auto input=fs::absolute(input_path),root=fs::absolute(cache_root);
    const auto input_hash=sha256_file(input);
    const auto media=disc?std::make_shared<DiscImage>(input,geometry):nullptr;
    const auto image=media?media->boot_elf():parse_ee_elf(read_file(input));
    const auto elf_hash=sha256_hex(image.bytes);
    const auto cache=root/digest(input_hash+native_runtime_abi()),receipt=cache/"native-prepare.json";
    BuildLock publication(cache);
    const auto bytes=read_file(receipt);auto info=json::parse(bytes.begin(),bytes.end());
    const auto tool=executable(fs::path(PS2N_DEV_BUILD),PS2N_DEV_CONFIG,"ps2n-dev");
    std::shared_ptr<const BiosImage> bios;
    if(!rom0.empty())bios=std::make_shared<const BiosImage>(load_bios_rom0(read_file(rom0),
        BiosLoadOptions{rom0_hash.empty()?std::nullopt:std::optional<std::string>{rom0_hash}}));
    const auto console=load_hle_console(profile,nvm,nvm_hash);
    if(console.hardware && (!bios || console.hardware->romver!=bios->version.romver))throw std::runtime_error("Console profile and HLE BIOS ROMVER differ");
    const auto geometryKey=geometry?std::to_string(geometry->layer0_sectors)+(geometry->opposite_track_path?":otp":":ptp"):"no-diagnostic-geometry";
    const auto runtime_hash=digest(sha256_file(fs::path(PS2N_CORE_FILE))+sha256_file(tool)+(bios?bios->sha256:"no-rom0")+profile+(console.nvm?console.nvm->sha256:"no-nvm")+geometryKey+(diagnostic_cache_uncached_hits?":uncached-cache-hits":":strict-cache-hits")+(spu2_effect_end?":spu2-eea:"+std::to_string((*spu2_effect_end)[0])+":"+std::to_string((*spu2_effect_end)[1]):":unknown-spu2-eea")+(spu2_reserved_latches?":spu2-reserved-latches":":strict-spu2-reserved")+(spu2_7ca_latch?":spu2-7ca-latch":":strict-spu2-7ca")+(spu2_quiescent_reset?":spu2-quiescent-reset":":unknown-spu2-reset")+":spu2-pio-cycles:"+std::to_string(spu2_pio_word_cycles)+(ee_early_digit_carry?":ee-early-digit-carry":":strict-ee-carry"));
    const auto cancelled=[&]() noexcept {
        if(cancel.empty())return false;
        try {InputFile ticket(cancel);std::array<std::uint8_t,1> state{};
            return ticket.read_at(0,state)!=1 || state[0]=='1';}catch(...){return true;}
    };
    if(cancelled())throw std::runtime_error("Native preparation cancelled");
    frontend::LoadedNativeModule initial(receipt,input_hash,elf_hash);
    auto overlays=info.value("overlays",json::array());
    auto iop_modules=info.value("iop_modules",json::array());
    for(const auto& overlay:overlays) {
        const auto hash=overlay.at("image_sha256").get<std::string>();
        const auto alias=overlay.value("execution_alias",0u);
        frontend::LoadedNativeModule verified(root/digest(hash+native_runtime_abi()+(alias?":"+std::to_string(alias):""))/"native-prepare.json",hash,hash,alias);
    }
    if(info.contains("discovery") && info["discovery"].is_object() && info["discovery"].value("runtime_sha256","")==runtime_hash &&
       (info["discovery"].value("outcome","")!="instruction budget reached" ||
        info["discovery"].value("retired",std::uint64_t{})>=discovery_budget)) {
        std::cout<<"Native discovery reused; compatibility remains unverified\n";return 0;
    }
    // One write bitmap, no RAM dump history. Unwritten reset padding is never promoted to code.
    struct Writes final:BusObserver {
        std::vector<bool> bytes=std::vector<bool>(Memory::kMainRamBytes);
        void on_bus_access(const BusAccess& access) override {
            if(!access.write || access.region!=BusRegion::MainRam)return;
            for(unsigned i=0;i<std::min<unsigned>(access.bytes,8);++i)
                if(std::uint64_t{access.physical_address}+i<bytes.size())
                    bytes[access.physical_address+i]=!(access.unknown_bits&(0xFFull<<(i*8u)));
        }
        bool word(std::uint32_t address) const {
            return address<=bytes.size()-4 && bytes[address] && bytes[address+1] && bytes[address+2] && bytes[address+3];
        }
    };
    const auto start=std::chrono::steady_clock::now();
    const auto rtcUtc=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    auto compilation_time=std::chrono::steady_clock::duration::zero();
    const auto elapsed=[&]{return std::chrono::steady_clock::now()-start-compilation_time;};
    BootReport boundary;
    constexpr unsigned max_iop_modules=64u,max_ee_regions=8u;
    // Each iteration can discover one dependency; allow a final execution after all of them.
    for(unsigned attempt=0;attempt<=max_iop_modules+max_ee_regions;++attempt) {
        if(cancelled())throw std::runtime_error("Native preparation cancelled");
        if(attempt && elapsed()>std::chrono::seconds(240)) {
            boundary.reason="Native discovery reached its 240-second limit";break;
        }
        frontend::LoadedNativeModule loaded(receipt,input_hash,elf_hash);
        PS2Runtime runtime;runtime.prepare_hle_elf(image,&loaded.module());
        if(ee_early_digit_carry)runtime.memory().diagnostic_ee_early_digit_carry();
        if(diagnostic_cache_uncached_hits)runtime.memory().diagnostic_data_cache(true);
        if(spu2_pio_word_cycles)const_cast<Iop&>(runtime.memory().iop()).diagnostic_spu2_pio_word_cycles(spu2_pio_word_cycles);
        if(spu2_quiescent_reset)configure_hle_spu2_quiescent_reset(runtime.memory());
        if(spu2_effect_end)const_cast<Iop&>(runtime.memory().iop()).diagnostic_spu2_effect_end_reset(*spu2_effect_end);
        if(spu2_reserved_latches)const_cast<Iop&>(runtime.memory().iop()).diagnostic_spu2_reserved_latches();
        if(spu2_7ca_latch)const_cast<Iop&>(runtime.memory().iop()).diagnostic_spu2_7ca_latch();
        runtime.memory().configure_rtc(rtcUtc);
        if(bios)runtime.memory().hle_kernel()->configure_bios(bios);
        configure_hle_console(runtime.memory(),console.hardware,console.nvm);
        if(media) {
            const auto probe=frontend::probe_disc(input);
            if(probe.status!=frontend::ProbeStatus::Valid)throw std::runtime_error("Disc refused: "+probe.error);
            runtime.memory().mount_disc(media,probe.info.at("media")=="DVD"?Iop::DiscMedium::Ps2Dvd:Iop::DiscMedium::Ps2Cd,true);
        }
        Writes writes;BootOptions options;options.max_instructions=100000;options.max_trace_events=1;
        options.tail_instructions=0;options.copy_device_histories=false;options.preparation_writes=&writes;
        std::uint64_t retired=0;
        do {
            if(cancelled())throw std::runtime_error("Native preparation cancelled");
            if(elapsed()>std::chrono::seconds(240)) {
                boundary.outcome=BootOutcome::BudgetExhausted;boundary.reason="Native discovery reached its 240-second limit";break;
            }
            options.max_instructions=std::min<std::uint64_t>(100000,discovery_budget-retired);
            boundary=runtime.run(&loaded.module(),options);retired+=boundary.retired;
            if(boundary.outcome==BootOutcome::BlockPoint) {
                if(loaded.activate_overlays(runtime.memory(),boundary.pc,options))continue;
                break;
            }
        }while(retired<discovery_budget);
        boundary.retired=retired;
        if(boundary.outcome==BootOutcome::BlockPoint && boundary.reason.find("missing IOP AOT:")!=std::string::npos) {
            const auto& iop=runtime.memory().iop();const auto region=iop.native_module_range(iop.pc());
            if(!region || (region->first&3u) || !region->second || region->second>1024u*1024u) {
                boundary.reason+="; no bounded SDK executable range at this IOP PC";break;
            }
            if(iop_modules.size()>=max_iop_modules){boundary.reason+="; native discovery reached its sixty-four-IOP-module limit";break;}
            std::vector<ObservedWord> words;
            for(unsigned offset=0;offset<(region->second+3u)/4u*4u;offset+=4u)
                words.push_back(iop.diagnostic_ram_observed(region->first+offset));
            const auto snapshot=encode_iop_aot_image(region->first,words);const auto hash=sha256_hex(snapshot);
            if(std::any_of(iop_modules.begin(),iop_modules.end(),[&](const auto& item){return item.at("image_sha256")==hash;})) {
                boundary.reason+="; prepared IOP range still has no translation for this instruction";break;
            }
            const auto file=root/"iop-regions"/(hash+".iop");
            if(file_exists(file)){if(sha256_file(file)!=hash)throw std::runtime_error("Native IOP region SHA-256 mismatch");}
            else write_file_atomically(file,snapshot);
            std::vector<fs::path> args{"prepare-iop",file,root};if(!cancel.empty())args.push_back(option("--cancel-file=",cancel));
            const auto before=std::chrono::steady_clock::now();
            const auto built=process(tool,args,cache/"iop-build.log",900,4u*1024u*1024u,cancelled);
            compilation_time+=std::chrono::steady_clock::now()-before;
            if(built.cancelled || cancelled())throw std::runtime_error("Native preparation cancelled");
            if(built.exit_code || built.timed_out)throw std::runtime_error("Native IOP preparation failed; see "+path_utf8(cache/"iop-build.log"));
            iop_modules.push_back({{"image_sha256",hash}});info["iop_modules"]=iop_modules;info.erase("discovery");
            const auto text=info.dump(2)+"\n";write_file_atomically(receipt,{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});
            std::cout<<"Native IOP range prepared offline: PC="<<iop.pc()<<" bytes="<<region->second<<'\n';continue;
        }
        if(boundary.outcome!=BootOutcome::BlockPoint ||
           (!boundary.reason.starts_with("missing EE AOT:") && boundary.reason.find("code modified")==std::string::npos))break;
        if(overlays.size()>=max_ee_regions){boundary.reason+="; native discovery reached its eight-region limit";break;}
        const auto pc=boundary.pc,physical=pc>=0x80000000u && pc<0xC0000000u?pc&0x1FFFFFFFu:pc;
        if(!writes.word(physical)){boundary.reason+="; no witnessed guest write to this entry; snapshot refused";break;}
        auto begin=physical,end=physical+4;
        while(begin>(physical&~4095u) && writes.word(begin-4))begin-=4;
        while(end<(physical&~4095u)+4096u && writes.word(end))end+=4;
        const auto address=pc-(physical-begin);
        std::vector<std::uint8_t> code;
        for(auto at=address;at<address+end-begin;at+=4) {
            const auto word=runtime.memory().fetch32(at);
            for(unsigned b=0;b<4;++b)code.push_back(static_cast<std::uint8_t>(word>>(b*8u)));
        }
        auto snapshot=snapshot_ee_code(runtime.memory(),address,end-begin,sha256_hex(code));
        for(unsigned b=0;b<4;++b)snapshot.bytes[24+b]=static_cast<std::uint8_t>(physical>>(b*8u));
        snapshot=parse_ee_elf(snapshot.bytes);
        const auto hash=sha256_hex(snapshot.bytes);
        const auto alias=pc&0xE0000000u;
        if(std::any_of(overlays.begin(),overlays.end(),[&](const auto& item){return item.at("image_sha256")==hash && item.value("execution_alias",0u)==alias;})) {
            boundary.reason+="; prepared region still has no translation at this PC";break;
        }
        const auto file=root/"regions"/(hash+".elf");ps2native::create_directories(file.parent_path());
        if(file_exists(file)) {if(sha256_file(file)!=hash)throw std::runtime_error("Native region cache SHA-256 mismatch");}
        else write_file_atomically(file,snapshot.bytes);
        std::vector<fs::path> args{"prepare-native",file,root,"--artifact-only"};
        if(alias)args.emplace_back("--execution-alias="+std::to_string(alias));
        if(!cancel.empty())args.push_back(option("--cancel-file=",cancel));
        const auto before=std::chrono::steady_clock::now();
        const auto built=process(tool,args,cache/"overlay-build.log",180,64u*1024u*1024u,cancelled);
        compilation_time+=std::chrono::steady_clock::now()-before;
        if(built.cancelled || cancelled())throw std::runtime_error("Native preparation cancelled");
        if(built.exit_code || built.timed_out)throw std::runtime_error("Native overlay preparation failed; see "+path_utf8(cache/"overlay-build.log"));
        overlays.push_back({{"image_sha256",hash},{"execution_alias",alias}});info["overlays"]=overlays;info.erase("discovery");
        const auto text=info.dump(2)+"\n";
        write_file_atomically(receipt,std::span(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));
        std::cout<<"Native region prepared offline: PC="<<pc<<" bytes="<<code.size()<<'\n';
    }
    if(sha256_file(input)!=input_hash)throw std::runtime_error("Content changed during native discovery");
    if(boundary.outcome==BootOutcome::BudgetExhausted && boundary.reason.empty())
        boundary.reason="Native discovery reached its "+std::to_string(discovery_budget)+"-instruction startup limit";
    info["discovery"]={{"diagnostic_spu2_effect_end_reset",spu2_effect_end?json(*spu2_effect_end):json(nullptr)},
        {"runtime_sha256",runtime_hash},{"instruction_budget",discovery_budget},{"diagnostic_ee_early_digit_carry",ee_early_digit_carry},{"diagnostic_cache_uncached_hits",diagnostic_cache_uncached_hits},{"diagnostic_spu2_reserved_latches",spu2_reserved_latches},{"diagnostic_spu2_7ca_latch",spu2_7ca_latch},{"diagnostic_spu2_quiescent_reset",spu2_quiescent_reset},{"diagnostic_spu2_pio_word_cycles",spu2_pio_word_cycles},{"pc",boundary.pc},{"retired",boundary.retired},
        {"rtc_epoch_utc",rtcUtc},
        {"console_profile",profile},{"nvm_sha256",console.nvm?console.nvm->sha256:""},
        {"diagnostic_dvd_geometry",geometryKey},
        {"system_rom0_sha256",bios?bios->sha256:""},
        {"reason",boundary.reason.substr(0,4096)},{"outcome",boot_outcome_name(boundary.outcome)}};
    const auto text=info.dump(2)+"\n";
    write_file_atomically(receipt,std::span(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));
    std::cout<<"Native discovery stopped: "<<boundary.reason<<"; compatibility remains unverified\n";
    return 0;
}
int run(Profile p, const fs::path& replay, bool prepare_only=false) {
    const auto source=p.path("source"), build=p.path("build"), input=p.path("input"), cmake=p.path("cmake");
    const auto config=p.get("config","Release"), engine=p.get("engine","native");
    if (engine!="native" && engine!="aot") throw std::runtime_error("engine must be native or aot (offline AOT only)");
    constexpr bool use_aot=true;
    if (config.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)
        throw std::runtime_error("invalid build configuration");
    const auto timeout = static_cast<unsigned>(cli::decimal(p.get("timeout","300"),86400));
    if (!timeout) throw std::runtime_error("timeout must be nonzero");
    if (!cli::decimal(p.get("ring","256"),1'000'000)) throw std::runtime_error("ring must be nonzero");
    (void)cli::decimal(p.get("stream_bytes","0"),1ull<<40);
    const auto kind=identify(input,p.get("kind","auto")); p.values["kind"]=kind;
    const auto target=std::string(kind=="bios" ? "ps2n-native" : "ps2n-run-native");
    const std::string execution_policy="native-aot";
    const auto input_hash=sha256_file(input);
    if (!p.get("input_sha256").empty() && p.get("input_sha256")!=input_hash) throw std::runtime_error("input SHA-256 changed; update the profile deliberately");
    p.values["input_sha256"]=input_hash;
    std::string identities=input_hash+' '+path_utf8(input)+'\n';
    std::string map_hash;
    auto aot_args=p.aot_args;
    std::optional<std::uint32_t> execution_alias;
    for(const auto& arg:aot_args)(void)cli::parse_execution_alias(arg,execution_alias);
    if(execution_alias && kind=="bios")throw std::runtime_error("execution alias applies only to physical RAM ELF inputs");
    if(execution_alias.value_or(0) && !prepare_only)
        throw std::runtime_error("execution alias requires prepare; link the module to the matching restored RAM execution path");
    if(!p.get("ghidra").empty()) {
        const auto map=p.path("ghidra");map_hash=sha256_file(map);identities+=map_hash+' '+path_utf8(map)+'\n';
        for(const auto& hint:parse_ghidra_csv(read_text(map)))aot_args.push_back("--entry="+std::to_string(hint.start));
    }
    if(!p.get("observed").empty()) {
        const auto events=p.path("observed");
        const auto events_hash=sha256_file(events);
        const auto image=kind=="disc" ? DiscImage(input).boot_elf() : kind=="bios" ? bios_code_image(load_bios_rom0(read_file(input))) : parse_ee_elf(read_file(input));
        for(const auto entry:discover_ee_entries(cli::ee_execution_alias(image,execution_alias.value_or(0)),events))aot_args.push_back("--entry="+std::to_string(entry));
        identities+=events_hash+' '+path_utf8(events)+'\n';
    }
    for (auto& arg : p.runtime_args) {
        if ((arg.starts_with("--diagnostic-") && !arg.starts_with("--diagnostic-ram=")) || arg.starts_with("--frame") || arg.starts_with("--trace-out="))
            throw std::runtime_error("output paths are managed by ps2n-dev; use its diagnostic reports");
        for (const auto* prefix : {"--nvm=","--iop-image="}) {
            if (!arg.starts_with(prefix)) continue;
            const auto path=absolute(cli::utf8_path(arg.substr(std::string_view(prefix).size())),p.origin.parent_path());
            arg=std::string(prefix)+path_utf8(path); identities+=sha256_file(path)+' '+path_utf8(path)+'\n';
        }
    }
    if (!replay.empty() && read_text(replay / "identities.txt")!=identities)
        throw std::runtime_error("replay input/firmware/dependency identity changed; refusing restoration of scenario");
    for (const auto& arg : p.cmake_args)
        if (!arg.starts_with("-D") || arg.starts_with("-DPS2N_ELF_AOT_SOURCE") || arg.starts_with("-DPS2N_BIOS_AOT_SOURCE") ||
            arg.starts_with("-DBUILD_TESTING") || arg.starts_with("-DPS2N_LAUNCHER") || arg.starts_with("-DPS2N_DEV_CONTENT_TRACKING") || arg.starts_with("-DCMAKE_BUILD_TYPE"))
            throw std::runtime_error("cmake_arg must be a -D definition; runtime AOT/testing/launcher/config settings are managed");
    for (const auto& arg : p.aot_args)
        if (!arg.starts_with("--entry=") && !arg.starts_with("--table=") && !arg.starts_with("--execution-alias=")) throw std::runtime_error("aot_arg accepts only --entry, --table and --execution-alias");
    // Refuse adoption of somebody else's build. No active game build is touched.
    const auto owner=build / ".ps2n-dev-owner";
    const auto ownership=path_utf8(source)+'\n'+path_utf8(p.origin)+'\n';
    const auto resolved_source=fs::weakly_canonical(source),resolved_build=fs::weakly_canonical(build);
    const auto relation=resolved_source.lexically_relative(resolved_build);
    if(resolved_source==resolved_build || (!relation.empty() && *relation.begin()!=".."))
        throw std::runtime_error("build directory must not contain the source tree");
    if (file_exists(build / "CMakeCache.txt") && !file_exists(owner)) throw std::runtime_error("use a new dedicated ps2n-dev build directory");
    ps2native::create_directories(build);
    BuildLock lock(build);
    if (file_exists(owner)) {
        // Replays use a saved profile path, but belong to the same build/source.
        const auto previous=read_text(owner);
        if (!previous.starts_with(path_utf8(source)+'\n') || (replay.empty() && previous!=ownership))
            throw std::runtime_error("build directory belongs to a different profile/source");
    } else save_text(owner,ownership);
    const auto run_id=std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    const auto directory=build / "ps2n-dev" / "runs" / run_id;
    ps2native::create_directories(directory);
    save_text(directory / "profile.ps2n",p.serialize()); save_text(directory / "identities.txt",identities);
    std::string stages; bool first_stage=true; std::string status="host-error", error;
    std::string source_hash, runtime_hash, commit, cache_hash;
    bool reused=false, content_reused=false, analysis_reused=false, dirty=false;
    int result=2;
    auto stage=[&](const std::string& name,const fs::path& tool,const std::vector<fs::path>& args,bool required=true) {
        std::cout << name << "..." << std::endl;
        const auto r=process(tool,args,directory / (name+".log"),timeout);
        if (!first_stage) stages+=','; first_stage=false;
        stages+="{\"name\":"+json_string(name)+",\"exit\":"+std::to_string(r.exit_code)+",\"timeout\":"+(r.timed_out ? "true" : "false")+
            ",\"milliseconds\":"+std::to_string(r.milliseconds)+",\"tool\":"+json_string(path_utf8(tool))+",\"args\":[";
        bool comma=false; for(const auto& a:args) { if(comma)stages+=','; comma=true; stages+=json_string(path_utf8(a)); }
        stages+="]}";
        std::cout << name << ": exit " << r.exit_code << ", " << r.milliseconds << " ms" << std::endl;
        if(required && r.exit_code) throw std::runtime_error(name+" failed; see "+path_utf8(directory / (name+".log")));
        return r.exit_code;
    };
    try {
        source_hash=source_identity(source);
        stage("git-commit","git",{"-C",source,"rev-parse","HEAD"});
        stage("git-status","git",{"-C",source,"status","--porcelain=v1"});
        stage("git-diff","git",{"-C",source,"diff","--no-ext-diff","--binary","HEAD"});
        commit=read_text(directory / "git-commit.log");while(!commit.empty() && (commit.back()=='\r' || commit.back()=='\n'))commit.pop_back();
        dirty=!read_text(directory / "git-status.log").empty();
        std::vector<fs::path> configure{"-S",source,"-B",build,"-DBUILD_TESTING=OFF","-DPS2N_LAUNCHER=OFF","-DPS2N_DEV_CONTENT_TRACKING=ON", "-DCMAKE_BUILD_TYPE="+config};
        if(!p.get("generator").empty()) { configure.emplace_back("-G");configure.push_back(cli::utf8_path(p.get("generator"))); }
        if(!p.get("architecture").empty()) { configure.emplace_back("-A");configure.push_back(cli::utf8_path(p.get("architecture"))); }
        for(const auto& arg:p.cmake_args)configure.push_back(cli::utf8_path(arg));
        // Configure bootstrap only once; retaining the previous AOT definition
        // avoids gratuitous regenerate cycles on every repeated scenario.
        const auto bootstrap_key=digest([&]{std::string s=source_hash;for(const auto& v:configure)s+=path_utf8(v)+'\n';return s;}());
        const auto bootstrap_stamp=build / "ps2n-dev" / "bootstrap.sha256";
        if(!file_exists(bootstrap_stamp) || read_text(bootstrap_stamp)!=bootstrap_key) {
            stage("configure-bootstrap",cmake,configure);save_text(bootstrap_stamp,bootstrap_key);
        }
        stage("build-tools",cmake,{"--build",build,"--config",config,"--target","ps2n-aot","ps2n-disc","ps2n-analyze","--parallel","2"});
        const auto aot=executable(build,config,"ps2n-aot");
        const auto analyzer=executable(build,config,"ps2n-analyze");
        std::string aot_material=kind+'\n'+input_hash+'\n'+sha256_file(aot)+'\n';
        for(const auto& arg:aot_args)aot_material+=arg+'\n';
        std::string analysis_material=kind+'\n'+input_hash+'\n'+sha256_file(analyzer)+'\n'+map_hash+'\n';
        for(const auto& arg:aot_args)analysis_material+=arg+'\n';
        // The original profile owns this build, also during snapshot replay.
        // Execution budgets, logging and analyzer revisions do not change AOT.
        // Separate content keys still isolate every executable/firmware input.
        const auto cache=build / "ps2n-dev" / "cache" / digest(read_text(owner));
        const auto aot_cache=cache / "aot" / digest(aot_material);
        const auto proof=aot_cache / "content.sha256";
        fs::path translation;
        const auto analysis=cache / "analysis" / digest(analysis_material);
        const auto analysis_proof=analysis / "report.sha256";
        const auto analysis_hash=[&]{std::string text;for(const auto* file:{"analysis.json","analysis.txt","cfg.dot"})text+=sha256_file(analysis / file);return digest(text);};
        if(file_exists(analysis / "analysis.json") && file_exists(analysis / "analysis.txt") && file_exists(analysis / "cfg.dot") && file_exists(analysis_proof))
            analysis_reused=read_text(analysis_proof)==analysis_hash();
        if(!analysis_reused) {
            std::vector<fs::path> args{input,analysis};
            if(kind!="elf")args.emplace_back("--"+kind);
            for(const auto& arg:aot_args)args.push_back(cli::utf8_path(arg));
            if(!p.get("ghidra").empty())args.push_back(option("--ghidra=",p.path("ghidra")));
            stage("analyze",analyzer,args);save_text(analysis_proof,analysis_hash());
        }
        save_text(directory / "analysis-reference.txt",path_utf8(analysis));
        save_text(directory / "analysis-reused.txt",analysis_reused ? "true" : "false");
        // Stable per-profile location lets Ninja retain unchanged partition
        // objects across input/tool revisions. Every unit is verified by hash;
        // the profile owner still isolates firmware revisions and regions.
        const auto source_path=cache / "sources" / "partitioned" / "module.cpp";
        if(use_aot && file_exists(proof)) {
            const auto hash=read_text(proof);
            if(hash.size()==64 && hash.find_first_not_of("0123456789abcdef")==std::string::npos) {
                try {
                    if(aot_sources_sha256(source_path)==hash){translation=source_path;reused=content_reused=true;}
                } catch(const std::exception&) { /* Regenerate a missing/corrupt source set. */ }
            }
        }
        if(!reused && use_aot) {
            translation=source_path;
            std::string previous;
            try {previous=aot_sources_sha256(translation);} catch(const std::exception&) {}
            std::vector<fs::path> args{input,translation,kind=="bios" ? "--bios" : "--hybrid","--split"};
            if(kind=="disc")args.emplace_back("--disc");
            for(const auto& arg:aot_args)args.push_back(cli::utf8_path(arg));
            stage("generate-aot",aot,args);
            const auto hash=aot_sources_sha256(translation);
            content_reused=previous==hash;
            save_text(proof,hash);
        }
        if(use_aot) {
            save_text(directory / "aot-reference.txt",path_utf8(translation));
            save_text(directory / "aot-key-reference.txt",path_utf8(proof));
        }
        if(kind=="disc") stage("identify-disc",executable(build,config,"ps2n-disc"),{input});
        configure.push_back(option("-DPS2N_BIOS_AOT_SOURCE=",use_aot && kind=="bios" ? translation : fs::path{}));
        configure.push_back(option("-DPS2N_ELF_AOT_SOURCE=",use_aot && kind!="bios" ? translation : fs::path{}));
        const auto configure_key=digest([&]{std::string s=source_hash;for(const auto& v:configure)s+=path_utf8(v)+'\n';return s;}());
        const auto key_file=build / "ps2n-dev" / "configure.sha256";
        if(!file_exists(key_file) || read_text(key_file)!=configure_key) {
            stage("configure-runtime",cmake,configure); save_text(key_file,configure_key);
        }
        stage("build-runtime",cmake,{"--build",build,"--config",config,"--target",target,"--parallel","2"});
        if(source_identity(source)!=source_hash) throw std::runtime_error("source changed during build; retry for a consistent runtime/ABI");
        const auto runner=executable(build,config,target);
        runtime_hash=sha256_file(runner);save_text(directory / "runtime.sha256",runtime_hash);
        // Configuration evidence is small text, not a copy of the build or AOT.
        const auto cache_text=read_text(build / "CMakeCache.txt");cache_hash=digest(cache_text);
        save_text(directory / "cmake-cache.txt",cache_text);
        for(const auto& entry:fs::directory_iterator(native_io_path(build / "CMakeFiles"))) {
            if(!entry.is_directory())continue;
            const auto compiler=entry.path() / "CMakeCXXCompiler.cmake";
            if(file_exists(compiler)){save_text(directory / "compiler.txt",read_text(compiler));break;}
        }
        // Check file content again before execution, including dependency paths.
        std::istringstream identity_input(identities);std::string identity_line;
        while(std::getline(identity_input,identity_line)) {
            if(identity_line.size()<66 || identity_line[64]!=' ')throw std::runtime_error("invalid scenario identity record");
            if(sha256_file(cli::utf8_path(identity_line.substr(65)))!=identity_line.substr(0,64))
                throw std::runtime_error("input/firmware/map dependency changed during build");
        }
        if(prepare_only) {
            status="prepared";result=0;
            save_text(build / "ps2n-dev" / "last-prepared.txt",path_utf8(directory));
        } else {
            std::vector<fs::path> args{input,option("--diagnostic-dir=",directory / "diagnostic"),
                "--diagnostic-ring="+p.get("ring","256")};
            if(p.get("stream_bytes","0")!="0")args.emplace_back("--diagnostic-stream-bytes="+p.get("stream_bytes"));
            if(kind=="disc")args.emplace_back("--disc");
            for(const auto& arg:p.runtime_args)args.push_back(cli::utf8_path(arg));
            result=stage("execute",runner,args,false);
            status=file_exists(directory / "diagnostic" / "report.json") && file_exists(directory / "diagnostic" / "complete") ? "diagnosed" : "execution-error";
            if(status!="diagnosed")throw std::runtime_error("runner failed without diagnostic report; see execute.log");
            if(!replay.empty() && file_exists(replay / "diagnostic" / "events.ndjson")) {
                std::string comparison;
                try { (void)compare_diagnostics(replay / "diagnostic" / "events.ndjson",directory / "diagnostic" / "events.ndjson",comparison); }
                catch(const std::exception& e) {comparison=e.what();}
                save_text(directory / "comparison.txt",comparison+'\n');
                std::cout << comparison << '\n';
            }
            save_text(build / "ps2n-dev" / "last.txt",path_utf8(directory));
        }
    } catch(const std::exception& e) { error=e.what();std::cerr << error << '\n'; result=2; }
    const auto manifest="{\"schema\":\"ps2n-dev-run-v1\",\"status\":"+json_string(status)+",\"error\":"+json_string(error)+
        ",\"execution_policy\":"+json_string(execution_policy)+",\"runner_target\":"+json_string(target)+
        ",\"profile\":"+json_string(p.serialize())+",\"source_sha256\":"+json_string(source_hash)+",\"input_sha256\":"+json_string(input_hash)+
        ",\"git_commit\":"+json_string(commit)+",\"git_dirty\":"+(dirty ? "true" : "false")+
        ",\"runtime_sha256\":"+json_string(runtime_hash)+",\"cmake_cache_sha256\":"+json_string(cache_hash)+
        ",\"analysis_reused\":"+(analysis_reused ? "true" : "false")+
        ",\"aot_content_reused\":"+(content_reused ? "true" : "false")+
        ",\"aot_reused\":"+(reused ? "true" : "false")+",\"disk_bytes\":"+std::to_string(disk_bytes(build))+",\"stages\":["+stages+"]}\n";
    save_text(directory / "manifest.json",manifest);
    std::cout << "run=" << path_utf8(directory) << "\nexecution_policy=" << execution_policy << "\nrunner_target=" << target
        << "\naot_reused=" << (reused ? "true" : "false") << '\n';
    return result;
}
template<class C> int dev_main(int argc,C** argv) {
    try {
        if(argc<3) {
            std::cout << "Usage: ps2n-dev init <input> <local-profile.ps2n> [--kind=auto|elf|bios|disc]\n"
                         "       ps2n-dev prepare <profile.ps2n>\n"
                         "       ps2n-dev prepare-sdk <ELF|ISO> <output-directory> [--disc]\n"
                         "       ps2n-dev prepare-native <ELF|ISO> <cache-directory> [--disc] [--rom0=FILE --rom0-sha256=HASH]\n"
                         "              [--console=MODEL:MAINBOARD] [--nvm=FILE --nvm-sha256=HASH]\n"
                         "              [--discovery-budget=N] (default 50000000 EE instructions; 240-second execution limit)\n"
                         "              [--assume-ee-early-digit-carry] (unverified external-reference scalar multiplier)\n"
                         "              [--assume-spu2-pio-word-cycles=N] (positive unverified IOP cycles per word)\n"
                         "              [--assume-spu2-quiescent-reset] (unverified inactive voices and inputs)\n"
                         "              [--assume-spu2-effect-end-reset=CORE0:CORE1] (unverified decimal four-bit EEA)\n"
                         "              [--assume-spu2-reserved-latches] (unmeasured unassigned-register effects)\n"
                         "              [--assume-spu2-7ca-latch] (unknown output effects, diagnostic only)\n"
                         "              [--diagnostic-cache-uncached-hits] (unverified physical cache alias translation)\n"
                         "              [--diagnostic-dvd-layer0=SECTORS --diagnostic-dvd-track=otp|ptp]\n"
                         "       ps2n-dev prepare-iop <loaded-executable.iop> <cache-directory>\n"
                         "       ps2n-dev run <profile.ps2n>\n       ps2n-dev replay <profile.ps2n|run-directory>\n";
            return 2;
        }
        const auto command=path_utf8(fs::path(argv[1]));
        if(command=="prepare-iop" && (argc==4 || argc==5)) {
            fs::path cancel;
            if(argc==5) {
                if(!path_utf8(fs::path(argv[4])).starts_with("--cancel-file="))throw std::runtime_error("Unknown IOP preparation option");
                cancel=fs::path(argv[4]).native().substr(14);
            }
            return prepare_iop(fs::path(argv[2]),fs::path(argv[3]),cancel);
        }
        if(command=="prepare-native" && argc>=4 && argc<=19) {
            std::optional<std::array<std::uint16_t,2>> spu2_effect_end;
            bool disc=false,artifact_only=false,ee_early_digit_carry=false,diagnostic_cache_uncached_hits=false,spu2_7ca_latch=false,spu2_reserved_latches=false,spu2_quiescent_reset=false;fs::path cancel,rom0,nvm;std::string rom0_hash,profile,nvm_hash;std::optional<std::uint32_t> alias;
            std::uint32_t spu2_pio_word_cycles=0;
            std::uint64_t discovery_budget=50000000u;bool discovery_budget_given=false;
            cli::DiagnosticDvdOptions dvd;
            for(int i=4;i<argc;++i) {
                const auto flag=path_utf8(fs::path(argv[i]));
                if(dvd.parse(flag))continue;
                if(flag=="--disc")disc=true;
                else if(flag=="--artifact-only")artifact_only=true;
                else if(flag.starts_with("--discovery-budget=")) {
                    if(discovery_budget_given)throw std::runtime_error("duplicate native discovery budget");
                    discovery_budget_given=true;discovery_budget=cli::decimal(flag.substr(19),UINT64_MAX);
                    if(!discovery_budget)throw std::runtime_error("Native discovery budget must be positive");
                }
                else if(flag=="--assume-ee-early-digit-carry") {
                    if(ee_early_digit_carry)throw std::runtime_error("duplicate EE scalar carry diagnostic");
                    ee_early_digit_carry=true;
                }
                else if(flag=="--diagnostic-cache-uncached-hits")diagnostic_cache_uncached_hits=true;
                else if(cli::parse_spu2_effect_end(flag,spu2_effect_end)) {}
                else if(flag=="--assume-spu2-reserved-latches")spu2_reserved_latches=true;
                else if(flag=="--assume-spu2-7ca-latch")spu2_7ca_latch=true;
                else if(flag=="--assume-spu2-quiescent-reset")spu2_quiescent_reset=true;
                else if(flag.starts_with("--assume-spu2-pio-word-cycles=")) {
                    if(spu2_pio_word_cycles)throw std::runtime_error("duplicate SPU2 PIO timing");
                    spu2_pio_word_cycles=static_cast<std::uint32_t>(cli::decimal(flag.substr(30),UINT32_MAX));
                    if(!spu2_pio_word_cycles)throw std::runtime_error("SPU2 PIO word cycles must be positive");
                }
                else if(cli::parse_execution_alias(flag,alias)){}
                else if(flag.starts_with("--cancel-file="))cancel=fs::path(argv[i]).native().substr(14);
                else if(flag.starts_with("--rom0="))rom0=fs::path(argv[i]).native().substr(7);
                else if(flag.starts_with("--rom0-sha256="))rom0_hash=flag.substr(14);
                else if(flag.starts_with("--console="))profile=flag.substr(10);
                else if(flag.starts_with("--nvm="))nvm=fs::path(argv[i]).native().substr(6);
                else if(flag.starts_with("--nvm-sha256="))nvm_hash=flag.substr(13);
                else throw std::runtime_error("Unknown native preparation option");
            }
            if(discovery_budget_given && artifact_only)throw std::runtime_error("Native discovery budget requires native discovery");
            if(ee_early_digit_carry && artifact_only)throw std::runtime_error("Diagnostic EE scalar carry requires native discovery");
            if(ee_early_digit_carry)std::cout<<"diagnostic_ee_early_digit_carry=external-reference-coarse-multiplier-unverified\n";
            if(spu2_pio_word_cycles && artifact_only)throw std::runtime_error("Diagnostic SPU2 PIO timing requires native discovery");
            if(spu2_pio_word_cycles)std::cout<<"diagnostic_spu2_pio_word_cycles="<<spu2_pio_word_cycles<<" (unverified timing)\n";
            if(spu2_quiescent_reset && artifact_only)throw std::runtime_error("Diagnostic SPU2 quiescent reset requires native discovery");
            if(spu2_quiescent_reset)std::cout<<"diagnostic_spu2_quiescent_reset=unverified-inactive-voices-and-inputs\n";
            if(spu2_effect_end && artifact_only)throw std::runtime_error("Diagnostic SPU2 initial EEA requires native discovery");
            if(spu2_effect_end)std::cout<<"diagnostic_spu2_effect_end_reset="<<(*spu2_effect_end)[0]<<':'<<(*spu2_effect_end)[1]<<" (unverified reset)\n";
            if(spu2_reserved_latches && artifact_only)throw std::runtime_error("Diagnostic SPU2 reserved latches requires native discovery");
            if(spu2_reserved_latches)std::cout<<"diagnostic_spu2_reserved_latches=unverified-output-effects\n";
            if(spu2_7ca_latch && artifact_only)throw std::runtime_error("Diagnostic SPU2 0x7CA latch requires native discovery");
            if(spu2_7ca_latch)std::cout<<"diagnostic_spu2_7ca_latch=unverified-output-effects\n";
            if(diagnostic_cache_uncached_hits && artifact_only)throw std::runtime_error("Diagnostic cache alias translation requires native discovery");
            if(diagnostic_cache_uncached_hits)std::cout<<"diagnostic_cache_uncached_hits=physical-translation-unverified\n";
            if(alias.value_or(0) && !artifact_only)throw std::runtime_error("Aliased native artifacts require the existing runtime overlay path");
            if(rom0.empty() && !rom0_hash.empty())throw std::runtime_error("ROM0 hash requires its original image path");
            const auto geometry=dvd.geometry(disc);
            const auto result=prepare_native(fs::path(argv[2]),fs::path(argv[3]),disc,cancel,alias.value_or(0));
            return result || artifact_only?result:discover_native(fs::path(argv[2]),fs::path(argv[3]),disc,cancel,rom0,rom0_hash,profile,nvm,nvm_hash,geometry,diagnostic_cache_uncached_hits,spu2_7ca_latch,spu2_quiescent_reset,spu2_pio_word_cycles,ee_early_digit_carry,discovery_budget,spu2_reserved_latches,spu2_effect_end);
        }
        if(command=="prepare-sdk" && argc>=4 && argc<=6) {
            bool disc=false;std::optional<std::uint32_t> alias;
            for(int i=4;i<argc;++i) {
                const auto flag=path_utf8(fs::path(argv[i]));
                if(flag=="--disc")disc=true;
                else if(!cli::parse_execution_alias(flag,alias))throw std::runtime_error("Unknown SDK preparation option");
            }
            return prepare_ps2recomp(fs::path(argv[2]),fs::path(argv[3]),disc,alias.value_or(0));
        }
        if(command=="init" && (argc==4 || argc==5)) {
            const auto input=fs::absolute(fs::path(argv[2])), file=fs::absolute(fs::path(argv[3]));
            if(file_exists(file))throw std::runtime_error("profile already exists");
            const auto flag=argc==5 ? path_utf8(fs::path(argv[4])) : "--kind=auto";
            if(!flag.starts_with("--kind="))throw std::runtime_error("expected --kind");
            Profile p; p.origin=file;
            p.values={{"schema","ps2n-dev-v1"},{"input",path_utf8(input)},{"input_sha256",sha256_file(input)},
                {"kind",identify(input,flag.substr(7))},{"source",PS2N_DEV_SOURCE},{"cmake",PS2N_DEV_CMAKE},
                {"build",path_utf8(cli::utf8_path(PS2N_DEV_SOURCE) / "out" / "dev-tools" / digest(path_utf8(file)).substr(0,24))},
                {"config","Release"},{"timeout","300"},{"engine","aot"},{"ring","256"},{"stream_bytes","0"}};
            p.runtime_args.push_back(p.get("kind")=="bios" ? "--max-instructions=1000000" : "--budget=1000000");
            save_text(file,p.serialize());std::cout << path_utf8(file) << '\n';return 0;
        }
        if(command=="run" && argc==3)return run(Profile::load(fs::path(argv[2])),{});
        if(command=="prepare" && argc==3)return run(Profile::load(fs::path(argv[2])),{},true);
        if(command=="replay" && argc==3) {
            auto directory=fs::absolute(fs::path(argv[2]));
            if(fs::is_regular_file(native_io_path(directory))) {
                const auto p=Profile::load(directory);directory=cli::utf8_path(read_text(p.path("build") / "ps2n-dev" / "last.txt"));
            }
            return run(Profile::load(directory / "profile.ps2n"),directory);
        }
        throw std::runtime_error("unknown command or argument count");
    }catch(const std::exception& e){std::cerr << "ps2n-dev: " << e.what() << '\n';return 2;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return dev_main(argc,argv);}
#else
int main(int argc,char** argv){return dev_main(argc,argv);}
#endif
