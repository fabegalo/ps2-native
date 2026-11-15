#include "ps2native/xff.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

// A synthetic xff2 module as the game's loader leaves it in EE RAM: header pointers and
// symbol values already absolute. No game data.
namespace {
void put(std::vector<std::uint8_t>& ram, std::size_t at, std::uint32_t value, unsigned size = 4) {
    for (unsigned i = 0; i < size; ++i) ram.at(at + i) = static_cast<std::uint8_t>(value >> (8 * i));
}
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }

constexpr std::uint32_t header = 0x1000, text = 0x2000;
std::vector<std::uint8_t> loaded(std::uint32_t functionValue) {
    std::vector<std::uint8_t> ram(0x4000);
    ram[header] = 'x'; ram[header + 1] = 'f'; ram[header + 2] = 'f'; ram[header + 3] = '2';
    put(ram, header + 0x10, text);  // entry
    // Pointer fields and the file offsets the loader rebased them from.
    constexpr std::uint32_t fields[] = {0x20, 0x28, 0x2c, 0x30, 0x34, 0x3c, 0x44, 0x48},
        offsets[] = {0x80, 0x100, 0x180, 0x200, 0x280, 0x300, 0x340, 0x360};
    for (int i = 0; i < 8; ++i) {
        put(ram, header + 0x50 + i * 4, offsets[i]);
        put(ram, header + fields[i], 0x80000000u | (header + offsets[i]));  // kseg0 alias
    }
    put(ram, header + 0x24, 3);  // symbols
    put(ram, header + 0x38, 0);  // relocation tables
    put(ram, header + 0x40, 2);  // sections: null, .text
    put(ram, header + 0x200 + 32 + 0, text);
    put(ram, header + 0x200 + 32 + 8, 0x100);
    put(ram, header + 0x200 + 32 + 16, 1);  // PROGBITS
    put(ram, header + 0x340 + 4, 1);
    const char name[] = ".text";
    for (std::size_t i = 0; i < sizeof name; ++i) ram[header + 0x361 + i] = static_cast<std::uint8_t>(name[i]);
    const auto symbol = header + 0x100 + 16;
    put(ram, symbol, 0x7fff);  // st_name: a string offset larger than the section
    put(ram, symbol + 4, functionValue);
    ram[symbol + 12] = 0x12;  // STB_GLOBAL | STT_FUNC
    put(ram, symbol + 14, 1, 2);
    put(ram, symbol + 16 + 4, 0x103110);  // a function of the program the module links against
    ram[symbol + 16 + 12] = 0x12;
    put(ram, symbol + 16 + 14, 0xfff1, 2);  // SHN_ABS
    return ram;
}
} // namespace

int main() {
    try {
        const auto ram = loaded(0x80000000u | (text + 0x40));
        const auto module = ps2native::find_loaded_xff(ram, text);
        require(module && module->header == header && module->entry == text, "module not found by its entry");
        require(module->code.size() == 1 && module->code[0].address == text && module->code[0].size == 0x100,
            "code section");
        require(module->functions == std::vector<std::uint32_t>{text + 0x40}, "function read from st_value");
        require(module->imports == std::vector<std::uint32_t>{0x103110}, "absolute function import");
        require(!ps2native::find_loaded_xff(ram, text + 4), "a non-entry address matched");
        bool rejected = false;
        try { (void)ps2native::find_loaded_xff(loaded(text + 0x100), text); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "a function outside its section was accepted");
    } catch (const std::exception& error) {
        std::cerr << "xff tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "xff tests passed\n";
    return 0;
}
