#include "ps2native/core.hpp"

#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

template <typename Character>
int aot_main(int argc, Character** argv) {
    if (argc < 3) {
        std::cerr << "Usage: ps2n-aot <owned-mips-elf> <output.cpp> [--entry=0xADDRESS ...]\n";
        return 2;
    }
    try {
        ps2native::AotOptions options;
        for (int i = 3; i < argc; ++i) {
            const auto flag = std::filesystem::path(argv[i]).string();
            if (!flag.starts_with("--entry="))
                throw std::runtime_error("Unrecognized argument; expected --entry=0xADDRESS");
            const auto value = flag.substr(8);
            std::size_t consumed = 0;
            const auto parsed = std::stoull(value, &consumed, 0);
            if (consumed != value.size() || parsed > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("Invalid 32-bit --entry address");
            options.additional_function_entries.push_back(static_cast<std::uint32_t>(parsed));
        }
        const auto input = ps2native::read_file(std::filesystem::path(argv[1]));
        const auto elf = ps2native::parse_ee_elf(input);
        const auto generated = ps2native::generate_aot_cpp(elf, options);
        const bool updated = ps2native::write_text_if_changed(
            std::filesystem::path(argv[2]), generated);
        std::cout << (updated ? "AOT source generated" : "AOT unchanged; reused")
                  << "; ELF entry: 0x" << std::hex << elf.entry
                  << "; ELF functions: " << std::dec << elf.function_entries.size() << '\n';
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
