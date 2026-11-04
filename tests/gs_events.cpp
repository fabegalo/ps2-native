#include "boot_checks.hpp"
#include "gs_events_program.hpp"
#include <iostream>

using namespace ps2native;
using namespace boot_checks;

#ifndef PS2N_GS_EVENTS_FIXTURE
namespace {
constexpr auto line = kEeCyclesPerBusCycle * kBusCyclesPerNtscLine;
constexpr auto held = kSmode1Sint | (2ull << kSmode1CmodShift);
constexpr auto running = 2ull << kSmode1CmodShift;
constexpr auto short_syncv = 2u | (2ull << 42);

void hold(GsRegisters& gs) {
    gs.reset(0);
    gs.write(kGsSmode1, 8, held, 0, {});
    gs.write(kGsCsr, 4, 31, 0, {});
}

void events() {
    GsRegisters gs; hold(gs);
    require(gs.read(kGsCsr, 1) == 0, "held GS starts without events");
    rejects([&] { (void)gs.read(kGsSiglblid, 8); }, "ID bits not written");
    gs.general_write(0x62, 0xFFFF000012340000ull);
    rejects([&] { (void)gs.read(kGsSiglblid + 4, 4); }, "ID bits not written");
    gs.general_write(0x62, 0x0000FFFF00005678ull);
    require(gs.read(kGsSiglblid + 4, 4) == 0x12345678u && gs.read(kGsCsr, 1) == 0,
            "LABEL masks preserve ID bits and generate no interrupt");
    gs.general_write(0x60, 0xFFFFFFFFA5A55A5Aull);
    require(gs.read(kGsSiglblid, 8) == 0x12345678A5A55A5Aull && gs.read(kGsCsr, 1) == 1,
            "SIGNAL updates ID and status while masked");
    require(!gs.take_interrupt_request(0).pending, "masked events do not request INTC");
    rejects([&] { gs.general_write(0x60, ~0ull); }, "second SIGNAL requires GIF backpressure");
    require(gs.read(kGsSiglblid, 4) == 0xA5A55A5Au, "refused second SIGNAL does not alter ID");
    gs.general_write(0x61, ~0ull);
    require(gs.read(kGsCsr, 1) == 3, "FINISH reports completed synchronous work");
    gs.write(kGsImr, 8, 0x7C00, 0, {});
    require(gs.take_interrupt_request(0).pending && !gs.take_interrupt_request(0).pending,
            "unmask latched events exactly once");
    gs.write(kGsCsr, 4, 0, 0, {});
    require(gs.read(kGsCsr, 1) == 3, "CSR zero does nothing");
    gs.write(kGsCsr, 4, kCsrSignal, 0, {});
    require(gs.read(kGsCsr, 1) == kCsrFinish, "CSR W1C leaves other events latched");
    gs.general_write(0x60, 0x000000FF00000012ull);
    require(gs.read(kGsSiglblid, 4) == 0xA5A55A12u, "acknowledged SIGNAL can update a masked subset");
    gs.write(kGsSiglblid, 4, 0xFACEB00Cu, 0, {});
    require(gs.read(kGsSiglblid, 8) == 0x12345678FACEB00Cull, "privileged ID half-write preserves other half");
    rejects([&] { (void)gs.read(kGsCsr, 4); }, "FIELD/NFIELD");

    GsRegisters upload; hold(upload);
    upload.general_write(0x50, 1ull << 48); // destination width 64, PSMCT32
    upload.general_write(0x51, 0); upload.general_write(0x52, 2u | (2ull << 32));
    upload.general_write(0x53, 0); upload.general_write(0x61, 0);
    require(upload.read(kGsCsr, 1) == 0, "FINISH waits for an incomplete host upload");
    upload.general_write(0x54, 0x2222222211111111ull);
    require(upload.read(kGsCsr, 1) == 0, "partial upload cannot finish early");
    rejects([&] { upload.general_write(0x53, 3); }, "FINISH waits");
    upload.general_write(0x54, 0x4444444433333333ull);
    require(upload.read(kGsCsr, 1) == kCsrFinish && upload.vram().read(0, 64, 0, 1, 1) == 0x44444444,
            "FINISH follows the final actual pixel transfer");
}

void timing() {
    GsRegisters gs; hold(gs);
    gs.write(kGsSyncv, 8, short_syncv, 0, {}); gs.write(kGsSmode1, 8, running, 0, {});
    rejects([&] { gs.write(kGsSmode1, 4, 0, line, {}); }, "64-bit stores only");
    require(gs.read(kGsCsr, 1) == 0, "invalid-width store does not change event state or event clock");
    rejects([&] { gs.write(kGsCsr, 8, kCsrReset, line, [] { throw DeviceRefusal("guard refuses"); }); }, "guard refuses");
    require(gs.read(kGsCsr, 1) == 0, "refused reset also rolls back lazy event sampling");
    gs.advance(line - 1);
    require(gs.read(kGsCsr, 1) == 0, "no H/V event before first line");
    gs.advance(line);
    require(gs.read(kGsCsr, 1) == kCsrHsint, "HSync edge at first line");
    gs.advance(2 * line);
    require(gs.read(kGsCsr, 1) == (kCsrHsint | kCsrVsint), "field start raises VSINT");
    gs.write(kGsCsr, 4, 31, 2 * line, {}); gs.advance(2 * line);
    require(gs.read(kGsCsr, 1) == 0, "acknowledged edge cannot repeat at the same cycle");
    gs.write(kGsSmode1, 8, held, 2 * line + 1, {});
    gs.advance(10 * line);
    require(gs.read(kGsCsr, 1) == 0, "held video generates no new H/V events");
    gs.write(kGsSmode1, 8, running, 10 * line, {}); gs.advance(11 * line);
    require(gs.read(kGsCsr, 1) == kCsrHsint, "restart uses the new epoch");

    GsRegisters unknown; unknown.reset(0); unknown.advance(1);
    rejects([&] { (void)unknown.read(kGsCsr, 1); }, "timing unknown");
    auto request = unknown.take_interrupt_request(1);
    require(!request.pending && !request.unknown, "unknown masked source cannot request IRQ");
    unknown.write(kGsImr, 8, 0x7700, 1, {}); // unmask unknown VSINT
    unknown.write(kGsImr, 8, 0x7F00, 1, {});
    require(unknown.take_interrupt_request(1).unknown, "remasking cannot erase the unmask window");
    unknown.write(kGsSmode1, 8, held, 1, {}); unknown.write(kGsCsr, 4, 31, 1, {});
    unknown.advance(100);
    require(unknown.read(kGsCsr, 1) == 0, "acknowledge plus held sync establishes future status");
}

void reset() {
    GsRegisters gs; hold(gs);
    gs.general_write(0x60, ~0ull); gs.write(kGsSiglblid + 4, 4, 0xBEEF, 0, {});
    gs.write(kGsImr, 8, 0x7E00, 0, {});
    gs.write(kGsSyncv, 8, short_syncv, 0, {});
    const auto generation = gs.timing_generation();
    rejects([&] { gs.write(kGsCsr, 8, kCsrReset, 10, [] { throw DeviceRefusal("guard refuses"); }); }, "guard refuses");
    require(gs.read(kGsSiglblid, 8) == 0xBEEFFFFFFFFFull && gs.latched(kGsImr) == 0x7E00,
            "refused reset preserves IDs and masks");
    gs.write(kGsCsr, 8, kCsrReset, 10, {});
    require(gs.latched(kGsImr) == 0x7F00 && gs.read(kGsCsr, 1) == 0 &&
            gs.timing_generation() != generation && gs.vertical_clock().state == GsRegisters::VerticalClock::State::Unknown,
            "reset clears events, restores masks and invalidates vertical timing");
    rejects([&] { (void)gs.read(kGsSiglblid, 8); }, "ID bits not written");
    gs.advance(11);
    rejects([&] { (void)gs.read(kGsCsr, 1); }, "timing unknown");
    require(!gs.take_interrupt_request(11).pending, "reset masks all GS interrupts");
}

void irq() {
    const auto fixture = gs_events_test::program();
    EEState state; Memory memory;
    prepare_ee_boot(load_bios_rom0(fixture.rom), nullptr, state, memory);
    memory.write64(0xB2000010u, held); memory.write32(0xB2001000u, 31);
    memory.write32(0xB000F000u, 0x7FFF);
    const auto send = [&](unsigned reg, std::uint64_t data) {
        memory.write128(0xB0006000u, (1ull << 60) | (1ull << 15) | 1u, 0xEu);
        memory.write128(0xB0006000u, data, reg);
    };
    send(0x60, 0xFFFFFFFF11223344ull);
    memory.write64(0xB2001010u, 0x7E00);
    require(memory.read32(0xB000F000u) == 1 && !memory.intc_interrupt_pending(),
            "GS request latches while I_MASK masks delivery");
    memory.write32(0xB000F010u, 1);
    require(memory.intc_interrupt_pending(), "I_MASK unmask observes retained GS request");
    memory.write32(0xB2001000u, 1);
    require(memory.read32(0xB000F000u) == 1, "GS acknowledge does not clear INTC");
    memory.write32(0xB000F000u, 1);
    require(!memory.intc_interrupt_pending(), "separate I_STAT acknowledge clears delivery");
    send(0x60, 0xFFFFFFFFAABBCCDDull);
    memory.write32(0xB000F000u, 1);
    require(!memory.intc_interrupt_pending(), "I_STAT acknowledgement does not create another GS edge");
    memory.write64(0xB2001010u, 0x7F00); memory.write64(0xB2001010u, 0x7E00);
    require(memory.intc_interrupt_pending(), "GS mask/unmask signals the still pending event");
    memory.write64(0xB2001000u, kCsrReset);
    require(memory.intc_interrupt_pending(), "GS reset cannot erase the EE's latched request");
    memory.write32(0xB000F000u, 1);
    require(!memory.intc_interrupt_pending(), "GS reset leaves no new unmasked request");
}

void held_counter() {
    const auto fixture = gs_events_test::program();
    EEState state; Memory memory;
    prepare_ee_boot(load_bios_rom0(fixture.rom), nullptr, state, memory);
    memory.write64(0xB2000010u, held);
    memory.write32(0xB0000000u, 7); memory.write32(0xB0000010u, 0x83);
    state.retired_instructions = 2 * line;
    require(memory.read32(0xB0000000u) == 7, "HBLANK timer freezes while SINT holds sync");
    memory.write64(0xB2000010u, running);
    state.retired_instructions += line - 1;
    require(memory.read32(0xB0000000u) == 7, "restart cannot count edges from the held interval");
    ++state.retired_instructions;
    require(memory.read32(0xB0000000u) == 8, "first actual resumed HBLANK advances the timer");
    memory.write64(0xB2000010u, held);
    state.retired_instructions += 20 * line;
    require(memory.read32(0xB0000000u) == 8, "holding an already enabled timer preserves its count");
    device_refused([&] { (void)memory.read32(0xB0000030u); }, "counter hold read before write");
    memory.write32(0xB0000030u, 0x12345678u);
    memory.write32(0xB0000830u, 0xFEDCBA98u);
    require(memory.read32(0xB0000030u) == 0x5678 && memory.read32(0xB0000830u) == 0xBA98,
            "T0/T1 HOLD stores retain only low 16 bits independently");
    require(memory.read32(0xB0000000u) == 8, "HOLD writes do not replace the live count");
    device_refused([&] { memory.write32(0xB0001030u, 0); }, "counter hold register does not exist");
}

void dma_setup() {
    Iop iop; iop.reset({}, nullptr);
    require(iop.ee_dma_observe(0x1000E020u).unknown==0x83FF03FFu, "PCR reset fields are symbolic");
    rejects([&] { iop.ee_dma_write(0x1000C400u,0x101); }, "priority depends on unknown");
    require(iop.ee_dma_read(0x1000C400u)==0, "unknown priority cannot start a transfer");
    for (const auto base : {0x10008000u,0x10009000u,0x1000A000u,0x1000B000u,0x1000B400u,
                           0x1000C000u,0x1000C800u,0x1000D000u,0x1000D400u}) {
        rejects([&] { (void)iop.ee_dma_read(base); }, "read before write");
        iop.ee_dma_write(base, 0); iop.ee_dma_write(base+0x10, 0x12340);
        iop.ee_dma_write(base+0x20, 0x12345);
        require(iop.ee_dma_read(base)==0 && iop.ee_dma_read(base+0x10)==0x12340 &&
                iop.ee_dma_read(base+0x20)==0x2345, "inactive DMA channel retains programmed addresses/count");
        rejects([&] { iop.ee_dma_write(base, 0x100); }, "transfer engine is not modeled");
        require(iop.ee_dma_read(base)==0, "refused start does not create a running/completed transfer");
    }
    iop.ee_dma_write(0x10008080u, 0);
    rejects([&] { (void)iop.ee_dma_read(0x10008080u); }, "absent channel register read");
    rejects([&] { iop.ee_dma_write(0x10008080u, 16); }, "nonzero write to absent");
    iop.ee_dma_write(0x1000D080u, 0xFFFFC120u);
    require(iop.ee_dma_read(0x1000D080u)==0x120, "SPR SADR width/alignment");
    iop.ee_dma_write(0x1000E010u, 0xFFFFFFFFu);
    require(iop.ee_dma_read(0x1000E010u)==0x63FF0000u, "D_STAT toggles masks without fabricating completion flags");
    iop.ee_dma_write(0x1000E010u, 0xFFFFFFFFu);
    require(iop.ee_dma_read(0x1000E010u)==0, "D_STAT mask toggles are reversible");
    const auto fixture=gs_events_test::program();
    EEState state; Memory memory;
    prepare_ee_boot(load_bios_rom0(fixture.rom),nullptr,state,memory);
    state.set_reg64(8,0xB000E020u);
    ee::exec<MipsOpcode::Lw>(state,memory,0,0x8D090000u);
    require(state.unknown_gpr_bits[9]==0xFFFFFFFF83FF03FFull, "LW sign-extension preserves unknown sign bit");
    state.set_reg64(10,~0x3FFull);
    ee::exec<MipsOpcode::And>(state,memory,0,(9u<<21)|(10u<<16)|(9u<<11)|0x24u);
    ee::exec<MipsOpcode::Sw>(state,memory,0,0xAD090000u);
    require(memory.load32_observed(0xB000E020u).unknown==0x83FF0000u,
            "PCR RMW defines CPC bits and preserves unknown priority bits without guessing");
    device_refused([&] { (void)memory.read32(0xB000E020u); }, "unknown reset bits");
    memory.write32(0xB000E020u,0);
    require(memory.read32(0xB000E020u)==0, "explicit PCR configuration establishes all fields");

    // DMA must never transfer the backing bytes of a symbolic RAM word.
    std::vector<std::uint8_t> ram(32u);
    std::unordered_map<std::uint32_t,std::uint32_t> unknown{{4u,1u}};
    Iop transfer;
    const std::array<std::uint8_t,4> nop{};
    transfer.load_program(nop,0,0);
    transfer.attach_ee_ram(ram,&unknown);
    transfer.ee_dma_write(0x1000E020u,0);
    transfer.ee_dma_write(0x1000E000u,1);
    transfer.ee_dma_write(0x1000C420u,1);
    transfer.ee_dma_write(0x1000C400u,0x101);
    transfer.run_until(1);
    require(transfer.stopped() && transfer.stop_reason()->find("unknown RAM bits")!=std::string::npos,
            "SIF1 refuses a symbolic qword before placing any bytes in its FIFO");
    require(transfer.sif1_metrics().peak_fifo_words==0 && transfer.ee_dma_read(0x1000C420u)==1,
            "refused symbolic DMA does not consume source qword");
    Iop priority;
    priority.load_program(nop,0,0);
    priority.attach_ee_ram(ram);
    priority.ee_dma_write(0x1000E020u,0x80000000u); // PCE, channel 6 disabled
    priority.ee_dma_write(0x1000E000u,1);
    priority.ee_dma_write(0x1000C420u,1);
    priority.ee_dma_write(0x1000C400u,0x101);
    priority.run_until(1);
    require(priority.ee_dma_read(0x1000C420u)==1 && priority.sif1_metrics().peak_fifo_words==0,
            "PCR can pause a started source without completing it");
    priority.ee_dma_write(0x1000E020u,0x80400000u);
    priority.run_until(2);
    require(priority.ee_dma_read(0x1000C420u)==0 && priority.sif1_metrics().peak_fifo_words==4,
            "enabling channel 6 resumes true qword transfer");
}

void ram_knowledge() {
    const auto fixture=gs_events_test::program();
    EEState state; Memory memory;
    const auto bios=load_bios_rom0(fixture.rom);
    prepare_ee_boot(bios,nullptr,state,memory);
    memory.tlb_write(0,{0,0x70000000u,0x80000007u,7u});
    memory.set_translation_context(0,false);
    for (const auto address : {0x80001800u,0x70000000u}) {
        memory.store32_observed(address,{0x12345678u,0x0000FF0Fu});
        const auto observed=memory.load32_observed(address);
        require(observed.value==0x12340070u && observed.unknown==0xFF0Fu,"RAM retains exact symbolic bits");
        device_refused([&] { (void)memory.read32(address); },"unknown RAM bits");
        device_refused([&] { (void)memory.fetch32(address); },"unknown RAM bits");
        require(memory.read16(address+2u)==0x1234u,"known bytes of symbolic word remain readable");
        memory.write8(address+1u,0xABu);
        require(memory.load32_observed(address).unknown==0xFu,"byte overwrite clears only its own uncertainty");
        memory.write8(address,0x78u);
        require(memory.read32(address)==0x1234AB78u,"known byte overwrite resolves word");
        memory.store32_observed(address+4u,{0,~0u});
        memory.write128(address,0x1122334455667788ull,0x99AABBCCDDEEFF00ull);
        require(memory.read64(address)==0x1122334455667788ull,"SQ clears symbolic words covered by store");
        state.set_reg64(8,address);
        state.set_reg64_observed(9,0x123456789ABCDEF0ull,0xFFFF000000001000ull);
        ee::exec<MipsOpcode::Sd>(state,memory,0,0xFD090000u);
        device_refused([&] { (void)memory.read64(address); },"unknown RAM bits");
        ee::exec<MipsOpcode::Ld>(state,memory,0,0xDD0A0000u);
        require(state.unknown_gpr_bits[10]==0xFFFF000000001000ull,"SD/LD preserve symbolic upper bits");
    }
    memory.store32_observed(0x80001800u,{0,1});
    require(memory.load32_observed(0xA0001800u).unknown==1,"KSEG aliases share symbolic RAM metadata");
    memory.write16(0xA0001800u,0xABCDu);
    require(memory.read32(0x80001800u)==0xABCDu,"aliased halfword overwrite clears symbolic bits");
    memory.store32_observed(0x80001800u,{0,1});
    prepare_ee_boot(bios,nullptr,state,memory);
    require(memory.read32(0x80001800u)==0,"reset clears symbolic memory with RAM");
}

void guest(const std::filesystem::path& rom_path) {
    const auto fixture = gs_events_test::program();
    const auto bios = load_bios_rom0(read_file(rom_path));
    std::vector<std::string> traces[2]; std::uint64_t retired{};
    for (unsigned run = 0; run < 2; ++run) {
        const auto* module = run ? &aot_module() : nullptr;
        EEState state; Memory memory; prepare_ee_boot(bios, module, state, memory);
        BootOptions options; options.max_instructions = 1000; options.stop_at = fixture.stop;
        options.trace = TraceLevel::Full;
        const auto report = run_ee_boot(state, memory, module, options);
        require(report.outcome == BootOutcome::StopAddress, "GS event guest reached stop: " + report.reason);
        require(memory.read32(0x80001000u) == 3 && memory.read32(0x80001004u) == 1 &&
                memory.read32(0x80001008u) == 0x12345678u,
                "guest interrupt handler acknowledges actual SIGNAL/FINISH exactly once and returns");
        require(state.cp0(Cop0::Epc) != 0 && !(state.cp0(Cop0::Status) & kStatusEXL), "IRQ delivered and ERET restored state");
        if (run) require(report.aot_instructions > 0 && report.retired == retired, "AOT executed with identical instruction count");
        else retired = report.retired;
        traces[run] = normalized(report.trace);
    }
    require(traces[0] == traces[1], "instruction/device event equivalence including exception handler");
}

void knowledge() {
    const auto fixture = gs_events_test::program();
    EEState state; Memory memory;
    prepare_ee_boot(load_bios_rom0(fixture.rom), nullptr, state, memory);
    state.retired_instructions = 10;
    state.set_reg64(8, 0xB000F000u);
    device_refused([&] { (void)memory.read32(0xB000F000u); }, "vertical timing unknown");
    ee::exec<MipsOpcode::Lw>(state, memory, 0, 0x8D090000u); // LW t1,0(t0)
    require(state.unknown_gpr_bits[9] == 0xCu, "LW retains unknown VBLANK bits symbolically");
    rejects([&] { (void)state.reg32(9); }, "consumes unknown MMIO bits");
    state.set_reg64(10, 1);
    ee::exec<MipsOpcode::And>(state, memory, 0, (9u<<21)|(10u<<16)|(9u<<11)|0x24u);
    require(state.unknown_gprs == 0 && state.reg64(9) == 0, "AND proves that the GS-only result is known");
    state.set_reg64_observed(9, 0, 0xCu);
    device_refused([&] { ee::exec<MipsOpcode::Sw>(state, memory, 0, 0xAD090000u); }, "unknown MMIO bits cannot be stored");
    rejects([&] { ee::exec<MipsOpcode::Addiu>(state, memory, 0, 0x252A0001u); }, "consumes unknown MMIO bits");
    state.set_reg64(10, 0xCu);
    ee::exec<MipsOpcode::Or>(state, memory, 0, (9u<<21)|(10u<<16)|(9u<<11)|0x25u);
    require(state.unknown_gprs == 0 && state.reg64(9) == 0xCu, "OR with known ones resolves those result bits");
    state.set_reg64_observed(9, 0, 0xCu);
    ee::exec<MipsOpcode::Andi>(state, memory, 0, (0xCu<<26)|(9u<<21)|(9u<<16)|1u);
    require(state.reg64(9) == 0 && state.unknown_gprs == 0, "ANDI independently resolves masked bits");
    state.set_reg64_observed(9, 0, 1ull<<40);
    require(state.reg32(9) == 0, "a 32-bit read does not consume an unknown upper bit");
    rejects([&] { (void)state.reg64(9); }, "consumes unknown MMIO bits");
    state.set_reg64(9, 7);
    require(state.reg64(9) == 7 && state.unknown_gprs == 0, "known overwrite clears uncertainty");
}
} // namespace
#endif

int main(int argc, char** argv) {
    try {
#ifdef PS2N_GS_EVENTS_FIXTURE
        require(argc == 2, "fixture output path");
        (void)write_binary_if_changed(argv[1], gs_events_test::program().rom);
#else
        require(argc == 3, "test case and fixture path");
        const std::string mode = argv[1];
        if (mode == "events") events(); else if (mode == "timing") timing();
        else if (mode == "reset") reset(); else if (mode == "irq") irq();
        else if (mode == "knowledge") knowledge();
        else if (mode == "ram-knowledge") ram_knowledge();
        else if (mode == "held-counter") held_counter();
        else if (mode == "dma-setup") dma_setup();
        else if (mode == "guest") guest(argv[2]); else throw std::runtime_error("unknown GS events test");
#endif
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
