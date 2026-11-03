#include "ps2native/core.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

template <typename Character>
int aot_main(int argc, Character** argv) {
    if (argc != 3) {
        std::cerr << "Usage: ps2n-aot <owned-mips-elf> <output.cpp>\n";
        return 2;
    }
    try {
        const auto input = ps2native::read_file(std::filesystem::path(argv[1]));
        const auto elf = ps2native::parse_ee_elf(input);
        const auto generated = ps2native::generate_aot_cpp(elf);
        const bool updated = ps2native::write_text_if_changed(
            std::filesystem::path(argv[2]), generated);
        std::cout << (updated ? "AOT source generated" : "AOT unchanged; reused")
                  << "; ELF entry: 0x" << std::hex << elf.entry << '\n';
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
