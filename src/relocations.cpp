#include "ps2native/elf.hpp"
#include "ps2native/mips.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace ps2native {
namespace {
constexpr std::uint32_t kShtProgbits = 1, kShtSymtab = 2, kShtRela = 4,
                        kShtRel = 9, kShtDynsym = 11;
constexpr std::uint32_t kShfAlloc = 2, kShfExecinstr = 4;
constexpr std::uint32_t kSttFunc = 2, kSttSection = 3;
constexpr std::uint32_t kShnLoreserve = 0xff00;
constexpr std::uint32_t kRMipsNone = 0, kRMips32 = 2, kRMips26 = 4,
                        kRMipsHi16 = 5, kRMipsLo16 = 6;
constexpr std::size_t kMaxRelocationRecords = 1u << 20;

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
std::string hex(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

struct Section {
    std::uint32_t type{}, flags{}, addr{}, offset{}, size{}, link{}, info{}, entsize{};
    [[nodiscard]] bool contains(std::uint32_t address, std::uint32_t bytes) const {
        return address >= addr &&
               static_cast<std::uint64_t>(address) + bytes <=
                   static_cast<std::uint64_t>(addr) + size;
    }
};
struct Symbol {
    std::uint32_t value{};
    std::uint32_t kind{};
    std::uint32_t section{};
};

// Instructions whose 16-bit immediate is sign-extended and added to a base:
// the only forms that can legitimately carry R_MIPS_LO16 (%lo) on the EE.
bool carries_lo16(const MipsInstruction& instruction) {
    switch (instruction.opcode) {
        case MipsOpcode::Addiu: case MipsOpcode::Daddiu:
        case MipsOpcode::Lb: case MipsOpcode::Lbu: case MipsOpcode::Lh:
        case MipsOpcode::Lhu: case MipsOpcode::Lw: case MipsOpcode::Lwu:
        case MipsOpcode::Ld: case MipsOpcode::Lq: case MipsOpcode::Sb:
        case MipsOpcode::Sh: case MipsOpcode::Sw: case MipsOpcode::Sd:
        case MipsOpcode::Sq:
            return true;
        default:
            return false;
    }
}

// M1.3 is deliberately NOT a relocator: ET_EXEC images are already linked,
// so relocation records (kept with --emit-relocs / -q) are read only as
// evidence about the linked bytes. Guest bytes are never modified.
class RelocationEvidence {
public:
    explicit RelocationEvidence(ElfImage& image) : image_(image), bytes_(image.bytes) {}

    void run() {
        const auto shoff = read32(bytes_, 32);
        const auto shnum = read16(bytes_, 48);
        if (shoff == 0 || shnum == 0) return;
        if (!within(bytes_.size(), shoff, static_cast<std::uint64_t>(shnum) * 40u))
            throw std::runtime_error("Invalid ELF32 section headers for relocation analysis");
        for (std::uint32_t index = 0; index < shnum; ++index) {
            const auto at = static_cast<std::size_t>(shoff) + 40u * index;
            sections_.push_back({read32(bytes_, at + 4u), read32(bytes_, at + 8u),
                                 read32(bytes_, at + 12u), read32(bytes_, at + 16u),
                                 read32(bytes_, at + 20u), read32(bytes_, at + 24u),
                                 read32(bytes_, at + 28u), read32(bytes_, at + 36u)});
        }
        for (const auto& section : sections_) {
            if (section.type == kShtRela) {
                ++image_.ignored_rela_sections;
                continue;
            }
            if (section.type == kShtRel) read_rel_section(section);
        }
        finish();
    }

private:
    void read_rel_section(const Section& rel) {
        if (rel.link >= sections_.size() || rel.info >= sections_.size() || rel.entsize != 8u ||
            (rel.size % 8u) != 0 || !within(bytes_.size(), rel.offset, rel.size))
            throw std::runtime_error("Invalid ELF32 SHT_REL bounds, link or entry size");
        if (rel.size / 8u > kMaxRelocationRecords - records_seen_)
            throw std::runtime_error("Too many ELF32 relocation records");
        records_seen_ += rel.size / 8u;

        symbols_ = &sections_[rel.link];
        if ((symbols_->type != kShtSymtab && symbols_->type != kShtDynsym) ||
            symbols_->entsize != 16u || (symbols_->size % 16u) != 0u ||
            !within(bytes_.size(), symbols_->offset, symbols_->size))
            throw std::runtime_error("SHT_REL references an invalid ELF32 symbol table");

        const auto& owner = sections_[rel.info];
        if (!(owner.flags & kShfAlloc)) return; // Debug/non-loaded metadata is not code evidence.
        if (owner.flags & kShfExecinstr) {
            read_code_relocations(rel, owner);
        } else if (owner.type == kShtProgbits) {
            read_data_relocations(rel, owner);
        }
    }

    // M1.2 contract, unchanged: R_MIPS_32 pointer words in loaded, non-executable data.
    void read_data_relocations(const Section& rel, const Section& owner) {
        for (std::uint64_t off = rel.offset; off < static_cast<std::uint64_t>(rel.offset) + rel.size; off += 8u) {
            const auto site = read32(bytes_, static_cast<std::size_t>(off));
            const auto info = read32(bytes_, static_cast<std::size_t>(off) + 4u);
            const auto type = info & 0xffu;
            if (type == kRMipsNone) continue;
            if (type != kRMips32) {
                ++image_.unused_relocation_counts[type];
                continue;
            }
            if (!owner.contains(site, 4u))
                throw std::runtime_error("R_MIPS_32 relocation site outside declared data section");
            std::uint32_t value{};
            try {
                value = image_.data_word_at(site);
            } catch (const std::exception& e) {
                throw std::runtime_error("R_MIPS_32 source at " + std::to_string(site) +
                                         ": " + e.what());
            }
            const auto symbol = read_symbol(info >> 8u, "R_MIPS_32");
            if (const auto problem = code_target_problem(symbol, value)) {
                reject(site, value, *problem);
                continue;
            }
            accept({site, value, CodeReferenceKind::DataWord32, 0u});
        }
    }

    void read_code_relocations(const Section& rel, const Section& owner) {
        // ABI pairing: every R_MIPS_HI16 is resolved by the next R_MIPS_LO16
        // against the same symbol; several HI16 may share one LO16 (GNU).
        std::map<std::uint32_t, std::vector<std::uint32_t>> pending_high;
        for (std::uint64_t off = rel.offset; off < static_cast<std::uint64_t>(rel.offset) + rel.size; off += 8u) {
            const auto site = read32(bytes_, static_cast<std::size_t>(off));
            const auto info = read32(bytes_, static_cast<std::size_t>(off) + 4u);
            const auto type = info & 0xffu;
            const auto symbol_index = info >> 8u;
            if (type == kRMipsNone) continue;
            if (!owner.contains(site, 4u))
                throw std::runtime_error("Relocation site " + hex(site) +
                                         " outside declared executable section");
            if (type != kRMips26 && type != kRMipsHi16 && type != kRMipsLo16) {
                ++image_.unused_relocation_counts[type];
                continue;
            }
            const auto word = linked_instruction(site, type);
            if (type == kRMips26) {
                const auto op = word >> 26;
                if (op != 0x02u && op != 0x03u)
                    throw std::runtime_error("R_MIPS_26 site " + hex(site) + " is not J/JAL");
                const auto target = mips_jump_target(site, word);
                const auto symbol = read_symbol(symbol_index, "R_MIPS_26");
                if (const auto problem = code_target_problem(symbol, target)) {
                    reject(site, target, *problem);
                    continue;
                }
                accept({site, target,
                        op == 0x03u ? CodeReferenceKind::Call26 : CodeReferenceKind::Jump26, 0u});
            } else if (type == kRMipsHi16) {
                if ((word >> 26) != 0x0fu || ((word >> 21) & 31u) != 0u)
                    throw std::runtime_error("R_MIPS_HI16 site " + hex(site) + " is not LUI");
                (void)read_symbol(symbol_index, "R_MIPS_HI16");
                pending_high[symbol_index].push_back(site);
            } else {
                MipsInstruction low{};
                try {
                    low = decode_mips(site, word);
                } catch (const std::exception& e) {
                    throw std::runtime_error("R_MIPS_LO16 site " + hex(site) + ": " + e.what());
                }
                if (!carries_lo16(low))
                    throw std::runtime_error("R_MIPS_LO16 site " + hex(site) +
                                             " uses an instruction that cannot carry R_MIPS_LO16");
                const auto symbol = read_symbol(symbol_index, "R_MIPS_LO16");
                const auto group = pending_high.find(symbol_index);
                if (group == pending_high.end()) {
                    ++image_.unpaired_lo16_relocations;
                    continue;
                }
                for (const auto high_site : group->second)
                    pair(high_site, low, symbol);
                pending_high.erase(group);
            }
        }
        for (const auto& [symbol_index, sites] : pending_high) {
            (void)symbol_index;
            if (!sites.empty())
                throw std::runtime_error("R_MIPS_HI16 at " + hex(sites.front()) +
                                         " has no matching R_MIPS_LO16 for the same symbol");
        }
    }

    void pair(std::uint32_t high_site, const MipsInstruction& low, const Symbol& symbol) {
        const auto high = image_.instruction_at(high_site);
        const auto value = ((high & 0xffffu) << 16) + mips_sign_extended_imm(low.word);
        if (symbol.section == 0u || symbol.section >= kShnLoreserve ||
            symbol.section >= sections_.size())
            return; // Undefined/absolute symbols are neither code nor data evidence.
        const auto& target_section = sections_[symbol.section];
        if (!(target_section.flags & kShfAlloc) || !target_section.contains(value, 1u))
            return;
        const bool executable = (target_section.flags & kShfExecinstr) != 0u;
        image_.address_pairs.push_back({high_site, low.pc, value, executable});
        if (!executable) return;
        if (low.opcode != MipsOpcode::Addiu && low.opcode != MipsOpcode::Daddiu) {
            reject(high_site, value, "R_MIPS_HI16/LO16 accesses code memory instead of taking its address");
            return;
        }
        if (const auto problem = code_target_problem(symbol, value)) {
            reject(high_site, value, *problem);
            return;
        }
        accept({high_site, value, CodeReferenceKind::AddressHi16Lo16, low.pc});
    }

    std::uint32_t linked_instruction(std::uint32_t site, std::uint32_t type) const {
        try {
            return image_.instruction_at(site);
        } catch (const std::exception& e) {
            throw std::runtime_error("Relocation type " + std::to_string(type) + " site " +
                                     hex(site) + ": " + e.what());
        }
    }

    Symbol read_symbol(std::uint32_t index, const char* type) const {
        if (index >= symbols_->size / 16u)
            throw std::runtime_error(std::string(type) + " relocation refers to invalid symbol index");
        const auto at = static_cast<std::size_t>(symbols_->offset) + 16u * index;
        return {read32(bytes_, at + 4u), bytes_[at + 12u] & 15u, read16(bytes_, at + 14u)};
    }

    // Shared rule for every code reference: STT_FUNC must match exactly,
    // STT_SECTION must stay inside its executable section.
    std::optional<std::string> code_target_problem(const Symbol& symbol, std::uint32_t value) const {
        if (symbol.section == 0u) return "undefined relocation symbol";
        if (symbol.kind != kSttFunc && symbol.kind != kSttSection)
            return "relocation symbol is not STT_FUNC or STT_SECTION";
        if (symbol.section >= kShnLoreserve)
            return "relocation symbol has a reserved section index";
        if (symbol.section >= sections_.size())
            throw std::runtime_error("Relocation symbol section out of bounds");
        const auto& target = sections_[symbol.section];
        if (!(target.flags & kShfExecinstr) || !target.contains(value, 4u))
            return "relocated address outside symbol's executable section";
        if (symbol.kind == kSttFunc && value != symbol.value)
            return "resolved pointer differs from STT_FUNC address";
        if (symbol.kind == kSttSection && symbol.value != 0u && value < symbol.value)
            return "resolved pointer precedes STT_SECTION base";
        if ((value & 3u) != 0u) return "unaligned relocated executable pointer";
        try {
            (void)image_.instruction_at(value);
        } catch (const std::exception& e) {
            return std::string("target not executable: ") + e.what();
        }
        return std::nullopt;
    }

    void accept(const RelocatedCodePointer& pointer) {
        if (image_.relocated_code_pointers.size() >= kMaxRelocationRecords)
            throw std::runtime_error("Too many verified executable relocation targets");
        image_.relocated_code_pointers.push_back(pointer);
    }
    void reject(std::uint32_t site, std::uint32_t value, const std::string& reason) {
        image_.rejected_code_pointers.push_back({site, value, reason});
    }

    void finish() {
        auto& targets = image_.relocated_code_pointers;
        const auto key = [](const RelocatedCodePointer& p) {
            return std::tuple(p.source, p.target, static_cast<int>(p.kind), p.low_source);
        };
        std::sort(targets.begin(), targets.end(),
                  [&](const auto& a, const auto& b) { return key(a) < key(b); });
        targets.erase(std::unique(targets.begin(), targets.end(),
                                  [&](const auto& a, const auto& b) { return key(a) == key(b); }),
                      targets.end());
        std::sort(image_.address_pairs.begin(), image_.address_pairs.end(),
                  [](const auto& a, const auto& b) {
                      return std::tie(a.high_site, a.low_site) < std::tie(b.high_site, b.low_site);
                  });
    }

    ElfImage& image_;
    std::span<const std::uint8_t> bytes_;
    std::vector<Section> sections_;
    const Section* symbols_{};
    std::size_t records_seen_{};
};
} // namespace

void discover_relocated_code_pointers(ElfImage& image) {
    RelocationEvidence(image).run();
}
} // namespace ps2native
