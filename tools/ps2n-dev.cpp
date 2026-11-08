#include "dev_profile.hpp"
#include "ee_alias_cli.hpp"
#include "dev_process.hpp"
#include "ps2native/bios.hpp"
#include "ps2native/disc.hpp"
#include "ps2native/analysis.hpp"
#include "ps2native/aot_sources.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>

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
struct BuildLock {
    fs::path path;
    explicit BuildLock(const fs::path& root) : path(root / ".ps2n-dev-lock") {
        if (!fs::create_directory(native_io_path(path))) throw std::runtime_error("developer build locked; another run may be active (remove stale lock only after verifying no process remains)");
    }
    ~BuildLock() { std::error_code ec; fs::remove(native_io_path(path),ec); }
};
std::uint64_t disk_bytes(const fs::path& root) {
    std::uint64_t n = 0;
    for (const auto& e : fs::recursive_directory_iterator(native_io_path(root)))
        if (e.is_regular_file() && !e.is_symlink()) n += e.file_size();
    return n;
}
int run(Profile p, const fs::path& replay, bool prepare_only=false) {
    const auto source=p.path("source"), build=p.path("build"), input=p.path("input"), cmake=p.path("cmake");
    const auto config=p.get("config","Release"), engine=p.get("engine","aot");
    if (engine!="native" && engine!="aot" && engine!="interpreter") throw std::runtime_error("engine must be native, aot or interpreter");
    const bool use_aot=engine!="interpreter";
    if (config.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)
        throw std::runtime_error("invalid build configuration");
    const auto timeout = static_cast<unsigned>(cli::decimal(p.get("timeout","300"),86400));
    if (!timeout) throw std::runtime_error("timeout must be nonzero");
    if (!cli::decimal(p.get("ring","256"),1'000'000)) throw std::runtime_error("ring must be nonzero");
    (void)cli::decimal(p.get("stream_bytes","0"),1ull<<40);
    const auto kind=identify(input,p.get("kind","auto")); p.values["kind"]=kind;
    const auto target=engine=="native" ? std::string(kind=="bios" ? "ps2n-native" : "ps2n-run-native")
        : std::string(kind=="bios" ? "ps2n-boot" : "ps2n-run")+(use_aot ? "-hybrid" : "");
    const std::string execution_policy=engine=="native" ? "native-aot" : use_aot ? "hybrid-aot" : "interpreter";
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
                         "       ps2n-dev run <profile.ps2n>\n       ps2n-dev replay <profile.ps2n|run-directory>\n";
            return 2;
        }
        const auto command=path_utf8(fs::path(argv[1]));
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
