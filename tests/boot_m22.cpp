// M2.2: GS privileged registers (control state only, nothing is drawn), the
// GS line clock that drives HBLANK for the EE timers, the DEV9 latches, the EE
// SIO transmit path, LWC1/SWC1 and the explicit MCH refusal, demonstrated on
// synthetic inputs only (no Sony content). The M2.2 variant of the synthetic
// ROM0 is booted interpreter-only and hybrid (AOT translation of the same
// ROM0 compiled into this executable) and both runs must agree.
#include "bios_program.hpp"
#include "boot_checks.hpp"

#include "ps2native/aot_runtime.hpp"
#include "ps2native/core.hpp"
#include "ps2native/interpreter.hpp"

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

constexpr std::uint64_t kNtscLine = kEeCyclesPerBusCycle * kBusCyclesPerNtscLine;
constexpr std::uint64_t kPalLine = kEeCyclesPerBusCycle * kBusCyclesPerPalLine;
constexpr std::uint64_t kCmodNtsc = 2ull << kSmode1CmodShift;
constexpr std::uint64_t kCmodPal = 3ull << kSmode1CmodShift;
constexpr std::array<std::uint32_t, 6> kModeRegisters{kGsSmode1, kGsSmode2, kGsSrfsh, kGsSynch1, kGsSynch2, kGsSyncv};

void check_gs() {
    require(kNtscLine == 18740u && kPalLine == 18872u, "line lengths in EE cycles");
    GsRegisters gs;
    gs.reset(100u);
    require(gs.line_clock() == GsRegisters::LineClock{100u, kNtscLine}, "power-on line clock: NTSC from reset");
    for (const auto reg : kModeRegisters) require(!gs.latched(reg), "nothing latched after reset");
    require(!gs.latched(kGsCsr), "CSR not written after reset");

    unsigned guard_calls = 0;
    const GsRegisters::LineClockGuard counted = [&] { ++guard_calls; };
    rejects([&] { gs.write(kGsSmode1, 4u, kCmodNtsc, 200u, counted); },
            "GS SMODE1 write of 4 byte(s) is not modeled (64-bit stores only)");
    rejects([&] { gs.write(kGsSyncv, 2u, 0u, 200u, counted); }, "GS SYNCV write of 2 byte(s)");
    rejects([&] { gs.write(kGsSmode2, 8u, 0x10u, 200u, counted); },
            "GS SMODE2 0x0000000000000010: reserved bits 0x0000000000000010 set");
    rejects([&] { gs.write(kGsSmode1, 8u, 1ull << 38, 200u, counted); }, "reserved bits 0x0000004000000000");
    rejects([&] { gs.write(kGsSynch1, 8u, 1ull << 59, 200u, counted); }, "GS SYNCH1");
    rejects([&] { gs.write(kGsSynch2, 8u, 1ull << 27, 200u, counted); }, "GS SYNCH2");
    require(guard_calls == 0u && !gs.latched(kGsSmode1) && !gs.latched(kGsSmode2), "refused writes change nothing");
    for (const auto reg : kModeRegisters)
        rejects([&] { (void)gs.read(reg, 8u); }, "read (8 byte(s)): write-only register");
    rejects([&] { (void)gs.read(kGsCsr, 4u); }, "GS CSR read");
    rejects([&] { (void)gs.read(kGsPrivilegedBase, 8u); }, "no register model");          // PMODE
    rejects([&] { gs.write(kGsPrivilegedBase, 8u, 0u, 200u, counted); }, "no register model");
    rejects([&] { gs.write(kGsSmode1 + 8u, 8u, 0u, 200u, counted); }, "no register model");  // misaligned
    rejects([&] { gs.write(kGsSyncv + 0x10u, 8u, 0u, 200u, counted); }, "no register model");

    // SRFSH and SYNCV have no reserved bits; neither touches the line clock.
    gs.write(kGsSrfsh, 8u, ~0ull, 250u, counted);
    gs.write(kGsSyncv, 8u, ~0ull, 250u, counted);
    gs.write(kGsSynch2, 8u, (1ull << 27) - 1u, 250u, counted);
    require(gs.latched(kGsSrfsh) == ~0ull && gs.latched(kGsSyncv) == ~0ull && guard_calls == 0u,
            "SRFSH/SYNCV latched without retiming");

    // SMODE1: SINT holds sync, clearing it starts the lines at the write.
    gs.write(kGsSmode1, 8u, kCmodNtsc | kSmode1Prst | kSmode1Sint, 300u, counted);
    require(guard_calls == 1u, "holding sync changes the line clock");
    rejects([&] { (void)gs.line_clock(); }, "GS video sync is held (SMODE1.SINT = 1)");
    gs.write(kGsSmode1, 8u, kCmodNtsc, 400u, counted);
    require(gs.line_clock() == GsRegisters::LineClock{400u, kNtscLine} && guard_calls == 2u, "sync restarts");
    gs.write(kGsSmode1, 8u, kCmodNtsc | 0x4u, 900u, counted);
    require(gs.line_clock() == GsRegisters::LineClock{400u, kNtscLine} && guard_calls == 2u,
            "same line length keeps the phase");
    require(gs.latched(kGsSmode1) == (kCmodNtsc | 0x4u), "SMODE1 latched");
    gs.write(kGsSmode1, 8u, kCmodPal, 1000u, counted);
    require(gs.line_clock() == GsRegisters::LineClock{1000u, kPalLine} && guard_calls == 3u, "PAL line");
    gs.write(kGsSmode1, 8u, 0u, 1100u, counted);
    require(guard_calls == 4u, "an unsupported mode also changes the line clock");
    rejects([&] { (void)gs.line_clock(); }, "HBLANK timing for SMODE1.CMOD = 0 is not modeled");
    const GsRegisters::LineClockGuard refuse = [] { throw DeviceRefusal("guard refuses"); };
    rejects([&] { gs.write(kGsSmode1, 8u, kCmodNtsc, 1200u, refuse); }, "guard refuses");
    require(gs.latched(kGsSmode1) == 0u, "a refused retiming leaves SMODE1 as it was");
    rejects([&] { (void)gs.line_clock(); }, "SMODE1.CMOD = 0");
    gs.write(kGsSmode1, 8u, 0u, 1250u, refuse); // no change: the guard is not consulted

    // CSR: 32/64-bit, action bits only; events and FLUSH have nothing to act on.
    rejects([&] { gs.write(kGsCsr, 2u, kCsrSignal, 1300u, counted); }, "32/64-bit stores at 0x12001000 only");
    rejects([&] { gs.write(kGsCsr, 4u, 1u << 5, 1300u, counted); }, "reserved or read-only bits 0x0000000000000020");
    rejects([&] { gs.write(kGsCsr, 8u, 1u << 12, 1300u, counted); }, "reserved or read-only bits 0x0000000000001000");
    require(!gs.latched(kGsCsr), "refused CSR writes are not latched");
    gs.write(kGsCsr, 4u, kCsrSignal | kCsrFinish | kCsrHsint | kCsrVsint | kCsrEdwint | kCsrFlush, 1300u, counted);
    require(gs.latched(kGsCsr) == 0x11Fu && guard_calls == 4u, "CSR event/FLUSH bits");
    rejects([&] { gs.write(kGsCsr, 8u, kCsrReset, 1400u, refuse); }, "guard refuses");
    require(gs.latched(kGsSmode1) == 0u, "refused CSR.RESET keeps the registers");
    gs.write(kGsCsr, 8u, kCsrReset, 1400u, counted);
    require(gs.line_clock() == GsRegisters::LineClock{1400u, kNtscLine} && guard_calls == 5u,
            "CSR.RESET restarts the NTSC line clock");
    for (const auto reg : kModeRegisters) require(!gs.latched(reg), "CSR.RESET clears the privileged registers");
    require(gs.latched(kGsCsr) == kCsrReset, "CSR latched");
    gs.reset(0u);
    require(!gs.latched(kGsCsr) && gs.line_clock() == GsRegisters::LineClock{0u, kNtscLine}, "reset");
}

void check_bus(const std::vector<std::uint8_t>& rom) {
    Memory m;
    m.reset_for_boot(rom);
    std::uint64_t cycles = 50u;
    m.set_clock(&cycles);

    // GS privileged registers through KSEG1, timers driven by the line clock.
    m.write64(0xB2000010u, kCmodNtsc | kSmode1Prst | kSmode1Sint);
    require(m.gs().latched(kGsSmode1) == (kCmodNtsc | kSmode1Prst | kSmode1Sint), "SMODE1 through the bus");
    device_refused([&] { m.write32(0xB2000010u, 0u); }, "GS SMODE1 write of 4 byte(s)");
    device_refused([&] { (void)m.read64(0xB2000010u); }, "write-only register");
    device_refused([&] { (void)m.read32(0xB2001000u); }, "GS CSR read");
    device_refused([&] { m.write32(0xB2001000u, 1u << 12); }, "reserved or read-only bits");
    device_refused([&] { m.write64(0xB2000000u, 0u); },
                   "GS privileged registers): device not implemented (no register model)");
    device_refused([&] { (void)m.fetch32(0xB2000010u); }, "instruction fetch from device registers");
    device_refused([&] { m.write32(0xB0000010u, 0x83u); }, "GS video sync is held (SMODE1.SINT = 1)");
    m.write32(0xB0000810u, 0x80u); // BUSCLK timers do not depend on the GS

    cycles = 1000u;
    m.write64(0xB2000010u, kCmodNtsc);
    m.write32(0xB0000010u, 0x83u);
    cycles = 1000u + kNtscLine - 1u;
    require(m.read32(0xB0000000u) == 0u, "no HBLANK before one line after the sync restart");
    cycles = 1000u + kNtscLine;
    require(m.read32(0xB0000000u) == 1u, "first HBLANK one line after the sync restart");
    // (Retiming while T0 counts HBLANK re-anchors the count: native-m27-iop.)
    m.write64(0xB2000010u, kCmodNtsc);           // same line length: no retiming, allowed
    m.write64(0xB2000040u, m20::kM22Synch1);     // sync registers do not retime
    cycles = 1000u + 3u * kNtscLine;
    require(m.read32(0xB0000000u) == 3u, "phase kept while counting");

    m.write32(0xB0000010u, 0u);
    m.write32(0xB0000000u, 0u);
    cycles = 50000u;
    m.write64(0xB2000010u, kCmodPal);
    m.write32(0xB0000010u, 0x83u);
    cycles = 50000u + 2u * kPalLine - 1u;
    require(m.read32(0xB0000000u) == 1u, "PAL line length");
    cycles += 1u;
    require(m.read32(0xB0000000u) == 2u, "PAL HBLANK edge");
    m.write32(0xB0000010u, 0u);
    m.write64(0xB2000010u, 0u);
    device_refused([&] { m.write32(0xB0000010u, 0x83u); }, "SMODE1.CMOD = 0 is not modeled");

    // DEV9 0x1F801470/0x1F801472: 16-bit write latches, reads refused.
    m.write16(0xBF801472u, 1u);
    m.write16(0xBF801470u, 0u);
    device_refused([&] { (void)m.read16(0xBF801472u); }, "DEV9 0x1F801472 read");
    device_refused([&] { (void)m.read16(0xBF801470u); }, "DEV9 0x1F801470 read");
    device_refused([&] { m.write32(0xBF801470u, 0u); }, "16-bit stores only");
    device_refused([&] { m.write16(0xBF801474u, 0u); }, "IOP registers): device not implemented (no register model)");

    // EE SIO: polled transmit, setup latched, everything else refused.
    require(m.read32(0xB000F130u) == 0u, "SIO_ISR: TX FIFO never full");
    device_refused([&] { (void)m.read8(0xB000F130u); }, "SIO_ISR read of 1 byte(s)");
    device_refused([&] { (void)m.read32(0xB000F110u); }, "only SIO_ISR is modeled");
    device_refused([&] { (void)m.read8(0xB000F1C0u); }, "only SIO_ISR is modeled");
    m.write32(0xB000F100u, 3u);
    m.write32(0xB000F140u, 0u);
    m.write32(0xB000F150u, 0x10u);
    m.write32(0xB000F120u, 0u);
    device_refused([&] { m.write32(0xB000F120u, 1u); }, "SIO interrupts need the INTC");
    device_refused([&] { m.write16(0xB000F100u, 3u); }, "EE SIO setup write of 2 byte(s)");
    device_refused([&] { m.write32(0xB000F180u, 'x'); }, "SIO TX FIFO write of 4 byte(s)");
    device_refused([&] { m.write32(0xB000F110u, 0u); }, "SIO): device not implemented (no register model)");
    m.write8(0xB000F180u, 'o');
    m.write8(0xB000F180u, 'k');
    require(m.sio_output() == "ok", "SIO TX bytes in order");

    // MCH / RDRAM controller: offsets the BIOS never touches stay refused, named
    // (MCH_RICM/MCH_DRD since M2.3, the control latches since M2.5: boot_m23/m25).
    device_refused([&] { m.write32(0xB000F470u, 0u); }, "MCH (RDRAM controller) register 0x1000F470 is not modeled");
    device_refused([&] { (void)m.read32(0xB000F4E0u); }, "register 0x1000F4E0 is not modeled");
    device_refused([&] { (void)m.read32(0xB000F460u); }, "MCH 0x1000F460 read before any write");

    m.reset_for_boot(rom);
    require(m.sio_output().empty() && !m.gs().latched(kGsSmode1) && !m.gs().latched(kGsSynch1),
            "reset clears SIO output and GS registers");
}

void check_fpu(const std::vector<std::uint8_t>& rom) {
    const auto lwc1 = try_decode_r5900(0u, 0xC5020010u);
    require(lwc1 && lwc1->opcode == MipsOpcode::Lwc1 && lwc1->rs == 8u && lwc1->rt == 2u, "LWC1 $f2, 0x10(t0)");
    const auto swc1 = try_decode_r5900(0u, 0xE5020014u);
    require(swc1 && swc1->opcode == MipsOpcode::Swc1, "SWC1 $f2, 0x14(t0)");
    require(!mips_aot_supported(MipsOpcode::Lwc1) && !mips_aot_supported(MipsOpcode::Swc1),
            "COP1 transfers run in the interpreter");

    EEState s = ee_reset_state();
    Memory m;
    m.reset_for_boot(rom);
    m.set_clock(&s.retired_instructions);
    m.set_translation_context(0, true);
    m20::Assembler a(0x80001000u);
    a.lui(m20::t0, 0x8000u);
    a.lwc1(2u, 0x2000, m20::t0);
    a.swc1(2u, 0x2004, m20::t0);
    const auto code = a.finish();
    for (std::size_t n = 0; n < code.size(); ++n) m.write32(0x80001000u + 4u * static_cast<std::uint32_t>(n), code[n]);
    m.write32(0x80002000u, 0xCAFEF00Du);
    s.pc = 0x80001000u;
    std::uint64_t executed = 0;
    ExecutionContext ctx{s, m, executed, 100u, 0xFFFFFFFFu, nullptr};
    interpret_instruction(ctx);
    try {
        interpret_instruction(ctx);
        throw std::runtime_error("LWC1 with CU1 = 0 was not stopped");
    } catch (const GuestStop& stop) {
        require(stop.kind == StopKind::GuestException &&
                std::string(stop.what()).find("LWC1 with Status.CU1 = 0: coprocessor unusable") != std::string::npos,
                std::string("coprocessor unusable: ") + stop.what());
    }
    require(s.fpr[2] == 0u, "no load without CU1");
    s.set_cp0(Cop0::Status, s.cp0(Cop0::Status) | kStatusCU1);
    s.pc = 0x80001004u;
    interpret_instruction(ctx);
    require(s.fpr[2] == 0xCAFEF00Du, "LWC1 raw word");
    interpret_instruction(ctx);
    require(m.read32(0x80002004u) == 0xCAFEF00Du, "SWC1 raw word");
}

void check_results(const EEState& state, const Memory& memory, const std::string& run) {
    const auto first_line = memory.read32(m20::kM22FirstLineResult);
    require(first_line + 8u >= kNtscLine && first_line <= kNtscLine + 8u,
            run + ": first HBLANK one NTSC line after the sync restart, measured with Count: " +
                std::to_string(first_line));
    require(memory.read32(m20::kM22T0Result) == 1u, run + ": T0 counted one HBLANK from 0");
    require(memory.read32(m20::kM22FpuResult) == m20::kTable[2] && state.fpr[1] == m20::kTable[2],
            run + ": LWC1/SWC1 moved SYNDATA[2]");
    const auto& gs = memory.gs();
    require(gs.latched(kGsSmode1) == m20::kM22Smode1Run && gs.latched(kGsSmode2) == m20::kM22Smode2 &&
            gs.latched(kGsSrfsh) == m20::kM22Srfsh && gs.latched(kGsSynch1) == m20::kM22Synch1 &&
            gs.latched(kGsSynch2) == m20::kM22Synch2 && gs.latched(kGsSyncv) == m20::kM22Syncv,
            run + ": GS registers as written");
    require(gs.latched(kGsCsr) == 0x1Fu, run + ": last CSR write");
    require(gs.line_clock().period == kNtscLine, run + ": NTSC line clock running");
    require(memory.sio_output() == m20::kM22SioText, run + ": SIO output");
    require((state.cp0(Cop0::Status) & kStatusCU1) == 0u, run + ": CU1 cleared again");
}

// Constant LUI/ORI/JR: one provable site, four that must stay runtime-checked.
void check_constant_jumps(const BiosImage& bios, const m20::Firmware& fw) {
    const auto graph = analyze_control_flow(bios_code_image(bios), {}, {}, true);
    require(graph.constant_jumps.size() == 1u, "exactly one constant JR proven");
    const auto& jump = graph.constant_jumps.front();
    require(jump.jump_site == fw.at("m22_const_jr") && jump.lui_site == fw.at("m22_const_jr") - 8u &&
            jump.target == fw.at("m22_const_target"), "constant JR site, LUI and target");
    require(graph.indirect_entries.contains(jump.target), "constant target is a verified indirect entry");
    require(std::any_of(graph.resolved_indirect_targets.begin(), graph.resolved_indirect_targets.end(),
                        [&](const AotResolvedTarget& t) {
                            return t.source == jump.jump_site && t.target == jump.target &&
                                   t.evidence == "constant LUI/ORI JR";
                        }), "constant target evidence");
    auto unresolved = [&](const char* site, const std::string& reason) {
        require(std::any_of(graph.unresolved_jumps.begin(), graph.unresolved_jumps.end(),
                            [&](const AotUnresolvedJump& j) {
                                return j.site == fw.at(site) && j.reason.find(reason) != std::string::npos;
                            }), std::string(site) + " unresolved: " + reason);
    };
    unresolved("m22_rewritten_jr", "not a constant target: JR register rewritten by Addu at 0x" +
                                       hex8(fw.at("m22_rewritten_jr") - 4u));
    unresolved("m22_entered_jr", "constant target window is entered at 0x" + hex8(fw.at("m22_entered_jr")) +
                                     ", bypassing the LUI");
    unresolved("m22_slot_jr", "LUI at 0x" + hex8(fw.at("m22_slot_jr") - 8u) + " is a branch delay slot");
    unresolved("m22_outside_jr", "constant target 0x80200000 is not code in the image");
    require(graph.unresolved_jumps.size() == 4u, "no other unresolved JR");
    const auto& module = aot_module();
    require(find_aot_block(module, fw.at("m22_const_target")) != nullptr, "constant target translated");
}

void check_boot(const std::vector<std::uint8_t>& rom, const m20::Firmware& fw) {
    const auto bios = load_bios_rom0(rom);
    const auto& module = aot_module();
    require(std::string(module.source_kind) == "bios-rom0" && bios.sha256 == module.source_sha256,
            "AOT module provenance");
    check_constant_jumps(bios, fw);
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
        for (const char* line : {"  M W32 v=B0000010 p=10000010 EE timers = 00000000",
                                 "  M W32 v=B2001000 p=12001000 GS privileged registers = 00000200",
                                 "  M W64 v=B2000010 p=12000010 GS privileged registers = 0000000000034000",
                                 "  M W64 v=B2000060 p=12000060 GS privileged registers = 0000002400000789",
                                 "  M W32 v=B2001000 p=12001000 GS privileged registers = 0000001F",
                                 "  M W64 v=B2000010 p=12000010 GS privileged registers = 0000000000004000",
                                 "  M W16 v=BF801472 p=1F801472 IOP registers = 0001",
                                 "  M R32 v=B000F130 p=1000F130 SIO = 00000000",
                                 "  M W8 v=B000F180 p=1000F180 SIO = 4D",
                                 "  M W32 v=80000308 p=00000308 main RAM = 0F1E2D3C", // SWC1 of $f1
                                 "  S MTC0 Status <- 0x30400004"})
            require(contains(report->trace, line), name + " trace: " + line);
        require(report->sio_output == m20::kM22SioText, name + " report SIO output");
    }
    check_results(interpreted_state, interpreted_memory, "interpreter");
    check_results(hybrid_state, hybrid_memory, "hybrid");
    require(interpreted.aot_instructions == 0u, "interpreter-only run");
    require(contains(hybrid.trace, " int ") && hybrid.aot_instructions > 0u, "hybrid used both engines");
    for (const char* op : {" Lwc1", " Swc1"})
        require(std::any_of(hybrid.trace.begin(), hybrid.trace.end(), [&](const std::string& line) {
                    return line.starts_with("I ") && line.find(" int ") != std::string::npos && line.ends_with(op);
                }), std::string("hybrid ran") + op + " in the interpreter");
    require(std::any_of(hybrid.trace.begin(), hybrid.trace.end(), [&](const std::string& line) {
                return line.starts_with("I ") && line.find(" aot ") != std::string::npos && line.ends_with(" Sb");
            }), "hybrid ran the SIO TX stores in AOT blocks");
    for (const char* site : {"m22_const_jr", "m22_rewritten_jr", "m22_entered_jr", "m22_slot_jr"})
        require(contains(hybrid.trace, " aot " + hex8(fw.at(site)) + " "), std::string("hybrid ran ") + site + " in AOT");
    require(contains(hybrid.trace, " aot " + hex8(fw.at("m22_const_target")) + " "),
            "hybrid entered the constant target in an AOT block");
    require(hybrid.retired == interpreted.retired, "same retired instruction count");
    require(normalized(hybrid.trace) == normalized(interpreted.trace), "hybrid and interpreter traces agree");
    for (unsigned n = 0; n < 32; ++n)
        require(hybrid_state.gpr[n].low == interpreted_state.gpr[n].low &&
                hybrid_state.gpr[n].high == interpreted_state.gpr[n].high, "same GPRs");
    require(hybrid_state.fpr == interpreted_state.fpr, "same FPRs");
    require(hybrid_state.cop0 == interpreted_state.cop0 && hybrid_state.count() == interpreted_state.count(),
            "same COP0 and Count");
    require(hybrid_memory.read32(m20::kM22FirstLineResult) == interpreted_memory.read32(m20::kM22FirstLineResult),
            "same HBLANK measurement");
    EEState again_state;
    Memory again_memory;
    require(run(&module, again_state, again_memory).trace == hybrid.trace, "deterministic trace");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: ps2n-boot-m22 <synthetic-rom0-m22.bin>");
        const auto rom = read_file(std::filesystem::path(argv[1]));
        const auto fw = m20::build_firmware(m20::Stage::M22);
        require(rom == fw.rom, "fixture file matches the synthetic M2.2 firmware definition");
        require(m20::build_firmware(m20::Stage::M21).rom != fw.rom, "M2.2 variant differs from the M2.1 firmware");
        check_gs();
        check_bus(rom);
        check_fpu(rom);
        check_boot(rom, fw);
        std::cout << "M2.2 boot: GS privileged registers, line clock, DEV9, EE SIO, COP1 transfers, MCH refusal "
                     "and hybrid checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "boot_m22: " << error.what() << '\n';
        return 1;
    }
}
