#include "ps2native/aot_runtime.hpp"
#include "ps2native/core.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const std::string& what) {
    if (!ok) throw std::runtime_error("Assertion failed: " + what);
}

template <typename Function>
void fails_with(Function&& function, const std::string& message) {
    try { function(); }
    catch (const std::runtime_error& e) {
        if (std::string(e.what()).find(message) != std::string::npos) return;
        throw std::runtime_error("Unexpected error: " + std::string(e.what()));
    }
    throw std::runtime_error("Expected explicit AOT failure: " + message);
}
} // namespace

template <typename Character>
int indirect_main(int argc, Character** argv) {
    if (argc != 3) return 2;
    try {
        const auto file = std::filesystem::path(argv[1]);
        const auto mode = std::filesystem::path(argv[2]);
        const auto input = ps2native::read_file(file);
        const auto elf = ps2native::parse_ee_elf(input);
        if (!elf.function_entries.empty()) {
            check(elf.function_entries.size() == 4, "four ELF function roots including entry");
            auto broken = input;
            // SHT_SYMTAB sh_link to nonexistent section (offset 0x300+80+24).
            broken[0x300u + 80u + 24u] = 0xff;
            fails_with([&] { (void)ps2native::parse_ee_elf(broken); },
                       "Invalid ELF symbol table bounds");
            broken = input;
            // Invalid symbol entry size must fail, not be interpreted heuristically.
            broken[0x300u + 80u + 36u] = 4;
            fails_with([&] { (void)ps2native::parse_ee_elf(broken); },
                       "Invalid ELF symbol table bounds");
        }
        constexpr std::uint32_t host_return = 0xfffffff0u;
        constexpr std::uint32_t base = 0x00100000u;
        ps2native::Memory memory;
        memory.load(elf);
        ps2native::EEState state;
        state.pc = elf.entry;
        state.set_reg32_sign_extended(31, host_return);

        if (mode == std::filesystem::path("unresolved")) {
            check(elf.function_entries.empty(), "stripped ELF has no symbols");
            fails_with([&] { (void)ps2native::run_recompiled(state, memory, host_return, 128); },
                       "AOT target not compiled");
            check(state.pc == base + 23u * 4u && state.retired_instructions == 13,
                  "constant JALR A is native; loaded unknown B rejected after its delay slot");
            check(memory.read32(0x40) == 12 && memory.read32(0x44) == 0, "proved function result, no made-up unknown result");
            std::cout << "Constant JALR discovered; unknown loaded function target rejected with PC diagnostic\n";
            return 0;
        }

        check(mode == std::filesystem::path("resolved"), "expected test mode");
        const auto executed = ps2native::run_recompiled(state, memory, host_return, 28);
        check(executed == 28 && state.retired_instructions == 28, "28 guest instructions executed");
        check(state.pc == host_return && !state.delay_pending, "return to host after JR slot");
        check(state.reg32(2) == 20, "three native function calls");
        check(memory.read32(0x40) == 12 && memory.read32(0x44) == 18 && memory.read32(0x48) == 20,
              "indirect targets and link register semantics");
        check(state.reg32(9) == 16, "JALR delay slots execute exactly once");
        check(state.reg32(12) == base + 15u * 4u, "JALR rs==rd reads target before link write");
        check(state.reg32(0) == 0, "$zero unchanged");

        // A guest-modified code word must never be executed by stale AOT.
        memory.load(elf);
        memory.write32(base + 20u * 4u, 0u);
        ps2native::EEState changed;
        changed.pc = elf.entry;
        changed.set_reg32_sign_extended(31, host_return);
        fails_with([&] { (void)ps2native::run_recompiled(changed, memory, host_return, 128); },
                   "AOT executable code modified at");
        check(changed.pc == base + 20u * 4u, "modified code address identified");

        // Pointer stored in ELF data can be altered by guest RAM. An unseen
        // target must not be fabricated or treated as a successful call.
        memory.load(elf);
        memory.write32(base + 0x80u, base + 31u * 4u);
        ps2native::EEState missing;
        missing.pc = elf.entry;
        missing.set_reg32_sign_extended(31, host_return);
        fails_with([&] { (void)ps2native::run_recompiled(missing, memory, host_return, 128); },
                   "AOT target not compiled");
        check(missing.pc == base + 31u * 4u, "unknown data function pointer preserved");
        check(memory.read32(0x40) == 12 && memory.read32(0x44) == 0,
              "no successful unresolved call or corrupted result");

        // Exact budget is sufficient; shorter budget fails instead of succeeding partially.
        memory.load(elf);
        ps2native::EEState limited;
        limited.pc = elf.entry;
        limited.set_reg32_sign_extended(31, host_return);
        fails_with([&] { (void)ps2native::run_recompiled(limited, memory, host_return, 27); },
                   "Guest instruction budget exhausted");
        check(limited.retired_instructions == 27, "budget counted guest instructions");

        std::cout << "M1.1 PASS: ELF function symbols / explicit roots, JALR/JR, delay slots, "
                     "self-modifying code guard, 28 instructions\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "aot_indirect: " << e.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return indirect_main(argc, argv); }
#else
int main(int argc, char** argv) { return indirect_main(argc, argv); }
#endif
