#include "ps2native/elf.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace ps2native {
namespace {
std::uint16_t read16(std::span<const std::uint8_t> b, std::size_t at) {
    if (at > b.size() || b.size() - at < 2u)
        throw std::runtime_error("ELF32 relocation metadata truncated");
    return static_cast<std::uint16_t>(b[at]) |
           (static_cast<std::uint16_t>(b[at + 1]) << 8);
}
std::uint32_t read32(std::span<const std::uint8_t> b, std::size_t at) {
    if (at > b.size() || b.size() - at < 4u)
        throw std::runtime_error("ELF32 relocation metadata truncated");
    return static_cast<std::uint32_t>(b[at]) |
           (static_cast<std::uint32_t>(b[at + 1]) << 8) |
           (static_cast<std::uint32_t>(b[at + 2]) << 16) |
           (static_cast<std::uint32_t>(b[at + 3]) << 24);
}
bool within(std::size_t length, std::uint64_t offset, std::uint64_t size) {
    return offset <= length && size <= length - offset;
}
std::size_t section_pos(std::uint32_t shoff, std::uint32_t section) {
    return static_cast<std::size_t>(shoff) + 40u * section;
}
void rejected(ElfImage& image, std::uint32_t site, std::uint32_t value,
              const std::string& reason) {
    image.rejected_code_pointers.push_back({site, value, reason});
}
} // namespace

// M1.2 is deliberately NOT a generic MIPS relocator. It recognizes evidence
// that a fully linked ET_EXEC contains function pointers in a non-executable
// data section: SHT_REL / R_MIPS_32 (type 2) referring to STT_FUNC or
// STT_SECTION in executable code. It never edits guest bytes or guesses a
// function from a word that merely resembles an address.
void discover_relocated_code_pointers(ElfImage& image) {
    const std::span<const std::uint8_t> b(image.bytes);
    const auto shoff = read32(b, 32);
    const auto shnum = read16(b, 48);
    if (shoff == 0 || shnum == 0) return;
    if (!within(b.size(), shoff, static_cast<std::uint64_t>(shnum) * 40u))
        throw std::runtime_error("Invalid ELF32 section headers for relocation analysis");

    std::size_t entries_seen = 0;
    for (std::uint32_t section = 0; section < shnum; ++section) {
        const auto rel_section = section_pos(shoff, section);
        if (read32(b, rel_section + 4u) != 9u) continue; // SHT_REL
        const auto rel_offset = read32(b, rel_section + 16u);
        const auto rel_size = read32(b, rel_section + 20u);
        const auto link = read32(b, rel_section + 24u);
        const auto info = read32(b, rel_section + 28u);
        const auto entry_size = read32(b, rel_section + 36u);
        if (link >= shnum || info >= shnum || entry_size != 8u ||
            (rel_size % 8u) != 0 || !within(b.size(), rel_offset, rel_size))
            throw std::runtime_error("Invalid ELF32 SHT_REL bounds, link or entry size");
        if ((rel_size / 8u) > 65536u - entries_seen)
            throw std::runtime_error("Too many ELF32 relocation records");
        entries_seen += rel_size / 8u;

        const auto sym_section = section_pos(shoff, link);
        const auto sym_type = read32(b, sym_section + 4u);
        const auto sym_offset = read32(b, sym_section + 16u);
        const auto sym_size = read32(b, sym_section + 20u);
        const auto sym_ent_size = read32(b, sym_section + 36u);
        if ((sym_type != 2u && sym_type != 11u) ||
            sym_ent_size != 16u || (sym_size % 16u) != 0u ||
            !within(b.size(), sym_offset, sym_size))
            throw std::runtime_error("SHT_REL references an invalid ELF32 symbol table");

        const auto owner = section_pos(shoff, info);
        const auto owner_type = read32(b, owner + 4u);
        const auto owner_flags = read32(b, owner + 8u);
        // Only ALLOC non-executable PROGBITS data is considered a pointer table.
        // Relocations against executable code are not entry-point evidence.
        if (owner_type != 1u || !(owner_flags & 2u) || (owner_flags & 4u))
            continue;
        const auto owner_addr = read32(b, owner + 12u);
        const auto owner_size = read32(b, owner + 20u);

        for (std::uint64_t off = rel_offset;
             off < static_cast<std::uint64_t>(rel_offset) + rel_size; off += 8u) {
            const auto site = read32(b, static_cast<std::size_t>(off));
            const auto relocation_info = read32(b, static_cast<std::size_t>(off) + 4u);
            if ((relocation_info & 0xffu) != 2u) continue; // Only R_MIPS_32
            if (site < owner_addr ||
                static_cast<std::uint64_t>(site) + 4u >
                    static_cast<std::uint64_t>(owner_addr) + owner_size)
                throw std::runtime_error("R_MIPS_32 relocation site outside declared data section");
            std::uint32_t value{};
            try {
                value = image.data_word_at(site);
            } catch (const std::exception& e) {
                throw std::runtime_error("R_MIPS_32 source at " + std::to_string(site) +
                                         ": " + e.what());
            }
            const auto symbol_index = relocation_info >> 8u;
            if (symbol_index >= sym_size / 16u)
                throw std::runtime_error("R_MIPS_32 relocation refers to invalid symbol index");
            const auto sym = static_cast<std::size_t>(sym_offset) + 16u * symbol_index;
            const auto symbol_kind = b[sym + 12u] & 15u;
            const auto symbol_section = read16(b, sym + 14u);
            const auto symbol_value = read32(b, sym + 4u);
            if (symbol_section == 0u) {
                rejected(image, site, value, "undefined relocation symbol");
                continue;
            }
            if (symbol_kind != 2u && symbol_kind != 3u) {
                rejected(image, site, value, "relocation symbol is not STT_FUNC or STT_SECTION");
                continue;
            }
            if (symbol_section >= shnum)
                throw std::runtime_error("R_MIPS_32 relocation symbol section out of bounds");
            const auto target_section = section_pos(shoff, symbol_section);
            const auto target_flags = read32(b, target_section + 8u);
            const auto section_addr = read32(b, target_section + 12u);
            const auto section_size = read32(b, target_section + 20u);
            if (!(target_flags & 4u) ||
                value < section_addr ||
                static_cast<std::uint64_t>(value) + 4u >
                    static_cast<std::uint64_t>(section_addr) + section_size) {
                rejected(image, site, value, "relocated word outside symbol's executable section");
                continue;
            }
            if (symbol_kind == 2u && value != symbol_value) {
                rejected(image, site, value, "resolved pointer differs from STT_FUNC address");
                continue;
            }
            if (symbol_kind == 3u && symbol_value != 0u && value < symbol_value) {
                rejected(image, site, value, "resolved pointer precedes STT_SECTION base");
                continue;
            }
            if ((value & 3u) != 0u) {
                rejected(image, site, value, "unaligned relocated executable pointer");
                continue;
            }
            try {
                (void)image.instruction_at(value);
            } catch (const std::exception& e) {
                rejected(image, site, value, std::string("target not executable: ") + e.what());
                continue;
            }
            if (image.relocated_code_pointers.size() >= 4096u)
                throw std::runtime_error("Too many verified executable relocation targets");
            image.relocated_code_pointers.push_back({site, value});
        }
    }
    auto& targets = image.relocated_code_pointers;
    std::sort(targets.begin(), targets.end(), [](const auto& a, const auto& b) {
        return a.source < b.source || (a.source == b.source && a.target < b.target);
    });
    targets.erase(std::unique(targets.begin(), targets.end(), [](const auto& a, const auto& b) {
        return a.source == b.source && a.target == b.target;
    }), targets.end());
}
} // namespace ps2native
