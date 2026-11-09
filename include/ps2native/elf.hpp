#pragma once
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace ps2native {
struct LoadSegment {
    std::uint32_t vaddr{};
    std::uint32_t file_offset{};
    std::uint32_t file_size{};
    std::uint32_t memory_size{};
    std::uint32_t flags{};
};
// How a relocation record proves that an address is executable code.
enum class CodeReferenceKind : std::uint8_t {
    DataWord32,      // R_MIPS_32 pointer word in non-executable data
    Jump26,          // R_MIPS_26 on J
    Call26,          // R_MIPS_26 on JAL
    AddressHi16Lo16  // R_MIPS_HI16 (LUI) paired with R_MIPS_LO16 (ADDIU/DADDIU)
};
struct RelocatedCodePointer {
    // Data word, J/JAL site, or LUI site of a HI16/LO16 pair.
    std::uint32_t source{};
    std::uint32_t target{};
    CodeReferenceKind kind{CodeReferenceKind::DataWord32};
    // ADDIU/DADDIU site of a HI16/LO16 pair; zero otherwise.
    std::uint32_t low_source{};
};
struct RejectedCodePointer {
    std::uint32_t source{};
    std::uint32_t value{};
    std::string reason;
};
// A validated R_MIPS_HI16/R_MIPS_LO16 pair, read from the linked instruction
// immediates: value = (LUI imm << 16) + sign_extend(LO16 imm).
struct RelocatedAddressPair {
    std::uint32_t high_site{};
    std::uint32_t low_site{};
    std::uint32_t value{};
    bool executable_target{};
};
class ElfImage {
public:
    std::uint32_t entry{};
    std::vector<LoadSegment> segments;
    // STT_FUNC symbols verified against executable PT_LOAD contents.
    std::vector<std::uint32_t> function_entries;
    // Evidence-backed code references from already-linked SHT_REL records.
    // Relocations are read as evidence only and are never applied.
    std::vector<RelocatedCodePointer> relocated_code_pointers;
    std::vector<RejectedCodePointer> rejected_code_pointers;
    std::vector<RelocatedAddressPair> address_pairs;
    // R_MIPS_LO16 records with no pending R_MIPS_HI16 for the same symbol
    // (valid per the MIPS ABI; their value depends on an earlier LUI).
    std::uint32_t unpaired_lo16_relocations{};
    // Relocation types that are not used as evidence, by type (counted, not applied).
    std::map<std::uint32_t, std::uint32_t> unused_relocation_counts;
    // SHT_RELA sections are not EE ELF32 practice and are not read as evidence.
    std::uint32_t ignored_rela_sections{};
    std::vector<std::uint8_t> bytes;
    [[nodiscard]] std::uint32_t instruction_at(std::uint32_t address) const;
    // 32-bit aligned words only from file-backed, non-executable PT_LOAD data.
    [[nodiscard]] std::uint32_t data_word_at(std::uint32_t address) const;
    // Executable PT_LOAD containing a file-backed instruction at address.
    [[nodiscard]] const LoadSegment& code_segment_at(std::uint32_t address) const;
};
[[nodiscard]] ElfImage parse_ee_elf(std::span<const std::uint8_t> data);
// Stripped physical-RAM ELF, unchanged bytes/provenance and explicit KSEG execution.
[[nodiscard]] ElfImage ee_execution_alias(ElfImage image,std::uint32_t alias);
} // namespace ps2native
