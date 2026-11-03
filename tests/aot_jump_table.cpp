#include "ps2native/core.hpp"
#include "ps2native/aot_runtime.hpp"
#include "ps2native/control_flow.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok, const std::string& reason) {
    if (!ok) throw std::runtime_error("M1.2 assertion failed: " + reason);
}
template <typename F>
void rejects(F&& action, const std::string& diagnostic) {
    try { action(); }
    catch (const std::exception& error) {
        if (std::string(error.what()).find(diagnostic) != std::string::npos) return;
        throw std::runtime_error("Wrong rejection reason: " + std::string(error.what()));
    }
    throw std::runtime_error("Expected explicit rejection: " + diagnostic);
}
} // namespace

template <typename Char>
int test_main(int argc, Char** argv) {
    if (argc != 3) return 2;
    try {
        constexpr std::uint32_t base = 0x00100000u;
        constexpr std::uint32_t table = 0x00101000u;
        constexpr std::uint32_t host_return = 0xfffffff0u;
        const auto mode = std::filesystem::path(argv[2]);
        const auto bytes = ps2native::read_file(std::filesystem::path(argv[1]));
        const auto elf = ps2native::parse_ee_elf(bytes);
        require(elf.function_entries.empty(), "no STT_FUNC for jump-table functions");
        require(elf.segments.size() == 2, "two distinct PT_LOADs");
        require(elf.data_word_at(table) == base + 15u * 4u, "first data pointer");
        require(elf.data_word_at(table + 4u) == base + 18u * 4u, "middle-of-section target");
        rejects([&] { (void)elf.data_word_at(base); }, "non-executable");
        rejects([&] { (void)elf.data_word_at(table + 1u); }, "Unaligned");

        ps2native::AotOptions options;
        if (mode == std::filesystem::path("reloc")) {
            require(elf.relocated_code_pointers.size() == 2, "two relocations accepted");
            require(elf.rejected_code_pointers.size() == 1, "data target relocation rejected");
            require(elf.rejected_code_pointers[0].source == table + 8u,
                    "rejection source address");
            require(elf.rejected_code_pointers[0].reason.find("executable section") != std::string::npos,
                    "rejection includes evidence/section reason");
            require(elf.relocated_code_pointers[1].target == base + 18u * 4u,
                    "jump table points to mid-section entry");
            const auto cfg = ps2native::analyze_control_flow(elf, {});
            require(cfg.resolved_indirect_targets.size() == 2, "two discovered executable roots");
            require(cfg.rejected_indirect_targets.size() == 1, "rejected candidates retained");
            require(cfg.function_entries.contains(base + 18u * 4u), "section-local target seeded");
            auto broken = bytes;
            broken[0x405] = 0xffu; // r_info: symbol index now 255, out of bounds
            rejects([&] { (void)ps2native::parse_ee_elf(broken); },
                    "invalid symbol index");
            broken = bytes;
            broken[0x400] = 0; broken[0x401] = 0; broken[0x402] = 0x10;
            broken[0x403] = 0; // relocation site now .text, not .rodata
            rejects([&] { (void)ps2native::parse_ee_elf(broken); },
                    "outside declared data section");
            broken = bytes;
            broken[0x500u + 5u * 40u + 36u] = 4u; // invalid REL entsize
            rejects([&] { (void)ps2native::parse_ee_elf(broken); },
                    "SHT_REL bounds");
        } else if (mode == std::filesystem::path("explicit")) {
            require(elf.relocated_code_pointers.empty(), "stripped ELF has no relocation evidence");
            options.jump_tables.push_back({table, 2u});
            const auto cfg = ps2native::analyze_control_flow(elf, {}, options.jump_tables);
            require(cfg.resolved_indirect_targets.size() == 2, "two explicitly verified hints");
            require(cfg.function_entries.contains(base + 18u * 4u), "middle of function is compiled");
            auto invalid = options;
            invalid.jump_tables[0].entries = 3;
            rejects([&] { (void)ps2native::generate_aot_cpp(elf, invalid); },
                    "Rejected explicit jump table entry 2");
            invalid.jump_tables = {{table + 1u, 2u}};
            rejects([&] { (void)ps2native::generate_aot_cpp(elf, invalid); },
                    "Invalid jump table alignment");
            invalid.jump_tables = {{table, 4097u}};
            rejects([&] { (void)ps2native::generate_aot_cpp(elf, invalid); },
                    "Invalid jump table alignment/count");
            invalid.jump_tables = {{0xfffffffcu, 2u}};
            rejects([&] { (void)ps2native::generate_aot_cpp(elf, invalid); },
                    "Rejected explicit jump table entry 0");
        } else {
            require(mode == std::filesystem::path("unresolved"), "test mode recognized");
            require(elf.relocated_code_pointers.empty(), "unresolved ELF has no table evidence");
            require(elf.function_entries.empty(), "unresolved ELF has no functions");
        }

        ps2native::Memory memory;
        memory.load(elf);
        ps2native::EEState state;
        state.pc = elf.entry;
        state.set_reg32_sign_extended(31, host_return);
        if (mode == std::filesystem::path("unresolved")) {
            rejects([&] { (void)ps2native::run_recompiled(state, memory, host_return, 128); },
                    "AOT target not compiled");
            require(state.pc == base + 15u * 4u && state.retired_instructions == 6u,
                    "unknown indirect target rejected after JALR delay slot");
            require(memory.read32(0x40) == 0u, "unresolved call cannot fake a result");
            std::cout << "M1.2 unresolved jump table failed closed\n";
            return 0;
        }

        const auto count = ps2native::run_recompiled(state, memory, host_return, 20u);
        require(count == 20u && state.retired_instructions == 20u, "exact instruction retirement");
        require(state.pc == host_return && !state.delay_pending, "host return after delay slot");
        require(state.reg32(2) == 15u, "second function result");
        require(memory.read32(0x40) == 12u && memory.read32(0x44) == 15u,
                "both table entries dispatched to native C++ function blocks");
        require(state.reg32(10) == 6u, "JALR delay slots exactly once");

        memory.load(elf);
        ps2native::EEState changed;
        changed.pc = elf.entry;
        changed.set_reg32_sign_extended(31, host_return);
        memory.write32(base + 15u * 4u, 0u); // stale AOT must never run.
        rejects([&] { (void)ps2native::run_recompiled(changed, memory, host_return, 100); },
                "AOT executable code modified at");
        require(changed.pc == base + 15u * 4u, "modified PC reported");

        memory.load(elf);
        memory.write32(table + 4u, base + 14u * 4u); // intentionally uncompiled opcode
        ps2native::EEState bad_pointer;
        bad_pointer.pc = elf.entry;
        bad_pointer.set_reg32_sign_extended(31, host_return);
        rejects([&] { (void)ps2native::run_recompiled(bad_pointer, memory, host_return, 100); },
                "AOT target not compiled");
        require(bad_pointer.pc == base + 14u * 4u, "unknown pointer destination identified");
        require(memory.read32(0x40) == 12u, "first result preserved");
        require(memory.read32(0x44) == 0u, "invalid destination did not fabricate a result");

        memory.load(elf);
        ps2native::EEState short_run;
        short_run.pc = elf.entry;
        short_run.set_reg32_sign_extended(31, host_return);
        rejects([&] { (void)ps2native::run_recompiled(short_run, memory, host_return, 19u); },
                "Guest instruction budget exhausted");
        require(short_run.retired_instructions == 19u, "accurate instruction budget");

        const auto cpp = ps2native::generate_aot_cpp(elf, options);
        require(cpp.find("evidence-backed indirect targets: 2") != std::string::npos,
                "AOT generation reports validated provenance");
        std::cout << "M1.2 PASS: evidence-backed jump table, two JALR calls, 20 instructions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aot_jump_table: " << error.what() << '\n';
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return test_main(argc, argv); }
#else
int main(int argc, char** argv) { return test_main(argc, argv); }
#endif
