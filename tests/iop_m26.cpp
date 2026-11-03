// M2.6: the IOP devices EECONF and THREADMAN reach once the MECHACON
// configuration is read, on synthetic programs only (no Sony content): the
// tool board id and DEV9 revision reads, the expansion-bay write, the i.LINK
// bus reset, values with known and unknown bits, the hardware timers raising
// I_STAT, and R3000A exception entry (SYSCALL and timer interrupts) with RFE.
#include "bios_program.hpp"
#include "boot_checks.hpp"

#include "ps2native/core.hpp"

#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ps2native;
using namespace boot_checks;
using namespace m20;

constexpr unsigned k1 = 27, s1 = 17, s2 = 18, s3 = 19;
constexpr unsigned kEpc = 14;
constexpr std::uint32_t kSyscall = 0x0000000Cu, kRfe = 0x42000010u;

const ConsoleHardware kProfile{"SYNTHETIC", "TEST-BOARD", "0000AC00000000", 0x00004111u,
                               ProfileEvidence::Measured, "iop_m26 synthetic profile", 0x30u,
                               "iop_m26 synthetic profile",
                               MechaconInfo{{0u, 6u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u}, NvmLayout::Dragon}, 0x35u};
const ConsoleHardware kNoDev9{"SYNTHETIC", "NO-DEV9", "0000AC00000000", 0x00004111u, ProfileEvidence::Measured,
                              "iop_m26 synthetic profile", 0x30u, "iop_m26 synthetic profile"};

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

bool has_event(const Iop& iop, const std::string& text) {
    for (const auto& event : iop.events())
        if (event.text.find(text) != std::string::npos) return true;
    return false;
}

void require_stop(const Iop& iop, const std::string& text) {
    require(iop.stopped() && iop.stop_reason()->find(text) != std::string::npos,
            "IOP stop '" + text + "': " + iop.stop_reason().value_or("(running)"));
}

// One-instruction program after the register bases: the IOP stops with `text`.
void refuses(const std::function<void(Assembler&)>& body, const ConsoleHardware& profile, const std::string& text) {
    Assembler a(0xBFC00000u);
    a.lui(t1, 0xBF80u);
    a.ori(t3, t1, 0x8400u);
    body(a);
    a.nop();
    a.nop();
    const auto rom = rom_of(a);
    Iop iop;
    iop.reset(rom, &profile);
    iop.run_until(64u);
    require_stop(iop, text);
}

void check_devices() {
    Assembler a(0xBFC00000u);
    a.lui(t1, 0xBF80u);           // IOP registers (KSEG1)
    a.lui(t2, 0xA000u);           // IOP RAM (KSEG1)
    a.ori(t3, t1, 0x8400u);       // i.LINK link controller
    a.lbu(a0, 0x3204, t1);        // tool board id
    a.lbu(a1, 0x146E, t1);        // DEV9_R_REV
    a.lw(a2, 0x7C, t3);           // i.LINK register 0x7C
    a.nop();
    a.sw(a0, 0x100, t2);
    a.sw(a1, 0x104, t2);
    a.sw(a2, 0x108, t2);
    a.lui(t4, 0xB600u);
    a.sh(zero, 0xA, t4);          // expansion-bay register
    a.lui(a3, 0x417Fu);
    a.sw(a3, 0x14, t3);           // PHY register 1 = 0x7F: bus reset
    a.lw(t5, 0, t3);              // NodeID: only bit 0 known
    a.nop();
    a.andi(t6, t5, 1u);           // known: masks every unknown bit
    a.sw(t6, 0x10C, t2);
    a.sw(t5, 0x110, t2);          // stored as unknown
    a.addiu(t7, zero, 0xFFFFu);
    a.sw(t7, 0x20, t3);           // INTR0 acknowledge
    a.label("checkpoint");
    a.andi(v0, t5, 3u);           // bit 1 is unknown
    a.nop();
    const auto rom = rom_of(a);
    Iop iop;
    iop.reset(rom, &kProfile);
    iop.run_until(index_of(a, "checkpoint"));
    require(!iop.stopped(), "devices program runs: " + iop.stop_reason().value_or(""));
    require(iop.ee_read(0x100u, 4u) == 0xFFu, "IOP tool board id: none on a retail console (hypothesis)");
    require(iop.ee_read(0x104u, 4u) == 0x35u, "DEV9_R_REV from the console profile");
    require(iop.ee_read(0x108u, 4u) == 0x10000001u, "i.LINK register 0x7C (hypothesis)");
    require(iop.ee_read(0x10Cu, 4u) == 1u, "NodeID bit 0 after a bus reset, through ANDI");
    rejects([&] { (void)iop.ee_read(0x110u, 4u); }, "has not been written by the IOP");
    require(has_event(iop, "expansion-bay register 0x1600000A = 0x0000"), "expansion-bay event");
    require(has_event(iop, "i.LINK PHY register 1 = 0x7F"), "PHY write event");
    iop.run_until(index_of(a, "checkpoint") + 4u);
    require_stop(iop, "NodeID bits 1-31 after a bus reset are not established");

    refuses([](Assembler& p) { p.lbu(a1, 0x146E, t1); }, kNoDev9, "no console profile with the SSBUS revision");
    refuses([](Assembler& p) { p.lhu(a1, 0x146E, t1); }, kProfile, "only 8-bit reads are modeled");
    refuses([](Assembler& p) { p.lw(a1, 0x3204, t1); }, kProfile, "only 8-bit reads are modeled");
    refuses([](Assembler& p) { p.lw(a1, 0, t3); }, kProfile, "before any bus reset");
    refuses([](Assembler& p) { p.lui(a3, 0x8100u); p.sw(a3, 0x14, t3); }, kProfile, "only PHY register writes");
    refuses([](Assembler& p) { p.sw(zero, 0x20, t3); }, kProfile, "only clearing every bit is modeled");
    refuses([](Assembler& p) { p.lui(t4, 0xB600u); p.sb(zero, 0xA, t4); }, kProfile, "only 16-bit writes");
    refuses([](Assembler& p) { p.lui(t4, 0xB600u); p.lhu(a1, 0xA, t4); }, kProfile, "device not implemented");
}

void check_timers() {
    Assembler a(0xBFC00000u);
    a.lui(t1, 0xBF80u);
    a.lui(t2, 0xA000u);
    // Timer 3 (32-bit, system clock): a mode write resets the count; it then
    // advances once per IOP cycle (one instruction).
    a.sw(zero, 0x1484, t1);
    a.lw(a0, 0x1480, t1);         // 1 tick after the mode write
    a.nop();
    a.sw(a0, 0x100, t2);
    // Timer 4 at 1/8: one tick after eight cycles.
    a.ori(a1, zero, 1u << 13);
    a.sw(a1, 0x1494, t1);
    for (int i = 0; i < 7; ++i) a.nop();
    a.lw(a2, 0x1490, t1);
    a.nop();
    a.sw(a2, 0x104, t2);
    // Timer 5: one-shot compare interrupt at count 3.
    a.ori(a3, zero, 3u);
    a.sw(a3, 0x14A8, t1);
    a.ori(t4, zero, 0x10u);
    a.sw(t4, 0x14A4, t1);         // mode: compare IRQ, bit 10 set by the write
    a.nop();
    a.nop();
    a.nop();
    a.lw(t5, 0x1070, t1);         // I_STAT: timer bits known
    a.nop();
    a.andi(t7, t5, 0x4070u);      // known timer bits (all clear)
    a.lw(s1, 0x14A4, t1);         // mode: compare flag set, bit 10 cleared (one-shot)
    a.lw(s2, 0x14A4, t1);         // the read cleared the flag
    a.sw(t7, 0x108, t2);
    a.sw(s1, 0x10C, t2);
    a.sw(s2, 0x110, t2);
    a.lui(t4, 0xFFFEu);
    a.ori(t4, t4, 0xFFFFu);       // acknowledge bit 16 (write 0)
    a.sw(t4, 0x1070, t1);
    a.label("checkpoint");
    a.addu(v0, t5, zero);         // the whole I_STAT is unknown
    a.nop();
    const auto rom = rom_of(a);
    Iop iop;
    iop.reset(rom, &kProfile);
    iop.run_until(index_of(a, "checkpoint"));
    require(!iop.stopped(), "timer program runs: " + iop.stop_reason().value_or(""));
    require(iop.ee_read(0x100u, 4u) == 1u, "timer 3 count one cycle after the mode write");
    require(iop.ee_read(0x104u, 4u) == 1u, "timer 4 at 1/8: one tick after eight cycles");
    require(iop.ee_read(0x108u, 4u) == 0u, "only timer 5 raised");
    require(iop.ee_read(0x10Cu, 4u) == 0x810u, "timer 5 compare flag set, IRQ bit cleared by the one-shot");
    require(iop.ee_read(0x110u, 4u) == 0x010u, "mode read clears the raised flags");
    iop.run_until(index_of(a, "checkpoint") + 4u);
    require_stop(iop, "I_STAT bits of sources other than the timers");

    refuses([](Assembler& p) { p.ori(a0, zero, 1u); p.sw(a0, 0x1484, t1); }, kProfile, "timer gates are not modeled");
    refuses([](Assembler& p) { p.ori(a0, zero, 0x100u); p.sw(a0, 0x1114, t1); }, kProfile, "HBLANK timer sources");
    refuses([](Assembler& p) { p.ori(a0, zero, 0x10u); p.sw(a0, 0x1104, t1); }, kProfile, "target never written");
    refuses([](Assembler& p) { p.lw(a0, 0x1100, t1); p.nop(); p.addu(v0, a0, zero); }, kProfile,
            "timer count before any write");
}

// The I_STAT bit 16 written by timer 5 ends in RAM 0x100 through the handler.
void check_exceptions() {
    // Handler at 0xBFC00180 (SR.BEV): records EPC, Cause and SR, acknowledges
    // I_STAT, then returns to EPC (interrupt) or EPC + 4 (SYSCALL) with RFE.
    Assembler h(0xBFC00180u);
    h.lui(k0, 0xA000u);
    h.lw(k1, 0x1F0, k0);          // next record offset
    h.mfc0(t7, kEpc);
    h.mfc0(t6, Cause);
    h.mfc0(t5, Status);
    h.addu(k1, k1, k0);
    h.sw(t7, 0, k1);
    h.sw(t6, 4, k1);
    h.sw(t5, 8, k1);
    h.lw(k1, 0x1F0, k0);
    h.nop();
    h.addiu(k1, k1, 12u);
    h.sw(k1, 0x1F0, k0);
    h.lui(t4, 0xBF80u);
    h.sw(zero, 0x1070, t4);       // acknowledge every timer bit
    h.andi(t6, t6, 0x7Cu);
    h.beq(t6, zero, "go");        // interrupt: back to EPC itself
    h.nop();
    h.addiu(t7, t7, 4u);          // SYSCALL: resume after it
    h.label("go");
    h.jr(t7);
    h.raw(kRfe);
    const auto handler = h.finish();

    Assembler a(0xBFC00000u);
    a.lui(t1, 0xBF80u);
    a.lui(t2, 0xA000u);
    a.ori(a0, zero, 0x100u);
    a.sw(a0, 0x1F0, t2);          // records start at RAM 0x100
    a.label("syscall");
    a.raw(kSyscall);
    a.ori(s1, zero, 0x55u);       // resumed here
    a.sw(s1, 0x1E0, t2);
    // Timer 5 compare interrupt, delivered through I_MASK, I_CTRL and SR.
    a.lui(a1, 0x0001u);
    a.sw(a1, 0x1074, t1);         // I_MASK: timer 5
    a.ori(a2, zero, 1u);
    a.sw(a2, 0x1078, t1);         // I_CTRL
    a.ori(a3, zero, 4u);
    a.sw(a3, 0x14A8, t1);
    a.ori(t3, zero, 0x10u);
    a.sw(t3, 0x14A4, t1);
    a.lui(s2, 0x0040u);
    a.ori(s2, s2, 0x0401u);       // BEV, IM2, IEc
    a.mtc0(s2, Status);
    a.label("spin");
    a.ori(s3, zero, 1u);
    a.beq(zero, zero, "spin");
    a.nop();
    const auto rom = rom_of(a, handler);
    Iop iop;
    iop.reset(rom, &kProfile);
    iop.run_until(400u);
    require(!iop.stopped(), "exception program runs: " + iop.stop_reason().value_or(""));
    require(iop.ee_read(0x100u, 4u) == static_cast<std::uint32_t>(0xBFC00000u + 4u * index_of(a, "syscall")),
            "SYSCALL EPC");
    require((iop.ee_read(0x104u, 4u) & 0x8000007Cu) == (8u << 2), "SYSCALL Cause.ExcCode 8, not in a delay slot");
    require(iop.ee_read(0x108u, 4u) == 0x00400000u, "SR in the handler: BEV, kernel, interrupts off");
    require(iop.ee_read(0x1E0u, 4u) == 0x55u, "RFE and resume after the SYSCALL");
    const auto epc = static_cast<std::uint32_t>(iop.ee_read(0x10Cu, 4u));
    require(epc >= a.at("spin") && epc < a.at("spin") + 12u, "interrupt EPC inside the spin loop");
    require((iop.ee_read(0x110u, 4u) & 0x7Cu) == 0u && (iop.ee_read(0x110u, 4u) & (1u << 10)) != 0u,
            "interrupt: ExcCode 0 with Cause.IP2");
    require(iop.ee_read(0x114u, 4u) == 0x00400404u, "SR in the handler: IEc pushed to IEp");
    require(iop.ee_read(0x1F0u, 4u) == 0x118u, "exactly two exceptions (one-shot timer)");

    // An interrupt line that depends on a source that is not modeled.
    Assembler v(0xBFC00000u);
    v.lui(t1, 0xBF80u);
    v.ori(a1, zero, 1u);
    v.sw(a1, 0x1074, t1);         // I_MASK: VBLANK start
    v.sw(a1, 0x1078, t1);
    v.ori(s2, zero, 0x0401u);
    v.mtc0(s2, Status);
    v.nop();
    v.nop();
    const auto vrom = rom_of(v);
    Iop vblank;
    vblank.reset(vrom, &kProfile);
    vblank.run_until(32u);
    require_stop(vblank, "sources that are not modeled (VBLANK start)");
    refuses([](Assembler& p) { p.ori(a0, zero, 1u); p.sw(a0, 0x1078, t1); p.ori(s2, zero, 0x0401u); p.mtc0(s2, Status); },
            kProfile,
            "I_MASK or I_CTRL, never written");
    refuses([](Assembler& p) { p.ori(s2, zero, 0x0100u); p.mtc0(s2, Cause); }, kProfile,
            "software interrupts are not modeled");
}
} // namespace

int main() {
    try {
        check_devices();
        check_timers();
        check_exceptions();
        std::cout << "M2.6 IOP checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
