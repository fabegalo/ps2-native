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
    // Naturally aligned little-endian accesses; misalignment is an explicit error
    // (the R5900 address-error exception is not modeled yet).
    [[nodiscard]] std::uint8_t read8(std::uint32_t guest_address) const;
    [[nodiscard]] std::uint16_t read16(std::uint32_t guest_address) const;
    [[nodiscard]] std::uint32_t read32(std::uint32_t guest_address) const;
    [[nodiscard]] std::uint64_t read64(std::uint32_t guest_address) const;
    void write8(std::uint32_t guest_address, std::uint8_t value);
    void write16(std::uint32_t guest_address, std::uint16_t value);
    void write32(std::uint32_t guest_address, std::uint32_t value);
    void write64(std::uint32_t guest_address, std::uint64_t value);
private:
    [[nodiscard]] static std::size_t physical_address(std::uint32_t guest_address, std::size_t size);
    [[nodiscard]] std::uint64_t read_le(std::uint32_t guest_address, std::size_t size, const char* what) const;
    void write_le(std::uint32_t guest_address, std::size_t size, std::uint64_t value, const char* what);
    std::vector<std::uint8_t> ram_;
};
} // namespace ps2native
