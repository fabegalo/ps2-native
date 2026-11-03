#include "ps2native/core.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error("Assertion failed: " + message);
}
void throws(const std::function<void()>& task, const std::string& message) {
    try { task(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Expected error: " + message);
}
}

template <typename Character>
int core_tests_main(int argc, Character** argv) {
    if (argc != 2) return 2;
    try {
        const auto bytes = ps2native::read_file(std::filesystem::path(argv[1]));
        const auto elf = ps2native::parse_ee_elf(bytes);
        check(elf.entry == 0x00100000u, "ELF32 entry address");
        check(elf.segments.size() == 1, "one loadable segment");
        check(elf.segments[0].file_size == 88, "segment file size");
        check(elf.instruction_at(elf.entry) == 0x03e08021u, "first MIPS word");
        throws([&] { (void)elf.instruction_at(0x70000000u); }, "non-mapped instruction");

        ps2native::Memory ram;
        ram.load(elf);
        check(ram.read32(0x00100000u) == 0x03e08021u, "PT_LOAD mapped into RAM");
        check(ram.read32(0x80100000u) == 0x03e08021u, "KSEG0 alias");
        check(ram.read32(0xA0100000u) == 0x03e08021u, "KSEG1 alias");
        check(ram.read32(elf.entry + 0xfc) == 0, "BSS zero-filled");
        ram.write32(0x40, 0x11223344u);
        check(ram.read8(0x40) == 0x44, "little endian read8");
        check(ram.read32(0x80000040u) == 0x11223344u, "aliased RAM read32");
        throws([&] { (void)ram.read32(0x70000000u); }, "scratchpad is not yet mapped");
        throws([&] { ram.write32(1, 0); }, "unaligned SW");
        throws([&] { (void)ram.read32(0x01fffffeu); }, "last-word overflow");

        ps2native::EEState state;
        state.set_reg32_sign_extended(8, 0xffffffffu);
        check(state.reg32(8) == 0xffffffffu &&
              state.gpr[8].low == 0xffffffffffffffffull, "M0 word sign extension");
        state.set_reg32(0, 123);
        state.gpr[0].low = 0xdeadbeefu; // Even externally corrupted storage cannot change $zero reads.
        check(state.reg32(0) == 0, "R5900 constant zero register");

        auto malformed = bytes;
        malformed[5] = 2;
        throws([&] { (void)ps2native::parse_ee_elf(malformed); }, "big endian rejected");
        malformed = bytes;
        malformed[18] = 3;
        throws([&] { (void)ps2native::parse_ee_elf(malformed); }, "non-MIPS rejected");
        malformed = bytes;
        malformed[68] = 0xff; malformed[69] = 0xff;
        throws([&] { (void)ps2native::parse_ee_elf(malformed); }, "truncated PT_LOAD rejected");
        malformed = bytes;
        // SYSCALL (0x0000000C): EE syscalls are not implemented by the AOT yet.
        malformed[0x100] = 0x0c; malformed[0x101] = 0;
        malformed[0x102] = 0; malformed[0x103] = 0;
        const auto unsupported = ps2native::parse_ee_elf(malformed);
        throws([&] { (void)ps2native::generate_aot_cpp(unsupported); },
               "unsupported reachable opcode fails instead of being silently stubbed");
        // Malformed known control-flow must fail analysis before generating C++.
        malformed = bytes;
        // The loop's branch delay slot at index 6 is replaced with a J instruction.
        constexpr std::size_t delay = 0x100u + 6u * 4u;
        malformed[delay] = 0; malformed[delay + 1] = 0; malformed[delay + 2] = 0; malformed[delay + 3] = 0x08;
        throws([&] { (void)ps2native::generate_aot_cpp(ps2native::parse_ee_elf(malformed)); },
               "branch in delay slot rejected");
        ps2native::AotOptions invalid_root;
        invalid_root.additional_function_entries.push_back(0x70000000u);
        throws([&] { (void)ps2native::generate_aot_cpp(elf, invalid_root); },
               "explicit entry outside executable segment rejected");

        // Verify generated C++ is content-addressed in practice: identical
        // content is never rewritten, so downstream CMake targets don't rebuild.
        auto probe = std::filesystem::path(argv[1]);
        probe += ".aot-cache-check.txt";
        ps2native::remove_file(probe);
        check(ps2native::write_text_if_changed(probe, "AOT-1"), "first cache write");
        check(!ps2native::write_text_if_changed(probe, "AOT-1"), "identical AOT reused");
        check(ps2native::write_text_if_changed(probe, "AOT-2"), "changed AOT rewritten");
        ps2native::remove_file(probe);
        std::cout << "Core tests OK: ELF validation, segment/BSS, RAM aliases, EE GPR, fail-closed AOT\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "core_tests: " << e.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return core_tests_main(argc, argv); }
#else
int main(int argc, char** argv) { return core_tests_main(argc, argv); }
#endif
