// Synthetic, independently-authored MIPS instructions. No Sony or game binaries.
#include "bios_program.hpp"
#include "gs_program.hpp"
#include "m13_program.hpp"
#include "ps2native/io.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
bool write_fixture(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    try { (void)ps2native::write_binary_if_changed(path, bytes); return true; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return false; }
}
void write16(std::vector<std::uint8_t>& bytes, std::size_t off, std::uint16_t v) {
    bytes.at(off) = static_cast<std::uint8_t>(v);
    bytes.at(off+1) = static_cast<std::uint8_t>(v >> 8);
}
void write32(std::vector<std::uint8_t>& bytes, std::size_t off, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(off+i) = static_cast<std::uint8_t>(v >> (8*i));
}
std::uint32_t r(unsigned rs, unsigned rt, unsigned rd, unsigned fn) {
    return (rs << 21) | (rt << 16) | (rd << 11) | fn;
}
std::uint32_t i(unsigned op, unsigned rs, unsigned rt, std::int16_t imm) {
    return (op << 26) | (rs << 21) | (rt << 16) | static_cast<std::uint16_t>(imm);
}
} // namespace

template <typename Character>
int fixture_main(int argc, Character** argv) {
    if (argc != 2 && argc != 3) { std::cerr << "Usage: ps2n-make-fixture <output.elf> [ops|indirect|indirect-nosym|jump-table|jump-table-nosym|m13|m13-stripped|bios|bios-m21|bios-m22|bios-m23|bios-m24|bios-m25]\n"; return 2; }
    constexpr std::uint32_t start = 0x00100000u;
    if (argc == 3 && std::filesystem::path(argv[2]) == std::filesystem::path("gs")) {
        if (!write_fixture(std::filesystem::path(argv[1]), m29::build().rom)) return 1;
        std::cout << "Created synthetic GS test program (not Sony firmware)\n";
        return 0;
    }

    // Synthetic M2.0 (and M2.1-M2.5 variants) boot firmware in the ROM0 container format (not Sony code).
    if (argc == 3 && (std::filesystem::path(argv[2]) == std::filesystem::path("bios") ||
                      std::filesystem::path(argv[2]) == std::filesystem::path("bios-m21") ||
                      std::filesystem::path(argv[2]) == std::filesystem::path("bios-m22") ||
                      std::filesystem::path(argv[2]) == std::filesystem::path("bios-m23") ||
                      std::filesystem::path(argv[2]) == std::filesystem::path("bios-m24") ||
                      std::filesystem::path(argv[2]) == std::filesystem::path("bios-m25"))) {
        const std::filesystem::path mode(argv[2]);
        const auto firmware = m20::build_firmware(mode == std::filesystem::path("bios-m25")   ? m20::Stage::M25
                                                  : mode == std::filesystem::path("bios-m24") ? m20::Stage::M24
                                                  : mode == std::filesystem::path("bios-m23") ? m20::Stage::M23
                                                  : mode == std::filesystem::path("bios-m22") ? m20::Stage::M22
                                                  : mode == std::filesystem::path("bios-m21") ? m20::Stage::M21
                                                                                              : m20::Stage::M20);
        if (!write_fixture(std::filesystem::path(argv[1]), firmware.rom)) return 1;
        std::cout << "Created synthetic ROM0, ROMDIR at 0x" << std::hex << firmware.romdir_offset << '\n';
        return 0;
    }

    // Synthetic M1.3: relocation evidence, bounded jump tables, R5900 integer subset.
    if (argc == 3 && (std::filesystem::path(argv[2]) == std::filesystem::path("m13") ||
                      std::filesystem::path(argv[2]) == std::filesystem::path("m13-stripped"))) {
        const bool with_sections = std::filesystem::path(argv[2]) == std::filesystem::path("m13");
        const auto b = m13::build_elf(m13::build_program(), with_sections);
        if (!write_fixture(std::filesystem::path(argv[1]), b)) return 1;
        std::cout << "Created M1.3 ELF, sections=" << with_sections << '\n';
        return 0;
    }

    // Synthetic M1.2: two valid function pointers, one non-executable pointer.
    if (argc == 3 && (std::filesystem::path(argv[2]) == std::filesystem::path("jump-table") ||
                      std::filesystem::path(argv[2]) == std::filesystem::path("jump-table-nosym"))) {
        const bool with_relocations = std::filesystem::path(argv[2]) == std::filesystem::path("jump-table");
        constexpr std::uint32_t table = start + 0x1000u;
        const std::vector<std::uint32_t> code{
            r(31,0,16,0x21), i(0x0f,0,8,0x0010), i(0x0d,8,8,0x1000),
            i(0x23,8,9,0), r(9,0,31,0x09), i(0x09,0,10,5),
            i(0x2b,0,2,0x40), i(0x23,8,9,4), r(9,0,31,0x09),
            i(0x09,10,10,1), i(0x2b,0,2,0x44), r(16,0,31,0x21),
            r(31,0,0,0x08), 0, 0x0000000cu, // SYSCALL: unsupported, never compiled
            i(0x09,10,2,7), r(31,0,0,0x08), 0,
            i(0x09,10,2,9), r(31,0,0,0x08), 0
        };
        constexpr std::size_t textoff = 0x100, dataoff = 0x200;
        constexpr std::size_t symoff = 0x300, stroff = 0x340, reloff = 0x400, shoff = 0x500;
        std::vector<std::uint8_t> b(with_relocations ? shoff+6u*40u : dataoff+12u, 0);
        b[0]=0x7f; b[1]='E'; b[2]='L'; b[3]='F'; b[4]=1; b[5]=1; b[6]=1;
        write16(b,16,2); write16(b,18,8); write32(b,20,1);
        write32(b,24,start); write32(b,28,52); write16(b,40,52);
        write16(b,42,32); write16(b,44,2);
        // PT_LOAD #1: executable text, #2: file-backed non-executable data.
        write32(b,52,1); write32(b,56,textoff); write32(b,60,start);
        write32(b,64,start); write32(b,68,static_cast<std::uint32_t>(code.size()*4u));
        write32(b,72,0x100); write32(b,76,5); write32(b,80,0x100);
        write32(b,84,1); write32(b,88,dataoff); write32(b,92,table);
        write32(b,96,table); write32(b,100,12); write32(b,104,0x100);
        write32(b,108,4); write32(b,112,0x100);
        for(std::size_t n=0;n<code.size();++n) write32(b,textoff+n*4,code[n]);
        write32(b,dataoff,start+15u*4u); write32(b,dataoff+4,start+18u*4u);
        write32(b,dataoff+8,table); // rejected: non-executable target
        if(with_relocations) {
            write32(b,32,shoff); write16(b,46,40); write16(b,48,6);
            const auto textsec=shoff+40u, datasec=shoff+80u;
            write32(b,textsec+4,1); write32(b,textsec+8,6);
            write32(b,textsec+12,start); write32(b,textsec+16,textoff);
            write32(b,textsec+20,static_cast<std::uint32_t>(code.size()*4u));
            write32(b,textsec+32,4);
            write32(b,datasec+4,1); write32(b,datasec+8,2);
            write32(b,datasec+12,table); write32(b,datasec+16,dataoff);
            write32(b,datasec+20,12); write32(b,datasec+32,4);
            const auto symsec=shoff+120u;
            write32(b,symsec+4,2); write32(b,symsec+16,symoff);
            write32(b,symsec+20,32); write32(b,symsec+24,4);
            write32(b,symsec+28,2); write32(b,symsec+32,4); write32(b,symsec+36,16);
            write32(b,symoff+20,start);
            b[symoff+28]=3; write16(b,symoff+30,1); // local STT_SECTION(.text)
            const auto strsec=shoff+160u;
            write32(b,strsec+4,3); write32(b,strsec+16,stroff); write32(b,strsec+20,1);
            const auto relsec=shoff+200u;
            write32(b,relsec+4,9); write32(b,relsec+16,reloff);
            write32(b,relsec+20,24); write32(b,relsec+24,3); write32(b,relsec+28,2);
            write32(b,relsec+32,4); write32(b,relsec+36,8);
            for(std::uint32_t n=0;n<3;++n){
                write32(b,reloff+n*8u,table+n*4u);
                write32(b,reloff+n*8u+4u,258); // symbol #1, R_MIPS_32
            }
        }
        if (!write_fixture(std::filesystem::path(argv[1]), b)) return 1;
        std::cout<<"Created M1.2 ELF with jump table and relocation="<<with_relocations<<'\n';
        return 0;
    }

    const std::vector<std::uint32_t> standard{
        r(31, 0, 16, 0x21),  // 00: addu $s0, $ra, $zero
        i(0x09, 0, 8, 5),    // 01: addiu $t0, $zero, 5
        i(0x09, 0, 9, 0),    // 02: addiu $t1, $zero, 0
        i(0x09, 9, 9, 7),    // 03: loop: addiu $t1, $t1, 7
        i(0x09, 8, 8, -1),   // 04: addiu $t0, $t0, -1
        i(0x05, 8, 0, -3),   // 05: bne $t0, $zero, loop
        0,                    // 06: delay NOP
        i(0x04, 8, 0, 2),    // 07: beq $t0, $zero, index 10 (taken)
        i(0x09, 9, 9, 1),    // 08: delay slot executes: t1 = 36
        i(0x09, 9, 9, 100),  // 09: unreachable branch-skipped instruction
        i(0x05, 8, 0, 1),    // 10: bne $t0, $zero, index 12 (not taken)
        i(0x09, 9, 9, 1),    // 11: delay slot executes: t1 = 37
        0x0C000000u | ((start + 19u * 4u) >> 2), // 12: jal function at index 19
        0,                    // 13: delay NOP
        i(0x2B, 0, 2, 0x40), // 14: sw $v0, 0x40($zero)
        i(0x23, 0, 2, 0x40), // 15: lw $v0, 0x40($zero)
        r(16, 0, 31, 0x21), // 16: restore ra from s0
        r(31, 0, 0, 0x08),  // 17: jr $ra
        0,                    // 18: delay NOP
        i(0x09, 9, 2, 0),    // 19: function: addiu $v0, $t1, 0
        r(31, 0, 0, 0x08),  // 20: jr $ra
        0                     // 21: delay NOP
    };
    const std::vector<std::uint32_t> ops{
        i(0x0f, 0, 8, 0x1234),        // 00: lui t0, 0x1234
        i(0x0d, 8, 8, 0x5678),        // 01: ori t0, t0, 0x5678
        i(0x09, 0, 0, 99),            // 02: ignored write to $zero
        0x08000000u | ((start + 6u * 4u) >> 2), // 03: j index 6
        i(0x09, 8, 8, 1),             // 04: jump delay slot, +1
        i(0x09, 8, 8, 100),           // 05: unreachable
        i(0x2b, 0, 8, 0x44),          // 06: sw t0, 0x44(zero)
        i(0x23, 0, 2, 0x44),          // 07: lw v0, 0x44(zero)
        r(31, 0, 0, 0x08),           // 08: jr ra
        0                              // 09: nop
    };
    // Three independently authored functions called through JALR/JR.
    // The non-symbol variant requires --entry hints; no "guess all words" heuristic.
    const std::vector<std::uint32_t> indirect{
        r(31, 0, 16, 0x21),                  // 00: save host return in s0
        i(0x0f, 0, 8, 0x0010),               // 01: lui t0, 0x10
        i(0x0d, 8, 8, 20 * 4),               // 02: t0 = function A
        r(8, 0, 31, 0x09),                   // 03: jalr ra, t0
        i(0x09, 0, 9, 10),                   // 04: delay slot t1 = 10
        i(0x2b, 0, 2, 0x40),                 // 05: store A result (12)
        i(0x0f, 0, 11, 0x0010),              // 06: lui t3, 0x10
        i(0x23, 11, 8, 0x80),                // 07: lw t0, 0x80(t3); pointer to B from data
        r(8, 0, 31, 0x09),                   // 08: jalr ra, t0
        i(0x09, 9, 9, 5),                    // 09: delay slot t1 = 15
        i(0x2b, 0, 2, 0x44),                 // 10: store B result (18)
        i(0x0f, 0, 12, 0x0010),              // 11: lui t4, 0x10
        i(0x0d, 12, 12, 26 * 4),             // 12: pointer to C
        r(12, 0, 12, 0x09),                  // 13: jalr t4, t4; read source BEFORE overwriting link
        i(0x09, 9, 9, 1),                    // 14: delay slot t1 = 16
        i(0x2b, 0, 2, 0x48),                 // 15: store C result (20)
        r(16, 0, 31, 0x21),                  // 16: restore host return address
        r(31, 0, 0, 0x08),                   // 17: jr ra
        0,                                    // 18: return delay slot
        0x0000000cu,                          // 19: unreachable unsupported SYSCALL
        i(0x09, 9, 2, 1),                    // 20: A: v0 = t1+1
        r(31, 0, 0, 0x08),                   // 21: jr ra
        i(0x09, 2, 2, 1),                    // 22: delay v0++ => 12
        i(0x09, 9, 2, 2),                    // 23: B: v0 = t1+2
        r(31, 0, 0, 0x08),                   // 24: jr ra
        i(0x09, 2, 2, 1),                    // 25: delay v0++ => 18
        i(0x09, 9, 2, 3),                    // 26: C: v0 = t1+3
        r(12, 0, 0, 0x08),                   // 27: jr t4
        i(0x09, 2, 2, 1)                     // 28: delay v0++ => 20
    };
    const auto fixture_mode = argc == 3 ? std::filesystem::path(argv[2]) : std::filesystem::path{};
    const bool indirect_mode = fixture_mode == std::filesystem::path("indirect") ||
                               fixture_mode == std::filesystem::path("indirect-nosym");
    const bool with_symbols = fixture_mode == std::filesystem::path("indirect");
    const auto& instr = indirect_mode ? indirect : (fixture_mode == std::filesystem::path("ops") ? ops : standard);
    constexpr std::size_t code_file_offset = 0x100;
    const auto load_bytes = indirect_mode ? std::size_t{0x84} : instr.size() * 4;
    std::vector<std::uint8_t> bytes(code_file_offset + load_bytes, 0);
    bytes[0] = 0x7f; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 1; bytes[5] = 1; bytes[6] = 1;
    write16(bytes, 16, 2);             // ET_EXEC
    write16(bytes, 18, 8);             // EM_MIPS
    write32(bytes, 20, 1);             // EV_CURRENT
    write32(bytes, 24, start);         // e_entry
    write32(bytes, 28, 52);            // e_phoff
    write16(bytes, 40, 52);            // e_ehsize
    write16(bytes, 42, 32);            // e_phentsize
    write16(bytes, 44, 1);             // e_phnum
    write32(bytes, 52, 1);             // PT_LOAD
    write32(bytes, 56, code_file_offset);
    write32(bytes, 60, start);         // p_vaddr
    write32(bytes, 64, start);         // p_paddr
    write32(bytes, 68, static_cast<std::uint32_t>(load_bytes));
    write32(bytes, 72, 0x100);         // p_memsz, BSS tail
    write32(bytes, 76, 5);             // PF_R | PF_X
    write32(bytes, 80, 0x100);         // alignment
    for (std::size_t n = 0; n < instr.size(); ++n)
        write32(bytes, code_file_offset + n * 4, instr[n]);
    if (indirect_mode) {
        write32(bytes, code_file_offset + 0x80, start + 23u * 4u); // B function pointer
    }
    if (with_symbols) {
        // Valid minimal ELF32 section and symbol tables. The function names are
        // illustrative; parsing relies on STT_FUNC + executable PT_LOAD address.
        constexpr std::size_t sym_offset = 0x200;
        constexpr std::size_t str_offset = 0x250;
        constexpr std::size_t sections_offset = 0x300;
        bytes.resize(sections_offset + 4 * 40, 0);
        constexpr char names[] = "\0main\0fn_a\0fn_b\0fn_c";
        for (std::size_t n = 0; n < sizeof(names); ++n) bytes[str_offset + n] = names[n];
        const std::uint32_t entry_indexes[] = {0, 20, 23, 26};
        const std::uint32_t name_indexes[] = {1, 6, 11, 16};
        for (std::size_t n = 0; n < 4; ++n) {
            const auto sym = sym_offset + (n + 1) * 16;
            write32(bytes, sym, name_indexes[n]);
            write32(bytes, sym + 4, start + entry_indexes[n] * 4);
            bytes[sym + 12] = 0x12;  // STB_GLOBAL | STT_FUNC
            write16(bytes, sym + 14, 1); // defined in .text (section 1)
        }
        // ELF header: section table location, section count, entry size.
        write32(bytes, 32, sections_offset);
        write16(bytes, 46, 40);
        write16(bytes, 48, 4);
        // Section #1: code + data, SHT_PROGBITS, SHF_ALLOC/EXECINSTR.
        const auto text_section = sections_offset + 40;
        write32(bytes, text_section + 4, 1);
        write32(bytes, text_section + 8, 6);
        write32(bytes, text_section + 12, start);
        write32(bytes, text_section + 16, code_file_offset);
        write32(bytes, text_section + 20, static_cast<std::uint32_t>(load_bytes));
        write32(bytes, text_section + 32, 4);
        // Section #2: SHT_SYMTAB with five entries incl. null symbol.
        const auto sym_section = sections_offset + 80;
        write32(bytes, sym_section + 4, 2);
        write32(bytes, sym_section + 16, sym_offset);
        write32(bytes, sym_section + 20, 5 * 16);
        write32(bytes, sym_section + 24, 3); // link to .strtab
        write32(bytes, sym_section + 28, 1); // one local null symbol
        write32(bytes, sym_section + 32, 4);
        write32(bytes, sym_section + 36, 16);
        // Section #3: SHT_STRTAB.
        const auto str_section = sections_offset + 120;
        write32(bytes, str_section + 4, 3);
        write32(bytes, str_section + 16, str_offset);
        write32(bytes, str_section + 20, sizeof(names));
        write32(bytes, str_section + 32, 1);
    }
    if (!write_fixture(std::filesystem::path(argv[1]), bytes)) return 1;
    std::cout << "Wrote synthetic EE/MIPS ELF, " << instr.size() << " instructions\n";
    return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return fixture_main(argc, argv); }
#else
int main(int argc, char** argv) { return fixture_main(argc, argv); }
#endif
