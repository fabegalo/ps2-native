// M2.7: VBLANK from the GS vertical timing, the deterministic EE/IOP ordering
// and the SIF registers, on synthetic programs only (no Sony content): GS
// vertical clock decoding (NTSC/PAL, held, unknown), IOP I_STAT bits 0/11 at
// the field edges with and without interrupts, mode changes, R3000A interrupt
// entry with RFE, values with partly known bits (AND/BEQ, SIF_BD6), the SIF
// semaphores and mailboxes from both sides, and the EE devices the kernel
// reaches after the GS reset (GIF, DEV9_R_REV, HBLANK timers across it).
#include "bios_program.hpp"
#include "boot_checks.hpp"

#include "ps2native/core.hpp"

#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ps2native;
using namespace boot_checks;
using namespace m20;

constexpr unsigned k1 = 27, s1 = 17, s2 = 18, s3 = 19;
constexpr unsigned kEpc = 14;
constexpr std::uint32_t kRfe = 0x42000010u;
constexpr unsigned kAnd = 0x24u, kNor = 0x27u, kSrl = 0x02u;

// SIF registers, EE side (ps2tek).
constexpr std::uint32_t kSifMscom = 0x1000F200u, kSifSmcom = 0x1000F210u, kSifMsflg = 0x1000F220u,
                        kSifSmflg = 0x1000F230u, kSifCtrl = 0x1000F240u;
// The BIOS's NTSC SMODE1/SYNCV (525 half-lines, 480 displayed) and the same
// SMODE1 with CMOD = 3 (PAL), SINT = 1 (held) and CMOD = 0.
constexpr std::uint64_t kSmode1Ntsc = 0x0000000740814504ull;
constexpr std::uint64_t kSmode1Pal = kSmode1Ntsc | (1ull << kSmode1CmodShift);
constexpr std::uint64_t kSmode1Held = kSmode1Ntsc | kSmode1Sint;
constexpr std::uint64_t kSmode1Cmod0 = kSmode1Ntsc & ~(3ull << kSmode1CmodShift);
constexpr std::uint64_t kSyncvNtsc = 0x00C7800601A01801ull;
constexpr std::uint64_t kNtscHalfLine = kBusCyclesPerNtscLine;   // EE cycles (2 per bus cycle, half a line)
constexpr std::uint64_t kPalHalfLine = kBusCyclesPerPalLine;
constexpr std::uint64_t kField = 525u, kBlank = 45u;

const ConsoleHardware kProfile{"SYNTHETIC", "TEST-BOARD", "0000AC00000000", 0x00004111u,
                               ProfileEvidence::Measured, "iop_m27 synthetic profile", 0x30u,
                               "iop_m27 synthetic profile",
                               MechaconInfo{{0u, 6u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u}, NvmLayout::Dragon}, 0x35u};

std::vector<std::uint8_t> rom_of(Assembler& a, const std::vector<std::uint32_t>& handler = {}) {
    const auto words = a.finish();
    std::vector<std::uint8_t> rom(4u * 1024u * 1024u);
    const auto put = [&](std::size_t offset, std::uint32_t word) {
        for (unsigned b = 0; b < 4u; ++b) rom[offset + b] = static_cast<std::uint8_t>(word >> (8u * b));
    };
    for (std::size_t i = 0; i < words.size(); ++i) put(4u * i, words[i]);
    require(handler.empty() || words.size() * 4u <= 0x180u, "program overlaps the BEV exception vector");
    for (std::size_t i = 0; i < handler.size(); ++i) put(0x180u + 4u * i, handler[i]);
    return rom;
}

std::uint64_t index_of(const Assembler& a, const std::string& label) { return (a.at(label) - 0xBFC00000u) / 4u; }

void require_stop(const Iop& iop, const std::string& text) {
    require(iop.stopped() && iop.stop_reason()->find(text) != std::string::npos,
            "IOP stop '" + text + "': " + iop.stop_reason().value_or("(running)"));
}

void refuses(const std::function<void(Assembler&)>& body, const std::string& text) {
    Assembler a(0xBFC00000u);
    a.lui(t1, 0xBF80u);
    a.lui(t3, 0xBD00u);
    body(a);
    a.nop();
    a.nop();
    const auto rom = rom_of(a);
    Iop iop;
    iop.reset(rom, &kProfile);
    iop.run_until(64u);
    require_stop(iop, text);
}

GsRegisters video(std::uint64_t smode1, std::optional<std::uint64_t> syncv) {
    GsRegisters gs;
    gs.reset(0u);
    gs.write(kGsSmode1, 8u, smode1, 0u, {});
    if (syncv) gs.write(kGsSyncv, 8u, *syncv, 0u, {});
    return gs;
}

// First IOP cycle whose end (cycle * 8 EE cycles) is at or after `ee`.
std::uint64_t iop_cycle_at(std::uint64_t ee) { return (ee + kEeCyclesPerIopCycle - 1u) / kEeCyclesPerIopCycle; }

void check_vertical_clock() {
    using State = GsRegisters::VerticalClock::State;
    const auto ntsc = video(kSmode1Ntsc, kSyncvNtsc).vertical_clock();
    require(ntsc.state == State::Running && ntsc.field == kField && ntsc.blank == kBlank &&
                ntsc.half_line == kNtscHalfLine && ntsc.epoch == 0u,
            "NTSC SYNCV: 525 half-lines, 45 blank");
    require(ntsc.next_edge(0u, false) == 0u && ntsc.next_edge(1u, false) == kField * kNtscHalfLine,
            "VBLANK start at the epoch, then one field later");
    require(ntsc.next_edge(0u, true) == kBlank * kNtscHalfLine &&
                ntsc.next_edge(kBlank * kNtscHalfLine + 1u, true) == (kField + kBlank) * kNtscHalfLine,
            "VBLANK end after the blank half-lines");
    const auto pal = video(kSmode1Pal, kSyncvNtsc).vertical_clock();
    require(pal.state == State::Running && pal.half_line == kPalHalfLine, "PAL line length");

    require(video(kSmode1Held, kSyncvNtsc).vertical_clock().state == State::Held, "SINT holds video sync");
    const auto cmod0 = video(kSmode1Cmod0, kSyncvNtsc).vertical_clock();
    require(cmod0.state == State::Unknown && cmod0.reason.find("CMOD = 0") != std::string::npos, "CMOD 0 is unknown");
    const auto unwritten = video(kSmode1Ntsc, std::nullopt).vertical_clock();
    require(unwritten.state == State::Unknown && unwritten.reason.find("SYNCV not written") != std::string::npos,
            "SYNCV never written");
    const auto no_display = video(kSmode1Ntsc, kSyncvNtsc & ((1ull << 42) - 1u)).vertical_clock();
    require(no_display.state == State::Unknown && no_display.reason.find("no display") != std::string::npos,
            "VDP = 0");

    // Every change of the vertical timing is visible to the IOP's cache.
    auto gs = video(kSmode1Ntsc, kSyncvNtsc);
    auto generation = gs.timing_generation();
    gs.write(kGsSyncv, 8u, kSyncvNtsc, 10u, {});
    require(gs.timing_generation() == generation, "rewriting the same SYNCV changes nothing");
    gs.write(kGsSyncv, 8u, kSyncvNtsc + 1u, 10u, {});
    require(gs.timing_generation() != generation, "a new SYNCV is a new timing");
    generation = gs.timing_generation();
    gs.write(kGsCsr, 8u, kCsrReset, 20u, {});
    require(gs.timing_generation() != generation && gs.vertical_clock().state == State::Unknown,
            "GS reset: SYNCV back to its unknown power-on value");
}

// Polls I_STAT with interrupts disabled, counting VBLANK starts (RAM 0x100)
// and ends (0x104) and acknowledging only the bits it saw.
std::vector<std::uint8_t> polling_rom() {
    Assembler a(0xBFC00000u);
    a.lui(t1, 0xBF80u);
    a.lui(t2, 0xA000u);
    a.ori(s1, zero, 0u);
    a.ori(s2, zero, 0u);
    a.label("loop");
    a.lw(a0, 0x1070, t1);
    a.nop();
    a.andi(a0, a0, 0x0801u);
    a.andi(a3, a0, 1u);
    a.addu(s1, s1, a3);
    a.r(0u, a0, a3, 11u, kSrl);
    a.addu(s2, s2, a3);
    a.r(a0, zero, a3, 0u, kNor);
    a.sw(a3, 0x1070, t1);         // acknowledge: write 0 to the bits seen
    a.sw(s1, 0x100, t2);
    a.sw(s2, 0x104, t2);
    a.beq(zero, zero, "loop");
    a.nop();
    return rom_of(a);
}

void check_vblank_polling() {
    const auto rom = polling_rom();
    const auto counts = [](const Iop& iop) {
        return std::pair{iop.ee_read(0x100u, 4u), iop.ee_read(0x104u, 4u)};
    };
    const auto end_edge = iop_cycle_at(kBlank * kNtscHalfLine);
    const auto next_start = iop_cycle_at(kField * kNtscHalfLine);
    {
        auto gs = video(kSmode1Ntsc, kSyncvNtsc);
        Iop iop;
        iop.reset(rom, &kProfile);
        iop.run_until(end_edge - 1u, &gs);
        require(counts(iop) == std::pair<std::uint64_t, std::uint64_t>{1u, 0u}, "VBLANK start at the epoch");
        iop.run_until(end_edge + 40u, &gs);
        require(counts(iop) == std::pair<std::uint64_t, std::uint64_t>{1u, 1u}, "VBLANK end 45 half-lines later");
        iop.run_until(next_start - 1u, &gs);
        require(counts(iop) == std::pair<std::uint64_t, std::uint64_t>{1u, 1u}, "nothing during the display");
        iop.run_until(next_start + 40u, &gs);
        require(counts(iop) == std::pair<std::uint64_t, std::uint64_t>{2u, 1u}, "next field");
    }
    {
        // Mode changes at known EE times, as the EE makes them (IOP synced first).
        auto gs = video(kSmode1Ntsc, kSyncvNtsc);
        Iop iop;
        iop.reset(rom, &kProfile);
        iop.run_until(1000u, &gs);
        gs.write(kGsSmode1, 8u, kSmode1Held, 1000u * kEeCyclesPerIopCycle, {});
        iop.run_until(100000u, &gs);
        require(counts(iop) == std::pair<std::uint64_t, std::uint64_t>{1u, 0u}, "held sync: no VBLANK end");
        const std::uint64_t restart = 100000u * kEeCyclesPerIopCycle;
        gs.write(kGsSmode1, 8u, kSmode1Ntsc, restart, {});
        iop.run_until(100040u, &gs);
        require(counts(iop).first == 2u, "restarted lines begin with VBLANK start");
        const auto end = iop_cycle_at(restart + kBlank * kNtscHalfLine);
        iop.run_until(end - 1u, &gs);
        require(counts(iop).second == 0u, "VBLANK end not before its half-line");
        iop.run_until(end + 40u, &gs);
        require(counts(iop).second == 1u, "VBLANK end of the restarted field");
        const auto pal_start = (end + 40u) * kEeCyclesPerIopCycle;
        gs.write(kGsSmode1, 8u, kSmode1Pal, pal_start, {});
        const auto pal_end = iop_cycle_at(pal_start + kBlank * kPalHalfLine);
        iop.run_until(pal_end - 1u, &gs);
        require(counts(iop) == std::pair<std::uint64_t, std::uint64_t>{3u, 1u}, "PAL field start");
        iop.run_until(pal_end + 40u, &gs);
        require(counts(iop).second == 2u, "PAL VBLANK end after 45 PAL half-lines");
        gs.write(kGsSmode1, 8u, kSmode1Cmod0, (pal_end + 40u) * kEeCyclesPerIopCycle, {});
        iop.run_until(pal_end + 100u, &gs);
        require_stop(iop, "I_STAT bits of sources other than the timers, VBLANK and DMA");
    }
    {
        // Without a GS the VBLANK bits are unknown.
        Iop iop;
        iop.reset(rom, &kProfile);
        iop.run_until(100u);
        require_stop(iop, "I_STAT bits of sources other than the timers, VBLANK and DMA");
    }
}

void check_vblank_interrupts() {
    // Handler at 0xBFC00180 (SR.BEV): records EPC, Cause and the VBLANK/DMA
    // bits of I_STAT, acknowledges them and returns to EPC with RFE.
    Assembler h(0xBFC00180u);
    h.lui(k0, 0xA000u);
    h.lw(k1, 0x1F0, k0);
    h.mfc0(t7, kEpc);
    h.mfc0(t6, Cause);
    h.lui(t4, 0xBF80u);
    h.lw(t5, 0x1070, t4);
    h.addu(k1, k1, k0);
    h.andi(t5, t5, 0x0809u);
    h.sw(t7, 0, k1);
    h.sw(t6, 4, k1);
    h.sw(t5, 8, k1);
    h.lw(k1, 0x1F0, k0);
    h.nop();
    h.addiu(k1, k1, 12u);
    h.sw(k1, 0x1F0, k0);
    h.r(t5, zero, t5, 0u, kNor);
    h.sw(t5, 0x1070, t4);
    h.jr(t7);
    h.raw(kRfe);
    const auto handler = h.finish();

    Assembler a(0xBFC00000u);
    a.lui(t1, 0xBF80u);
    a.lui(t2, 0xA000u);
    a.ori(a0, zero, 0x100u);
    a.sw(a0, 0x1F0, t2);
    a.ori(a1, zero, 0x0801u);
    a.sw(a1, 0x1074, t1);         // I_MASK: VBLANK start and end
    a.ori(a2, zero, 1u);
    a.sw(a2, 0x1078, t1);         // I_CTRL
    a.lui(s2, 0x0040u);
    a.ori(s2, s2, 0x0401u);       // BEV, IM2, IEc
    a.mtc0(s2, Status);
    a.label("spin");
    a.ori(s3, zero, 1u);
    a.beq(zero, zero, "spin");
    a.nop();
    const auto rom = rom_of(a, handler);

    const auto records = [](const Iop& iop) { return (iop.ee_read(0x1F0u, 4u) - 0x100u) / 12u; };
    const auto check_record = [&](const Iop& iop, std::uint64_t n, std::uint32_t bits, const std::string& what) {
        const auto base = static_cast<std::uint32_t>(0x100u + 12u * n);
        const auto epc = static_cast<std::uint32_t>(iop.ee_read(base, 4u));
        const auto cause = static_cast<std::uint32_t>(iop.ee_read(base + 4u, 4u));
        require((cause & 0x7Cu) == 0u && (cause & (1u << 10)) != 0u, what + ": interrupt with Cause.IP2");
        const bool delay_slot = (cause & 0x80000000u) != 0u;
        require(delay_slot ? epc == a.at("spin") + 4u : epc >= a.at("spin") && epc < a.at("spin") + 12u,
                what + ": EPC in the spin loop (the branch when BD is set)");
        require(iop.ee_read(base + 8u, 4u) == bits, what + ": I_STAT bits");
    };
    auto gs = video(kSmode1Ntsc, kSyncvNtsc);
    Iop iop;
    iop.reset(rom, &kProfile);
    const auto end_edge = iop_cycle_at(kBlank * kNtscHalfLine);
    iop.run_until(end_edge - 1u, &gs);
    require(!iop.stopped(), "VBLANK interrupts are modeled: " + iop.stop_reason().value_or(""));
    require(records(iop) == 1u, "one interrupt: VBLANK start");
    check_record(iop, 0u, 0x001u, "VBLANK start");
    iop.run_until(end_edge + 60u, &gs);
    require(records(iop) == 2u, "second interrupt: VBLANK end");
    check_record(iop, 1u, 0x800u, "VBLANK end");
    iop.run_until(iop_cycle_at(kField * kNtscHalfLine) + 60u, &gs);
    require(records(iop) == 3u, "third interrupt: next field");
    check_record(iop, 2u, 0x001u, "next VBLANK start");

    // Interrupts enabled while the vertical timing is unknown stop the IOP.
    auto unknown = video(kSmode1Ntsc, std::nullopt);
    Iop blocked;
    blocked.reset(rom, &kProfile);
    blocked.run_until(100u, &unknown);
    require_stop(blocked, "VBLANK: GS SYNCV not written since reset");
}

// AND and BEQ on values with unknown bits; SIF_BD6; the DMA IRQ bit.
void check_partial_values() {
    Assembler a(0xBFC00000u);
    a.lui(t1, 0xBF80u);
    a.lui(t2, 0xA000u);
    a.lui(t3, 0xBD00u);
    a.lw(a0, 0x60, t3);           // SIF_BD6: bits 12-31 known 0
    a.lui(a1, 0xFFFFu);
    a.ori(a1, a1, 0xF000u);
    a.r(a0, a1, a2, 0u, kAnd);    // known: SIFMAN's residency test
    a.sw(a2, 0x100, t2);
    a.lw(a3, 0x1070, t1);         // I_STAT: timers and DMA known, the rest not
    a.ori(t4, zero, 0x78u);
    a.ori(t6, zero, 0x08u);
    a.r(a3, t4, t5, 0u, kAnd);    // known 0 (no timer started, no DMA)
    a.sw(t5, 0x104, t2);
    a.beq(a3, t6, "bad");         // decided: the known bit 3 differs
    a.nop();
    a.ori(s1, zero, 0x77u);
    a.sw(s1, 0x108, t2);
    a.label("checkpoint");
    a.beq(a3, zero, "bad");       // undecidable: only unknown bits could differ
    a.nop();
    a.label("bad");
    a.nop();
    const auto rom = rom_of(a);
    Iop iop;
    iop.reset(rom, &kProfile);
    iop.run_until(index_of(a, "checkpoint"));
    require(!iop.stopped(), "partial values: " + iop.stop_reason().value_or(""));
    require(iop.ee_read(0x100u, 4u) == 0u, "BD6 & 0xFFFFF000 is known");
    require(iop.ee_read(0x104u, 4u) == 0u, "timer and DMA bits of I_STAT are known 0");
    require(iop.ee_read(0x108u, 4u) == 0x77u, "BEQ decided by a known differing bit");
    iop.run_until(index_of(a, "checkpoint") + 4u);
    require_stop(iop, "I_STAT bits of sources other than the timers, VBLANK and DMA");

    refuses([](Assembler& p) { p.lw(a0, 0x60, t3); p.nop(); p.addu(v0, a0, zero); },
            "SIF_BD6 bits 0-11 are not established");
    refuses([](Assembler& p) { p.lhu(a0, 0x60, t3); }, "only 32-bit reads are modeled");
    refuses([](Assembler& p) { p.lui(a0, 0x4100u); p.ori(a0, a0, 0x0300u); p.sw(a0, 0x1538, t1); },
            "only destination chain mode without TTE supported");
}

// SIF semaphores and mailboxes between an EE (EeRegisters) and the IOP.
void check_sif() {
    Assembler a(0xBFC00000u);
    a.lui(t2, 0xA000u);
    a.lui(t3, 0xBD00u);
    a.ori(s1, zero, 0u);
    a.label("poll");               // SIFMAN-like wait for MSFLG 0x10000
    a.lw(a0, 0x20, t3);
    a.addiu(s1, s1, 1u);
    a.r(0u, a0, a0, 16u, kSrl);
    a.andi(a0, a0, 1u);
    a.beq(a0, zero, "poll");
    a.nop();
    a.lw(a1, 0, t3);              // MSCOM from the EE
    a.lui(a2, 0x0001u);
    a.sw(a2, 0x20, t3);           // clear MSFLG 0x10000
    a.sw(s1, 0x100, t2);
    a.sw(a1, 0x104, t2);
    a.sw(a1, 0x10, t3);           // SMCOM = MSCOM
    a.sw(a2, 0x30, t3);           // SMFLG |= 0x10000
    a.label("done");
    a.beq(zero, zero, "done");
    a.nop();
    const auto rom = rom_of(a);

    const auto run = [&](std::uint64_t write_cycle) {
        EeRegisters registers;
        std::uint64_t ee = 0;
        registers.set_clock(&ee);
        registers.reset(&kProfile, rom);
        require(registers.read(kSifSmflg, 4u) == 0u && registers.read(kSifMsflg, 4u) == 0u,
                "SIF flags clear at power-on (hypothesis)");
        rejects([&] { (void)registers.read(kSifMscom, 4u); }, "SIF_MSCOM not written since reset");
        rejects([&] { (void)registers.read(kSifSmcom, 4u); }, "SIF_SMCOM not written by the IOP");
        ee = write_cycle * kEeCyclesPerIopCycle;
        registers.write(kSifMscom, 4u, 0xCAFE0010u);
        require(registers.iop().cycle() == write_cycle, "an EE SIF write first brings the IOP to the EE time");
        registers.write(kSifMsflg, 4u, 0x10000u);
        require(registers.read(kSifMsflg, 4u) == 0x10000u, "the EE sets MSFLG bits");
        ee += 200u * kEeCyclesPerIopCycle;
        require(registers.read(kSifMsflg, 4u) == 0u, "the IOP cleared MSFLG");
        require(registers.read(kSifSmflg, 4u) == 0x10000u, "the IOP set SMFLG");
        require(registers.read(kSifSmcom, 4u) == 0xCAFE0010u, "SMCOM written by the IOP");
        require(registers.read(kIopRamEePhysical + 0x104u, 4u) == 0xCAFE0010u, "the IOP read MSCOM");
        registers.write(kSifSmflg, 4u, 0x10000u);
        require(registers.read(kSifSmflg, 4u) == 0u, "the EE clears SMFLG bits");
        ee += 8u;
        registers.write(kGsSmode1, 8u, kSmode1Ntsc);
        require(registers.iop().cycle() == write_cycle + 201u, "a GS write first brings the IOP to the EE time");
        rejects([&] { registers.write(kSifSmcom, 4u, 0u); }, "written by the IOP only");
        rejects([&] { registers.write(kSifCtrl, 4u, 0u); }, "not modeled");
        rejects([&] { (void)registers.read(kSifCtrl, 4u); }, "not modeled");
        rejects([&] { registers.write(kSifMsflg, 2u, 0u); }, "32-bit registers only");
        return registers.read(kIopRamEePhysical + 0x100u, 4u);
    };
    // The IOP's polls before the EE write (instruction index < write cycle)
    // read 0; the first poll at or after it sees the flag.
    const auto first_poll = index_of(a, "poll");
    for (const std::uint64_t write_cycle : {1003u, 1004u, 1009u}) {
        const auto polls = (write_cycle - first_poll + 5u) / 6u + 1u;
        require(run(write_cycle) == polls, "IOP polls of MSFLG before the EE write at cycle " +
                                               std::to_string(write_cycle));
    }

    refuses([](Assembler& p) { p.sw(zero, 0, t3); }, "SIF_MSCOM is written by the EE only");
    refuses([](Assembler& p) { p.lw(a0, 0x10, t3); p.nop(); p.addu(v0, a0, zero); }, "before any write");
    refuses([](Assembler& p) { p.lhu(a0, 0x20, t3); }, "only 32-bit SIF accesses");
}

// EE devices the kernel reaches after the GS reset.
void check_ee_devices() {
    EeRegisters registers;
    std::uint64_t ee = 0;
    registers.set_clock(&ee);
    registers.reset(&kProfile);
    require(registers.read(kDev9Revision, 1u) == 0x35u, "EE DEV9_R_REV from the console profile");
    rejects([&] { (void)registers.read(kDev9Revision, 2u); }, "8-bit read only");
    registers.write(0x10003000u, 4u, 1u);       // GIF_CTRL reset
    registers.write(0x10003000u, 4u, 8u);       // temporary stop
    registers.write(0x10003010u, 4u, 5u);       // GIF_MODE
    rejects([&] { registers.write(0x10003000u, 4u, 2u); }, "only bits 0 (reset) and 3 (stop) are modeled");
    rejects([&] { registers.write(0x10003010u, 2u, 1u); }, "only 32-bit writes are modeled");
    registers.write(kUndocumented190x + 0x18u, 1u, 0u);
    registers.write(kUndocumented190x + 0x18u, 2u, 1u);
    rejects([&] { (void)registers.read(kUndocumented190x + 0x08u, 1u); }, "before it was cleared");

    // T0 counts HBLANK across the GS reset: old lines up to the reset, new
    // ones from it.
    constexpr std::uint64_t line = 2u * kNtscHalfLine;
    registers.write(0x10000010u, 4u, 0x83u);    // T0_MODE: HBLANK, enabled
    ee = 10u * line + line / 2u;
    require(registers.read(0x10000000u, 4u) == 10u, "ten lines");
    registers.write(kGsCsr, 8u, kCsrReset);
    require(registers.gs().vertical_clock().state == GsRegisters::VerticalClock::State::Unknown,
            "VBLANK unknown after the GS reset");
    ee += 2u * line + line * 7u / 10u;
    require(registers.read(0x10000000u, 4u) == 12u, "two more lines from the reset");

    EeRegisters bare;
    bare.set_clock(&ee);
    bare.reset();
    rejects([&] { (void)bare.read(kDev9Revision, 1u); }, "no console profile with the SSBUS revision");
    rejects([&] { (void)bare.read(kSifMsflg, 4u); }, "after the IOP stopped");
}
} // namespace

int main() {
    try {
        check_vertical_clock();
        check_vblank_polling();
        check_vblank_interrupts();
        check_partial_values();
        check_sif();
        check_ee_devices();
        std::cout << "M2.7 IOP checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
