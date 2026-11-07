#include "dev_profile.hpp"
#include "ee_alias_cli.hpp"
#include "ps2native/analysis.hpp"
#include "ps2native/bios.hpp"
#include "ps2native/disc.hpp"
#include <iostream>

template<class C> int analyze_main(int argc,C** argv) {
    using namespace ps2native;using namespace ps2native::dev;
    try {
        if(argc<3) {
            std::cout<<"Usage: ps2n-analyze <ELF|ISO|ROM0|IRX> <output-directory> [--disc|--bios|--irx]\n"
                         "       [--entry=ADDRESS] [--table=ADDRESS:COUNT] [--ghidra=functions.csv]\n"
                         "       [--execution-alias=0x80000000|0xA0000000] (stripped physical-RAM ELF only)\n";return 2;
        }
        AnalysisOptions options;std::string kind="elf";fs::path ghidra;
        std::optional<std::uint32_t> execution_alias;
        auto u32=[](const std::string& s){std::size_t n{};const auto v=std::stoull(s,&n,0);if(n!=s.size() || v>UINT32_MAX || s.empty() || s[0]=='-')throw std::runtime_error("invalid 32-bit analysis number");return static_cast<std::uint32_t>(v);};
        for(int i=3;i<argc;++i) {
            const auto arg=path_utf8(fs::path(argv[i]));
            if(cli::parse_execution_alias(arg,execution_alias)){}
            else if(arg=="--disc" || arg=="--bios" || arg=="--irx") {if(kind!="elf")throw std::runtime_error("input kinds are mutually exclusive");kind=arg.substr(2);}
            else if(arg.starts_with("--entry="))options.entries.push_back(u32(arg.substr(8)));
            else if(arg.starts_with("--table=")) {
                const auto colon=arg.find(':',8);if(colon==std::string::npos)throw std::runtime_error("table requires ADDRESS:COUNT");
                options.tables.push_back({u32(arg.substr(8,colon-8)),u32(arg.substr(colon+1))});
            } else if(arg.starts_with("--ghidra=")) {if(!ghidra.empty())throw std::runtime_error("duplicate Ghidra map");ghidra=cli::utf8_path(arg.substr(9));}
            else throw std::runtime_error("unknown analysis option: "+arg);
        }
        const auto input=fs::path(argv[1]),output=fs::path(argv[2]);
        if(execution_alias && (kind=="bios" || kind=="irx"))throw std::runtime_error("execution alias applies only to physical RAM ELF inputs");
        for(const auto* name:{"analysis.json","analysis.txt","cfg.dot"})
            for(const auto& path:{input,ghidra})if(!path.empty() && file_exists(output / name) && files_equivalent(path,output / name))
                throw std::runtime_error("analysis output aliases an input");
        if(!ghidra.empty())options.ghidra=parse_ghidra_csv(read_text(ghidra));
        AnalysisReport report;
        if(kind=="irx") {
            if(!options.entries.empty() || !options.tables.empty() || !options.ghidra.empty())throw std::runtime_error("R5900 CFG hints do not apply to IOP IRX metadata");
            report=analyze_irx_metadata(read_file(input));
        } else {
            const auto elf=kind=="disc" ? DiscImage(input).boot_elf() : kind=="bios" ? bios_code_image(load_bios_rom0(read_file(input))) : parse_ee_elf(read_file(input));
            report=analyze_ee_image(cli::ee_execution_alias(elf,execution_alias.value_or(0)),options);
        }
        (void)write_text_if_changed(output / "analysis.json",report.json);
        (void)write_text_if_changed(output / "analysis.txt",report.text);
        (void)write_text_if_changed(output / "cfg.dot",report.dot);
        std::cout<<report.text;return 0;
    }catch(const std::exception& e){std::cerr<<"ps2n-analyze: "<<e.what()<<'\n';return 1;}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return analyze_main(argc,argv);}
#else
int main(int argc,char** argv){return analyze_main(argc,argv);}
#endif
