#include "ps2native/memory.hpp"

#include <algorithm>
#include <stdexcept>

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
    std::fill(ram_.begin(), ram_.end(), 0);
    for (const auto& seg : elf.segments) {
        const auto start = physical_address(seg.vaddr, seg.memory_size);
        std::copy_n(elf.bytes.data() + seg.file_offset, seg.file_size, ram_.begin() + start);
    }
}
std::uint8_t Memory::read8(std::uint32_t address) const {
    return ram_[physical_address(address, 1)];
}
void Memory::write8(std::uint32_t address, std::uint8_t value) {
    ram_[physical_address(address, 1)] = value;
}
std::uint32_t Memory::read32(std::uint32_t address) const {
    if ((address & 3u) != 0) throw std::runtime_error("Unaligned guest LW");
    const auto offset = physical_address(address, 4);
    return static_cast<std::uint32_t>(ram_[offset]) |
           (static_cast<std::uint32_t>(ram_[offset+1]) << 8) |
           (static_cast<std::uint32_t>(ram_[offset+2]) << 16) |
           (static_cast<std::uint32_t>(ram_[offset+3]) << 24);
}
void Memory::write32(std::uint32_t address, std::uint32_t value) {
    if ((address & 3u) != 0) throw std::runtime_error("Unaligned guest SW");
    const auto offset = physical_address(address, 4);
    for (unsigned i = 0; i < 4; ++i) ram_[offset+i] = static_cast<std::uint8_t>(value >> (8*i));
}
}  // namespace ps2native
