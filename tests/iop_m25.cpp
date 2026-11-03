// M2.5: the IOP the real BIOS needs before the EE gets past QueryBootMode(2),
// on synthetic inputs only (no Sony content): a synthetic IOP program at the
// reset vector exercising the load delay slot, POST codes, the interrupt
// controller, DMA interrupt control, the CDVD N status and the MECHACON S
// command interface, unknown-value propagation and the explicit stops; the EE
// view of IOP RAM (run up to the EE time, refused when unwritten or after the
// IOP stopped); the EE INTC mask; the undocumented blocks the EE kernel
// writes; and the MMI logical group PAND/PXOR/POR/PNOR.
#include "bios_program.hpp"
#include "boot_checks.hpp"

#include "ps2native/core.hpp"
#include "ps2native/ee_semantics.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ps2native;
using namespace boot_checks;
using namespace m20;

constexpr unsigned s1 = 17, s2 = 18, s3 = 19, s4 = 20, s5 = 21, s6 = 22, s7 = 23;

const ConsoleHardware kIopProfile{"SYNTHETIC", "TEST-BOARD", "0000AC00000000", 0x00004111u,
                                  ProfileEvidence::Measured, "iop_m25 synthetic profile", 0x30u,
                                  "iop_m25 synthetic profile",
                                  MechaconInfo{{0u, 6u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u}, NvmLayout::Dragon}};
const ConsoleHardware kNoPrid{"SYNTHETIC", "NO-PRID", "0000AC00000000", 0x00004111u,
                              ProfileEvidence::Measured, "iop_m25 synthetic profile"};

struct IopRom {
    std::vector<std::uint8_t> bytes;
    std::uint64_t before_dma;  // instructions retired before the refused CHCR store
};

// The IOP program; results go to IOP RAM 0x100.. (through KSEG1).
IopRom build_iop_rom() {
    Assembler a(0xBFC00000u);
    a.mfc0(t0, PRid);
    a.lui(t1, 0xBF80u);           // IOP registers (KSEG1)
    a.lui(t2, 0xA000u);           // IOP RAM (KSEG1)
    a.ori(a0, zero, 0xABu);
    a.sb(a0, 0x2070, t1);         // POST 0xAB
    a.sw(t0, 0x130, t2);          // PRId
    // Load delay: the instruction after LW still sees the old register.
    a.ori(a1, zero, 0x1234u);
    a.sw(a1, 0x100, t2);
    a.addiu(t3, zero, 7u);
    a.lw(t3, 0x100, t2);
    a.addu(t4, t3, zero);         // old t3 (7)
    a.addu(t5, t3, zero);         // loaded t3 (0x1234)
    a.sw(t4, 0x104, t2);
    a.sw(t5, 0x108, t2);
    // INTC: I_MASK latch; I_CTRL keeps bit 0 only and a read clears it.
    a.ori(a2, zero, 5u);
    a.sw(a2, 0x1074, t1);
    a.lw(s0, 0x1074, t1);
    a.addiu(a3, zero, 0xFFFFu);   // -1: only bit 0 sticks
    a.sw(a3, 0x1078, t1);
    a.lw(s1, 0x1078, t1);
    a.lw(s2, 0x1078, t1);
    a.nop();
    a.sw(s0, 0x10C, t2);
    a.sw(s1, 0x110, t2);
    a.sw(s2, 0x114, t2);
    a.sw(zero, 0x1070, t1);       // I_STAT acknowledge
    // DICR keeps only its writable fields; every flag reads 0.
    a.addiu(v0, zero, 0xFFFFu);
    a.sw(v0, 0x10F4, t1);
    a.lw(s3, 0x10F4, t1);
    a.nop();
    a.sw(s3, 0x118, t2);
    // CDVD N status, then OpenConfig(read, block 0, 4 blocks).
    a.lui(t6, 0xBF40u);
    a.lbu(s4, 0x2005, t6);
    a.nop();
    a.sw(s4, 0x11C, t2);
    a.sb(zero, 0x2017, t6);
    a.sb(zero, 0x2017, t6);
    a.ori(a0, zero, 4u);
    a.sb(a0, 0x2017, t6);
    a.ori(a0, zero, 0x40u);
    a.sb(a0, 0x2016, t6);
    a.lbu(s5, 0x2017, t6);        // 0: a result byte is pending
    a.lbu(s6, 0x2018, t6);        // the OpenConfig status
    a.lbu(s7, 0x2017, t6);        // 0x40: no result left
    a.nop();
    a.sw(s5, 0x120, t2);
    a.sw(s6, 0x124, t2);
    a.sw(s7, 0x128, t2);
    // Never-written RAM: the copy stays unknown.
    a.lw(v1, 0x200, t2);
    a.nop();
    a.sw(v1, 0x12C, t2);
    // A DMA channel start is refused.
    a.lui(v0, 0x0100u);
    a.label("dma_start");
    a.sw(v0, 0x1088, t1);
    const auto before_dma = (a.at("dma_start") - 0xBFC00000u) / 4u;
    const auto words = a.finish();
    std::vector<std::uint8_t> rom(4u * 1024u * 1024u);
    for (std::size_t i = 0; i < words.size(); ++i)
        for (unsigned b = 0; b < 4u; ++b) rom[4u * i + b] = static_cast<std::uint8_t>(words[i] >> (8u * b));
    return {std::move(rom), before_dma};
}

bool has_event(const Iop& iop, const std::string& text) {
    for (const auto& event : iop.events())
        if (event.text.find(text) != std::string::npos) return true;
    return false;
}

void check_iop(const IopRom& rom) {
    Iop iop;
    iop.reset(rom.bytes, &kIopProfile);
    iop.run_until(rom.before_dma);
    require(!iop.stopped(), "the IOP runs up to the DMA start: " + iop.stop_reason().value_or(""));
    require(iop.ee_read(0x130u, 4u) == 0x30u, "PRId from the console profile");
    require(iop.ee_read(0x104u, 4u) == 7u, "load delay slot sees the old register");
    require(iop.ee_read(0x108u, 4u) == 0x1234u, "loaded value after the delay slot");
    require(iop.ee_read(0x10Cu, 4u) == 5u, "I_MASK latch");
    require(iop.ee_read(0x110u, 4u) == 1u, "I_CTRL keeps bit 0");
    require(iop.ee_read(0x114u, 4u) == 0u, "I_CTRL read clears bit 0");
    require(iop.ee_read(0x118u, 4u) == 0x00FF007Fu, "DICR writable fields, flags 0");
    require(iop.ee_read(0x11Cu, 4u) == 0x4Cu, "CDVD N status: MECHA_INIT, ready, DEV9 connected");
    require(iop.ee_read(0x120u, 4u) == 0u && iop.ee_read(0x124u, 4u) == 0u && iop.ee_read(0x128u, 4u) == 0x40u,
            "OpenConfig status byte, then no result");
    rejects([&] { (void)iop.ee_read(0x12Cu, 4u); }, "has not been written by the IOP");
    rejects([&] { (void)iop.ee_read(0x200u, 4u); }, "has not been written by the IOP");
    require(has_event(iop, "POST 0xAB"), "POST event");
    require(has_event(iop, "S command 0x40 (params 0x00 0x00 0x04)"), "S command event");

    iop.run_until(rom.before_dma + 10u);
    require(iop.stopped() && iop.stop_reason()->find("DMA transfers are not modeled") != std::string::npos,
            "CHCR start refused: " + iop.stop_reason().value_or(""));
    rejects([&] { (void)iop.ee_read(0x108u, 4u); }, "read after the IOP stopped");

    Iop no_prid;
    no_prid.reset(rom.bytes, &kNoPrid);
    no_prid.run_until(4u);
    require(no_prid.stopped() && no_prid.stop_reason()->find("PRId") != std::string::npos,
            "PRId without a measured IOP revision stops the IOP");
    Iop no_rom;
    no_rom.reset({}, &kIopProfile);
    require(no_rom.stopped(), "no ROM0: the IOP stays stopped");
}

// ReadConfig with no area opened is refused (with an NVM: native-m26-nvm).
void check_read_config() {
    Assembler a(0xBFC00000u);
    a.lui(t6, 0xBF40u);
    a.ori(a0, zero, 0x41u);
    a.sb(a0, 0x2016, t6);
    const auto words = a.finish();
    std::vector<std::uint8_t> rom(4u * 1024u * 1024u);
    for (std::size_t i = 0; i < words.size(); ++i)
        for (unsigned b = 0; b < 4u; ++b) rom[4u * i + b] = static_cast<std::uint8_t>(words[i] >> (8u * b));
    Iop iop;
    iop.reset(rom, &kIopProfile);
    iop.run_until(10u);
    require(iop.stopped() && iop.stop_reason()->find("ReadConfig without an open configuration area") != std::string::npos,
            "ReadConfig needs OpenConfig: " + iop.stop_reason().value_or(""));
}

void check_ee_view(const IopRom& rom) {
    EeRegisters registers;
    std::uint64_t ee_cycles = 0;
    registers.set_clock(&ee_cycles);
    registers.reset(&kIopProfile, rom.bytes);
    // The IOP runs at one eighth of the EE clock and catches up on each read.
    ee_cycles = rom.before_dma * kEeCyclesPerIopCycle;
    require(registers.read(kIopRamEePhysical + 0x108u, 4u) == 0x1234u, "EE reads IOP RAM at the EE time");
    require(registers.iop().cycle() == rom.before_dma, "IOP caught up to EE time / 8");
    rejects([&] { (void)registers.read(kIopRamEePhysical + 0x200u, 4u); }, "has not been written by the IOP");
    rejects([&] { registers.write(kIopRamEePhysical + 0x100u, 4u, 0u); }, "EE write to IOP RAM is not modeled");
    ee_cycles += 100u * kEeCyclesPerIopCycle;
    rejects([&] { (void)registers.read(kIopRamEePhysical + 0x108u, 4u); }, "read after the IOP stopped");

    // EE INTC: I_MASK reverses written bits; I_STAT reads are refused.
    require(registers.read(kIntcMask, 4u) == 0u, "I_MASK 0 after reset (hypothesis)");
    registers.write(kIntcMask, 4u, 0x5u);
    require(registers.read(kIntcMask, 4u) == 0x5u && registers.intc_mask() == 0x5u, "I_MASK set");
    registers.write(kIntcMask, 4u, 0xFFFF0004u);
    require(registers.read(kIntcMask, 4u) == 0x1u, "I_MASK toggles only sources 0-14");
    registers.write(kIntcStat, 4u, 0xFFFFu);
    rejects([&] { (void)registers.read(kIntcStat, 4u); }, "no EE interrupt source");
    rejects([&] { registers.write(kIntcMask, 2u, 1u); }, "I_MASK write of 2 byte(s)");

    // Undocumented blocks: writes latched, reads refused.
    registers.write(kUndocumented190x + 0xCu, 1u, 7u);
    registers.write(kUndocumented1A + 8u, 2u, 3u);
    rejects([&] { (void)registers.read(kUndocumented190x + 0xCu, 1u); }, "an undocumented register");
    rejects([&] { (void)registers.read(kUndocumented1A + 8u, 2u); }, "an undocumented register");
    rejects([&] { registers.write(kUndocumented1A + 0x1Eu, 4u, 0u); }, "crosses the end");
}

void check_mmi_logical() {
    for (const auto& [word, opcode] : {std::pair{0x70002489u, MipsOpcode::Pand}, std::pair{0x700024C9u, MipsOpcode::Pxor},
                                       std::pair{0x700014A9u, MipsOpcode::Por}, std::pair{0x700014E9u, MipsOpcode::Pnor}}) {
        const auto decoded = try_decode_r5900(0u, word);
        require(decoded && decoded->opcode == opcode && mips_aot_supported(opcode), "MMI logical decode");
    }
    // 0x700014A9 is the POR v0, zero, zero of the real kernel.
    const auto por = try_decode_r5900(0u, 0x700014A9u);
    require(por->rd == 2u && por->rs == 0u && por->rt == 0u && por->written_gpr() == 2u, "POR v0, zero, zero");

    EEState s;
    Memory m;
    s.set_reg128(t0, 0xF0F0F0F0F0F0F0F0ull, 0x00000000FFFFFFFFull);
    s.set_reg128(t1, 0xFF00FF00FF00FF00ull, 0x0123456789ABCDEFull);
    auto run = [&](void (Assembler::*emit)(unsigned, unsigned, unsigned), auto op) {
        Assembler a(0u);
        (a.*emit)(t2, t0, t1);
        ee::exec<decltype(op)::value>(s, m, 0u, a.finish().front());
        return s.reg128(t2);
    };
    const auto pand = run(&Assembler::pand, std::integral_constant<MipsOpcode, MipsOpcode::Pand>{});
    require(pand.low == 0xF000F000F000F000ull && pand.high == 0x0000000089ABCDEFull, "PAND 128-bit");
    const auto pxor = run(&Assembler::pxor, std::integral_constant<MipsOpcode, MipsOpcode::Pxor>{});
    require(pxor.low == 0x0FF00FF00FF00FF0ull && pxor.high == 0x0123456776543210ull, "PXOR 128-bit");
    const auto por_value = run(&Assembler::por, std::integral_constant<MipsOpcode, MipsOpcode::Por>{});
    require(por_value.low == 0xFFF0FFF0FFF0FFF0ull && por_value.high == 0x01234567FFFFFFFFull, "POR 128-bit");
    const auto pnor = run(&Assembler::pnor, std::integral_constant<MipsOpcode, MipsOpcode::Pnor>{});
    require(pnor.low == 0x000F000F000F000Full && pnor.high == 0xFEDCBA9800000000ull, "PNOR 128-bit");
    Assembler a(0u);
    a.por(zero, t0, t1);
    ee::exec<MipsOpcode::Por>(s, m, 0u, a.finish().front());
    require(s.reg128(0).low == 0u && s.reg128(0).high == 0u, "POR never writes $zero");
    rejects([] { (void)decode_r5900(0u, 0x700016A9u); }, "(MMI) not implemented"); // PEXCH (MMI3 sa 0x1A)
}
} // namespace

int main() {
    try {
        const auto rom = build_iop_rom();
        check_iop(rom);
        check_read_config();
        check_ee_view(rom);
        check_mmi_logical();
        std::cout << "M2.5 IOP checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
