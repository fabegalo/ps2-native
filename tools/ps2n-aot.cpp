#include "ps2native/core.hpp"

#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <algorithm>
#include <stdexcept>
#include <string>

template <typename Character>
int aot_main(int argc, Character** argv) {
    if (argc < 3) {
        std::cerr << "Usage: ps2n-aot <owned-mips-elf> <output.cpp> [--hybrid] [--entry=0xADDRESS ...] [--table=0xADDRESS:COUNT ...]\n"
                     "       ps2n-aot <your-rom0.bin> <output.cpp> --bios [--expect-sha256=HEX] [--entry=0xADDRESS ...]\n";
        return 2;
    }
    try {
        ps2native::AotOptions options;
        bool bios_input = false;
        std::optional<std::string> expected_sha256;
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
            const auto flag = ps2native::path_utf8(std::filesystem::path(argv[i]));
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
            } else if (flag == "--hybrid") {
                options.interpreter_boundaries = true;
            } else if (flag == "--bios") {
                bios_input = true;
            } else if (flag.starts_with("--expect-sha256=")) {
                expected_sha256 = flag.substr(16);
            } else {
                throw std::runtime_error("Unrecognized argument; expected --entry, --table, --hybrid, --bios or --expect-sha256");
            }
        }
        const auto input = ps2native::read_file(std::filesystem::path(argv[1]));
        if (bios_input) {
            // Hybrid translation of user-supplied boot firmware: reachable ROM code
            // only, interpreter boundaries listed; nothing is embedded but code.
            if (!options.jump_tables.empty()) throw std::runtime_error("--table is not supported with --bios");
            const auto bios = ps2native::load_bios_rom0(input, {expected_sha256});
            options.interpreter_boundaries = true;
            options.source_kind = "bios-rom0";
            options.source_sha256 = bios.sha256;
            const auto generated = ps2native::generate_aot_cpp(ps2native::bios_code_image(bios), options);
            const bool updated = ps2native::write_text_if_changed(std::filesystem::path(argv[2]), generated);
            std::cout << (updated ? "AOT source generated" : "AOT unchanged; reused")
                      << "; ROM0 " << bios.version.romver << " sha256 " << bios.sha256
                      << "; reset vector 0x" << std::hex << ps2native::kEeResetVector << std::dec << '\n';
            return 0;
        }
        if (expected_sha256) throw std::runtime_error("--expect-sha256 applies to --bios inputs");
        const auto elf = ps2native::parse_ee_elf(input);
        const auto generated = ps2native::generate_aot_cpp(elf, options);
        const bool updated = ps2native::write_text_if_changed(
            std::filesystem::path(argv[2]), generated);
        std::cout << (updated ? "AOT source generated" : "AOT unchanged; reused")
                  << "; ELF entry: 0x" << std::hex << elf.entry
                  << "; ELF functions: " << std::dec << elf.function_entries.size()
                  << "; verified relocations: " << elf.relocated_code_pointers.size()
                  << "; rejected relocation candidates: " << elf.rejected_code_pointers.size()
                  << "; HI16/LO16 address pairs: " << elf.address_pairs.size()
                  << "; unpaired LO16: " << elf.unpaired_lo16_relocations
                  << "; explicit tables: " << options.jump_tables.size() << '\n';
        for (const auto& [type, count] : elf.unused_relocation_counts)
            std::cout << "relocation type " << type << " not used as evidence: " << count << '\n';
        if (elf.ignored_rela_sections != 0)
            std::cout << "SHT_RELA sections ignored: " << elf.ignored_rela_sections << '\n';
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
