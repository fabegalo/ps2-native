#pragma once
#include "ps2native/elf.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>
namespace ps2native {
class Memory {
public:
    static constexpr std::size_t kMainRamBytes = 32u * 1024u * 1024u;
    Memory();
    void load(const ElfImage& elf);
    [[nodiscard]] std::uint32_t read32(std::uint32_t guest_address) const;
    void write32(std::uint32_t guest_address, std::uint32_t value);
    [[nodiscard]] std::uint8_t read8(std::uint32_t guest_address) const;
    void write8(std::uint32_t guest_address, std::uint8_t value);
private:
    [[nodiscard]] static std::size_t physical_address(std::uint32_t guest_address, std::size_t size);
    std::vector<std::uint8_t> ram_;
};
} // namespace ps2native
