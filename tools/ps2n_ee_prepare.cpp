#include "analysis.hpp"
#include "codegen.hpp"
#include "elf.hpp"
#include "ps2/os.hpp"
#include "ps2native/elf.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"
#include <cstdio>
#include <sstream>
#include <format>
#include <deque>
#include <regex>
#include <map>


namespace {
struct PreviousOutput {std::string text;std::filesystem::file_time_type time;};
std::string generated_text(const std::filesystem::path& path){const auto bytes=ps2native::read_file(path);return {bytes.begin(),bytes.end()};}
std::vector<std::string> primary_declarations(const std::string& text){
    std::vector<std::string> result;std::istringstream input(text);std::string line;std::smatch match;
    const std::regex function(R"(void ([A-Za-z_][A-Za-z_0-9]*)\(ps2::Cpu &c, std::uint32_t entry, std::uint32_t depth\);)");
    const std::regex table(R"(extern const std::uint16_t ([A-Za-z_][A-Za-z_0-9]*)\[\];)");
    while(std::getline(input,line)){
        if(!line.empty() && line.back()=='\r')line.pop_back();
        if(line.empty() || line=="#pragma once" || line=="#include \"ps2/generated.hpp\"" || line=="namespace gen {" || line=="}")continue;
        if(!std::regex_match(line,match,function) && !std::regex_match(line,match,table))throw std::runtime_error("Unexpected pinned AOT declarations format");
        const auto name=match[1].str();if(name.starts_with("unit_") || name.starts_with("entries_"))result.push_back(line);
    }
    return result;
}
// Keep unrelated module declarations out of the primary objects' dependency. Exact bytes gate restat.
void retain_output(const std::filesystem::path& path,const std::string& text,const std::map<std::filesystem::path,PreviousOutput>& previous){
    (void)ps2native::write_text_if_changed(path,text);
    const auto old=previous.find(path);
    if(old!=previous.end() && old->second.text==text)std::filesystem::last_write_time(ps2native::native_io_path(path),old->second.time);
}
void generate_incrementally(const std::vector<recomp::Translation>& translations,const recomp::CodegenOptions& options){
    const auto output=ps2::os::path(options.output),header=output/"units.hpp";
    std::map<std::filesystem::path,PreviousOutput> previous;
    const auto remember=[&](const auto& path){if(ps2native::file_exists(path))previous.emplace(path,PreviousOutput{generated_text(path),ps2native::file_write_time(path)});};
    remember(header);remember(output/"table.cpp");
    if(std::filesystem::exists(ps2native::native_io_path(output)))for(const auto& file:std::filesystem::directory_iterator(ps2native::native_io_path(output))){
        const auto name=file.path().filename().string();
        for(const auto& t:translations)if(!t.prefix.empty() && name.starts_with(t.prefix+"unit_") && file.path().extension()==".cpp")remember(output/file.path().filename());
    }
    std::vector<std::string> old_primary;if(previous.contains(header))old_primary=primary_declarations(previous.at(header).text);
    recomp::generate(translations,options);
    const auto complete=generated_text(header);const auto primary=primary_declarations(complete);
    std::string base="#pragma once\n\n#include \"ps2/generated.hpp\"\n\nnamespace gen {\n";
    for(const auto& line:primary)base+=line+'\n';base+="}\n";
    if(previous.contains(header) && old_primary==primary)base=previous.at(header).text;
    retain_output(header,base,previous);
    std::string includes;
    for(const auto& t:translations)if(!t.prefix.empty()){
        const auto name=t.prefix+"decls.hpp";std::string declarations="#pragma once\n#include \"ps2/generated.hpp\"\nnamespace gen {\n";
        std::istringstream input(complete);std::string line;
        while(std::getline(input,line))if(line.starts_with("void "+t.prefix) || line.starts_with("extern const std::uint16_t "+t.prefix))declarations+=line+'\n';
        declarations+="}\n";(void)ps2native::write_text_if_changed(output/name,declarations);
        const auto include="#include \""+name+"\"\n";includes+=include;
        for(const auto& file:std::filesystem::directory_iterator(ps2native::native_io_path(output)))if(file.path().filename().string().starts_with(t.prefix+"unit_") && file.path().extension()==".cpp"){
            const auto path=output/file.path().filename();auto text=generated_text(path);const auto end=text.find('\n');
            if(end==std::string::npos || text.substr(0,end)!="#include \"units.hpp\"")throw std::runtime_error("Unexpected pinned AOT unit include");
            text.insert(end+1,include);retain_output(path,text,previous);
        }
    }
    if(!includes.empty()){
        const auto path=output/"table.cpp";auto text=generated_text(path);const auto end=text.find('\n');
        if(end==std::string::npos || text.substr(0,end)!="#include \"units.hpp\"")throw std::runtime_error("Unexpected pinned AOT registry include");
        text.insert(end+1,includes);retain_output(path,text,previous);
    }
}
}


int main(int argc, char** argv) {
    if (argc < 4 || ((argc-4)%4) || std::string(argv[2]) != "--out") {
        std::fprintf(stderr, "usage: ps2n-ee-recomp ELF --out DIRECTORY [--module IMAGE BASE_HEX ENTRY_HEX]...\n"); return 2;
    }
    try {
        const auto bytes = ps2native::read_file(ps2::os::path(argv[1]));
        const auto validated = ps2native::parse_ee_elf(bytes);
        const auto image = recomp::Image::load(bytes);
        auto program = recomp::analyse(image, validated.function_entries);
        recomp::CodegenOptions options;
        options.output = argv[3];
        std::vector<recomp::Translation> translations{{.image=&image,.program=&program}};
        std::deque<recomp::Image> images;
        std::deque<recomp::Program> programs;
        const auto number=[](const char* text) {
            const std::string value=text;
            if (value.empty() || value.size()>8 || value.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)
                throw std::runtime_error("Invalid module address");
            return static_cast<std::uint32_t>(std::stoul(value,nullptr,16));
        };
        for (int i=4; i<argc; i+=4) {
            if (std::string(argv[i])!="--module") throw std::runtime_error("Expected --module");
            const auto base=number(argv[i+2]), entry=number(argv[i+3]);
            auto raw=ps2native::read_file(ps2::os::path(argv[i+1]));
            if (raw.size()!=0x10000 || base>0xfffeffff || (base&0x1fffffff)>0x1ff0000 || (base&0xffff) || (entry&3) || entry<base || entry-base>=raw.size())
                throw std::runtime_error("Invalid captured AOT image");
            const auto identity=ps2native::sha256_hex(raw);
            images.push_back(recomp::Image::raw(base,std::move(raw),{base,base+0x10000}));
            programs.push_back(recomp::analyse(images.back(),{entry},{},recomp::Search{.whole_image=false}));
            auto& translated=programs.back();
            if (!translated.is_code(entry) || !translated.entries.contains(entry)) throw std::runtime_error("Captured AOT entry rejected");
            std::vector<std::pair<std::uint32_t,std::uint32_t>> words;
            for (auto pc=translated.base; pc<translated.end; pc+=4)
                if (translated.is_code(pc)) words.emplace_back(pc-base,*images.back().word(pc));
            const auto name=std::format("captured_{}_{}",identity.substr(0,16),i);
            translations.push_back({.image=&images.back(),.program=&translated,.prefix=name+"_",.name=name,
                                    .size=0x10000,.checks=std::move(words),.fixed_module=true,.fixed_base=base});
        }
        generate_incrementally(translations, options);
        std::ostringstream checks;
        checks << "#pragma once\n#include <utility>\n#include <cstdint>\n"
               << "inline constexpr const char* kAotElfHash = \"" << ps2native::sha256_hex(bytes) << "\";\n"
               << "inline constexpr std::pair<std::uint32_t,std::uint32_t> kAotWords[] = {\n";
        for (auto pc = program.base; pc < program.end; pc += 4)
            if (program.is_code(pc)) checks << std::format("{{0x{:08X}u,0x{:08X}u}},\n", pc, *image.word(pc));
        checks << "};\n";
        (void)ps2native::write_text_if_changed(ps2::os::path(argv[3]) / "aot_image.hpp", checks.str());
        std::printf("ELF %s, %zu AOT entries, %zu rejected seeds\n", ps2native::sha256_hex(bytes).c_str(), program.entries.size(), program.rejected.size());
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
