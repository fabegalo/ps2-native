// Synthetic, independently-authored MIPS instructions. No Sony or game binaries.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
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
    if (argc != 2 && argc != 3) { std::cerr << "Usage: ps2n-make-fixture <output.elf> [ops|indirect|indirect-nosym]\n"; return 2; }
    constexpr std::uint32_t start = 0x00100000u;
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
        0xffffffffu,                          // 19: unreachable unsupported opcode
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
    std::ofstream out(std::filesystem::path(argv[1]), std::ios::binary | std::ios::trunc);
    if (!out || !out.write(reinterpret_cast<const char*>(bytes.data()),
                           static_cast<std::streamsize>(bytes.size()))) {
        std::cerr << "Failed to write fixture ELF\n";
        return 1;
    }
    std::cout << "Wrote synthetic EE/MIPS ELF, " << instr.size() << " instructions\n";
    return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return fixture_main(argc, argv); }
#else
int main(int argc, char** argv) { return fixture_main(argc, argv); }
#endif
