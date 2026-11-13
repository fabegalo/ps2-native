#include "ps2native/bios.hpp"
#include "ps2native/sha256.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
void word(std::vector<std::uint8_t>& data, std::size_t at, std::uint32_t value, unsigned size = 4) {
    for (unsigned i = 0; i < size; ++i) data.at(at + i) = static_cast<std::uint8_t>(value >> (i * 8));
}
template<class F> void refused(F call) {
    try { call(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Invalid firmware association was accepted");
}
}

int main() {
    try {
        const auto* hardware = ps2native::find_console_hardware("SCPH-70004:GH-032-11");
        require(hardware && hardware->mechacon, "Fixture profile disappeared");
        std::vector<std::uint8_t> rom(4u * 1024u * 1024u), nvm(ps2native::kNvmBytes);
        const auto& layout = ps2native::nvm_layout_info(hardware->mechacon->layout);
        std::copy(hardware->model.begin(), hardware->model.end(), nvm.begin() + layout.model_name);
        std::copy(hardware->mechacon->model_id.begin(), hardware->mechacon->model_id.end(), nvm.begin() + layout.ilink_id + 1);
        std::copy(hardware->mechacon->con_model_id.begin(), hardware->mechacon->con_model_id.end(), nvm.begin() + layout.console_id);
        const auto entry = [&](unsigned index, const char* name, std::uint32_t size) {
            const auto at = 16u + index * 16u;
            std::copy_n(name, std::char_traits<char>::length(name), rom.begin() + at);
            word(rom, at + 12, size);
        };
        entry(0, "RESET", 16); entry(1, "ROMDIR", 96); entry(2, "EXTINFO", 0);
        entry(3, "ROMVER", 14); entry(4, "OSDSYS", 136);
        const std::string version(hardware->romver);
        std::copy(version.begin(), version.end(), rom.begin() + 112);
        const std::size_t elf = 128;
        const std::uint8_t ident[]{0x7f, 'E', 'L', 'F', 1, 1, 1};
        std::copy(std::begin(ident), std::end(ident), rom.begin() + elf);
        word(rom, elf + 16, 2, 2); word(rom, elf + 18, 8, 2); word(rom, elf + 20, 1);
        word(rom, elf + 24, 0x100000); word(rom, elf + 28, 52);
        word(rom, elf + 40, 52, 2); word(rom, elf + 42, 32, 2); word(rom, elf + 44, 1, 2);
        word(rom, elf + 52, 1); word(rom, elf + 56, 128); word(rom, elf + 60, 0x100000);
        word(rom, elf + 68, 8); word(rom, elf + 72, 16); word(rom, elf + 76, 5); word(rom, elf + 80, 16);
        word(rom, elf + 128, 0x03e00008);
        const auto firmware = ps2native::load_browser_firmware(rom, "SCPH-70004:GH-032-11", nvm);
        require(firmware.program.entry == 0x100000 && firmware.program.bytes.size() == 136,
                "Original OSDSYS bytes or entry changed");
        require(std::equal(firmware.program.bytes.begin(), firmware.program.bytes.end(), rom.begin() + elf),
                "Firmware preparation changed guest instructions");
        require(firmware.nvm.bytes == nvm && firmware.bios.rom0 == rom, "Original state changed");
        refused([&] { (void)ps2native::load_browser_firmware(rom, "SCPH-77008", nvm); });
        refused([&] { (void)ps2native::load_browser_firmware(rom, "unknown", nvm); });
        refused([&] { (void)ps2native::load_browser_firmware(rom, "SCPH-70004:GH-032-11", nvm,
            {std::string(64, '0')}); });
        refused([&] { (void)ps2native::load_browser_firmware(rom, "SCPH-70004:GH-032-11", nvm, {},
            {std::string(64, '0')}); });
        nvm[layout.model_name] ^= 1;
        refused([&] { (void)ps2native::load_browser_firmware(rom, "SCPH-70004:GH-032-11", nvm); });
        nvm[layout.model_name] ^= 1;
        rom[elf] = 0;
        refused([&] { (void)ps2native::load_browser_firmware(rom, "SCPH-70004:GH-032-11", nvm); });
        std::cout << "PASS original OSDSYS, ROM/profile/NVM association and mismatched inputs\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
