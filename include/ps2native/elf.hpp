#pragma once
#include <cstdint>
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
struct RelocatedCodePointer {
    std::uint32_t source{};
    std::uint32_t target{};
};
struct RejectedCodePointer {
    std::uint32_t source{};
    std::uint32_t value{};
    std::string reason;
};
class ElfImage {
public:
    std::uint32_t entry{};
    std::vector<LoadSegment> segments;
    // STT_FUNC symbols verified against executable PT_LOAD contents.
    std::vector<std::uint32_t> function_entries;
    // Only evidence-backed, already-linked ELF32 MIPS R_MIPS_32 data pointers.
    std::vector<RelocatedCodePointer> relocated_code_pointers;
    std::vector<RejectedCodePointer> rejected_code_pointers;
    std::vector<std::uint8_t> bytes;
    [[nodiscard]] std::uint32_t instruction_at(std::uint32_t address) const;
    // 32-bit aligned words only from file-backed, non-executable PT_LOAD data.
    [[nodiscard]] std::uint32_t data_word_at(std::uint32_t address) const;
};
[[nodiscard]] ElfImage parse_ee_elf(std::span<const std::uint8_t> data);
} // namespace ps2native
