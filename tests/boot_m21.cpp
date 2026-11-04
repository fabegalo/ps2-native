// M2.1: what the real BIOS needs right after the reset vector, demonstrated on
// synthetic inputs only (no Sony content): MMI pipeline-1 multiply/divide and
// PADDUW (decode and shared semantics), COP0 Count on the EE cycle clock, EE
// timers T0..T3, the 0x1000F500 latch, scratchpad TLB entries with PageMask 0,
// and the M2.1 variant of the synthetic ROM0 booted interpreter-only and
// hybrid (AOT translation of the same ROM0 compiled into this executable).
#include "bios_program.hpp"
#include "boot_checks.hpp"

#include "ps2native/aot_runtime.hpp"
#include "ps2native/core.hpp"
#include "ps2native/ee_semantics.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ps2native;
using namespace boot_checks;

std::uint32_t word_of(void (*emit)(m20::Assembler&)) {
    m20::Assembler a(0u);
    emit(a);
    return a.finish().front();
}

void check_decode() {
    // Words exactly as the real BIOS reset code contains them.
    const auto padduw = try_decode_r5900(0u, 0x7000D428u);
    require(padduw && padduw->opcode == MipsOpcode::Padduw && padduw->rd == 26u && padduw->rs == 0u &&
            padduw->rt == 0u, "PADDUW k0, zero, zero");
    const auto div1 = try_decode_r5900(0u, 0x70E2001Au);
    require(div1 && div1->opcode == MipsOpcode::Div1 && div1->rs == 7u && div1->rt == 2u, "DIV1 a3, v0");
    const auto mflo1 = try_decode_r5900(0u, 0x70003812u);
    require(mflo1 && mflo1->opcode == MipsOpcode::Mflo1 && mflo1->written_gpr() == 7u, "MFLO1 a3");
    require(mips_aot_supported(MipsOpcode::Padduw) && mips_aot_supported(MipsOpcode::Divu1), "AOT subset");
    require(decode_mips(0u, 0x7000D428u).opcode == MipsOpcode::Padduw, "AOT decodes PADDUW");
    require(!div1->written_gpr(), "DIV1 writes no GPR");
    rejects([] { (void)decode_r5900(0u, 0x70000468u); }, "(MMI) not implemented"); // PSUBUW (MMI1 sa 0x11)
    rejects([] { (void)decode_r5900(0u, 0x7000081Au); }, "(MMI) not implemented"); // DIV1 with rd != 0
    rejects([] { (void)decode_r5900(0u, 0x70000000u); }, "(MMI) not implemented"); // MADD
    rejects([] { (void)decode_mips(0u, 0x70000000u); }, "AOT unsupported R5900 instruction");
}

void check_semantics() {
    EEState s;
    Memory m;
    s.set_reg128(m20::t0, 0xFFFFFFFF00000001ull, 0x7FFFFFFF80000000ull);
    s.set_reg128(m20::t1, 0x0000000100000002ull, 0x8000000080000001ull);
    const auto padduw = word_of([](m20::Assembler& a) { a.padduw(m20::t2, m20::t0, m20::t1); });
    ee::exec<MipsOpcode::Padduw>(s, m, 0u, padduw);
    const auto sum = s.reg128(m20::t2);
    require(sum.low == 0xFFFFFFFF00000003ull && sum.high == 0xFFFFFFFFFFFFFFFFull, "PADDUW lanes saturate");
    const auto to_zero = word_of([](m20::Assembler& a) { a.padduw(m20::zero, m20::t0, m20::t1); });
    ee::exec<MipsOpcode::Padduw>(s, m, 0u, to_zero);
    require(s.reg128(0).low == 0u && s.reg128(0).high == 0u, "PADDUW never writes $zero");

    s.lo = 0x11u;
    s.hi = 0x22u;
    const auto multu1 = word_of([](m20::Assembler& a) { a.multu1(m20::t3, m20::t0, m20::t1); });
    ee::exec<MipsOpcode::Multu1>(s, m, 0u, multu1);
    require(s.lo1 == 2u && s.hi1 == 0u && s.reg64(m20::t3) == 2u, "MULTU1 into LO1/HI1 and rd");
    require(s.lo == 0x11u && s.hi == 0x22u, "MULTU1 leaves pipeline 0 alone");
    s.lo1 = 0x33u;
    const auto mflo1 = word_of([](m20::Assembler& a) { a.mflo1(m20::t4); });
    ee::exec<MipsOpcode::Mflo1>(s, m, 0u, mflo1);
    require(s.reg64(m20::t4) == 0x33u, "MFLO1");

    s.retired_instructions = 100u;
    s.set_cp0(Cop0::Count, 5u);
    s.count_anchor = 90u;
    require(s.count() == 15u, "Count advances one per retired instruction since the last MTC0");
    s.set_cp0(Cop0::Count, 0xFFFFFFFEu);
    require(s.count() == 8u, "Count wraps at 32 bits");
}

void check_registers(const std::vector<std::uint8_t>& rom) {
    Memory m;
    m.reset_for_boot(rom);
    device_refused([&] { m.write32(0xB0000010u, 0x80u); }, "no EE clock is attached");
    std::uint64_t cycles = 0;
    m.set_clock(&cycles);
    require(m.read32(0xB0000000u) == 0u, "T0_COUNT is zero (disabled) after reset");

    // T0 on BUSCLK: one count per two EE cycles, edges at fixed multiples since reset.
    cycles = 100u;
    m.write32(0xB0000010u, 0x80u);
    cycles = 110u;
    require(m.read32(0xB0000000u) == 5u, "BUSCLK counting");
    cycles = 111u;
    require(m.read32(0xB0000000u) == 5u, "no partial BUSCLK edge");
    cycles = 100u + 2u * 65536u + 6u;
    require(m.read32(0xB0000000u) == 3u, "16-bit wrap");
    cycles = 1000u;
    m.write32(0xB0000000u, 0xFFFEu);
    cycles = 1004u;
    require(m.read32(0xB0000000u) == 0u, "written COUNT continues and wraps");
    m.write32(0xB0000010u, 0x00u);
    cycles = 5000u;
    require(m.read32(0xB0000000u) == 0u, "disabled timer holds its count");

    // T1 BUSCLK/16, T2 BUSCLK/256, T3 HBLANK (NTSC line: 9370 BUSCLK).
    cycles = 65536u;
    m.write32(0xB0000810u, 0x81u);
    cycles += 32u * 7u + 31u;
    require(m.read32(0xB0000800u) == 7u, "BUSCLK/16");
    cycles += 1u;
    require(m.read32(0xB0000800u) == 8u, "BUSCLK/16 edge");
    cycles = 0u;
    m.write32(0xB0001010u, 0x82u);
    cycles = 512u * 3u;
    require(m.read32(0xB0001000u) == 3u, "BUSCLK/256");
    constexpr std::uint64_t kLine = kEeCyclesPerBusCycle * kBusCyclesPerNtscLine;
    require(kLine == 18740u, "HBLANK period in EE cycles");
    cycles = kLine * 10u + 5u;
    m.write32(0xB0001810u, 0x83u);
    cycles = kLine * 11u - 1u;
    require(m.read32(0xB0001800u) == 0u, "before the next HBLANK");
    cycles = kLine * 11u;
    require(m.read32(0xB0001800u) == 1u, "HBLANK edge");
    cycles += kLine * 3u;
    require(m.read32(0xB0001800u) == 4u, "HBLANK count");

    device_refused([&] { (void)m.read32(0xB0000020u); }, "T0_COMP read before any write");
    m.write32(0xB0000020u, 0x12345678u);
    require(m.read32(0xB0000020u) == 0x5678u, "COMP keeps 16 bits");
    require(m.read32(0xB0000010u) == 0u && m.read32(0xB0001810u) == 0x83u,
            "Tn_MODE retains stopped/running configuration; no enabled interrupts means flags stay clear");
    device_refused([&] { m.write32(0xB0000010u, 0x84u); }, "gating is not modeled");
    device_refused([&] { m.write32(0xB0000010u, 0x180u); }, "timer interrupts need the INTC");
    device_refused([&] { m.write32(0xB0000010u, 0x280u); }, "timer interrupts need the INTC");
    device_refused([&] { m.write32(0xB0000010u, 0xC0u); }, "clear-on-compare is not modeled");
    m.write32(0xB0000010u, 0x100u); // interrupt bit while stopped: nothing can fire
    require(m.read32(0xB0000010u) == 0x100u, "disabled counter retains interrupt enable");
    m.write32(0xB0000010u, 0xC00u); // W1C flags do not set them
    require(m.read32(0xB0000010u) == 0u, "W1C timer flags cannot be set by the guest");
    device_refused([&] { (void)m.read32(0xB0000030u); }, "T0_HOLD: counter hold");
    device_refused([&] { m.write32(0xB0001030u, 0u); }, "T2_HOLD: counter hold");
    device_refused([&] { (void)m.read16(0xB0000000u); }, "32-bit Tn_COUNT/MODE/COMP only");
    device_refused([&] { (void)m.read32(0xB0000040u); }, "offset 0x040");

    m.write32(0xB000F500u, 0xFFFFFFFFu);
    device_refused([&] { (void)m.read32(0xB000F500u); }, "unidentified register");
    device_refused([&] { m.write16(0xB000F500u, 1u); }, "no register model");
    device_refused([&] { m.write32(0xB000F510u, 1u); }, "DMAC extended): device not implemented (no register model)");
    device_refused([&] { (void)m.read32(0xB000F020u); }, "INTC): device not implemented (no register model)");
    device_refused([&] { (void)m.read32(0xB000F000u); }, "INTC): device not implemented (I_STAT read: no EE interrupt source");
    device_refused([&] { (void)m.read32(0xB000F410u); }, "MCH (RDRAM controller)");
    device_refused([&] { (void)m.fetch32(0xB0000000u); }, "instruction fetch from device registers");

    // Tool board id: a retail console has no tool board (0xFF is a hypothesis).
    require(m.read8(0xBF803204u) == kRetailToolBoardId && m.read8(0x9F803204u) == 0xFFu,
            "tool board id byte through KSEG1 and KSEG0");
    require((kRetailToolBoardId & 0xF0u) != 0x10u && (kRetailToolBoardId & 0xF0u) != 0x20u &&
            (kRetailToolBoardId & 0xF0u) != 0x30u && kRetailToolBoardId != 0x60u &&
            kRetailToolBoardId != 0x61u, "retail value is outside the tool board classes");
    device_refused([&] { (void)m.read16(0xBF803204u); }, "tool board id access of 2 byte(s)");
    device_refused([&] { (void)m.read32(0xBF803204u); }, "tool board id access of 4 byte(s)");
    device_refused([&] { m.write8(0xBF803204u, 0u); }, "tool board id write");
    device_refused([&] { (void)m.read8(0xBF803205u); }, "IOP registers): device not implemented (no register model)");
    device_refused([&] { (void)m.read8(0xBF803100u); }, "IOP registers): device not implemented (no register model)");
    device_refused([&] { (void)m.fetch32(0xBF803204u); }, "instruction fetch from device registers");
}


std::array<std::uint32_t, 4> padduw_lanes() {
    std::array<std::uint32_t, 4> lanes{};
    for (unsigned n = 0; n < 4; ++n) {
        const auto sum = static_cast<std::uint64_t>(m20::kTable[n]) + m20::kTable[4 + n];
        lanes[n] = sum > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(sum);
    }
    return lanes;
}

void check_results(const EEState& state, const Memory& memory, const std::string& run) {
    const auto lanes = padduw_lanes();
    for (unsigned n = 0; n < 4; ++n)
        require(memory.read32(m20::kM21PaddResult + 4u * n) == lanes[n], run + ": PADDUW lane " + std::to_string(n));
    constexpr std::array<std::uint64_t, m20::kM21MulDivCount> kMulDiv{
        0xFFFFFFFFFFFFFFEBull, // MULT -7 * 3 (rd)
        0xFFFFFFFFFFFFFFEBull, // MULTU1 0xFFFFFFF9 * 3: LO1 (sign-extended low word)
        2u,                    // HI1
        0xFFFFFFFFFFFFFFFEull, // DIV1 -7 / 3: LO1
        0xFFFFFFFFFFFFFFFFull, // HI1
        0xFFFFFFFFFFFFFFEBull, // MFLO: pipeline 0 unchanged
        0xFFFFFFFFFFFFFFFFull, // MFHI
        0xFFFFFFFFFFFFFFFFull, // DIVU1 by zero: LO1
        0xFFFFFFFFFFFFFFF9ull, // HI1 = dividend
        3u,                    // MTLO1 / MFLO1
        0xFFFFFFFFFFFFFFF9ull, // MTHI1 / MFHI1
        0xFFFFFFFF80000000ull  // DIV1 INT32_MIN / -1
    };
    for (unsigned n = 0; n < kMulDiv.size(); ++n)
        require(memory.read64(m20::kM21MulDivResults + 8u * n) == kMulDiv[n],
                run + ": MULT/DIV result " + std::to_string(n));
    require(memory.read32(m20::kM21CountResult) == 6u, run + ": Count after MTC0 and five NOPs");
    const auto hblank = memory.read32(m20::kM21HblankResult);
    require(hblank >= 2u * 18740u - 2u && hblank <= 2u * 18740u + 2u,
            run + ": two HBLANK periods measured with Count: " + std::to_string(hblank));
    require(memory.read32(m20::kM21T0Result) >= 2u, run + ": T0 counted HBLANK edges");
    require(memory.read32(m20::kM21BoardIdResult) == 0xFFu, run + ": LBU of the tool board id zero-extends 0xFF");
    require(memory.read32(m20::kM21ScratchpadAlias + 0x3FF0u) == m20::table_checksum() &&
            memory.read32(m20::kScratchpadBase + 0x3FF0u) == m20::table_checksum(),
            run + ": PageMask-0 scratchpad entry spans 16 KiB");
    require(state.cp0(Cop0::Index) == 2u, run + ": TLBWI index 2");
}

void check_boot(const std::vector<std::uint8_t>& rom, const m20::Firmware& fw) {
    const auto bios = load_bios_rom0(rom);
    const auto& module = aot_module();
    require(std::string(module.source_kind) == "bios-rom0" && bios.sha256 == module.source_sha256,
            "AOT module provenance");
    BootOptions options;
    options.trace = TraceLevel::Full;
    auto run = [&](const AotModule* aot, EEState& state, Memory& memory) {
        prepare_ee_boot(bios, aot, state, memory);
        return run_ee_boot(state, memory, aot, options);
    };
    EEState interpreted_state, hybrid_state;
    Memory interpreted_memory, hybrid_memory;
    const auto interpreted = run(nullptr, interpreted_state, interpreted_memory);
    const auto hybrid = run(&module, hybrid_state, hybrid_memory);
    const auto mmio = fw.at("mmio") + 4u;
    for (const auto* report : {&interpreted, &hybrid}) {
        const std::string name = report == &interpreted ? "interpreter" : "hybrid";
        require(report->outcome == BootOutcome::BlockPoint && report->pc == mmio &&
                report->reason.find("0xB000F000 (physical 0x1000F000, INTC)") != std::string::npos,
                name + " block point at the INTC load: " + report->reason);
        require(contains(report->trace, "  M W32 v=B0000010 p=10000010 EE timers = 00000083"), name + " T0_MODE");
        require(contains(report->trace, "  M R32 v=B0000000 p=10000000 EE timers = "), name + " T0_COUNT reads");
        require(contains(report->trace, "  M W32 v=B000F500 p=1000F500 DMAC extended = FFFFFFFF"), name + " F500");
        require(contains(report->trace, "  M R8 v=BF803204 p=1F803204 IOP registers = FF"), name + " tool board id");
        require(contains(report->trace, "  S MTC0 Count <- 0x00000000"), name + " MTC0 Count");
        require(contains(report->trace, "  S TLBWI 2: PageMask 0x00000000 EntryHi 0x70010000 EntryLo0 0x80000007"),
                name + " PageMask-0 scratchpad entry");
    }
    check_results(interpreted_state, interpreted_memory, "interpreter");
    check_results(hybrid_state, hybrid_memory, "hybrid");
    require(interpreted.aot_instructions == 0u, "interpreter-only run");
    for (const char* op : {"Padduw", "Multu1", "Div1", "Divu1", "Mflo1", "Mfhi1", "Mtlo1", "Mthi1"})
        require(std::any_of(hybrid.trace.begin(), hybrid.trace.end(), [&](const std::string& line) {
                    return line.starts_with("I ") && line.find(" aot ") != std::string::npos &&
                           line.ends_with(std::string(" ") + op);
                }), std::string("hybrid ran ") + op + " in an AOT block");
    require(hybrid.retired == interpreted.retired, "same retired instruction count");
    require(normalized(hybrid.trace) == normalized(interpreted.trace), "hybrid and interpreter traces agree");
    for (unsigned n = 0; n < 32; ++n)
        require(hybrid_state.gpr[n].low == interpreted_state.gpr[n].low &&
                hybrid_state.gpr[n].high == interpreted_state.gpr[n].high, "same GPRs");
    require(hybrid_state.lo1 == interpreted_state.lo1 && hybrid_state.hi1 == interpreted_state.hi1, "same LO1/HI1");
    require(hybrid_state.cop0 == interpreted_state.cop0 && hybrid_state.count() == interpreted_state.count(),
            "same COP0 and Count");
    EEState again_state;
    Memory again_memory;
    require(run(&module, again_state, again_memory).trace == hybrid.trace, "deterministic trace");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: ps2n-boot-m21 <synthetic-rom0-m21.bin>");
        const auto rom = read_file(std::filesystem::path(argv[1]));
        const auto fw = m20::build_firmware(m20::Stage::M21);
        require(rom == fw.rom, "fixture file matches the synthetic M2.1 firmware definition");
        require(m20::build_firmware().rom != fw.rom, "M2.1 variant differs from the M2.0 firmware");
        check_decode();
        check_semantics();
        check_registers(rom);
        check_boot(rom, fw);
        std::cout << "M2.1 boot: MMI, COP0 Count, EE timers, 0x1000F500, scratchpad TLB and hybrid checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "boot_m21: " << error.what() << '\n';
        return 1;
    }
}
