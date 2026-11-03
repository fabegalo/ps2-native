#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace ps2native {
struct LoadSegment {
    std::uint32_t vaddr{};
    std::uint32_t file_offset{};
    std::uint32_t file_size{};
    std::uint32_t memory_size{};
    std::uint32_t flags{};
};
class ElfImage {
public:
    std::uint32_t entry{};
    std::vector<LoadSegment> segments;
    std::vector<std::uint8_t> bytes;
    [[nodiscard]] std::uint32_t instruction_at(std::uint32_t address) const;
};
[[nodiscard]] ElfImage parse_ee_elf(std::span<const std::uint8_t> data);
} // namespace ps2native
