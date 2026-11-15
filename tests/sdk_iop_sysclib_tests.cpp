#include "iop_compat_test_support.h"
#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_memory.h"
#include "emulator/imports/iop_sysclib.h"

#include <iostream>
#include <string>

using namespace iop_test;
using namespace ps2x::iop::detail;

namespace {
void put(IopMemory& memory, uint32_t address, const std::string& text) {
    for (size_t i = 0; i <= text.size(); ++i) memory.write8(address + static_cast<uint32_t>(i), i < text.size() ? static_cast<uint8_t>(text[i]) : 0u);
}

std::string get(IopMemory& memory, uint32_t address) {
    std::string text;
    for (uint8_t c; (c = memory.read8(address + static_cast<uint32_t>(text.size()))) != 0u;) text.push_back(static_cast<char>(c));
    return text;
}

uint32_t strtok(IopSysclib& sysclib, uint32_t string, uint32_t delimiters) {
    IopCpuState cpu;
    cpu.gpr[4] = string;
    cpu.gpr[5] = delimiters;
    require(sysclib.dispatchImport(35, cpu), "strtok import not handled");
    return cpu.gpr[2];
}
} // namespace

int main() try {
    IopMemory memory;
    IopSysclib sysclib(memory);
    constexpr uint32_t text = 0x10000, delimiters = 0x10100;
    // The pattern of an IRX argument list such as "wad/,,sound/" split with one saved position.
    put(memory, text, ",wad/,,sound/;x");
    put(memory, delimiters, ",;");
    uint32_t token = strtok(sysclib, text, delimiters);
    require(token == text + 1 && get(memory, token) == "wad/", "first token differs");
    token = strtok(sysclib, 0, delimiters);
    require(token == text + 7 && get(memory, token) == "sound/", "continuation skips repeated delimiters");
    token = strtok(sysclib, 0, delimiters);
    require(token == text + 14 && get(memory, token) == "x", "last token without a trailing delimiter");
    require(strtok(sysclib, 0, delimiters) == 0, "exhausted string returns null");
    require(strtok(sysclib, 0, delimiters) == 0, "exhausted string stays null");
    put(memory, text, ",,;");
    require(strtok(sysclib, text, delimiters) == 0, "delimiter-only string returns null");
    std::cout << "IOP sysclib strtok regression passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
