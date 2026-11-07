#include "ps2native/core.hpp"
#include "ps2native/aot_sources.hpp"
#include "ps2native/disc.hpp"
#include "ps2native/vu_aot.hpp"
#include "ee_alias_cli.hpp"

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
        std::cerr << "Usage: ps2n-aot <owned-mips-elf|disc.iso> <output.cpp> [--split] [--disc] [--hybrid] [--entry=0xADDRESS ...] [--table=0xADDRESS:COUNT ...]\n"
                     "       [--execution-alias=0x80000000|0xA0000000] (stripped physical-RAM ELF only)\n"
                     "       ps2n-aot <your-rom0.bin> <output.cpp> --bios [--expect-sha256=HEX] [--entry=0xADDRESS ...]\n"
                     "       ps2n-aot <vu1-image.bin|capture.vu1> <output.cpp> --vu1 [--entry=BYTE_PC ...] [--split[=BUCKET_BYTES]]\n";
        return 2;
    }
    try {
        ps2native::AotOptions options;
        bool split_output = false;
        std::uint32_t partition_bytes=65536;
        bool bios_input = false;
        bool disc_input = false;
        bool vu1_input = false;
        bool vu1_carry = false;
        std::optional<std::string> expected_sha256;
        std::optional<std::uint32_t> execution_alias;
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
            if (ps2native::cli::parse_execution_alias(flag,execution_alias)) {
            } else if (flag.starts_with("--entry=")) {
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
            } else if (flag == "--split") {
                split_output = true;
            } else if (flag.starts_with("--split=")) {
                split_output = true;
                partition_bytes=parse_u32(flag.substr(8),"--split address bucket size");
            } else if (flag == "--hybrid") {
                options.interpreter_boundaries = true;
            } else if (flag == "--bios") {
                bios_input = true;
            } else if (flag == "--disc") {
                disc_input = true;
            } else if (flag == "--vu1") {
                vu1_input = true;
            } else if (flag == "--vu1-diagnostic-power-two-carry") {
                vu1_carry = true;
            } else if (flag.starts_with("--expect-sha256=")) {
                expected_sha256 = flag.substr(16);
            } else {
                throw std::runtime_error("Unrecognized argument; expected --entry, --table, --execution-alias, --split, --hybrid, --disc, --bios, --vu1, --vu1-diagnostic-power-two-carry or --expect-sha256");
            }
        }
        if (unsigned(bios_input)+unsigned(disc_input)+unsigned(vu1_input)>1u)
            throw std::runtime_error("--bios, --disc and --vu1 are mutually exclusive");
        if (vu1_carry && !vu1_input)throw std::runtime_error("--vu1-diagnostic-power-two-carry requires --vu1");
        if(execution_alias && (bios_input || vu1_input))throw std::runtime_error("execution alias applies only to physical RAM ELF inputs");
        if (ps2native::file_exists(std::filesystem::path(argv[2])) &&
            ps2native::files_equivalent(std::filesystem::path(argv[1]),std::filesystem::path(argv[2])))
            throw std::runtime_error("AOT output must not overwrite its input");
        const auto input = disc_input ? ps2native::DiscImage(std::filesystem::path(argv[1])).boot_elf().bytes
                                     : ps2native::read_file(std::filesystem::path(argv[1]));
        const auto output=std::filesystem::path(argv[2]);
        auto manifest=output;manifest+=".sources";
        if (split_output && ps2native::file_exists(manifest) &&
            ps2native::files_equivalent(manifest,std::filesystem::path(argv[1])))
            throw std::runtime_error("AOT manifest must not overwrite its input");
        if (!split_output && ps2native::file_exists(manifest))
            throw std::runtime_error("Existing partitioned AOT output requires --split");
        if(vu1_input) {
            if(!options.jump_tables.empty() || options.interpreter_boundaries)
                throw std::runtime_error("--vu1 does not accept EE --table/--hybrid options");
            const auto image=ps2native::parse_vu1_aot_image(input);
            ps2native::VuAotOptions vu_options;
            if(!options.additional_function_entries.empty())vu_options.entries=options.additional_function_entries;
            vu_options.diagnostic_power_two_carry=vu1_carry;
            const auto identity=ps2native::vu1_aot_identity(image,vu1_carry);
            if(expected_sha256 && *expected_sha256!=identity)throw std::runtime_error("VU1 AOT identity does not match --expect-sha256");
            bool updated=false;
            if(split_output) {
                const auto sources=ps2native::generate_vu1_aot_sources(image,vu_options,
                    ps2native::path_utf8(output.filename()),partition_bytes);
                for(const auto& unit:sources.partitions) {
                    const auto path=output.parent_path()/unit.name;
                    if(ps2native::file_exists(path) && ps2native::files_equivalent(path,std::filesystem::path(argv[1])))
                        throw std::runtime_error("AOT partition must not overwrite its input");
                }
                updated=ps2native::write_aot_sources(output,sources);
            } else updated=ps2native::write_text_if_changed(output,ps2native::generate_vu1_aot_cpp(image,vu_options));
            const auto graph=ps2native::analyze_vu1_aot(image,vu_options);
            std::cout<<(updated ? "VU1 AOT source generated" : "VU1 AOT unchanged; reused")
                     <<"; identity "<<identity<<"; native pairs "<<graph.instructions.size()
                     <<"; interpreter boundaries "<<graph.boundaries.size()<<'\n';
            for(const auto& boundary:graph.boundaries)std::cout<<"VU1 interpreter boundary 0x"<<std::hex
                <<boundary.pc<<std::dec<<": "<<boundary.reason<<'\n';
            return 0;
        }
        const auto generate=[&](const ps2native::ElfImage& image) {
            if (split_output) {
                const auto sources=ps2native::generate_aot_sources(image,options,
                    ps2native::path_utf8(output.filename()),partition_bytes);
                for (const auto& unit:sources.partitions) {
                    const auto path=output.parent_path()/unit.name;
                    if (ps2native::file_exists(path) && ps2native::files_equivalent(path,std::filesystem::path(argv[1])))
                        throw std::runtime_error("AOT partition must not overwrite its input");
                }
                return ps2native::write_aot_sources(output,sources);
            }
            return ps2native::write_text_if_changed(output,ps2native::generate_aot_cpp(image,options));
        };
        if (bios_input) {
            // Hybrid translation of user-supplied boot firmware: reachable ROM code
            // only, interpreter boundaries listed; nothing is embedded but code.
            if (!options.jump_tables.empty()) throw std::runtime_error("--table is not supported with --bios");
            const auto bios = ps2native::load_bios_rom0(input, {expected_sha256});
            options.interpreter_boundaries = true;
            options.source_kind = "bios-rom0";
            options.source_sha256 = bios.sha256;
            const bool updated = generate(ps2native::bios_code_image(bios));
            std::cout << (updated ? "AOT source generated" : "AOT unchanged; reused")
                      << "; ROM0 " << bios.version.romver << " sha256 " << bios.sha256
                      << "; reset vector 0x" << std::hex << ps2native::kEeResetVector << std::dec << '\n';
            return 0;
        }
        if (expected_sha256) throw std::runtime_error("--expect-sha256 applies to --bios inputs");
        const auto elf = ps2native::cli::ee_execution_alias(ps2native::parse_ee_elf(input),execution_alias.value_or(0));
        const bool updated = generate(elf);
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
