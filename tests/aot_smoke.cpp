#include "ps2native/aot_runtime.hpp"
#include "ps2native/core.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>

template <typename Character>
int aot_smoke_main(int argc, Character** argv) {
    if (argc != 2) return 2;
    try {
        const auto data = ps2native::read_file(std::filesystem::path(argv[1]));
        const auto elf = ps2native::parse_ee_elf(data);
        ps2native::Memory memory;
        memory.load(elf);
        ps2native::EEState state;
        state.pc = elf.entry;
        constexpr std::uint32_t host_return = 0xfffffff0u;
        state.set_reg32_sign_extended(31, host_return);

        // Exact instruction budget must accept termination on the final instruction.
        const auto executed = ps2native::run_recompiled(state, memory, host_return, 37);
        if (state.reg32(2) != 37u || memory.read32(0x40u) != 37u ||
            state.pc != host_return || executed != 37u ||
            state.retired_instructions != 37u)
            throw std::runtime_error("AOT result mismatch (branches, delay slots, JAL/JR, LW/SW)");
        // Budget exhaustion is explicit, never a silent partial success.
        ps2native::EEState short_state;
        short_state.pc = elf.entry;
        short_state.set_reg32_sign_extended(31, host_return);
        bool budget_rejected = false;
        try { (void)ps2native::run_recompiled(short_state, memory, host_return, 5); }
        catch (const std::runtime_error&) { budget_rejected = true; }
        if (!budget_rejected) throw std::runtime_error("Instruction budget not enforced");
        if (memory.read32(elf.entry) != elf.instruction_at(elf.entry))
            throw std::runtime_error("Guest ELF modified unexpectedly");
        std::cout << "AOT TEST PASS: ELF -> generated C++ -> host native code -> 37 executed MIPS instructions;"
                  << " $v0=37; RAM[0x40]=37; returned through JR delay slot.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "aot_smoke: " << e.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return aot_smoke_main(argc, argv); }
#else
int main(int argc, char** argv) { return aot_smoke_main(argc, argv); }
#endif
