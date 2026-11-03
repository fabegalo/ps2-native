#include "ps2native/elf.hpp"
#include "ps2native/memory.hpp"

#include <algorithm>
#include <stdexcept>

namespace ps2native {
namespace {
std::uint16_t u16(std::span<const std::uint8_t> b, std::size_t pos) {
    if (pos > b.size() || b.size() - pos < 2) throw std::runtime_error("ELF truncated (u16)");
    return static_cast<std::uint16_t>(b[pos]) |
           (static_cast<std::uint16_t>(b[pos + 1]) << 8);
}
std::uint32_t u32(std::span<const std::uint8_t> b, std::size_t pos) {
    if (pos > b.size() || b.size() - pos < 4) throw std::runtime_error("ELF truncated (u32)");
    return static_cast<std::uint32_t>(b[pos]) |
           (static_cast<std::uint32_t>(b[pos + 1]) << 8) |
           (static_cast<std::uint32_t>(b[pos + 2]) << 16) |
           (static_cast<std::uint32_t>(b[pos + 3]) << 24);
}
bool in_range(std::size_t length, std::uint64_t begin, std::uint64_t size) {
    return begin <= length && size <= length - begin;
}
} // namespace

ElfImage parse_ee_elf(std::span<const std::uint8_t> data) {
    if (data.size() < 52 || data[0] != 0x7f || data[1] != 'E' ||
        data[2] != 'L' || data[3] != 'F')
        throw std::runtime_error("Not an ELF file");
    if (data[4] != 1 || data[5] != 1 || data[6] != 1)
        throw std::runtime_error("M0 accepts only ELF32 little-endian v1");
    if (u16(data, 16) != 2 || u16(data, 18) != 8 || u32(data, 20) != 1)
        throw std::runtime_error("M0 accepts only ET_EXEC / EM_MIPS / ELF v1");
    if (u16(data, 40) != 52) throw std::runtime_error("Invalid ELF32 header size");
    const auto phoff = u32(data, 28);
    const auto phentsize = u16(data, 42);
    const auto phnum = u16(data, 44);
    if (phentsize != 32 || phnum == 0 || phnum > 128 ||
        !in_range(data.size(), phoff, static_cast<std::uint64_t>(phentsize) * phnum))
        throw std::runtime_error("Invalid ELF program header table");

    ElfImage elf;
    elf.entry = u32(data, 24);
    if ((elf.entry & 3u) != 0) throw std::runtime_error("Unaligned ELF entry");
    for (unsigned i = 0; i < phnum; ++i) {
        const std::size_t pos = static_cast<std::size_t>(phoff) + 32 * i;
        if (u32(data, pos) != 1) continue; // Only PT_LOAD.
        LoadSegment seg{
            u32(data, pos + 8), u32(data, pos + 4), u32(data, pos + 16),
            u32(data, pos + 20), u32(data, pos + 24)
        };
        const auto align = u32(data, pos + 28);
        if ((align > 1 && ((align & (align - 1)) != 0 ||
             (seg.vaddr % align) != (seg.file_offset % align))) ||
            seg.file_size > seg.memory_size ||
            !in_range(data.size(), seg.file_offset, seg.file_size) ||
            !in_range(Memory::kMainRamBytes, seg.vaddr, seg.memory_size))
            throw std::runtime_error("Invalid or unmapped PT_LOAD segment");
        if (seg.memory_size == 0) continue;
        for (const auto& existing : elf.segments) {
            const auto a = static_cast<std::uint64_t>(seg.vaddr);
            const auto b = static_cast<std::uint64_t>(existing.vaddr);
            if (a < b + existing.memory_size && b < a + seg.memory_size)
                throw std::runtime_error("Overlapping PT_LOAD segments not supported");
        }
        elf.segments.push_back(seg);
    }
    if (elf.segments.empty()) throw std::runtime_error("No loadable segments");
    bool entry_found = false;
    for (const auto& seg : elf.segments) {
        if ((seg.flags & 1) && elf.entry >= seg.vaddr &&
            static_cast<std::uint64_t>(elf.entry) + 4 <=
            static_cast<std::uint64_t>(seg.vaddr) + seg.file_size)
            entry_found = true;
    }
    if (!entry_found) throw std::runtime_error("Entry not in file-backed executable segment");
    elf.bytes.assign(data.begin(), data.end());
    return elf;
}

std::uint32_t ElfImage::instruction_at(std::uint32_t address) const {
    if ((address & 3u) != 0) throw std::runtime_error("Misaligned instruction address");
    for (const auto& seg : segments) {
        if ((seg.flags & 1) && address >= seg.vaddr &&
            static_cast<std::uint64_t>(address) + 4 <=
            static_cast<std::uint64_t>(seg.vaddr) + seg.file_size) {
            const std::size_t offset = seg.file_offset + (address - seg.vaddr);
            return u32(bytes, offset);
        }
    }
    throw std::runtime_error("Instruction outside executable PT_LOAD / dynamic code unsupported");
}

}  // namespace ps2native
