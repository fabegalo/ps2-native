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

int main(int argc, char** argv) {
    if (argc != 4 || std::string(argv[2]) != "--out") {
        std::fprintf(stderr, "usage: ps2n-ee-recomp ELF --out DIRECTORY\n"); return 2;
    }
    try {
        const auto bytes = ps2native::read_file(ps2::os::path(argv[1]));
        const auto validated = ps2native::parse_ee_elf(bytes);
        const auto image = recomp::Image::load(bytes);
        auto program = recomp::analyse(image, validated.function_entries);
        recomp::CodegenOptions options;
        options.output = argv[3];
        recomp::generate({recomp::Translation{.image=&image, .program=&program}}, options);
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
