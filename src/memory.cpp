#include "ps2native/memory.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace ps2native {
Memory::Memory() : ram_(kMainRamBytes) {}
std::size_t Memory::physical_address(std::uint32_t address, std::size_t size) {
    std::uint64_t base = address;
    if (address >= 0x80000000u && address < 0x82000000u) base = address - 0x80000000u;
    else if (address >= 0xA0000000u && address < 0xA2000000u) base = address - 0xA0000000u;
    if (base > kMainRamBytes || size > kMainRamBytes - base)
        throw std::out_of_range("Guest address is not in implemented EE main RAM");
    return static_cast<std::size_t>(base);
}

void Memory::load(const ElfImage& elf) {
    std::fill(ram_.begin(), ram_.end(), std::uint8_t{0});
    for (const auto& seg : elf.segments) {
        const auto start = physical_address(seg.vaddr, seg.memory_size);
        std::copy_n(elf.bytes.data() + seg.file_offset, seg.file_size, ram_.begin() + start);
    }
}
std::uint64_t Memory::read_le(std::uint32_t address, std::size_t size, const char* what) const {
    if ((address & (size - 1u)) != 0) throw std::runtime_error(std::string("Unaligned guest ") + what);
    const auto offset = physical_address(address, size);
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i)
        value |= static_cast<std::uint64_t>(ram_[offset + i]) << (8u * i);
    return value;
}
void Memory::write_le(std::uint32_t address, std::size_t size, std::uint64_t value, const char* what) {
    if ((address & (size - 1u)) != 0) throw std::runtime_error(std::string("Unaligned guest ") + what);
    const auto offset = physical_address(address, size);
    for (std::size_t i = 0; i < size; ++i)
        ram_[offset + i] = static_cast<std::uint8_t>(value >> (8u * i));
}
std::uint8_t Memory::read8(std::uint32_t address) const {
    return ram_[physical_address(address, 1)];
}
void Memory::write8(std::uint32_t address, std::uint8_t value) {
    ram_[physical_address(address, 1)] = value;
}
std::uint16_t Memory::read16(std::uint32_t address) const {
    return static_cast<std::uint16_t>(read_le(address, 2, "LH"));
}
void Memory::write16(std::uint32_t address, std::uint16_t value) {
    write_le(address, 2, value, "SH");
}
std::uint32_t Memory::read32(std::uint32_t address) const {
    return static_cast<std::uint32_t>(read_le(address, 4, "LW"));
}
void Memory::write32(std::uint32_t address, std::uint32_t value) {
    write_le(address, 4, value, "SW");
}
std::uint64_t Memory::read64(std::uint32_t address) const {
    return read_le(address, 8, "LD");
}
void Memory::write64(std::uint32_t address, std::uint64_t value) {
    write_le(address, 8, value, "SD");
}
}  // namespace ps2native
