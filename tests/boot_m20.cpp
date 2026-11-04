// M2.0 end-to-end: synthetic ROM0 (tests/bios_program.hpp) -> BIOS loader ->
// EE bus/TLB/COP0 reset state -> execution from the reset vector, once with
// the interpreter only and once hybrid with the AOT translation generated from
// the same ROM0 by `ps2n-aot --bios` (compiled into this executable). Both runs
// must retire the same instructions, produce the same trace (engine aside),
// reach the same final state and stop at the same unmodeled device access.
#include "bios_program.hpp"
#include "boot_checks.hpp"

#include "ps2native/aot_runtime.hpp"
#include "ps2native/core.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ps2native;
using namespace boot_checks;

void put32(std::vector<std::uint8_t>& rom, std::size_t at, std::uint32_t value) {
    for (unsigned n = 0; n < 4; ++n) rom.at(at + n) = static_cast<std::uint8_t>(value >> (8u * n));
}

void check_sha256() {
    const std::string abc = "abc";
    const std::string two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const std::vector<std::uint8_t> million(1000000, static_cast<std::uint8_t>('a'));
    auto bytes = [](const std::string& s) { return std::vector<std::uint8_t>(s.begin(), s.end()); };
    require(sha256_hex({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 empty");
    require(sha256_hex(bytes(abc)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 abc");
    require(sha256_hex(bytes(two_blocks)) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
            "SHA-256 two blocks");
    require(sha256_hex(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
            "SHA-256 one million a");
}

void check_loader(const std::vector<std::uint8_t>& rom, const m20::Firmware& fw) {
    const auto bios = load_bios_rom0(rom);
    require(bios.version.romver == m20::kRomver && bios.version.major == 1 && bios.version.minor == 0,
            "ROMVER version");
    require(bios.version.region == "Test" && bios.version.kind == "Devel" && bios.version.date == "2026-11-03",
            "ROMVER region/kind/date");
    require(bios.romdir_offset == fw.romdir_offset && bios.modules.size() == 6u, "ROMDIR location and entries");
    require(bios.find("SYNPAYLD") && bios.find("SYNPAYLD")->offset + m20::kResetVector == fw.payload_rom,
            "file offsets follow ROMDIR order");
    require(bios.find("RESET")->comment == "ps2-native synthetic firmware (not Sony)" &&
            bios.find("RESET")->date == 0x20261103u, "RESET EXTINFO date and comment");
    require(bios.find("ROMVER")->version == 0x0100u, "ROMVER EXTINFO version");
    require(bios.sha256 == sha256_hex(rom), "image hash");
    (void)load_bios_rom0(rom, {bios.sha256});
    rejects([&] { (void)load_bios_rom0(rom, {std::string(64, '0')}); }, "does not match the expected");

    auto mutated = [&](auto&& change) { auto copy = rom; change(copy); return copy; };
    const auto entry = [&](std::size_t index) { return static_cast<std::size_t>(fw.romdir_offset) + 16u * index; };
    const auto truncated = std::vector<std::uint8_t>(rom.begin(), rom.end() - 16);
    rejects([&] { (void)load_bios_rom0(truncated); }, "exactly 4194304 bytes");
    auto doubled = rom;
    doubled.insert(doubled.end(), rom.begin(), rom.end());
    rejects([&] { (void)load_bios_rom0(doubled); }, "concatenated ROM0+ROM1");
    rejects([&] { (void)load_bios_rom0(mutated([&](auto& b) { b[entry(0)] = 'X'; })); }, "no ROMDIR");
    rejects([&] { (void)load_bios_rom0(mutated([&](auto& b) { put32(b, entry(1) + 12, 0x60u); })); },
            "does not cover");
    rejects([&] { (void)load_bios_rom0(mutated([&](auto& b) { b[entry(0) + 10] += 4; })); }, "differs from");
    rejects([&] { (void)load_bios_rom0(mutated([&](auto& b) {
        b[static_cast<std::size_t>(bios.find("EXTINFO")->offset) + 3] = 0x09; })); }, "unknown field type");
    const auto romver = static_cast<std::size_t>(bios.find("ROMVER")->offset);
    rejects([&] { (void)load_bios_rom0(mutated([&](auto& b) { b[romver + 4] = 'Q'; })); }, "unknown region code");
    rejects([&] { (void)load_bios_rom0(mutated([&](auto& b) { b[romver + 10] = '1'; b[romver + 11] = '3'; })); },
            "invalid build date");
    rejects([&] { (void)load_bios_rom0(mutated([&](auto& b) { b[romver + 5] = 'Q'; })); }, "unknown console kind");
    rejects([&] { (void)load_bios_rom0(mutated([&](auto& b) { put32(b, entry(5) + 12, 0x00500000u); })); },
            "lies outside");
    rejects([&] { (void)load_bios_rom0(mutated([&](auto& b) { b[entry(4)] = 0x01; })); }, "non-printable");
    rejects([&] { (void)load_bios_rom0(mutated([&](auto& b) { put32(b, entry(0) + 12, 0u); })); }, "RESET is empty");
    rejects([&] { (void)load_bios_rom0(mutated([&](auto& b) {
        const std::string name = "ROMVER";
        for (std::size_t c = 0; c < 10; ++c) b[entry(5) + c] = c < name.size() ? static_cast<std::uint8_t>(name[c]) : 0u;
    })); }, "exactly one 'ROMVER'");
}

void check_bus(const std::vector<std::uint8_t>& rom) {
    Memory memory;
    memory.reset_for_boot(rom);
    const auto word0 = static_cast<std::uint32_t>(rom[0]) | (static_cast<std::uint32_t>(rom[1]) << 8) |
                       (static_cast<std::uint32_t>(rom[2]) << 16) | (static_cast<std::uint32_t>(rom[3]) << 24);
    require(memory.read32(0xBFC00000u) == word0 && memory.read32(0x9FC00000u) == word0, "ROM0 through KSEG1/KSEG0");
    memory.write32(0x80001000u, 0xCAFEF00Du);
    require(memory.read32(0xA0001000u) == 0xCAFEF00Du, "RAM through KSEG0 and KSEG1");
    bus_fault([&] { memory.write32(0xBFC00000u, 1u); }, BusFault::RomWrite, "write to ROM");
    bus_fault([&] { (void)memory.read32(0x80000002u); }, BusFault::Unaligned, "Unaligned guest LW");
    require(memory.read32(0x00001000u)==0xCAFEF00Du,"ERL kuseg bypasses the TLB");
    bus_fault([&] { (void)memory.read32(0xB000F000u); }, BusFault::Device, "INTC");
    bus_fault([&] { (void)memory.read32(0xB0003000u); }, BusFault::Device, "GIF");
    bus_fault([&] { (void)memory.read32(0xB000F410u); }, BusFault::Device, "MCH (RDRAM controller)");
    bus_fault([&] { (void)memory.read64(0xB2000000u); }, BusFault::Device, "GS privileged registers");
    bus_fault([&] { (void)memory.read32(0xBC000000u); }, BusFault::Device, "IOP memory");
    bus_fault([&] { (void)memory.read32(0xBF801000u); }, BusFault::Device, "IOP registers");
    bus_fault([&] { (void)memory.read32(0xA4000000u); }, BusFault::Unmapped, "unmapped physical address");

    memory.set_translation_context(0, false);
    bus_fault([&] { (void)memory.read32(0x00010000u); }, BusFault::TlbRefill,
              "48 TLB entries undefined since reset are assumed not to match");
    require(!memory.tlb_probe(0x00010000u), "TLBP never matches entries undefined since reset");
    rejects([&] { (void)memory.tlb_entry(5); }, "undefined since reset");
    // Only entry 0 written, as the real BIOS does (PageMask 0, S bit): it maps the
    // whole 16 KiB scratchpad and the 47 undefined entries never match.
    memory.tlb_write(0, {0u, 0x70000000u, 0x80000007u, 7u});
    memory.write32(0x70003FF0u, 0xA5A5A5A5u);
    require(memory.read32(0x70003FF0u) == 0xA5A5A5A5u, "S entry with PageMask 0 spans 16 KiB");
    require(memory.tlb_probe(0x70000000u) == 0u, "TLBP finds the only defined entry");
    for (unsigned n = 0; n < Memory::kTlbEntries; ++n)
        memory.tlb_write(n, {0u, 0xE0000000u + 0x2000u * n, 0u, 0u});
    // Entry 1: 0x00010000 -> physical 0x2000 (writable), 0x00011000 -> 0x3000 (read-only).
    memory.tlb_write(1, {0u, 0x00010000u, (0x2u << 6) | 0x1Fu, (0x3u << 6) | 0x1Bu});
    memory.tlb_write(2, {0u, 0x00020000u | 5u, (0x4u << 6) | 0x1Eu, (0x5u << 6) | 0x1Cu}); // ASID 5, half 1 invalid
    memory.tlb_write(3, {0x6000u, 0x70000000u, 0x80000007u, 1u});                        // scratchpad
    memory.write32(0x80002004u, 0x11223344u);
    require(memory.read32(0x00010004u) == 0x11223344u, "kuseg through TLB entry 1 (even page)");
    memory.write32(0x00010008u, 0x55667788u);
    require(memory.read32(0x80002008u) == 0x55667788u, "TLB store reaches physical RAM");
    bus_fault([&] { memory.write32(0x00011000u, 1u); }, BusFault::TlbModified, "TLB modified");
    require(memory.read32(0x00011000u) == memory.read32(0x80003000u), "odd page translates to its own frame");
    bus_fault([&] { (void)memory.read32(0x00020000u); }, BusFault::TlbRefill, "no entry for ASID 0");
    memory.set_translation_context(5, false);
    require(memory.read32(0x00020000u) == 0u, "ASID-matched entry");
    bus_fault([&] { (void)memory.read32(0x00021000u); }, BusFault::TlbInvalid, "TLB invalid");
    memory.set_translation_context(0, false);
    bus_fault([&] { (void)memory.read32(0x00400000u); }, BusFault::TlbRefill, "TLB refill");
    memory.write64(0x70003FF8u, 0x0123456789ABCDEFull);
    require(memory.read64(0x70003FF8u) == 0x0123456789ABCDEFull, "scratchpad through the S bit");
    bus_fault([&] { (void)memory.read32(0x70004000u); }, BusFault::TlbRefill, "no entry for ASID 0"); // past 16 KiB
    require(memory.tlb_probe(0x00010000u) == 1u && !memory.tlb_probe(0x00400000u), "TLBP");
    memory.tlb_write(4, {0u, 0x00010000u, 0x1Fu, 0x1Fu});
    bus_fault([&] { (void)memory.read32(0x00010000u); }, BusFault::TlbMultipleMatch, "entries 1 and 4");
}

// The reset vector replaced by one instruction: the stop must name it.
BootReport run_patched(const std::vector<std::uint8_t>& rom, std::uint32_t word) {
    auto patched = rom;
    put32(patched, 0, word);
    const auto bios = load_bios_rom0(patched);
    EEState state;
    Memory memory;
    prepare_ee_boot(bios, nullptr, state, memory);
    return run_ee_boot(state, memory, nullptr, {});
}
void check_interpreter_stops(const std::vector<std::uint8_t>& rom) {
    auto expect = [&](std::uint32_t word, const std::string& category, const std::string& reason) {
        const auto report = run_patched(rom, word);
        require(report.outcome == BootOutcome::BlockPoint && report.pc == m20::kResetVector &&
                report.category == category && report.reason.find(reason) != std::string::npos,
                "stop for " + hex8(word) + ": [" + report.category + "] " + report.reason);
        require(report.word == word && report.retired == 0u, "stop leaves the instruction unretired");
    };
    for (const auto word : {0x0000000Cu, 0x0000000Du}) {
        auto patched = rom;
        put32(patched, 0, word);
        EEState state;
        Memory memory;
        prepare_ee_boot(load_bios_rom0(patched), nullptr, state, memory);
        BootOptions options;
        options.stop_at = 0xBFC00380u;
        const auto report = run_ee_boot(state, memory, nullptr, options);
        require(report.outcome == BootOutcome::StopAddress && report.retired == 1 &&
                state.cp0(Cop0::Epc) == m20::kResetVector &&
                (state.cp0(Cop0::Cause) & 0x7Cu) == (word == 0xCu ? 8u : 9u) * 4u &&
                (state.cp0(Cop0::Status) & kStatusEXL), "synchronous exception enters bootstrap vector");
    }
    expect(0x44000000u, "unsupported", "(COP1/FPU) not implemented by the interpreter");
    expect(0x70000000u, "unsupported", "(MMI) not implemented");
    expect(0x88080000u, "unsupported", "(unaligned load/store)");
    expect(0x42000006u, "not modeled", "TLBWR");
    expect(0xBC000000u, "not modeled", "CACHE operation 0x00000000");
    // These retire nothing before failing, but they do start executing.
    auto started = [&](std::uint32_t word, const std::string& category, const std::string& reason) {
        const auto report = run_patched(rom, word);
        require(report.outcome == BootOutcome::BlockPoint && report.category == category &&
                report.reason.find(reason) != std::string::npos,
                "stop for " + hex8(word) + ": [" + report.category + "] " + report.reason);
    };
    started(0x40080800u, "not modeled", "MFC0 Random"); // Count is modeled since M2.1 (boot_m21)
    started(0x40083800u, "architecturally undefined", "reserved7");
    started(0x42000001u, "unsupported", "TLBR of entry 0, undefined since reset");
}


void check_final_state(const EEState& state, const Memory& memory, const m20::Firmware& fw, const std::string& run) {
    const auto checksum = m20::table_checksum();
    const auto payload = (static_cast<std::uint64_t>(checksum) << 32) | 0x1234u;
    require(state.reg32(m20::s0) == 1u, run + ": TLBP found entry 1");
    require(state.reg32(m20::v0) == checksum, run + ": checksum register");
    require(state.reg64(m20::v1) == payload, run + ": payload result");
    require(state.reg32(m20::ra) == fw.at("mmio") - 8u, run + ": JALR link (jalr, nop, lui, sd, mmio)");
    require(memory.read32(m20::kChecksumRam) == checksum, run + ": checksum in RAM");
    require(memory.read64(m20::kPayloadResultRam) == payload, run + ": payload result in RAM");
    require(memory.read32(0x80100010u) == checksum, run + ": kuseg TLB store reached physical RAM");
    require(memory.read32(m20::kScratchpadBase) == checksum, run + ": scratchpad store");
    require(memory.read32(m20::kPayloadRam) == m20::payload_code().front(), run + ": payload copied to RAM");
    require(state.cp0(Cop0::Status) == m20::kStatusAfterEret, run + ": Status after ERET");
    require(state.cp0(Cop0::Index) == 1u && state.cp0(Cop0::ErrorEpc) == fw.at("after_eret"), run + ": COP0");
}

void check_boot(const std::vector<std::uint8_t>& rom, const m20::Firmware& fw) {
    const auto bios = load_bios_rom0(rom);
    const auto& module = aot_module();
    require(std::string(module.source_kind) == "bios-rom0" && bios.sha256 == module.source_sha256,
            "AOT module provenance");
    require(find_aot_block(module, fw.at("sum_loop")) != nullptr, "checksum loop translated");
    require(find_aot_block(module, m20::kResetVector) == nullptr, "MFC0 at the reset vector stays interpreted");
    require(find_aot_block(module, fw.at("after_eret")) == nullptr, "ERET target is not statically known");

    const auto graph = analyze_control_flow(bios_code_image(bios), {}, {}, true);
    auto boundary = [&](std::uint32_t address, const std::string& reason) {
        return std::any_of(graph.interpreter_boundaries.begin(), graph.interpreter_boundaries.end(),
                           [&](const AotInterpreterBoundary& b) { return b.address == address && b.reason == reason; });
    };
    require(boundary(m20::kResetVector, "system instruction Mfc0"), "boundary at the reset vector");
    require(boundary(fw.at("iop_path"), "system instruction Break"), "boundary at BREAK");
    rejects([&] { (void)generate_aot_cpp(bios_code_image(bios)); }, "AOT unsupported R5900 instruction");

    BootOptions options;
    options.trace = TraceLevel::Full;
    const auto mmio = fw.at("mmio") + 4u;
    auto run = [&](const AotModule* aot, EEState& state, Memory& memory) {
        prepare_ee_boot(bios, aot, state, memory);
        require(state.pc == m20::kResetVector && state.cp0(Cop0::PRid) == 0x2E20u &&
                state.cp0(Cop0::Status) == (kStatusBEV | kStatusERL) && state.cp0(Cop0::Config) == 0x440u,
                "EE reset state");
        return run_ee_boot(state, memory, aot, options);
    };
    EEState interpreted_state, hybrid_state;
    Memory interpreted_memory, hybrid_memory;
    const auto interpreted = run(nullptr, interpreted_state, interpreted_memory);
    const auto hybrid = run(&module, hybrid_state, hybrid_memory);
    for (const auto* report : {&interpreted, &hybrid}) {
        const std::string name = report == &interpreted ? "interpreter" : "hybrid";
        require(report->outcome == BootOutcome::BlockPoint && report->category == "bus: device not implemented",
                name + " stop category: " + report->category + " " + report->reason);
        require(report->pc == mmio && report->word == 0x8D09F020u && !report->in_delay_slot,
                name + " block point at the INTC load");
        require(report->reason.find("0xB000F020 (physical 0x1000F020, INTC)") != std::string::npos,
                name + " block point names the device: " + report->reason);
        require(contains(report->trace, "  S TLBWI 47: PageMask 0x00000000 EntryHi 0xE005E000"), name + " TLB init");
        require(contains(report->trace, "  S TLBP EntryHi 0x00080000 -> entry 1"), name + " TLBP");
        require(contains(report->trace, "  S CACHE DXIN 0x80000000"), name + " cache invalidation");
        require(contains(report->trace, "  S ERET -> 0x" + hex8(fw.at("after_eret")) + ", Status 0x10400000"),
                name + " ERET");
        require(contains(report->trace, "  M W32 v=70000000 p=00000000 scratchpad"), name + " scratchpad store");
        require(contains(report->trace, "  M W32 v=00080010 p=00100010 main RAM"), name + " kuseg store");
        require(report->tail.size() == 32u && report->tail.back().find(hex8(mmio)) != std::string::npos,
                name + " instruction tail ends at the block point");
    }
    check_final_state(interpreted_state, interpreted_memory, fw, "interpreter");
    check_final_state(hybrid_state, hybrid_memory, fw, "hybrid");
    require(interpreted.aot_instructions == 0u && interpreted.interpreted_instructions == interpreted.retired + 1u,
            "interpreter-only run (the faulting instruction started but did not retire)");
    require(hybrid.aot_instructions > 0u && hybrid.interpreted_instructions > 0u && hybrid.engine_switches > 2u,
            "hybrid run used both engines");
    require(hybrid.retired == interpreted.retired, "same retired instruction count");
    require(contains(hybrid.trace, "E aot (host-compiled block) at ") &&
            contains(hybrid.trace, "E interpreter (no AOT block) at BFC00000"), "engine changes traced");
    require(normalized(hybrid.trace) == normalized(interpreted.trace), "hybrid and interpreter traces agree");
    for (unsigned n = 0; n < 32; ++n)
        require(hybrid_state.gpr[n].low == interpreted_state.gpr[n].low &&
                hybrid_state.gpr[n].high == interpreted_state.gpr[n].high, "same GPRs");
    require(hybrid_state.cop0 == interpreted_state.cop0, "same COP0");

    // Reproducible: a second run produces a byte-identical trace.
    EEState again_state;
    Memory again_memory;
    require(run(&module, again_state, again_memory).trace == hybrid.trace, "deterministic trace");

    // Budget and stop address end the run without inventing a block point.
    for (const auto* aot : {static_cast<const AotModule*>(nullptr), &module}) {
        EEState state;
        Memory memory;
        prepare_ee_boot(bios, aot, state, memory);
        BootOptions short_run;
        short_run.max_instructions = 10;
        const auto limited = run_ee_boot(state, memory, aot, short_run);
        require(limited.outcome == BootOutcome::BudgetExhausted && limited.retired == 10u, "budget");
        prepare_ee_boot(bios, aot, state, memory);
        BootOptions until;
        until.stop_at = fw.at("after_eret");
        const auto stopped = run_ee_boot(state, memory, aot, until);
        require(stopped.outcome == BootOutcome::StopAddress && stopped.pc == fw.at("after_eret") &&
                state.cp0(Cop0::Status) == m20::kStatusAfterEret, "stop address after ERET");
    }

    // A translation of one ROM0 revision never runs against another.
    auto other = rom;
    other[static_cast<std::size_t>(bios.find("ROMVER")->offset) + 13] = '4';
    const auto other_bios = load_bios_rom0(other);
    EEState state;
    Memory memory;
    rejects([&] { prepare_ee_boot(other_bios, &module, state, memory); }, "firmware revisions are never mixed");
    // And the interpreter still boots it (only the date in ROMVER differs).
    prepare_ee_boot(other_bios, nullptr, state, memory);
    require(run_ee_boot(state, memory, nullptr, {}).pc == mmio, "other revision boots in the interpreter");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: ps2n-boot-m20 <synthetic-rom0.bin>");
        const auto rom = read_file(std::filesystem::path(argv[1]));
        const auto fw = m20::build_firmware();
        require(rom == fw.rom, "fixture file matches the synthetic firmware definition");
        check_sha256();
        check_loader(rom, fw);
        check_bus(rom);
        check_interpreter_stops(rom);
        check_boot(rom, fw);
        std::cout << "M2.0 boot: loader, EE bus/TLB, COP0, interpreter and hybrid AOT checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "boot_m20: " << error.what() << '\n';
        return 1;
    }
}
