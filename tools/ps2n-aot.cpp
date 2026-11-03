#include "ps2native/core.hpp"

#include <filesystem>
#include <iostream>
#include <limits>
#include <algorithm>
#include <stdexcept>
#include <string>

template <typename Character>
int aot_main(int argc, Character** argv) {
    if (argc < 3) {
        std::cerr << "Usage: ps2n-aot <owned-mips-elf> <output.cpp> [--entry=0xADDRESS ...] [--table=0xADDRESS:COUNT ...]\n";
        return 2;
    }
    try {
        ps2native::AotOptions options;
        auto parse_u32 = [](const std::string& value, const std::string& label) {
            if (value.empty() || value[0] == '-')
                throw std::runtime_error("Invalid " + label);
            std::size_t consumed = 0;
            std::uint64_t parsed = 0;
            try { parsed = std::stoull(value, &consumed, 0); }
            catch (const std::exception&) { throw std::runtime_error("Invalid " + label); }
            if (consumed != value.size() || parsed > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("Invalid 32-bit " + label);
            return static_cast<std::uint32_t>(parsed);
        };
        for (int i = 3; i < argc; ++i) {
            const auto flag = std::filesystem::path(argv[i]).string();
            if (flag.starts_with("--entry=")) {
                options.additional_function_entries.push_back(
                    parse_u32(flag.substr(8), "--entry address"));
            } else if (flag.starts_with("--table=")) {
                const auto argument = flag.substr(8);
                const auto colon = argument.find(':');
                if (colon == std::string::npos || argument.find(':', colon + 1u) != std::string::npos)
                    throw std::runtime_error("Invalid --table syntax (expected 0xADDRESS:COUNT)");
                options.jump_tables.push_back({
                    parse_u32(argument.substr(0, colon), "--table address"),
                    parse_u32(argument.substr(colon + 1), "--table entry count")
                });
            } else {
                throw std::runtime_error("Unrecognized argument; expected --entry or --table");
            }
        }
        const auto input = ps2native::read_file(std::filesystem::path(argv[1]));
        const auto elf = ps2native::parse_ee_elf(input);
        const auto generated = ps2native::generate_aot_cpp(elf, options);
        const bool updated = ps2native::write_text_if_changed(
            std::filesystem::path(argv[2]), generated);
        std::cout << (updated ? "AOT source generated" : "AOT unchanged; reused")
                  << "; ELF entry: 0x" << std::hex << elf.entry
                  << "; ELF functions: " << std::dec << elf.function_entries.size()
                  << "; verified relocations: " << elf.relocated_code_pointers.size()
                  << "; rejected relocation candidates: " << elf.rejected_code_pointers.size()
                  << "; explicit tables: " << options.jump_tables.size() << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ps2n-aot: " << e.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return aot_main(argc, argv); }
#else
int main(int argc, char** argv) { return aot_main(argc, argv); }
#endif
