#include "ps2native/elf.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void word(std::vector<std::uint8_t>& data, std::size_t at, std::uint32_t value, unsigned size = 4) {
    for (unsigned i = 0; i < size; ++i) data.at(at + i) = static_cast<std::uint8_t>(value >> (8 * i));
}
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
std::vector<std::uint8_t> fixture(const std::vector<std::uint8_t>& stream,
                                std::uint32_t base = 0x1000, std::uint32_t destination = 0x200000) {
    std::vector<std::uint8_t> data(0x200 + 5 * 40);
    const std::uint8_t ident[]{0x7f, 'E', 'L', 'F', 1, 1, 1};
    std::copy(std::begin(ident), std::end(ident), data.begin());
    word(data, 16, 2, 2); word(data, 18, 8, 2); word(data, 20, 1);
    word(data, 24, base); word(data, 28, 52); word(data, 32, 0x200);
    word(data, 40, 52, 2); word(data, 42, 32, 2); word(data, 44, 1, 2);
    word(data, 46, 40, 2); word(data, 48, 5, 2); word(data, 50, 4, 2);
    word(data, 52, 1); word(data, 56, 0x80); word(data, 60, base);
    word(data, 68, 0x100); word(data, 72, 0x100); word(data, 76, 7); word(data, 80, 16);
    const auto source = base + 0x80;
    word(data, 0x80, 0x3c040000u | ((source + 0x8000u) >> 16));
    word(data, 0x84, 0x3c050000u | (destination >> 16));
    word(data, 0x88, 0x0c000000u | ((base + 0x40) >> 2));
    word(data, 0x8c, 0x24840000u | (source & 0xffff));
    word(data, 0xc0, 0x03e00008);
    std::copy(stream.begin(), stream.end(), data.begin() + 0x100);
    const char names[] = "\0.text\0.text.Expand\0.data\0.shstrtab\0";
    std::copy(std::begin(names), std::end(names), data.begin() + 0x180);
    const auto section = [&](unsigned i, unsigned name, unsigned type, unsigned flags,
                             unsigned address, unsigned offset, unsigned size) {
        const auto at = 0x200 + i * 40;
        word(data, at, name); word(data, at + 4, type); word(data, at + 8, flags);
        word(data, at + 12, address); word(data, at + 16, offset); word(data, at + 20, size);
    };
    section(1, 1, 1, 6, base, 0x80, 16);
    section(2, 7, 1, 6, base + 0x40, 0xc0, 8);
    section(3, 20, 1, 3, source, 0x100, static_cast<unsigned>(stream.size()));
    section(4, 26, 3, 0, 0, 0x180, sizeof(names));
    return data;
}
void refused(const std::vector<std::uint8_t>& bytes) {
    try { (void)ps2native::unpack_ee_payload(ps2native::parse_ee_elf(bytes)); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Invalid packed image was accepted");
}
}
int main() {
    try {
        const std::vector<std::uint8_t> literals{10, 0, 0, 0, 0x10, 0, 0, 0, 'A', 'B', 'C', 0xc0, 2, 'Z'};
        for (const auto base : {0x1000u, 0x5f000u}) {
            const auto image = ps2native::unpack_ee_payload(ps2native::parse_ee_elf(fixture(literals, base)));
            require(image && image->load_address == 0x200000 &&
                    std::string(image->bytes.begin(), image->bytes.end()) == "ABCABCABCZ", "Literal/copy decoding or argument relocation failed");
        }
        const auto overlap = ps2native::unpack_ee_payload(ps2native::parse_ee_elf(
            fixture({6, 0, 0, 0, 0x40, 0, 0, 3, 'A', 0x10, 0})));
        require(overlap && std::string(overlap->bytes.begin(), overlap->bytes.end()) == "AAAAAA", "Overlapping copy or token split failed");
        std::vector<std::uint8_t> ram(0x200020);
        std::copy(overlap->bytes.begin(), overlap->bytes.end(), ram.begin() + overlap->load_address);
        ps2native::verify_ee_payload(*overlap, overlap->load_address, ram);
        const auto rejected = [](auto check) {
            try { check(); } catch (const std::runtime_error&) { return; }
            throw std::runtime_error("Invalid native executable image was accepted");
        };
        rejected([&] { ps2native::verify_ee_payload(*overlap, overlap->load_address + 4, ram); });
        rejected([&] { ps2native::verify_ee_payload(*overlap, overlap->load_address + 1, ram); });
        rejected([&] { ps2native::verify_ee_payload(*overlap, overlap->load_address, std::span(ram).first(16)); });
        ram[overlap->load_address + 5] ^= 1;
        rejected([&] { ps2native::verify_ee_payload(*overlap, overlap->load_address, ram); });
        refused(fixture({4, 0, 0, 0, 0x80, 0, 0, 0, 0, 0}));
        refused(fixture({1, 0, 0, 0}));
        refused(fixture({0xff, 0xff, 0xff, 0xff}));
        refused(fixture(literals, 0x1000, 0x2000000));
        auto changed = fixture(literals); word(changed, 0x8c, 0); refused(changed);
        changed = fixture(literals); word(changed, 0x200 + 3 * 40 + 8, 6); refused(changed);
        changed = fixture(literals);
        std::copy(literals.begin(), literals.end(), changed.begin() + 0x120);
        word(changed, 0x200 + 3 * 40 + 16, 0x120); refused(changed);
        changed = fixture(literals); word(changed, 0x200 + 2 * 40, 1);
        require(!ps2native::unpack_ee_payload(ps2native::parse_ee_elf(changed)), "Unpacked ELF was misidentified");
        std::cout << "PASS packed EE arguments, signed immediates, literals, copies, bounds and malformed streams\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
