#pragma once
#include <cstdint>
#include <span>
#include <optional>
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
class ElfImage {
public:
    std::uint32_t entry{};
    std::vector<LoadSegment> segments;
    // STT_FUNC symbols verified against executable PT_LOAD contents.
    std::vector<std::uint32_t> function_entries;
    std::vector<std::uint8_t> bytes;
    [[nodiscard]] std::uint32_t instruction_at(std::uint32_t address) const;
    // 32-bit aligned words only from file-backed, non-executable PT_LOAD data.
    [[nodiscard]] std::uint32_t data_word_at(std::uint32_t address) const;
    // Executable PT_LOAD containing a file-backed instruction at address.
    [[nodiscard]] const LoadSegment& code_segment_at(std::uint32_t address) const;
};
[[nodiscard]] ElfImage parse_ee_elf(std::span<const std::uint8_t> data);

struct UnpackedEeImage {
    std::uint32_t load_address{};
    std::vector<std::uint8_t> bytes;
};
// Recognizes the Expand loader metadata and its actual argument setup. No fixed ROM offsets.
[[nodiscard]] std::optional<UnpackedEeImage> unpack_ee_payload(const ElfImage& loader);
// Called after the producer stopped, before accepting its executable image.
void verify_ee_payload(const UnpackedEeImage& image, std::uint32_t entry,
                       std::span<const std::uint8_t> ram);
} // namespace ps2native
