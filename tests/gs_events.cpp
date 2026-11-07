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
    gs.write(kGsImr, 8, 0xFF00, 0, {});
    require(gs.latched(kGsImr) == 0x7F00 && gs.read(kGsCsr, 1) == 1 &&
            !gs.take_interrupt_request(0).pending,
            "IMR bit 15 is discarded without acknowledging or unmasking events");
    rejects([&] { gs.general_write(0x60, ~0ull); }, "second SIGNAL requires GIF backpressure");
    require(gs.read(kGsSiglblid, 4) == 0xA5A55A5Au, "refused second SIGNAL does not alter ID");
    gs.general_write(0x61, ~0ull);
    require(gs.read(kGsCsr, 1) == 3, "FINISH reports completed synchronous work");
    gs.write(kGsImr, 4, 0xFC00, 0, {});
    require(gs.latched(kGsImr) == 0x7C00, "32-bit IMR writes also discard bit 15");
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
    for(const auto cmod:{2u,3u})for(const bool initial:{false,true}) {
        GsRegisters field;
        const auto sync=1ull|(10ull<<42u);
        field.reset(0,sync,initial);
        field.write(kGsSmode1,8,std::uint64_t{cmod}<<kSmode1CmodShift,0,{});
        const auto clock=field.vertical_clock();const auto period=clock.half_line*clock.field;
        const auto edge=clock.half_line*(cmod==2u ? 7u : 6u);
        const auto polarity=[&]{const auto value=field.observe_csr();
            require(!(value.unknown&(1ull<<13u)),"explicit FIELD polarity stays known under modeled sync");
            require(value.unknown&(1ull<<12u),"NFIELD remains unverified");
            return (value.value&(1ull<<13u))!=0;};
        field.advance(edge-1u);require(polarity()==initial && !(field.read(kGsCsr,1)&kCsrVsint),
            "FIELD/VSINT cannot precede the selected analog GS edge");
        field.advance(edge);require(polarity()!=initial && (field.read(kGsCsr,1)&kCsrVsint),
            "FIELD and VSINT use the same modeled GS edge");
        field.advance(edge);require(polarity()!=initial,"reading the same cycle never flips FIELD");
        field.advance(edge+3u*period);require(polarity()==initial,"batch advancement counts every crossed field");
        field.write(kGsSmode1,8,kSmode1Sint|(std::uint64_t{cmod}<<kSmode1CmodShift),edge+3u*period,{});
        field.advance(edge+8u*period);require(polarity()==initial,"held sync preserves field without new edges");
        field.write(kGsSmode1,8,std::uint64_t{cmod}<<kSmode1CmodShift,edge+8u*period,{});
        field.advance(2u*edge+8u*period);require(polarity()!=initial,"resumed sync uses its new phase");
        const auto now=2u*edge+8u*period;
        rejects([&]{field.write(kGsCsr,8,kCsrReset,now+period,[]{throw DeviceRefusal("guard refuses");});},"guard refuses");
        require(polarity()!=initial,"rejected reset rolls back lazy field advancement");
        field.write(kGsCsr,8,kCsrReset,now,{});require(polarity()==initial,"soft reset restores only explicit initial polarity");
        field.reset(0,sync);field.advance(edge);
        require(field.observe_csr().unknown&(1ull<<13u),"reset without opt-in leaves FIELD unknown");
        field.reset(0,std::nullopt,initial);field.advance(1);
        require(field.observe_csr().unknown&(1ull<<13u),"unknown vertical clock destroys field phase knowledge");
        field.reset(0,2ull|(10ull<<42u),false);field.advance(7u*line/2u);
        require(polarity(),"even front porch selects the non-interlaced analog field at the first edge");
        field.advance(100u*line);require(polarity(),"non-interlaced analog field does not alternate");
    }
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
    require(iop.ee_dma_observe(0x1000E020u).unknown==0x03FF03FFu, "PCR resets PCE clear while CDE/CPC remain symbolic");
    iop.ee_dma_write_observed(0x1000E020u,{0x80000000u,0x03FF03FFu});
    rejects([&] { iop.ee_dma_write(0x1000C400u,0x101); }, "priority depends on unknown");
    require(iop.ee_dma_read(0x1000C400u)==0, "unknown priority cannot start a transfer");
    for (const auto base : {0x10008000u,0x10009000u,0x1000A000u,0x1000B000u,0x1000B400u,
                           0x1000C000u,0x1000C800u,0x1000D000u,0x1000D400u}) {
        rejects([&] { (void)iop.ee_dma_read(base); }, "read before write");
        iop.ee_dma_write(base, 0); iop.ee_dma_write(base+0x10, 0x12340);
        iop.ee_dma_write(base+0x20, 0x12345);
        require(iop.ee_dma_read(base)==0 && iop.ee_dma_read(base+0x10)==0x12340 &&
                iop.ee_dma_read(base+0x20)==0x2345, "inactive DMA channel retains programmed addresses/count");
        rejects([&] { iop.ee_dma_write(base, 0x100); },
                base==0x1000C000u ? "destination chain mode" :
                (base==0x10008000u || base==0x10009000u) ? "normal or source chain mode" :
                base==0x1000A000u ? "normal or source chain mode" :
                base==0x1000D000u ? "requires programmed MADR/QWC/SADR" : "transfer engine is not modeled");
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
    require(state.unknown_gpr_bits[9]==0x03FF03FFull, "LW sign-extends the documented clear PCE bit");
    state.set_reg64(10,~0x3FFull);
    ee::exec<MipsOpcode::And>(state,memory,0,(9u<<21)|(10u<<16)|(9u<<11)|0x24u);
    ee::exec<MipsOpcode::Sw>(state,memory,0,0xAD090000u);
    require(memory.load32_observed(0xB000E020u).unknown==0x03FF0000u,
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

void vu_control() {
    const auto fixture=gs_events_test::program();
    EEState state; Memory memory;
    prepare_ee_boot(load_bios_rom0(fixture.rom),nullptr,state,memory);
    constexpr auto cfc2=(0x12u<<26)|(2u<<21)|(9u<<16)|(28u<<11);
    constexpr auto ctc2=(0x12u<<26)|(6u<<21)|(9u<<16)|(28u<<11);
    require(decode_mips(0,cfc2).opcode==MipsOpcode::Cfc2 &&
            decode_mips(0,ctc2|1u).opcode==MipsOpcode::Ctc2,"COP2 control transfers decode in both engines");
    rejects([&] { (void)decode_mips(0,cfc2|2u); },"AOT unsupported");
    rejects([&] { ee::exec<MipsOpcode::Cfc2>(state,memory,0,cfc2); },"Status.CU2 = 0");
    state.set_cp0(Cop0::Status,state.cp0(Cop0::Status)|(1u<<30));
    ee::exec<MipsOpcode::Cfc2>(state,memory,0,cfc2);
    require(state.reg64(9)==0,"VU FBRST documented initial value");
    state.set_reg64(9,0xA0Au); // RS0/RS1 commands and TE0/TE1 enables
    ee::exec<MipsOpcode::Ctc2>(state,memory,0,ctc2|1u);
    ee::exec<MipsOpcode::Cfc2>(state,memory,0,cfc2|1u);
    require(state.reg64(9)==0x808u,"VU reset commands read zero while debug enables retain values");
    require(memory.read_vu_control(29)==0,"reset leaves idle VUs in Ready state");
    rejects([&] { memory.write_vu_control(28,0xF0Fu); },"force break transition");
    require(memory.read_vu_control(28)==0x808u,"refused transition leaves VU controls intact");
    rejects([&] { memory.write_vu_control(28,0x10); },"reserved bits");
    rejects([&] { (void)memory.read_vu_control(1); },"unknown initial bits");
    require(memory.observe_vu_control(1).unknown==0xFFFFu,"uninitialized VI bits are symbolic");
    memory.write_vu_control(1,0xFFFF8000u);
    ee::exec<MipsOpcode::Cfc2>(state,memory,0,(cfc2&~(31u<<11))|(1u<<11));
    require(state.reg64(9)==0x8000u,"VI register transfers are unsigned 16-bit");
    rejects([&] { memory.write_vu_control(31,0); },"not modeled");
    memory.write_vu_control(2,0x421u);
    constexpr auto viswr=0x4A0003FFu|(1u<<16)|(2u<<11)|(8u<<21);
    require(decode_mips(0,viswr).opcode==MipsOpcode::Viswr,"VISWR decodes actual integer store");
    ee::exec<MipsOpcode::Viswr>(state,memory,0,viswr);
    require(memory.observe_vu_data(0x4210u).value==0x8000u && memory.observe_vu_data(0x4210u).unknown==0,
            "VISWR scales address by sixteen and writes remote VU1 integer x lane");
    require(memory.observe_vu_data(0x4220u).unknown==0xFFFFu,"adjacent VU1 integer remains undefined");
    memory.write_vu_control(2,0);
    ee::exec<MipsOpcode::Viswr>(state,memory,0,viswr|(7u<<21));
    for(unsigned lane=0;lane<4;++lane)
        require(memory.observe_vu_data(lane*4u).value==0x8000u && !memory.observe_vu_data(lane*4u).unknown,
                "VISWR stores zero-extended integer in each selected data lane");
    memory.write_vu_control(2,1);
    ee::exec<MipsOpcode::Viswr>(state,memory,0,(viswr&~(15u<<21))|(1u<<21));
    require(memory.observe_vu_data(16u).unknown==~0u && memory.observe_vu_data(28u).value==0x8000u,
            "VISWR lane mask does not define untouched words");
    memory.write_vu_control(2,0x434u);
    ee::exec<MipsOpcode::Viswr>(state,memory,0,viswr);
    require(memory.observe_vu_data(0x4340u).value==0x8000u && memory.observe_vu_data(0x4350u).unknown==~0u,
            "VISWR writes actual VU1 R register without defining I/Q/P");
    constexpr auto qmfc2=0x48290000u; // QMFC2 t1,VF00
    constexpr auto qmtc2=0x48A90800u; // QMTC2 t1,VF01
    ee::exec<MipsOpcode::Qmfc2>(state,memory,0,qmfc2);
    require(state.reg128(9).low==0 && state.reg128(9).high==0x3F80000000000000ull,"VF00 has constant (0,0,0,1)");
    ee::exec<MipsOpcode::Qmfc2>(state,memory,0,qmfc2|(1u<<11));
    require(state.unknown_gpr_bits[9]==~0ull && state.unknown_gpr_high_bits[9]==~0ull,
            "uninitialized VF01 is preserved symbolically across all 128 bits");
    state.set_reg64(8,0x80001800u);
    ee::exec<MipsOpcode::Sq>(state,memory,0,0x7D090000u);
    ee::exec<MipsOpcode::Lq>(state,memory,0,0x790A0000u);
    require(state.unknown_gpr_bits[10]==~0ull && state.unknown_gpr_high_bits[10]==~0ull,
            "SQ/LQ spills preserve unknown vector halves");
    state.set_reg64(10,7);
    require(state.reg64(10)==7,"scalar overwrite defines low half independently");
    rejects([&] { (void)state.reg128(10); },"128-bit operand");
    state.set_reg128(9,0x1122334455667788ull,0x99AABBCCDDEEFF00ull);
    ee::exec<MipsOpcode::Qmtc2>(state,memory,0,qmtc2|1u);
    ee::exec<MipsOpcode::Qmfc2>(state,memory,0,qmfc2|(1u<<11));
    require(state.reg128(9).low==0x1122334455667788ull && state.reg128(9).high==0x99AABBCCDDEEFF00ull,
            "QMTC2/QMFC2 roundtrip all vector lanes without arithmetic");
    constexpr auto vsub=0x4BE0006Cu; // VSUB.xyzw VF01,VF00,VF00
    ee::exec<MipsOpcode::Vsub>(state,memory,0,vsub);
    ee::exec<MipsOpcode::Qmfc2>(state,memory,0,qmfc2|(1u<<11));
    require(state.reg128(9).low==0 && state.reg128(9).high==0 && memory.read_vu_control(17)==15 &&
            memory.read_vu_control(16)==0x41u,"exact cancellation defines destination and zero/sticky flags");
    memory.vu_subtract(2,0,1,15);
    require(memory.observe_vu_vector(2).high.value==0x3F80000000000000ull,
            "VU subtraction now handles non-cancelling operands with the shared adder");
    memory.write_vu_control(2,0x400u);
    constexpr auto vsqi=0x4BE20B7Du; // VSQI.xyzw VF01,(VI02++)
    ee::exec<MipsOpcode::Vsqi>(state,memory,0,vsqi);
    require(memory.read_vu_control(2)==0x401u && memory.observe_vu_data(0x400Cu).value==0x3F800000u,
            "VSQI increments qword address and preserves immutable VU1 VF00");
    ee::exec<MipsOpcode::Vsqi>(state,memory,0,vsqi);
    for(unsigned lane=0;lane<4;++lane)
        require(!memory.observe_vu_data(0x4010u+lane*4u).unknown && !memory.observe_vu_data(0x4010u+lane*4u).value,
                "VSQI writes four real vector lanes to VU1 VF01");
    for(const auto address : {0x11000000u,0x11004000u,0x11008000u,0x1100C000u}) {
        device_refused([&] { (void)memory.read64(address+0x100u); },"unknown VU memory bits");
        memory.write128(address+0x100u,0x1122334455667788ull,0x99AABBCCDDEEFF00ull);
        require(memory.read64(address+0x100u)==0x1122334455667788ull &&
                memory.read64(address+0x108u)==0x99AABBCCDDEEFF00ull,"EE writes/readback actual VU micro/data bytes");
    }
    require(memory.read32(0x11004000u)==memory.observe_vu_data(0).value,"EE and VU use the same VU0 data storage");
    device_refused([&] {memory.write32(0x11001000u,0);},"unimplemented VU memory aperture");
    memory.write_vu_control(1,0xFFFFu);memory.write_vu_control(2,2);
    constexpr auto viadd=0x4A000030u|(2u<<16)|(1u<<11)|(1u<<6);
    require(decode_mips(0,viadd).opcode==MipsOpcode::Viadd,"VIADD decodes the integer register fields");
    const auto mac_before_integer=memory.read_vu_control(17),status_before_integer=memory.read_vu_control(16);
    ee::exec<MipsOpcode::Viadd>(state,memory,0,viadd);
    require(memory.read_vu_control(1)==1 && memory.read_vu_control(17)==mac_before_integer &&
            memory.read_vu_control(16)==status_before_integer,
            "VIADD wraps to 16 bits, supports source/destination alias and preserves flags");
    ee::exec<MipsOpcode::Viadd>(state,memory,0,viadd&~(31u<<6));
    require(memory.read_vu_control(0)==0,"VIADD cannot change constant VI00");
    rejects([&] {(void)decode_mips(0,viadd|(16u<<16));},"unsupported");
    memory.write_vu_control_observed(2,{0,0xFFFFu});
    rejects([&] {ee::exec<MipsOpcode::Viadd>(state,memory,0,viadd);},"unknown initial bits");
    require(memory.read_vu_control(1)==1,"refused VIADD does not change its destination");
    memory.write_vu_control_observed(16,{0xFC0u,0xFFFFF03Fu});
    require(memory.read_vu_control(16)==0xFC1u,"STATUS ignores writes and uncertainty outside sticky flags");
    memory.write_vu_control(16,0);
    require(memory.read_vu_control(16)==1,"clearing sticky flags preserves current arithmetic flags");
    memory.write_vu_control(18,0xFEDCBA98u);
    require(memory.read_vu_control(18)==0xDCBA98u,"CLIP accepts only 24 bits");
    memory.write_vu_control_observed(16,{0,0xFC0u});
    memory.write_vu_control_observed(18,{0,0xFFFFFFu});
    rejects([&] {(void)memory.read_vu_control(16);},"unknown initial bits");
    memory.write_vu_control(28,2);
    require(memory.read_vu_control(16)==0 && memory.read_vu_control(18)==0,"VU reset clears symbolic flag history");
    require(memory.observe_vu_control(20).unknown==0x7FFFFFu && memory.observe_vu_control(21).unknown==~0u &&
            memory.observe_vu_control(22).unknown==~0u && memory.observe_vu_control(27).unknown==0xFFFFu,
            "VU scalar/control initial contents remain indeterminate");
    for(const auto index : {20u,21u,22u,27u}) memory.write_vu_control(index,0xFEDCBA98u);
    require(memory.read_vu_control(20)==0x5CBA98u && memory.read_vu_control(21)==0xFEDCBA98u &&
            memory.read_vu_control(22)==0xFEDCBA98u && memory.read_vu_control(27)==0xBA98u,
            "CTC2 preserves raw I/Q values and applies R/CMSAR0 register widths");
    memory.write_vu_control(28,2);
    require(memory.read_vu_control(20)==0x5CBA98u && memory.read_vu_control(27)==0xBA98u,
            "VU flag reset does not fabricate scalar/control reinitialization");
}

void vif_control() {
    const auto fixture=gs_events_test::program();
    EEState state; Memory memory;
    prepare_ee_boot(load_bios_rom0(fixture.rom),nullptr,state,memory);
    const auto unphased_status=[&] {
        const auto status=memory.load32_observed(0xB0003C00u);
        require(status.unknown==0x80u,"unestablished VIF1 double-buffer phase remains unknown");
        return status.value;
    };
    memory.write128(0xB0005000u,0x50000003u,0);
    require(unphased_status()==1 && memory.read32(0xB0003C80u)==0x50000003u &&
            memory.read32(0xB0003C60u)==3,
            "VIF DIRECT waits for actual payload and records its command");
    device_refused([&] {memory.write32(0xB0003C10u,2);},"FIFO stall/backpressure");
    require(unphased_status()==1,"refused force break leaves pending DIRECT intact");
    memory.write32(0xB0003C10u,1);
    require(unphased_status()==0,"VIF reset discards pending DIRECT payload");
    memory.write128(0xB0005000u,0,0);
    require(unphased_status()==0,"post-reset NOP is parsed as VIFcode rather than GIF payload");
    device_refused([&] {(void)memory.read32(0xB0003C10u);},"write-only");
    device_refused([&] {memory.write16(0xB0003C10u,1);},"32-bit");
    memory.write32(0xB0003C20u,5);
    require(memory.read32(0xB0003C20u)==5,"VIF error mask retains explicit settings");
    memory.write32(0xB0003C30u,0x12345678u);
    require(memory.read32(0xB0003C30u)==0x5678u,"VIF MARK CPU write retains low16 bits");
    memory.write128(0xB0006000u,(1ull<<60)|(1ull<<15)|1u,0xEu);
    memory.write128(0xB0006000u,0xFFFFFFFF12345678ull,0x60u);
    memory.write32(0xB0003C10u,1);
    require(memory.gs().read(kGsSiglblid,4)==0x12345678u,"VIF reset preserves independently received GS SIGNAL");
    const auto words=[&](std::uint32_t a,std::uint32_t b,std::uint32_t c,std::uint32_t d) {
        memory.write128(0xB0005000u,std::uint64_t{a}|(std::uint64_t{b}<<32),std::uint64_t{c}|(std::uint64_t{d}<<32));
    };
    words(0x01000304u,0x030003FFu,0x02000155u,0x0400FFFFu);
    require(memory.read32(0xB0003C40u)==0x304u && memory.read32(0xB0003CA0u)==0x3FFu &&
            memory.read32(0xB0003CB0u)==0x155u && memory.read32(0xB0003CC0u)==0x3FFu &&
            memory.read32(0xB0003C90u)==0x3FFu,"VIF configuration comes from actual command bytes with field widths");
    device_refused([&] {(void)memory.read32(0xB0003CD0u);},"before write/command");
    words(0x30000000u,0x01020304u,0xFFFFFFFFu,0x80000000u);
    require(memory.read32(0xB0003C00u)==1,"partial STROW waits across FIFO qwords");
    words(0x12345678u,0x31000000u,0x11111111u,0x22222222u);
    words(0x33333333u,0x44444444u,0x20000000u,0xFFFF0000u);
    require(memory.read32(0xB0003D00u)==0x01020304u && memory.read32(0xB0003D10u)==0xFFFFFFFFu &&
            memory.read32(0xB0003D20u)==0x80000000u && memory.read32(0xB0003D30u)==0x12345678u &&
            memory.read32(0xB0003D40u)==0x11111111u && memory.read32(0xB0003D70u)==0x44444444u &&
            memory.read32(0xB0003C70u)==0xFFFF0000u && memory.read32(0xB0003C00u)==0,
            "row/column/mask payload crosses qwords without being decoded as commands");
    words(0x0700ABCDu,0,0,0);
    require(memory.read32(0xB0003C30u)==0xABCDu && memory.read32(0xB0003C00u)==0x40u &&
            memory.read32(0xB0003C80u)==0,"MARK flag persists while CODE records subsequent real NOP");
    memory.write32(0xB0003C30u,7);
    require(memory.read32(0xB0003C00u)==0,"CPU MARK store acknowledges only MRK");
    memory.write32(0xB0003010u,1);
    words(0x06008000u,0,0,0);
    require((memory.read32(0xB0003020u)&3u)==3u,"VIF and GIF own independent PATH3 masks");
    words(0x06000000u,0,0,0);
    require((memory.read32(0xB0003020u)&3u)==1u,"VIF unmask preserves GIF_MODE mask");
    memory.write32(0xB0003010u,0);
    memory.write128(0xB0006000u,(1ull<<60)|(1ull<<15)|1u,0xEu);
    words(0x06008000u,0,0,0);
    memory.write128(0xB0006000u,0xFFFFFFFF11223344ull,0x62u);
    require(memory.gs().read(kGsSiglblid+4,4)==0x11223344u,"new mask lets the already active PATH3 block finish");
    device_refused([&] {memory.write128(0xB0006000u,1ull<<15,0);},"PATH3 masked");
    memory.write32(0xB0003C10u,1);
    require((memory.read32(0xB0003020u)&2u)==0,"VIF reset cancels its PATH3 mask");
    memory.write128(0xB0006000u,1ull<<15,0);
    const auto words0=[&](std::uint32_t a,std::uint32_t b,std::uint32_t c,std::uint32_t d) {
        memory.write128(0xB0004000u,std::uint64_t{a}|(std::uint64_t{b}<<32),std::uint64_t{c}|(std::uint64_t{d}<<32));
    };
    device_refused([&] {(void)memory.read32(0xB0003820u);},"before write/command");
    memory.write32(0xB0003820u,2);
    memory.write32(0xB0003830u,0x12345678u);
    require(memory.read32(0xB0003820u)==2 && memory.read32(0xB0003830u)==0x5678u,
            "VIF0 has its own ERR and low16 MARK settings");
    device_refused([&] {memory.write32(0xB0003800u,0);},"read-only");
    device_refused([&] {memory.write16(0xB0003820u,0);},"32-bit");
    device_refused([&] {(void)memory.read32(0xB00038A0u);},"reserved");
    words0(0x01000203u,0x0400FFFFu,0x05000002u,0x07004321u);
    require(memory.read32(0xB0003840u)==0x203u && memory.read32(0xB0003890u)==0x3FFu &&
            memory.read32(0xB0003850u)==2 && memory.read32(0xB0003830u)==0x4321u &&
            memory.read32(0xB0003800u)==0x40u,"VIF0 parses actual configuration commands independently");
    for(const auto command : {0x02000000u,0x03000000u,0x06008000u,0x50000001u})
        device_refused([&] {words0(command,0,0,0);},"VIF1-only");
    words0(0x30000000u,1,2,3);
    rejects([&] {(void)memory.read_vu_control(29);},"active VIF processing");
    words0(0xFEDCBA98u,0,0,0);
    require(memory.read32(0xB0003900u)==1 && memory.read32(0xB0003930u)==0xFEDCBA98u,
            "VIF0 STROW consumes real data across FIFO writes");
    words(0x01000405u,0x06008000u,0,0);
    memory.write32(0xB0003810u,1);
    require(memory.read32(0xB0003800u)==0 && memory.read32(0xB0003C40u)==0x405u &&
            (memory.read32(0xB0003020u)&2u),"VIF0 reset preserves VIF1 configuration and its GIF PATH3 mask");
}

void ipu_control() {
    const auto fixture=gs_events_test::program();
    EEState state; Memory memory;
    prepare_ee_boot(load_bios_rom0(fixture.rom),nullptr,state,memory);
    const auto before=memory.load32_observed(0xB0002010u);
    require(before.unknown==(IpuRegisters::kSettings|0x4000FF00u),"IPU configuration and status are not guessed at power-on");
    memory.write32(0xB0002010u,0x40000000u);
    const auto reset=memory.load32_observed(0xB0002010u);
    require(reset.value==0 && reset.unknown==0x4000FF00u,
            "idle reset clears BUSY/FIFO counts and writes settings without inventing CBP/ECD/SCD");
    device_refused([&] {(void)memory.read32(0xB0002010u);},"unverified status/reset bits");
    state.set_reg64(8,0xB0002010u);
    ee::exec<MipsOpcode::Lw>(state,memory,0,0x8D090000u);
    state.set_reg64(10,0x80000000u);
    ee::exec<MipsOpcode::And>(state,memory,0,(9u<<21)|(10u<<16)|(9u<<11)|0x24u);
    require(state.reg64(9)==0,"CPU can prove that reset left IPU idle without consuming unrelated unknown flags");
    state.set_reg64_observed(9,0,0x7FFFFFFFFFFFFFFFull);
    ee::exec<MipsOpcode::Bgez>(state,memory,0x1000u,(1u<<26)|(9u<<21)|(1u<<16)|3u);
    require(state.delay_pending && state.delay_target==0x1010u,"BGEZ consumes only a known positive sign bit");
    state.delay_pending=false;
    state.set_reg64_observed(9,0x8000000000000000ull,0x7FFFFFFFFFFFFFFFull);
    ee::exec<MipsOpcode::Bltzl>(state,memory,0x1000u,(1u<<26)|(9u<<21)|(2u<<16)|3u);
    require(state.delay_pending && state.delay_target==0x1010u,"BLTZL consumes only a known negative sign bit");
    state.delay_pending=false;state.set_reg64(31,0x1234u);
    state.set_reg64_observed(9,0,0x8000000000000000ull);
    rejects([&] {ee::exec<MipsOpcode::Bgezal>(state,memory,0x1000u,(1u<<26)|(9u<<21)|(17u<<16)|3u);},"unknown sign bit");
    require(!state.delay_pending && state.reg64(31)==0x1234u,"unknown sign cannot select a branch or update its link");
    memory.write32(0xB0002010u,0x07F2FFFFu);
    require(memory.load32_observed(0xB0002010u).value==0x07F20000u &&
            memory.load32_observed(0xB0002010u).unknown==reset.unknown,"CTRL ignores writes to read-only flags and retains settings");
    device_refused([&] {memory.write32(0xB0002010u,0x40040000u);},"reserved bits");
    require(memory.load32_observed(0xB0002010u).value==0x07F20000u,"invalid reset write is atomic");
    device_refused([&] {memory.write64(0xB0002010u,0x40000000u);},"32-bit");
    device_refused([&] {memory.write32(0xB0002000u,0x40000000u);},"decode command is not modeled");
    require(!(memory.load32_observed(0xB000F000u).value&0x100u),"idle reset and refused command do not fabricate IPU completion IRQ");
    device_refused([&] {(void)memory.read32(0xB0002020u);},"requires BCLR");
    memory.write64(0xB0002000u,0xFFFFFFFF0000007Full);
    require(memory.read32(0xB0002020u)==127 && memory.load32_observed(0xB0002010u).unknown==0x40003F00u &&
            (memory.load32_observed(0xB000F000u).value&0x100u),"BCLR sets BP, clears ECD/SCD and requests a real completion interrupt");
    require(!memory.intc_interrupt_pending(),"masked IPU completion remains latched without interrupt delivery");
    memory.write32(0xB000F010u,0x100u);
    require(memory.intc_interrupt_pending(),"unmasking the actual IPU source enables interrupt delivery");
    memory.write32(0xB0002010u,0x40000000u);
    require(memory.load32_observed(0xB000F000u).value&0x100u,"IPU reset preserves the independent INTC latch");
    memory.write32(0xB000F000u,0x100u);
    require(!(memory.load32_observed(0xB000F000u).value&0x100u) && !memory.intc_interrupt_pending(),
            "INTC acknowledges the completed command once");
    device_refused([&] {memory.write32(0xB0002000u,0x80u);},"reserved option");
    device_refused([&] {(void)memory.read32(0xB0002000u);},"no register model");
    device_refused([&] {(void)memory.read32(0xB0002020u);},"requires BCLR");
    memory.write32(0xB0002000u,0);
    require(memory.read32(0xB0002020u)==0,"new BCLR explicitly initializes bit position zero");
}

void iop_memory() {
    m20::Assembler program(0xBFC00000u);
    program.la(8,0xA0000100u);program.lw(9,0,8);program.nop();
    program.addiu(9,9,1);program.sw(9,4,8);
    program.label("spin");program.beq(0,0,"spin");program.nop();
    const auto words=program.finish();const auto bytes=m20::le_words(words);
    auto rom=m20::build_firmware().rom;
    std::copy(bytes.begin(),bytes.end(),rom.begin());
    EEState state;Memory memory;
    const ConsoleHardware profile{"SYNTHETIC","TEST","0100XD20261103",std::nullopt,
                                  ProfileEvidence::Measured,"authored shared-RAM test"};
    prepare_ee_boot(load_bios_rom0(rom),nullptr,state,memory,&profile);
    device_refused([&] {(void)memory.read32(0xBC000100u);},"has not been written");
    memory.write32(0xBC000100u,0x11223344u);
    require(memory.read32(0x9C000100u)==0x11223344u,"EE aliases share actual IOP RAM bytes");
    state.retired_instructions=words.size()*kEeCyclesPerIopCycle;
    require(memory.read32(0xBC000104u)==0x11223345u,
            "IOP executes a load, add and store using actual data written by the EE");
    memory.write64(0xBC000200u,0x1122334455667788ull);
    memory.write8(0xBC000203u,0xAAu);memory.write16(0xBC000206u,0xBEEFu);
    require(memory.read64(0x9C000200u)==0xBEEF3344AA667788ull,"byte/halfword overwrites preserve adjacent IOP RAM bytes");
    device_refused([&] {memory.write32(0xBC200000u,0);},"outside the 2 MiB");
    device_refused([&] {(void)memory.read32(0xBC000300u);},"has not been written");
    Iop stopped;std::fill(rom.begin(),rom.begin()+4u,0xFFu);
    stopped.reset(rom,&profile);stopped.run_until(1);
    require(stopped.stopped(),"invalid IOP instruction establishes an actual stop");
    rejects([&] {stopped.ee_write(0x100u,4,1);},"after the IOP stopped");
    rejects([&] {stopped.ee_write(0,16,0);},"requires 1/2/4/8");
}

void dma_suspend() {
    const std::vector<std::uint8_t> rom(4u*1024u*1024u);
    std::vector<std::uint8_t> ram(0x200u);
    Iop iop;iop.reset(rom,nullptr);iop.attach_ee_ram(ram);
    iop.ee_dma_write(0x1000E020u,0);iop.ee_dma_write(0x1000E000u,1);
    iop.ee_dma_write(0x1000C410u,0);iop.ee_dma_write(0x1000C420u,9);iop.ee_dma_write(0x1000C400u,0x101u);
    iop.run_until(2);
    require(iop.ee_dma_read(0x1000C410u)==32 && iop.ee_dma_read(0x1000C420u)==7,
            "SIF1 transfers actual qwords before suspension: "+iop.stop_reason().value_or("running")+
            " MADR="+std::to_string(iop.ee_dma_read(0x1000C410u))+" QWC="+std::to_string(iop.ee_dma_read(0x1000C420u)));
    rejects([&] {iop.ee_dma_write(0x1000C400u,0);},"write while busy");
    iop.set_ee_dma_suspended(true);iop.run_until(8);
    require(iop.ee_dma_read(0x1000C410u)==32 && iop.ee_dma_read(0x1000C420u)==7 &&
            (iop.ee_dma_read(0x1000C400u)&0x100u) && !(iop.ee_dma_read(0x1000E010u)&0x40u),
            "CPND holds source progress and does not manufacture completion");
    iop.set_ee_dma_suspended(false);iop.run_until(9);
    require(iop.ee_dma_read(0x1000C410u)==48 && iop.ee_dma_read(0x1000C420u)==6,"resume continues at the next source qword");
    iop.set_ee_dma_suspended(true);
    rejects([&] {iop.ee_dma_write(0x1000C410u,0x100u);},"write while busy");
    iop.ee_dma_write(0x1000C400u,0);
    require(!(iop.ee_dma_read(0x1000C400u)&0x100u) && !(iop.ee_dma_read(0x1000E010u)&0x40u),
            "documented suspended STR clear stops the transfer without a completion IRQ");
    iop.ee_dma_write(0x1000C410u,0x100u);iop.ee_dma_write(0x1000C420u,1);iop.ee_dma_write(0x1000C400u,0x101u);
    iop.run_until(10);require(iop.ee_dma_read(0x1000C410u)==0x100u,"rearmed DMA remains held until CPND clears");
    iop.set_ee_dma_suspended(false);iop.run_until(11);
    require(iop.ee_dma_read(0x1000C410u)==0x110u && (iop.ee_dma_read(0x1000E010u)&0x40u),"only the new transfer's real completion raises CIS");
    Iop receiver;receiver.reset(rom,nullptr);receiver.attach_ee_ram(ram);
    receiver.ee_dma_write(0x1000E020u,0);receiver.ee_dma_write(0x1000C020u,0);receiver.ee_dma_write(0x1000C000u,0x184u);
    receiver.ee_dma_write(0x1000C000u,0);
    require(!(receiver.ee_dma_read(0x1000C000u)&0x100u) && !(receiver.ee_dma_read(0x1000E010u)&0x20u),
            "empty receiver can stop at a tag boundary without a fabricated completion");
    receiver.ee_dma_write(0x1000C000u,0x184u);
    receiver.set_ee_dma_suspended(true);receiver.ee_dma_write(0x1000C000u,0);
    require(!(receiver.ee_dma_read(0x1000C000u)&0x100u) && !(receiver.ee_dma_read(0x1000E010u)&0x20u),
            "suspended SIF0 receiver can stop without a fabricated receive completion");
    EeRegisters registers;std::uint64_t clock=0;registers.set_clock(&clock);
    const auto* profile=find_console_hardware("SCPH-70004:GH-032-11");registers.reset(profile);
    const auto initial=registers.read(kDmacEnabler,4);
    registers.write(0x1000F590u,4,initial|0x10000u);
    require(registers.read(kDmacEnabler,4)==(initial|0x10000u),"D_ENABLER reflects CPND and preserves measured profile bits");
    rejects([&] {registers.write(0x1000F590u,4,initial^1u);},"CPND-only");
    require(registers.read(kDmacEnabler,4)==(initial|0x10000u),"refused control changes preserve the suspension state");
    registers.write(0x1000F590u,4,initial);require(registers.read(kDmacEnabler,4)==initial,"CPND can be restored through the actual bus register");
}

void diagnostic_boundary() {
    DiagnosticAssumptions assumptions;
    require(assumptions.empty(),"unselected experiments do not affect original boot classification");
    EeRegisters registers;
    registers.reset();
    rejects([&] {(void)registers.read(0x1F803800u,2);},"not been measured");
    for(const auto board : {"SCPH-70004:GH-032-11","SCPH-70004:GH-032-32"}) {
        const auto* profile=find_console_hardware(board);
        require(profile && profile->mpu_board_id==0xFFFFu,"matching PS2Ident records supply each SCPH-70004 MPU board ID");
        registers.reset(profile);
        require(registers.read(0x1F803800u,2)==0xFFFFu,"MPU register returns the selected board's measured 16-bit value");
        rejects([&] {(void)registers.read(0x1F803800u,4);},"16-bit read");
        m20::Assembler program(0xBFC00000u);
        program.lui(8,0xBF80u);program.lhu(9,0x3800,8);program.nop();
        program.la(10,0xA0000100u);program.sw(9,0,10);
        const auto words=program.finish();const auto bytes=m20::le_words(words);
        std::vector<std::uint8_t> rom(4u*1024u*1024u);
        std::copy(bytes.begin(),bytes.end(),rom.begin());
        Iop iop;iop.reset(rom,profile);iop.run_until(words.size());
        require(!iop.stopped() && iop.ee_read(0x100u,4)==0xFFFFu,
                "R3000A reads the same measured MPU ID and transfers its actual bytes to IOP RAM");
    }
    rejects([&] {registers.write(0x1000F510u,4,0);},"no register model");
    assumptions.omit_f510_zero_write=true;
    require(!assumptions.empty(),"omitting unknown F510 semantics always marks a diagnostic run");
    registers.reset(nullptr,{},nullptr,&assumptions);
    registers.write(0x1000F510u,4,0);
    rejects([&] {registers.write(0x1000F510u,4,1);},"zero initialization");
    rejects([&] {(void)registers.read(0x1000F510u,4);},"not verified");
    registers.reset();
    rejects([&] {registers.write(0x1000F510u,4,0);},"no register model");
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
        require(memory.read32(0x8000100Cu)==0x808u,"guest executes real VU control reset/readback in both engines");
        require(memory.observe_vu_data(0x4210u).value==0x1234u && !memory.observe_vu_data(0x4210u).unknown,
                "guest VISWR writes shared VU1 register state in both engines");
        require(state.unknown_gpr_bits[5]==~0ull && state.unknown_gpr_high_bits[5]==~0ull &&
                !memory.observe_vu_data(0x4020u).unknown && memory.observe_vu_data(0x4020u).value==0 &&
                memory.read_vu_control(17)==15,"guest vector spill, exact subtraction and VSQI agree in both engines");
        require(memory.read_vu_control(4)==0x806u,"guest VIADD uses actual VI operands in both engines");
        require(memory.read_vu_control(16)==1 && memory.read_vu_control(18)==0x1234u,
                "guest CTC2 updates sticky STATUS and CLIP in both engines");
        require(memory.read_vu_control(20)==0x4DEF21u && memory.read_vu_control(21)==0xABCDEF21u &&
                memory.read_vu_control(22)==0xABCDEF21u && memory.read_vu_control(27)==0xEF21u,
                "guest initializes scalar and microprogram address registers in both engines");
        require(memory.read32(0x80001010u)==0xDDEEFF00u && memory.read32(0xB0003C40u)==0x101u &&
                memory.read32(0xB0003C70u)==0xFFFFFFFFu,"guest VIF configuration transfers actual payload in both engines");
        require(memory.read32(0xB0003820u)==2 && memory.read32(0xB0003840u)==0x402u &&
                memory.read32(0xB0003890u)==0x3FFu && memory.read32(0xB0003830u)==0x4321u,
                "guest initializes VIF0 from actual MMIO and FIFO transfers in both engines");
        require(memory.read32(0x80001014u)==0 && memory.load32_observed(0xB0002010u).unknown==0x40003F00u &&
                memory.read32(0xB0002020u)==31,
                "guest polls actual IPU BUSY after reset without resolving unrelated unknown bits");
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
    const auto identity_bios=load_bios_rom0(fixture.rom);
    auto measured=ConsoleHardware{"SYNTHETIC","GS-IDENT",identity_bios.version.romver};
    measured.gs_revision_id=std::array<std::uint8_t,2>{0x37,0xA5};
    prepare_ee_boot(load_bios_rom0(fixture.rom),nullptr,state,memory,&measured);
    state.set_reg64(8,kGsCsr);
    ee::exec<MipsOpcode::Ld>(state,memory,0,(0x37u<<26)|(8u<<21)|(9u<<16));
    require((state.unknown_gpr_bits[9]&0xFFFF0000ull)==0 && (state.unknown_gpr_bits[9]&(1u<<13)),
            "CSR exposes measured identity while FIELD stays unknown");
    ee::exec<MipsOpcode::Dsrl>(state,memory,0,(9u<<16)|(9u<<11)|(16u<<6)|0x3Au);
    ee::exec<MipsOpcode::Andi>(state,memory,0,(0xCu<<26)|(9u<<21)|(9u<<16)|0xFFu);
    require(state.reg64(9)==0x37u,"guest LD/DSRL/ANDI obtains exactly the measured GS revision");
    prepare_ee_boot(load_bios_rom0(fixture.rom),nullptr,state,memory);
    require((memory.load64_observed(kGsCsr).unknown&0xFFFF0000ull)==0xFFFF0000ull,
            "no profile means no invented GS identity or inherited prior profile");
    const auto csr=memory.load64_observed(kGsCsr);
    require((csr.value&0xC000u)==0x4000u && !(csr.unknown&0xC000u) && (csr.unknown&(1u<<12u)),
            "CSR exposes only the empty synchronous input FIFO while NFIELD remains opaque");
    state.set_reg64(8,kGsCsr);
    ee::exec<MipsOpcode::Ld>(state,memory,0,(0x37u<<26)|(8u<<21)|(9u<<16));
    ee::exec<MipsOpcode::Andi>(state,memory,0,(0xCu<<26)|(9u<<21)|(9u<<16)|0xC000u);
    require(state.reg64(9)==0x4000u && !state.unknown_gpr_bits[9],
            "guest LD/ANDI can poll the proven FIFO bits without consuming other unknown CSR fields");
    const auto send=[&](unsigned index,std::uint64_t data) {
        memory.write128(kGifFifo,1u|(1ull<<15u)|(1ull<<60u),0xEu);
        memory.write128(kGifFifo,data,index);
    };
    send(0x50,1ull<<48u);send(0x51,0);send(0x52,2u|(2ull<<32u));send(0x53,0);
    send(0x61,0);send(0x54,0x2222222211111111ull);
    const auto pending=memory.load64_observed(kGsCsr);
    require((pending.value&0xC002u)==0x4000u && !(pending.unknown&0xC002u),
            "consumed FIFO data stays empty while FINISH waits for future upload words");
    send(0x54,0x4444444433333333ull);
    require((memory.load64_observed(kGsCsr).value&0xC002u)==0x4002u,
            "the final actual upload raises FINISH independently of the empty input FIFO");
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
        else if (mode == "vu-control") vu_control();
        else if (mode == "vif-control") vif_control();
        else if (mode == "ipu-control") ipu_control();
        else if (mode == "diagnostic-boundary") diagnostic_boundary();
        else if (mode == "iop-memory") iop_memory();
        else if (mode == "dma-suspend") dma_suspend();
        else if (mode == "held-counter") held_counter();
        else if (mode == "dma-setup") dma_setup();
        else if (mode == "guest") guest(argv[2]); else throw std::runtime_error("unknown GS events test");
#endif
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
