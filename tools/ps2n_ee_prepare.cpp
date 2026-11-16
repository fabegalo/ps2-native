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
        recomp::generate(translations, options);
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
