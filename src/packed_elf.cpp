#include "ps2native/elf.hpp"
#include <algorithm>
#include <stdexcept>

namespace ps2native {
namespace {
constexpr std::uint64_t ram_size = 32u * 1024u * 1024u;
std::uint32_t read(std::span<const std::uint8_t> data, std::size_t at, unsigned count, bool big = false) {
    if (at > data.size() || count > data.size() - at) throw std::runtime_error("Packed ELF: truncated input");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i)
        value |= std::uint32_t(data[at + i]) << (8u * (big ? count - i - 1u : i));
    return value;
}
struct Section { std::string name; std::uint32_t flags, address, offset, size; };
}

std::optional<UnpackedEeImage> unpack_ee_payload(const ElfImage& loader) {
    const std::span<const std::uint8_t> data(loader.bytes);
    const auto table = read(data, 32, 4), count = read(data, 48, 2), strings = read(data, 50, 2);
    if (!table && !count) return std::nullopt;
    if (read(data, 46, 2) != 40 || strings >= count ||
        std::uint64_t(table) + std::uint64_t(count) * 40 > data.size())
        throw std::runtime_error("Packed ELF: invalid section metadata");
    const auto string_section = std::size_t(table) + strings * 40u;
    const auto names_at = read(data, string_section + 16u, 4);
    const auto names_size = read(data, string_section + 20u, 4);
    if (read(data, string_section + 4u, 4) != 3 ||
        std::uint64_t(names_at) + names_size > data.size())
        throw std::runtime_error("Packed ELF: invalid section names");
    std::vector<Section> sections;
    std::optional<std::uint32_t> expand;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto at = std::size_t(table) + i * 40u;
        const auto name = read(data, at, 4);
        if (name >= names_size) throw std::runtime_error("Packed ELF: section name out of range");
        const auto start = data.begin() + names_at + name, end = data.begin() + names_at + names_size;
        const auto zero = std::find(start, end, 0);
        if (zero == end) throw std::runtime_error("Packed ELF: unterminated section name");
        Section section{std::string(start, zero), read(data, at + 8, 4), read(data, at + 12, 4),
            read(data, at + 16, 4), read(data, at + 20, 4)};
        if (section.name == ".text.Expand") {
            if (expand || !(section.flags & 4u)) throw std::runtime_error("Packed ELF: ambiguous Expand entry");
            expand = section.address;
        }
        if (read(data, at + 4, 4) == 1) sections.push_back(std::move(section));
    }
    if (!expand) return std::nullopt;
    (void)loader.instruction_at(*expand);
    std::optional<std::pair<std::uint32_t, std::uint32_t>> arguments;
    for (const auto& section : sections) {
        if (!(section.flags & 4u)) continue;
        if (std::uint64_t(section.offset) + section.size > data.size() || section.size % 4)
            throw std::runtime_error("Packed ELF: invalid executable section");
        for (std::uint32_t at = 0; std::uint64_t(at) + 16 <= section.size; at += 4) {
            const auto address = section.address + at;
            const auto file_at = std::size_t(section.offset) + at;
            const auto a = read(data, file_at, 4), b = read(data, file_at + 4, 4);
            const auto call = read(data, file_at + 8, 4), delay = read(data, file_at + 12, 4);
            if ((a >> 16) != 0x3c04 || (b >> 16) != 0x3c05 || (call >> 26) != 3 ||
                (delay >> 16) != 0x2484 || (((address + 12) & 0xf0000000u) | ((call & 0x03ffffffu) << 2)) != *expand)
                continue;
            // Require the argument instructions to be the actual loaded instruction bytes.
            for (unsigned i = 0; i < 4; ++i)
                if (loader.instruction_at(address + i * 4) != read(data, file_at + i * 4, 4))
                    throw std::runtime_error("Packed ELF: executable section differs from PT_LOAD");
            const auto source = ((a & 0xffffu) << 16) + static_cast<std::int16_t>(delay & 0xffffu);
            const std::pair<std::uint32_t, std::uint32_t> found{source, (b & 0xffffu) << 16};
            if (arguments && *arguments != found) throw std::runtime_error("Packed ELF: multiple unpack destinations");
            arguments = found;
        }
    }
    if (!arguments) throw std::runtime_error("Packed ELF: Expand argument setup is unsupported");
    const auto [source, destination] = *arguments;
    std::optional<std::span<const std::uint8_t>> stream;
    for (const auto& section : sections) {
        if ((section.flags & 6u) != 2u || source < section.address || source - section.address >= section.size) continue;
        if (stream || std::uint64_t(section.offset) + section.size > data.size())
            throw std::runtime_error("Packed ELF: invalid packed data extent");
        const auto offset = std::size_t(section.offset) + source - section.address;
        const auto length = section.size - (source - section.address);
        const auto mapped = std::any_of(loader.segments.begin(), loader.segments.end(), [&](const auto& segment) {
            return source >= segment.vaddr && std::uint64_t(source) + length <= std::uint64_t(segment.vaddr) + segment.file_size &&
                std::uint64_t(segment.file_offset) + source - segment.vaddr == offset;
        });
        if (!mapped) throw std::runtime_error("Packed ELF: data section differs from PT_LOAD");
        stream = data.subspan(offset, length);
    }
    if (!stream) throw std::runtime_error("Packed ELF: packed source is outside an allocated data section");
    const auto size = read(*stream, 0, 4);
    if (!size || destination % 4 || std::uint64_t(destination) + size > ram_size)
        throw std::runtime_error("Packed ELF: unpacked image is outside EE RAM");
    for (const auto& segment : loader.segments)
        if (destination < std::uint64_t(segment.vaddr) + segment.memory_size && segment.vaddr < std::uint64_t(destination) + size)
            throw std::runtime_error("Packed ELF: overlapping source loader and unpacked image");
    UnpackedEeImage result{destination, {}};
    result.bytes.reserve(size);
    std::size_t at = 4;
    while (result.bytes.size() < size) {
        const auto descriptor = read(*stream, at, 4, true);
        at += 4;
        const auto shift = 14u - (descriptor & 3u);
        for (unsigned token = 0; token < 30 && result.bytes.size() < size; ++token) {
            if ((descriptor & (0x80000000u >> token)) == 0) {
                result.bytes.push_back(static_cast<std::uint8_t>(read(*stream, at++, 1)));
                continue;
            }
            const auto encoded = read(*stream, at, 2, true);
            at += 2;
            const auto distance = (encoded & ((1u << shift) - 1u)) + 1u;
            if (distance > result.bytes.size()) throw std::runtime_error("Packed ELF: back reference precedes output");
            const auto length = std::min<std::size_t>((encoded >> shift) + 3u, size - result.bytes.size());
            for (std::size_t n = 0; n < length; ++n) result.bytes.push_back(result.bytes[result.bytes.size() - distance]);
        }
    }
    return result;
}
void verify_ee_payload(const UnpackedEeImage& image, std::uint32_t entry,
                       std::span<const std::uint8_t> ram) {
    if (image.bytes.empty() || image.load_address > ram.size() || image.bytes.size() > ram.size() - image.load_address)
        throw std::runtime_error("[guest-exec:invalid-image] unpacked extent is outside RAM");
    if (entry % 4 || entry < image.load_address ||
        std::uint64_t(entry) + 4 > std::uint64_t(image.load_address) + image.bytes.size())
        throw std::runtime_error("[guest-exec:invalid-entry] target is outside the unpacked image");
    const auto mismatch = std::mismatch(image.bytes.begin(), image.bytes.end(), ram.begin() + image.load_address);
    if (mismatch.first != image.bytes.end())
        throw std::runtime_error("[guest-exec:content-mismatch] first differing byte=" +
            std::to_string(mismatch.first - image.bytes.begin()));
}
} // namespace ps2native
