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
    a.andi(v0, t5, 3u);           // bit 1 remains unknown
    a.addiu(v0, v0, 1);           // consume it, rather than merely transport it
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
    refuses([](Assembler& p) { p.lhu(a1,0x146E,t1);p.nop();p.addiu(a1,a1,1); },kProfile,"DEV9_R_REV bits 8..15");
    refuses([](Assembler& p) { p.lw(a1, 0x3204, t1); }, kProfile, "only 8-bit reads are modeled");
    refuses([](Assembler& p) { p.lw(a1, 0, t3); }, kProfile, "before any bus reset");
    refuses([](Assembler& p) { p.lui(a3, 0x8100u); p.sw(a3, 0x14, t3); }, kProfile, "only PHY register writes");
    refuses([](Assembler& p) { p.sw(zero, 0x20, t3); }, kProfile, "only clearing every bit is modeled");
    refuses([](Assembler& p) { p.lui(t4, 0xB600u); p.sb(zero, 0xA, t4); }, kProfile, "only 16-bit writes");
    refuses([](Assembler& p) { p.lui(t4, 0xB600u); p.lhu(a1, 0xA, t4); }, kProfile, "device not implemented");
}

void check_dev9_halfword() {
    Assembler a(0xBFC00000u);a.lui(t1,0xBF80);a.lui(t2,0xA000);
    a.lhu(a1,0x146E,t1);a.nop();a.andi(a1,a1,0xFF);a.sw(a1,0x100,t2);
    a.lhu(a1,0x146E,t1);a.nop();a.r(0,a1,a1,16,0x02);a.sw(a1,0x104,t2);
    a.raw((0x21u<<26)|(t1<<21)|(a1<<16)|0x146Eu);a.nop();a.andi(a1,a1,0xF0);a.sw(a1,0x108,t2);
    a.label("idle");a.beq(0,0,"idle");a.nop();Iop iop;iop.reset(rom_of(a),&kProfile);iop.run_until(100);
    require(!iop.stopped() && iop.ee_read(0x100,4)==0x35 && !iop.ee_read(0x104,4) && iop.ee_read(0x108,4)==0x30,
            "DEV9 byte/halfword aliases retain the measured lane, LHU upper zeros, and masked signed loads without inventing bits 8..15");
    refuses([](Assembler& p){p.lhu(a1,0x146E,t1);},kNoDev9,"no console profile with the SSBUS revision");
}
void check_dev9_power() {
    Assembler a(0xBFC00000u);a.lui(t1,0xBF80);a.lui(t2,0xA000);
    a.lhu(a1,0x146C,t1);a.nop();a.r(0,a1,a1,16,0x02);a.sw(a1,0x100,t2);
    a.lhu(a1,0x146C,t1);a.nop();a.andi(a1,a1,5);a.addiu(a1,a1,1);a.sw(a1,0x104,t2);
    a.label("idle");a.beq(0,0,"idle");a.nop();const auto rom=rom_of(a);
    Iop cold;cold.reset(rom,&kProfile);cold.run_until(100);
    require_stop(cold,"DEV9 POWER reset/control bits have not been established");
    require(cold.diagnostic_ram32(0x100)==0,"unsigned extension is known without inventing any POWER bit");
    for(const auto value:{0u,1u,4u,5u}) {
        Iop iop;iop.reset(rom,&kProfile);iop.diagnostic_dev9_power_reset(static_cast<std::uint16_t>(value));iop.run_until(100);
        require(!iop.stopped() && iop.ee_read(0x104,4)==value+1u,"explicit reset inputs define only independently documented bits 0/2");
        rejects([&]{iop.diagnostic_dev9_power_reset(0);},"before execution");
        iop.reset(rom,&kProfile);iop.run_until(100);require_stop(iop,"DEV9 POWER reset/control bits have not been established");
    }
    refuses([](Assembler& p){p.lhu(a1,0x146C,t1);p.nop();p.andi(a1,a1,2);p.addiu(a1,a1,1);},kProfile,"DEV9 POWER");
    Iop bad;bad.reset(rom,&kProfile);rejects([&]{bad.diagnostic_dev9_power_reset(2);},"only bits 0/2");
    bad.reset(rom,&kNoDev9);rejects([&]{bad.diagnostic_dev9_power_reset(0);},"expansion-bay hardware");
}
void check_speed_identities() {
    Assembler a(0xBFC00000u);a.lui(t1,0xB000);a.lui(t2,0xA000);
    for(const auto offset:{2u,4u,14u}){a.lhu(a1,offset,t1);a.nop();a.sw(a1,offset*4u,t2);}
    a.label("idle");a.beq(0,0,"idle");a.nop();const auto rom=rom_of(a);
    for(const auto name:{"SCPH-70004:GH-032-11","SCPH-70004:GH-032-32"}) {
        const auto* profile=find_console_hardware(name);require(profile && profile->speed_revisions.has_value(),"matching measured board profile exists");
        Iop iop;iop.reset(rom,profile);iop.diagnostic_dev9_power_reset(5);iop.run_until(100);
        require(!iop.stopped() && iop.ee_read(8,4)==0x13 && iop.ee_read(16,4)==0xB && iop.ee_read(56,4)==2,
                "all three complete SPEED halfwords come from the exact board/ROMVER measurements");
        auto alternate=*profile;alternate.speed_revisions=std::array<std::uint16_t,3>{0xA1B2,0xC3D4,0xE5F6};
        iop.reset(rom,&alternate);iop.diagnostic_dev9_power_reset(5);iop.run_until(100);
        require(!iop.stopped() && iop.ee_read(8,4)==0xA1B2 && iop.ee_read(16,4)==0xC3D4 && iop.ee_read(56,4)==0xE5F6,
                "hardware lookup preserves complete profile values rather than hardcoding one revision");
        alternate.speed_revisions.reset();iop.reset(rom,&alternate);iop.diagnostic_dev9_power_reset(5);iop.run_until(100);
        require_stop(iop,"no measured SPEED identity");
        iop.reset(rom,profile);iop.run_until(100);require_stop(iop,"requires established power");
        iop.reset(rom,profile);iop.diagnostic_dev9_power_reset(0);iop.run_until(100);require_stop(iop,"requires established power");
    }
}
void check_speed_interrupt_masking() {
    for(unsigned mode=0;mode<8;++mode) {
        Assembler a(0xBFC00000u);a.lui(t1,0xBF80);a.lui(t2,0xB000);a.lui(t3,0xA000);
        if(mode==2)a.sw(0,0x1070,t1); // An earlier ack cannot prove a later unknown source quiet.
        a.sh(0,0x2A,t2);
        if(mode!=2 && mode!=5) {a.la(a1,mode==3 ? 0xFFFFFFFFu : 0u);a.sw(a1,0x1070,t1);}
        if(mode==1){a.la(a1,4);a.sh(a1,0x2A,t2);}
        if(mode==4 || mode>=6){a.la(a1,4);a.sh(a1,0x146C,t1);a.la(a1,5);a.sh(a1,0x146C,t1);}
        if(mode==7)a.sh(0,0x2A,t2);
        if(mode>=6)a.sw(0,0x1070,t1);
        a.la(a1,1u<<13u);a.sw(a1,0x1074,t1);a.la(a1,1);a.sw(a1,0x1078,t1);
        a.la(a1,0x00400401);a.mtc0(a1,Status);a.nop();
        a.lw(a1,0x1070,t1);a.nop();a.andi(a1,a1,1u<<13u);a.sw(a1,0x100,t3);
        a.label("idle");a.beq(0,0,"idle");a.nop();const auto rom=rom_of(a);
        Iop iop;iop.reset(rom,find_console_hardware("SCPH-70004:GH-032-32"));
        iop.diagnostic_dev9_power_reset(5);iop.diagnostic_dev9_power_settle_cycles(1);
        if(mode==5)iop.diagnostic_dev9_irq_reset_clear();iop.run_until(120);
        if(mode==0 || mode==5 || mode==7)require(!iop.stopped() && iop.ee_read(0x100,4)==0,
            "a real zero SPEED mask followed by INTC ack proves IRQ13 quiet without inventing pending status");
        else require_stop(iop,"sources that are not modeled (PCMCIA)");
    }
}
void check_speed_mask() {
    Assembler a(0xBFC00000u);a.lui(t1,0xB000);a.lui(t2,0xA000);
    a.lhu(a1,0x2A,t1);a.nop();a.andi(a1,a1,0);a.sh(a1,0x2A,t1);
    a.lhu(a1,0x2A,t1);a.nop();a.sw(a1,0x100,t2);
    a.la(a1,0x64);a.sh(a1,0x2A,t1);a.lhu(a1,0x2A,t1);a.nop();a.sw(a1,0x104,t2);
    a.lhu(a1,0x28,t1);a.nop();a.andi(a1,a1,0);a.sw(a1,0x108,t2);
    a.lhu(a1,0x28,t1);a.nop();a.addiu(a1,a1,1);
    const auto rom=rom_of(a);Iop iop;iop.reset(rom,find_console_hardware("SCPH-70004:GH-032-32"));
    iop.diagnostic_dev9_power_reset(5);iop.run_until(200);require_stop(iop,"SPEED interrupt pending/mask bits are not established");
    require(iop.diagnostic_ram32(0x100)==0 && iop.diagnostic_ram32(0x104)==0x64 && iop.diagnostic_ram32(0x108)==0,
            "a real zero mask is established by firmware, later masks retain their bytes, and unknown pending bits are never fabricated");
}
void check_dev9_power_transition() {
    Assembler a(0xBFC00000u);a.lui(t1,0xBF80);a.la(a1,5);a.sh(a1,0x146C,t1);a.label("written");
    a.lhu(a1,0x146C,t1);a.nop();a.andi(a1,a1,5);a.lui(t2,0xA000);a.sw(a1,0x100,t2);
    a.lui(t3,0xB000);a.label("sample");a.lhu(a1,2,t3);a.nop();a.sw(a1,0x104,t2);
    a.label("idle");a.beq(0,0,"idle");a.nop();const auto rom=rom_of(a);
    const auto write=index_of(a,"written")-1u,sample=index_of(a,"sample");
    for(unsigned late:{0u,1u}) {
        Iop iop;iop.reset(rom,find_console_hardware("SCPH-70004:GH-032-32"));
        iop.diagnostic_dev9_power_settle_cycles(static_cast<std::uint32_t>(sample-write+late));iop.run_until(100);
        if(late)require_stop(iop,"requires established power");
        else require(!iop.stopped() && iop.ee_read(0x100,4)==5 && iop.ee_read(0x104,4)==0x13,
                     "guest power writes retain control bits immediately but SPEED access waits for the exact configured boundary");
    }
    Iop cold;cold.reset(rom,find_console_hardware("SCPH-70004:GH-032-32"));cold.run_until(100);
    require_stop(cold,"requires established power");require(cold.diagnostic_ram32(0x100)==5,"a written POWER control does not invent transition latency");
}
void check_dev9_control_experiment() {
    Assembler a(0xBFC00000u);a.lui(t1,0xBF80);a.lui(t2,0xA000);a.la(a1,1);a.sh(a1,0x1466,t1);
    a.la(a1,3);a.sh(a1,0x1464,t1);a.lhu(a1,0x1464,t1);a.nop();a.sw(a1,0x100,t2);
    a.la(a1,2);a.sh(a1,0x1460,t1);a.lhu(a1,0x1460,t1);a.nop();a.sw(a1,0x104,t2);
    a.sh(0,0x1466,t1);a.lhu(a1,0x1466,t1);a.nop();a.sw(a1,0x108,t2);
    a.label("idle");a.beq(0,0,"idle");a.nop();const auto rom=rom_of(a);
    Iop iop;iop.reset(rom,&kProfile);iop.run_until(100);require_stop(iop,"control side effects are unverified");
    iop.reset(rom,&kProfile);iop.diagnostic_dev9_control_latches();iop.run_until(100);
    require(!iop.stopped() && iop.ee_read(0x100,4)==3 && iop.ee_read(0x104,4)==2 && !iop.ee_read(0x108,4),
            "explicit control-latch experiment retains distinct actual guest writes instead of supplying cold constants or device success");
    rejects([&]{iop.diagnostic_dev9_control_latches();},"before execution");
    iop.reset(rom,&kProfile);iop.run_until(100);require_stop(iop,"control side effects are unverified");
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
    a.addiu(v0, t5, 1);          // arithmetic consumes the unknown I_STAT bits
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
    refuses([](Assembler& p) { p.lw(a0, 0x1100, t1); p.nop(); p.addiu(v0, a0, 1); }, kProfile,
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
    require_stop(vblank, "sources that are not modeled (VBLANK start; VBLANK: no GS attached)");
    refuses([](Assembler& p) { p.ori(a0, zero, 1u); p.sw(a0, 0x1078, t1); p.ori(s2, zero, 0x0401u); p.mtc0(s2, Status); },
            kProfile,
            "I_MASK or I_CTRL, never written");
    refuses([](Assembler& p) { p.ori(s2, zero, 0x0100u); p.mtc0(s2, Cause); }, kProfile,
            "software interrupts are not modeled");
}
} // namespace

int main() {
    try {
        check_devices();check_dev9_halfword();check_dev9_power();check_speed_identities();check_speed_mask();check_speed_interrupt_masking();check_dev9_power_transition();check_dev9_control_experiment();
        check_timers();
        check_exceptions();
        std::cout << "M2.6 IOP checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
