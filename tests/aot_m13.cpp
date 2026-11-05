// M1.3 end-to-end: synthetic ELF -> relocation evidence / bounded jump tables
// -> generated C++ (compiled into this executable) -> native execution ->
// registers, memory and control flow checked against R5900 semantics.
#include "m13_program.hpp"

#include "ps2native/aot_runtime.hpp"
#include "ps2native/control_flow.hpp"
#include "ps2native/core.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool ok, const std::string& reason) {
    if (!ok) throw std::runtime_error("M1.3 assertion failed: " + reason);
}
template <typename F>
void rejects(F&& action, const std::string& diagnostic) {
    try { action(); }
    catch (const std::exception& error) {
        if (std::string(error.what()).find(diagnostic) != std::string::npos) return;
        throw std::runtime_error("Wrong rejection reason for '" + diagnostic + "': " + error.what());
    }
    throw std::runtime_error("Expected explicit rejection: " + diagnostic);
}
std::uint32_t read32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return static_cast<std::uint32_t>(b.at(at)) | (static_cast<std::uint32_t>(b.at(at + 1)) << 8) |
           (static_cast<std::uint32_t>(b.at(at + 2)) << 16) | (static_cast<std::uint32_t>(b.at(at + 3)) << 24);
}
void write32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t value) {
    for (unsigned n = 0; n < 4; ++n) b.at(at + n) = static_cast<std::uint8_t>(value >> (8 * n));
}
// File offset of the SHT_REL record for (site, type) in the fixture.
std::size_t relocation_record(const std::vector<std::uint8_t>& b, std::uint32_t site, std::uint32_t type) {
    const auto shoff = read32(b, 32);
    const auto shnum = static_cast<std::uint32_t>(b.at(48)) | (static_cast<std::uint32_t>(b.at(49)) << 8);
    for (std::uint32_t section = 0; section < shnum; ++section) {
        const auto header = static_cast<std::size_t>(shoff) + 40u * section;
        if (read32(b, header + 4u) != 9u) continue;
        const auto offset = read32(b, header + 16u);
        const auto size = read32(b, header + 20u);
        for (std::size_t at = offset; at < offset + size; at += 8u) {
            if (read32(b, at) == site && (read32(b, at + 4u) & 0xffu) == type) return at;
        }
    }
    throw std::runtime_error("Fixture relocation record not found");
}
void set_type(std::vector<std::uint8_t>& b, std::size_t record, std::uint32_t type) {
    write32(b, record + 4u, (read32(b, record + 4u) & ~0xffu) | type);
}
void set_symbol(std::vector<std::uint8_t>& b, std::size_t record, std::uint32_t symbol) {
    write32(b, record + 4u, (symbol << 8) | (read32(b, record + 4u) & 0xffu));
}
ps2native::EEState start_state(const ps2native::ElfImage& elf) {
    ps2native::EEState state;
    state.pc = elf.entry;
    state.set_reg32_sign_extended(m13::ra, m13::kHostReturn);
    return state;
}
std::uint64_t slot(const ps2native::Memory& memory, unsigned index) {
    return memory.read64(m13::kResults + m13::kSlotBase + 8u * index);
}
const ps2native::AotRecognizedJumpTable& table_at(const ps2native::AotControlFlow& cfg, std::uint32_t site) {
    for (const auto& table : cfg.jump_tables)
        if (table.jump_site == site) return table;
    throw std::runtime_error("Expected a proven jump table at JR");
}
const ps2native::AotUnresolvedJump& unresolved_at(const ps2native::AotControlFlow& cfg, std::uint32_t site) {
    for (const auto& jump : cfg.unresolved_jumps)
        if (jump.site == site) return jump;
    throw std::runtime_error("Expected an unresolved JR");
}

void check_decoder() {
    using ps2native::decode_mips;
    using ps2native::MipsOpcode;
    rejects([] { (void)decode_mips(0u, 0x0000000cu); }, "unsupported R5900 instruction"); // SYSCALL
    rejects([] { (void)decode_mips(0u, 0x07f00000u); }, "unsupported R5900 instruction"); // BLTZAL $ra
    rejects([] { (void)decode_mips(0u, (1u << 21) | (9u << 16) | (9u << 11)); },        // SLL, rs != 0
            "unsupported R5900 instruction");
    rejects([] { (void)decode_mips(0u, (5u << 21) | (13u << 16) | (11u << 11) | 0x1au); }, // DIV, rd != 0
            "unsupported R5900 instruction");
    rejects([] { (void)decode_mips(0u, 0x70000040u); }, "unsupported R5900 instruction"); // MADD reserved sa != 0
    require(decode_mips(0u, 0x0000000fu).opcode == MipsOpcode::Sync, "SYNC decoded");
    require(decode_mips(0u, 0x0000040fu).opcode == MipsOpcode::Sync, "SYNC.P decoded");
    const auto beql = decode_mips(0u, 0x50000000u);
    require(beql.is_likely_branch() && beql.has_delay_slot() && !beql.links(), "BEQL classified");
    const auto bgezall = decode_mips(0u, 0x04130000u);
    require(bgezall.is_likely_branch() && bgezall.links() && bgezall.written_gpr() == 31u,
            "BGEZALL classified");
}

void check_relocation_evidence(const m13::Program& program, const std::vector<std::uint8_t>& bytes,
                               const ps2native::ElfImage& elf) {
    using ps2native::CodeReferenceKind;
    require(elf.function_entries.size() == 7u, "seven STT_FUNC symbols");
    const auto count = [&](CodeReferenceKind kind) {
        return std::count_if(elf.relocated_code_pointers.begin(), elf.relocated_code_pointers.end(),
                             [&](const auto& p) { return p.kind == kind; });
    };
    require(count(CodeReferenceKind::Call26) == 7, "seven R_MIPS_26 JAL references");
    require(count(CodeReferenceKind::AddressHi16Lo16) == 2, "two HI16 share one LO16 (GNU pairing)");
    require(elf.relocated_code_pointers.size() == 9u, "no other code evidence");
    for (const auto* high : {"callback_hi_1", "callback_hi_2"}) {
        require(std::any_of(elf.relocated_code_pointers.begin(), elf.relocated_code_pointers.end(),
                            [&](const auto& p) {
                                return p.kind == CodeReferenceKind::AddressHi16Lo16 &&
                                       p.source == program.at(high) && p.target == m13::kCallback &&
                                       p.low_source == program.at("callback_lo");
                            }),
                "callback address proven by HI16/LO16 pair");
    }
    require(elf.rejected_code_pointers.empty(), "no rejected code references");
    require(elf.address_pairs.size() == 7u, "results, 2x callback and four table pairs");
    require(std::any_of(elf.address_pairs.begin(), elf.address_pairs.end(), [&](const auto& p) {
                return p.value == m13::kResults && !p.executable_target;
            }), "data pair recorded as data");
    require(elf.unpaired_lo16_relocations == 1u, "second callback LO16 is unpaired");
    require(elf.unused_relocation_counts.size() == 1u && elf.unused_relocation_counts.at(m13::kRMipsGprel16) == 1u,
            "R_MIPS_GPREL16 counted, not applied");
    require(elf.ignored_rela_sections == 0u, "no SHT_RELA");

    auto broken = bytes;
    set_type(broken, relocation_record(bytes, program.at("callback_lo"), m13::kRMipsLo16), 0u);
    set_type(broken, relocation_record(bytes, program.at("callback_lo_unpaired"), m13::kRMipsLo16), 0u);
    rejects([&] { (void)ps2native::parse_ee_elf(broken); }, "has no matching R_MIPS_LO16");

    broken = bytes;
    write32(broken, relocation_record(bytes, program.at("callback_hi_1"), m13::kRMipsHi16),
            program.at("callback_call"));
    rejects([&] { (void)ps2native::parse_ee_elf(broken); }, "is not LUI");

    broken = bytes;
    write32(broken, relocation_record(bytes, program.at("jal_sum3"), m13::kRMips26), program.at("main"));
    rejects([&] { (void)ps2native::parse_ee_elf(broken); }, "is not J/JAL");

    broken = bytes;
    write32(broken, relocation_record(bytes, program.at("select_a_lw"), m13::kRMipsLo16), program.at("ori_site"));
    rejects([&] { (void)ps2native::parse_ee_elf(broken); }, "cannot carry R_MIPS_LO16");

    broken = bytes;
    set_symbol(broken, relocation_record(bytes, program.at("jal_sum3"), m13::kRMips26), 200u);
    rejects([&] { (void)ps2native::parse_ee_elf(broken); }, "invalid symbol index");

    broken = bytes;
    write32(broken, relocation_record(bytes, program.at("jal_sum3"), m13::kRMips26), m13::kDataBase);
    rejects([&] { (void)ps2native::parse_ee_elf(broken); }, "outside declared executable section");

    // A pair against STT_FUNC sum3 whose linked value is the callback: rejected, not seeded.
    broken = bytes;
    for (const auto* site : {"callback_hi_1", "callback_hi_2"})
        set_symbol(broken, relocation_record(bytes, program.at(site), m13::kRMipsHi16), m13::kSymSum3);
    set_symbol(broken, relocation_record(bytes, program.at("callback_lo"), m13::kRMipsLo16), m13::kSymSum3);
    const auto mismatched = ps2native::parse_ee_elf(broken);
    require(mismatched.rejected_code_pointers.size() == 2u &&
                mismatched.rejected_code_pointers[0].reason.find("differs from STT_FUNC") != std::string::npos,
            "STT_FUNC/value mismatch rejected with reason");
    const auto mismatched_cfg = ps2native::analyze_control_flow(mismatched, {});
    require(mismatched_cfg.instructions.contains(m13::kCallback) &&
            std::any_of(mismatched_cfg.constant_jumps.begin(),mismatched_cfg.constant_jumps.end(),[&](const auto& jump) {
                return jump.jump_site==program.at("callback_call") && jump.target==m13::kCallback;
            }), "rejected relocations stay rejected; independent constant JALR proof discovers callback");
    auto dynamic_bytes=m13::build_elf(program,false);
    const auto plain=ps2native::parse_ee_elf(dynamic_bytes);
    const auto site=program.at("callback_lo");const auto& segment=plain.code_segment_at(site);
    // A loaded target must not be accepted by the LUI proof merely because its
    // previous register contents happened to resemble a function address.
    write32(dynamic_bytes,segment.file_offset+site-segment.vaddr,(0x23u<<26)|(m13::t0<<21)|(m13::t0<<16));
    const auto dynamic_cfg=ps2native::analyze_control_flow(ps2native::parse_ee_elf(dynamic_bytes),{});
    require(!dynamic_cfg.instructions.contains(m13::kCallback),"unproved load-derived callback seeds nothing");
}
} // namespace

template <typename Char>
int test_main(int argc, Char** argv) {
    if (argc != 3) return 2;
    try {
        const auto mode = std::filesystem::path(argv[2]);
        const bool reloc = mode == std::filesystem::path("reloc");
        const bool stripped = mode == std::filesystem::path("stripped");
        const bool hinted = mode == std::filesystem::path("stripped-hint");
        require(reloc || stripped || hinted, "test mode recognized");
        const auto program = m13::build_program();
        const auto bytes = ps2native::read_file(std::filesystem::path(argv[1]));
        const auto elf = ps2native::parse_ee_elf(bytes);
        check_decoder();
        if (reloc) {
            check_relocation_evidence(program, bytes, elf);
        } else {
            require(elf.function_entries.empty() && elf.relocated_code_pointers.empty() &&
                        elf.address_pairs.empty(), "stripped ELF carries no evidence");
        }

        std::vector<std::uint32_t> hints;
        if (hinted) hints.push_back(m13::kCallback);
        const auto cfg = ps2native::analyze_control_flow(elf, hints);
        require(cfg.jump_tables.size() == 2u, "two proven jump tables");
        const auto& a = table_at(cfg, program.at("select_a_jr"));
        require(a.table_address == m13::kTableA && a.entries == 4u && a.bound_site == program.at("select_a"),
                "GCC-shape table: address, SLTIU size and bound site");
        const auto& b = table_at(cfg, program.at("select_b_jr"));
        require(b.table_address == m13::kTableB && b.entries == 3u, "ee-gcc-shape table");
        for (const auto* table : {&a, &b}) {
            require((table->evidence.find("R_MIPS_HI16/LO16") != std::string::npos) == reloc,
                    "relocation evidence cross-checks the table address only when present");
        }
        require(cfg.unresolved_jumps.size() == 2u, "two unprovable JRs reported");
        require(unresolved_at(cfg, program.at("unbounded_jr")).reason.find("no SLTIU") != std::string::npos,
                "missing bound reported");
        require(unresolved_at(cfg, program.at("entered_jr")).reason.find("entered at") != std::string::npos,
                "branch into guarded window reported");
        for (const auto* label : {"a_case0", "a_case1", "a_case2", "a_case3", "b_case0", "b_case1", "b_case2"})
            require(cfg.indirect_entries.contains(program.at(label)), "table targets are verified entries");
        for (const auto* label : {"u_case0", "e_case0"})
            require(!cfg.instructions.contains(program.at(label)), "unproven tables seed nothing");
        require(cfg.function_entries.contains(program.at("alu")), "JAL target is a function root");
        require(cfg.instructions.contains(m13::kCallback), "callback proved from linked code, with or without metadata");
        const auto cpp = ps2native::generate_aot_cpp(elf, {hints, {}});
        require(cpp.find("proven jump tables: 2") != std::string::npos, "generated provenance");

        ps2native::Memory memory;
        memory.load(elf);
        auto state = start_state(elf);

        const auto count = ps2native::run_recompiled(state, memory, m13::kHostReturn, 100000u);
        require(count == state.retired_instructions, "retired instruction accounting");
        require(state.pc == m13::kHostReturn && !state.delay_pending && !state.indirect_entry_pending,
                "clean host return");
        const std::uint32_t words[] = {12u, 21u, 102u, 0xffffffffu, 201u};
        for (std::uint32_t n = 0; n < 5u; ++n)
            require(memory.read32(m13::kResults + 4u * n) == words[n], "main result " + std::to_string(n));
        for (unsigned n = 0; n < m13::kAluExpected.size(); ++n)
            require(slot(memory, n) == m13::kAluExpected[n], "R5900 ALU/memory slot " + std::to_string(n));
        const char* links[] = {"bgezal_site", "bltzal_site", "bltzall_site", "bgezall_site"};
        for (unsigned n = 0; n < 4u; ++n)
            require(slot(memory, m13::kLinkSlot + n) == program.at(links[n]) + 8u,
                    std::string("link register written by ") + links[n]);
        require(slot(memory, m13::kBranchMaskSlot) == m13::kBranchMask,
                "64-bit branch conditions and branch-likely nullification");
        require(state.gpr[m13::t5].high == 0xFFFFFFFF80000001ull, "64-bit ops preserve upper GPR half");
        require(state.reg32(m13::s1) == m13::kResults, "HI16/LO16 data address formed");

        memory.load(elf);
        auto short_run = start_state(elf);
        rejects([&] { (void)ps2native::run_recompiled(short_run, memory, m13::kHostReturn, count - 1u); },
                "Guest instruction budget exhausted");
        require(short_run.retired_instructions == count - 1u, "exact budget");

        // JR to compiled code that is not a verified entry fails closed.
        memory.load(elf);
        memory.write32(m13::kTableA + 8u, program.at("a_case1") + 4u); // a delay-slot leader
        auto forged = start_state(elf);
        rejects([&] { (void)ps2native::run_recompiled(forged, memory, m13::kHostReturn, 100000u); },
                "is not a verified entry");
        require(forged.pc == program.at("a_case1") + 4u, "forged indirect target reported");
        memory.load(elf);
        memory.write32(m13::kTableA + 8u, program.at("u_case0"));
        auto missing = start_state(elf);
        rejects([&] { (void)ps2native::run_recompiled(missing, memory, m13::kHostReturn, 100000u); },
                "AOT target not compiled");

        std::cout << "M1.3 PASS (" << (reloc ? "relocations" : hinted ? "stripped + --entry" : "stripped + constant JALR proof")
                  << "): 2 proven jump tables, " << count << " native guest instructions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aot_m13: " << error.what() << '\n';
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return test_main(argc, argv); }
#else
int main(int argc, char** argv) { return test_main(argc, argv); }
#endif
