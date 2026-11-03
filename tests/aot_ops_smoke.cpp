#include "ps2native/aot_runtime.hpp"
#include "ps2native/core.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>

template <typename Character>
int aot_ops_main(int argc, Character** argv) {
    if (argc != 2) return 2;
    try {
        const auto elf = ps2native::parse_ee_elf(ps2native::read_file(std::filesystem::path(argv[1])));
        ps2native::Memory memory;
        memory.load(elf);
        ps2native::EEState state;
        state.pc = elf.entry;
        constexpr std::uint32_t host_return = 0xfffffff0u;
        state.set_reg32_sign_extended(31, host_return);
        const auto executed = ps2native::run_recompiled(state, memory, host_return, 9);
        constexpr std::uint32_t expected = 0x12345679u;
        if (state.reg32(2) != expected || memory.read32(0x44) != expected ||
            state.reg32(0) != 0 || state.pc != host_return || executed != 9)
            throw std::runtime_error("AOT ORI/LUI/J, jump delay slot or zero register mismatch");
        std::cout << "AOT OPS PASS: LUI/ORI/J/delay slot and $zero, 9 instructions, v0=0x12345679\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "aot_ops_smoke: " << e.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return aot_ops_main(argc, argv); }
#else
int main(int argc, char** argv) { return aot_ops_main(argc, argv); }
#endif
