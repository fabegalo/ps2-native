#include "ps2native/xff.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>

// Layout facts (header fields, 32-byte section headers, 28-byte REL descriptors) follow the
// xff2 description in volstants/recomp-ShadowOfTheColossus-pc Docs/XFF_FORMAT.md; this is an
// independent implementation that checks each table against the others before trusting it.
namespace ps2native {
namespace {
constexpr std::uint32_t kPhysical = 0x1fffffffu;

struct Ram {
    std::span<const std::uint8_t> bytes;
    std::uint32_t u32(std::uint64_t at) const {
        if (at > bytes.size() || bytes.size() - at < 4) throw std::runtime_error("xff2 table outside EE RAM");
        return std::uint32_t(bytes[at]) | std::uint32_t(bytes[at + 1]) << 8 |
               std::uint32_t(bytes[at + 2]) << 16 | std::uint32_t(bytes[at + 3]) << 24;
    }
    std::uint16_t u16(std::uint64_t at) const {
        if (at > bytes.size() || bytes.size() - at < 2) throw std::runtime_error("xff2 table outside EE RAM");
        return std::uint16_t(bytes[at] | bytes[at + 1] << 8);
    }
    std::uint8_t u8(std::uint64_t at) const {
        if (at >= bytes.size()) throw std::runtime_error("xff2 table outside EE RAM");
        return bytes[at];
    }
};

LoadedXffModule read_module(const Ram& ram, std::uint32_t header) {
    // Pointer fields 0x20..0x48 are rebased by the loader to header + the file offsets kept at 0x50..0x6c.
    constexpr std::uint32_t pointers[] = {0x20, 0x28, 0x2c, 0x30, 0x34, 0x3c, 0x44, 0x48};
    for (std::uint32_t i = 0; i < 8; ++i)
        if ((ram.u32(header + pointers[i]) & kPhysical) != header + ram.u32(header + 0x50 + i * 4))
            throw std::runtime_error("xff2 header pointers disagree with its file offsets");
    const std::uint64_t sections = header + ram.u32(header + 0x5c), symbols = header + ram.u32(header + 0x54),
        relocations = header + ram.u32(header + 0x64);
    const auto sectionCount = ram.u32(header + 0x40), symbolCount = ram.u32(header + 0x24),
        relocationCount = ram.u32(header + 0x38);
    if (sectionCount == 0 || sectionCount > 256 || symbolCount > 0x100000 || relocationCount > 4096)
        throw std::runtime_error("xff2 table counts are out of range");

    LoadedXffModule module;
    module.header = header;
    module.entry = ram.u32(header + 0x10) & kPhysical;
    std::vector<std::uint32_t> base(sectionCount), size(sectionCount);
    std::vector<bool> code(sectionCount);
    const std::uint64_t nameIndex = header + ram.u32(header + 0x68), names = header + ram.u32(header + 0x6c);
    const auto named = [&](std::uint32_t section, std::string_view wanted) {
        const auto name = names + ram.u32(nameIndex + section * 4ull);
        for (std::size_t i = 0; i <= wanted.size(); ++i)
            if (ram.u8(name + i) != (i < wanted.size() ? static_cast<std::uint8_t>(wanted[i]) : 0u)) return false;
        return true;
    };
    for (std::uint32_t i = 0; i < sectionCount; ++i) {
        const auto at = sections + i * 32ull;
        base[i] = ram.u32(at) & kPhysical;
        size[i] = ram.u32(at + 8);
        // The file keeps no ELF section flags; EE code lives in PROGBITS .text (.vutext is VU code).
        code[i] = ram.u32(at + 16) == 1 && size[i] != 0 && named(i, ".text");
        if (!code[i]) continue;
        if ((base[i] & 3u) || (size[i] & 3u) || std::uint64_t(base[i]) + size[i] > ram.bytes.size())
            throw std::runtime_error("xff2 code section is outside EE RAM");
        module.code.push_back({base[i], size[i]});
    }
    if (module.code.empty()) throw std::runtime_error("xff2 module has no executable section");
    const auto inCode = [&](std::uint32_t address) {
        return std::any_of(module.code.begin(), module.code.end(), [&](const auto& s) {
            return address >= s.address && address - s.address < s.size; });
    };
    if (!inCode(module.entry)) throw std::runtime_error("xff2 entry is outside its executable sections");

    const auto symbol = [&](std::uint32_t index) { return symbols + index * 16ull; };
    for (std::uint32_t i = 0; i < symbolCount; ++i) {
        const auto shndx = ram.u16(symbol(i) + 14);
        if ((ram.u8(symbol(i) + 12) & 0xfu) != 2 || shndx >= sectionCount || !code[shndx]) continue;  // STT_FUNC
        const auto value = ram.u32(symbol(i));
        if (value >= size[shndx] || (value & 3u)) throw std::runtime_error("xff2 function symbol is outside its section");
        module.functions.push_back(base[shndx] + value);
    }
    std::sort(module.functions.begin(), module.functions.end());
    module.functions.erase(std::unique(module.functions.begin(), module.functions.end()), module.functions.end());

    for (std::uint32_t i = 0; i < relocationCount; ++i) {
        const auto at = relocations + i * 28ull;
        const auto type = ram.u32(at), count = ram.u32(at + 4), target = ram.u32(at + 8);
        if (type != 9 || target >= sectionCount || count > 0x400000)
            throw std::runtime_error("xff2 relocation descriptor is not a REL table");
        if (!code[target]) continue;
        const std::uint64_t entries = header + ram.u32(at + 20);
        for (std::uint32_t j = 0; j < count; ++j) {
            const auto offset = ram.u32(entries + j * 8ull), info = ram.u32(entries + j * 8ull + 4);
            const auto kind = info & 0xffu, index = info >> 8;
            if (offset >= size[target] || index >= symbolCount || (kind != 2 && kind != 4 && kind != 5 && kind != 6))
                throw std::runtime_error("xff2 relocation entry is invalid");
            if (index == 0 || ram.u16(symbol(index) + 14) != 0) continue;  // defined: fixed at load time
            if (kind == 2 || (offset & 3u)) throw std::runtime_error("xff2 import relocates a data word in code");
            module.live.push_back({base[target] + offset, static_cast<std::uint8_t>(kind)});
        }
    }
    std::sort(module.live.begin(), module.live.end(), [](const auto& a, const auto& b) { return a.address < b.address; });
    for (std::size_t i = 1; i < module.live.size(); ++i)
        if (module.live[i].address == module.live[i - 1].address && module.live[i].type != module.live[i - 1].type)
            throw std::runtime_error("xff2 import site has conflicting relocation types");
    module.live.erase(std::unique(module.live.begin(), module.live.end(),
        [](const auto& a, const auto& b) { return a.address == b.address; }), module.live.end());
    return module;
}
} // namespace

std::optional<LoadedXffModule> find_loaded_xff(std::span<const std::uint8_t> bytes, std::uint32_t entry) {
    const Ram ram{bytes};
    entry &= kPhysical;
    std::optional<LoadedXffModule> found;
    for (std::size_t at = 0; at + 0x70 <= bytes.size(); at += 4) {
        if (bytes[at] != 'x' || bytes[at + 1] != 'f' || bytes[at + 2] != 'f' || bytes[at + 3] != '2' ||
            (ram.u32(at + 0x10) & kPhysical) != entry)
            continue;
        if (found) throw std::runtime_error("several xff2 modules claim the same entry");
        found = read_module(ram, static_cast<std::uint32_t>(at));
    }
    return found;
}
} // namespace ps2native
