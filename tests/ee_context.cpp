#include "boot_checks.hpp"
#include "bios_program.hpp"
#include "ps2native/interpreter.hpp"
#include "ps2native/ee_fpu.hpp"
#include <iostream>

using namespace ps2native;
using namespace boot_checks;
namespace {
void privilege_and_cache() {
    Memory memory;
    const auto rom=m20::build_firmware().rom;
    memory.reset_for_boot(rom);
    // A global kuseg mapping and a supervisor mapping, each with two 4 KiB pages.
    memory.tlb_write(0,{0,0x00100000u,0x1Fu,0x5Fu});
    memory.tlb_write(1,{0,0xC0000000u,0x1Fu,0x5Fu});
    memory.write32(0x80000000u,0x12345678);
    memory.set_cpu_context(0x10u,0);
    require(memory.read32(0x00100000u)==0x12345678,"user translates kuseg through TLB");
    bus_fault([&]{(void)memory.fetch32(0x80000000u);},BusFault::Privilege,"privilege mode");
    bus_fault([&]{memory.write32(0xA0000000u,9);},BusFault::Privilege,"privilege mode");
    bus_fault([&]{(void)memory.read32(0xC0000000u);},BusFault::Privilege,"privilege mode");
    memory.set_cpu_context(0x08u,0);
    require(memory.read32(0xC0000000u)==0x12345678,"supervisor translates sseg");
    for(const auto address:{0x80000000u,0xA0000000u,0xE0000000u})
        bus_fault([&]{(void)memory.read32(address);},BusFault::Privilege,"privilege mode");
    memory.set_cpu_context(0x10u|kStatusEXL,0);
    require(memory.read32(0x80000000u)==0x12345678,"EXL overrides user KSU");
    memory.set_cpu_context(0x10u|kStatusERL,0);
    require(memory.read32(0x80000000u)==0x12345678,"ERL overrides user KSU");
    require(memory.read32(0)==0x12345678,"ERL kuseg identity mapping, without TLB entry");
    memory.write32(4,0x87654321);
    require(memory.read32(0x80000004u)==0x87654321,"ERL identity stores reach same RAM");
    bus_fault([&]{(void)memory.read32(0x20000000u);},BusFault::Unmapped,"unmapped physical address");
    memory.set_cpu_context(0x10u,0);
    bus_fault([&]{(void)memory.read32(0);},BusFault::TlbRefill,"no entry");
    memory.set_cpu_context(0,0);
    EEState state;state.pc=0x80001000u;std::uint64_t executed=0;
    ExecutionContext ctx{state,memory,executed,100,0xFFFFFFFFu};
    for(const auto op:{0x04u,0x07u,0x0Cu,0x12u,0x14u,0x16u}) {
        memory.write32(state.pc,0xBC000000u|(op<<16u)); // Index using unmapped VA 0
        interpret_instruction(ctx);
    }
    require(executed==6,"cache indices do not look up a TLB tag");
    for(const auto op:{0x18u,0x1Au,0x1Cu}) {
        memory.write32(state.pc,0xBC000000u|(op<<16u));
        bus_fault([&]{interpret_instruction(ctx);},BusFault::TlbRefill,"no entry");
        state.pc=0x80001000u;
    }
    state.pc=0x00100000u;state.set_cp0(Cop0::Status,0x10u);
    // Restore the word via a kernel alias before attempting the user MFC0.
    memory.set_cpu_context(0,0);memory.write32(0x80000000u,0x40026000u);
    rejects([&]{interpret_instruction(ctx);},"COP0 unusable");
    memory.set_cpu_context(0,0);memory.write32(0x80000000u,0xBC140000u);
    state.pc=0x00100000u;
    rejects([&]{interpret_instruction(ctx);},"COP0 unusable");
    require(executed==6,"privileged COP0 cannot silently execute in user mode");
}
void floating_comparisons() {
    Memory memory;memory.reset_for_boot(m20::build_firmware().rom);
    struct Row {std::uint32_t a,b;bool equal,less;};
    const Row rows[]={{0,0x80000000u,true,false},{0,1,true,false},
        {0x80001337u,0,true,false},{0,0x00800000u,false,true},
        {0x7F800000u,0x7F800001u,false,true},{0x7FFFFFFFu,0x7FFFFFFFu,true,false},
        {0xFFFFFFFFu,0x7FFFFFFFu,false,true},{0xBF800000u,0xC0000000u,false,false},
        {0xC0000000u,0xBF800000u,false,true},{0x3F800000u,0x40000000u,false,true}};
    for(const auto& r:rows)for(unsigned condition:{0u,2u,4u,6u})for(auto flags:{0x0103C079u,0x0183C079u}) {
        EEState state;state.pc=0x80001000u;state.set_cp0(Cop0::Status,kStatusCU1);
        state.fpr[0]=r.a;state.fpr[1]=r.b;state.fcr31=flags;
        const auto before=state.fpr;memory.write32(state.pc,0x46010030u|condition);
        std::uint64_t count=0;ExecutionContext ctx{state,memory,count,10,0xFFFFFFFFu};
        interpret_instruction(ctx);
        const bool expected=((condition&2u)&&r.equal)||((condition&4u)&&r.less);
        require(state.fcr31==((flags&~0x800000u)|(expected?0x800000u:0u)) && state.fpr==before,
            "EE comparison uses signed magnitude, flushed denormals and finite exponent 255; only FCC changes");
    }
    for(unsigned kind=0;kind<4;++kind) {
        const auto d=decode_mips(0x1000u,0x45000002u|(kind<<16));
        require(d.is_conditional_branch() && d.has_delay_slot() && d.is_likely_branch()==(kind>=2u) && !d.written_gpr(),
            "COP1 branch metadata preserves CFG delay-slot and likely semantics");
    }
    for(auto word:{0x46010070u,0x46010031u,0x45040000u})
        require(!try_decode_r5900(0,word),"reserved comparison destination, condition and FCC encodings refuse");
    for(auto word:{0x46010034u,0x45010002u}) {
        EEState state;state.pc=0x80001000u;state.fcr31=0x0183C079u;
        memory.write32(state.pc,word);std::uint64_t count=0;ExecutionContext ctx{state,memory,count,10,0xFFFFFFFFu};
        rejects([&]{interpret_instruction(ctx);},"Status.CU1 = 0");
        require(state.fcr31==0x0183C079u && state.instruction_pc==0x80001000u && count==0 && !state.delay_pending,
            "disabled COP1 cannot change FCC or schedule a branch");
    }
}
void nested_exception() {
    EEState state;state.set_cp0(Cop0::Cause,0x80008000u);state.set_cp0(Cop0::Epc,0x12345678u);
    state.set_cp0(Cop0::Status,kStatusEXL|kStatusBEV|0x10u);
    state.delay_pending=true;state.delay_indirect=true;state.indirect_entry_pending=true;
    enter_ee_exception(state,9,0x200004u,true);
    require(state.pc==0xBFC00380u && state.cp0(Cop0::Epc)==0x12345678u &&
            state.cp0(Cop0::Cause)==0x80008024u,"nested EXL preserves original EPC/BD and pending interrupts");
    require(!state.delay_pending && !state.delay_indirect && !state.indirect_entry_pending,"nested trap cancels branch bookkeeping");
    const auto cause=state.cp0(Cop0::Cause);
    rejects([&]{enter_ee_exception(state,32,0,false);},"exception code");
    require(state.cp0(Cop0::Cause)==cause,"invalid exception leaves state intact");
    state.set_cp0(Cop0::Status,0x10u);
    enter_ee_exception(state,8,0x200000u,false);
    require(state.pc==0x80000180u && state.cp0(Cop0::Epc)==0x200000u &&
            state.cp0(Cop0::Cause)==0x8020u,"fresh exception clears BD and preserves IP");
}
void addi_exception() {
    Memory memory; memory.reset_for_boot(m20::build_firmware().rom);
    EEState state; std::uint64_t executed=0;
    constexpr std::uint32_t pc=0x80001004u;
    const auto run=[&](std::uint32_t source,std::uint16_t immediate,unsigned target,bool delay,bool bev) {
        state={};state.pc=pc;state.set_cp0(Cop0::Status,bev?kStatusBEV:0u);
        state.set_reg128(8,source,0x1122334455667788ull);
        state.set_reg128(9,0x12345678u,0x8877665544332211ull);
        state.delay_pending=delay;state.delay_indirect=delay;state.delay_target=0x80002000u;
        memory.set_cpu_context(0,0);
        const auto word=0x21000000u|(target<<16u)|immediate;
        memory.write32(pc,word);
        require(decode_r5900(pc,word).opcode==MipsOpcode::Addi && !mips_aot_supported(MipsOpcode::Addi),
                "ADDI is an explicit interpreter boundary until AOT exception exits are supported");
        ExecutionContext ctx{state,memory,executed,100,0xFFFFFFFFu};
        interpret_instruction(ctx);
    };
    run(16,0xFFE0u,8,false,false);
    require(state.reg64(8)==0xFFFFFFFFFFFFFFF0ull && state.gpr[8].high==0x1122334455667788ull &&
            state.pc==pc+4 && state.retired_instructions==1,"ADDI sign extends a word and preserves the upper 64 bits");
    run(0x7FFFFFFFu,1,9,false,false);
    require(state.pc==0x80000180u && state.cp0(Cop0::Epc)==pc && state.cp0(Cop0::Cause)==48 &&
            state.reg64(9)==0x12345678u && state.gpr[9].high==0x8877665544332211ull,
            "positive ADDI overflow vectors without modifying the destination");
    run(0x80000000u,0xFFFFu,0,true,true);
    require(state.pc==0xBFC00380u && state.cp0(Cop0::Epc)==pc-4 && state.cp0(Cop0::Cause)==0x80000030u &&
            !state.delay_pending && !state.indirect_entry_pending && state.reg64(0)==0,
            "negative overflow traps even for zero destination and cancels the pending branch");
    run(0x80000000u,1,9,true,false);
    require(state.reg64(9)==0xFFFFFFFF80000001ull && state.pc==0x80002000u &&
            state.indirect_entry_pending,"non-overflow ADDI retires a real delay slot");
    state={};state.pc=pc;state.set_reg64_observed(8,0,1);
    memory.set_cpu_context(0,0);memory.write32(pc,0x21090001u);
    ExecutionContext ctx{state,memory,executed,100,0xFFFFFFFFu};
    rejects([&]{interpret_instruction(ctx);},"consumes unknown MMIO bits");
    require(state.pc==pc && state.retired_instructions==0,"unknown arithmetic does not fabricate a sum or exception");
}
void signed_arithmetic() {
    Memory memory;memory.reset_for_boot(m20::build_firmware().rom);
    struct Case {std::uint32_t word;std::uint64_t a,b,result;bool overflow;};
    constexpr Case cases[]{
        {0x01094020u,0x7FFFFFFFu,1,0,true}, // ADD
        {0x01094020u,0x80000000u,0xFFFFFFFFu,0,true},
        {0x01094020u,0x80000000u,1,0xFFFFFFFF80000001ull,false},
        {0x01094022u,0x7FFFFFFFu,0xFFFFFFFFu,0,true}, // SUB
        {0x01094022u,0x80000000u,1,0,true},
        {0x01094022u,0x80000000u,0x80000000u,0,false},
        {0x0109402Cu,0x7FFFFFFFFFFFFFFFull,1,0,true}, // DADD
        {0x0109402Cu,0x8000000000000000ull,~0ull,0,true},
        {0x0109402Cu,0x8000000000000000ull,1,0x8000000000000001ull,false},
        {0x0109402Eu,0x7FFFFFFFFFFFFFFFull,~0ull,0,true}, // DSUB
        {0x0109402Eu,0x8000000000000000ull,1,0,true},
        {0x0109402Eu,0x8000000000000000ull,0x8000000000000000ull,0,false},
        {0x61080001u,0x7FFFFFFFFFFFFFFFull,0,0,true}, // DADDI
        {0x6108FFFFu,0x8000000000000000ull,0,0,true},
        {0x6108FFFFu,0,0,~0ull,false},
    };
    for(const auto& c:cases) {
        EEState state;state.pc=0x80001000u;
        state.set_reg128(8,c.a,0x0123456789ABCDEFull);state.set_reg64(9,c.b);
        memory.set_cpu_context(0,0);memory.write32(state.pc,c.word);
        std::uint64_t executed=0;ExecutionContext ctx{state,memory,executed,4,0};
        const auto instruction=decode_r5900(state.pc,c.word);
        require(instruction.written_gpr()==8u && !mips_aot_supported(instruction.opcode),
                "signed arithmetic decode records the correct destination and interpreter boundary");
        interpret_instruction(ctx);
        require(state.gpr[8].high==0x0123456789ABCDEFull && executed==1,
                "signed arithmetic preserves the upper GPR half and accounts for one instruction");
        if(c.overflow)require(state.pc==0x80000180u && state.cp0(Cop0::Cause)==48 &&
                              state.reg64(8)==c.a,"signed overflow preserves aliased destination");
        else require(state.pc==0x80001004u && state.reg64(8)==c.result,"signed arithmetic result at representable limits");
    }
    for(const auto fn:{0x20u,0x22u,0x2Cu,0x2Eu})
        require(!try_decode_r5900(0,0x01094040u|fn),"signed register arithmetic rejects reserved shift bits");
}
void packed_copy() {
    require(decode_mips(0,0x700006E9u).opcode==MipsOpcode::Pcpyh,"PCPYH is AOT-capable");
    require(!try_decode_r5900(0,0x702006E9u),"PCPYH rejects reserved rs");
    EEState state; Memory memory;
    state.set_reg128(8,0xFEDCBA9876543210ull,0x0123456789ABCDEFull);
    const auto word=0x700006E9u|(8u<<16u)|(8u<<11u);
    ee::exec<MipsOpcode::Pcpyh>(state,memory,0,word);
    require(state.reg128(8).low==0x3210321032103210ull && state.reg128(8).high==0xCDEFCDEFCDEFCDEFull,
            "PCPYH source/destination alias and independent 64-bit halves");
    ee::exec<MipsOpcode::Pcpyh>(state,memory,0,0x700006E9u|(8u<<11u));
    require(state.reg128(8).low==0 && state.reg128(8).high==0,"PCPYH zero source");
    ee::exec<MipsOpcode::Pcpyh>(state,memory,0,word&~(31u<<11u));
    require(state.reg128(0).low==0 && state.reg128(0).high==0,"PCPYH zero destination");
    state.set_reg128(8,0x0123456789ABCDEFull,~0ull);
    state.set_reg128(9,0xFEDCBA9876543210ull,0);
    constexpr auto copy=0x70000389u|(8u<<21u)|(9u<<16u)|(8u<<11u);
    require(decode_mips(0,copy).opcode==MipsOpcode::Pcpyld,"PCPYLD decoding and AOT support");
    ee::exec<MipsOpcode::Pcpyld>(state,memory,0,copy);
    require(state.reg128(8).low==0xFEDCBA9876543210ull && state.reg128(8).high==0x0123456789ABCDEFull,
            "PCPYLD combines low doublewords without alias corruption");
    const auto seed=[&]{state.set_reg128(8,0x2222222211111111ull,0x4444444433333333ull);
                       state.set_reg128(9,0x6666666655555555ull,0x8888888877777777ull);};
    const auto check=[&](MipsOpcode op,std::uint32_t base,std::uint64_t low,std::uint64_t high) {
        seed();const auto w=base|(8u<<21u)|(9u<<16u)|(8u<<11u);
        require(decode_mips(0,w).opcode==op,"MMI packing decode");
        switch(op){
            case MipsOpcode::Pcpyud:ee::exec<MipsOpcode::Pcpyud>(state,memory,0,w);break;
            case MipsOpcode::Pextlw:ee::exec<MipsOpcode::Pextlw>(state,memory,0,w);break;
            case MipsOpcode::Pextuw:ee::exec<MipsOpcode::Pextuw>(state,memory,0,w);break;
            case MipsOpcode::Ppacw:ee::exec<MipsOpcode::Ppacw>(state,memory,0,w);break;
            default:throw std::runtime_error("invalid packing test");
        }
        require(state.reg128(8).low==low && state.reg128(8).high==high,"MMI distinct source lanes, aliasing destination");
    };
    check(MipsOpcode::Pcpyud,0x700003A9u,0x4444444433333333ull,0x8888888877777777ull);
    check(MipsOpcode::Pextlw,0x70000488u,0x1111111155555555ull,0x2222222266666666ull);
    check(MipsOpcode::Pextuw,0x700004A8u,0x3333333377777777ull,0x4444444488888888ull);
    check(MipsOpcode::Ppacw,0x700004C8u,0x7777777755555555ull,0x3333333311111111ull);
    state.set_reg128(8,0x0011223344556677ull,0x80FF7F00000100FFull);
    state.set_reg128(9,0x0101010101010101ull,0x0202020202020202ull);
    const auto sub=0x70000248u|(8u<<21u)|(9u<<16u)|(8u<<11u);
    require(decode_mips(0,sub).opcode==MipsOpcode::Psubb,"PSUBB decodes as an AOT operation");
    ee::exec<MipsOpcode::Psubb>(state,memory,0,sub);
    require(state.reg128(8).low==0xFF10213243546576ull && state.reg128(8).high==0x7EFD7DFEFEFFFEFDull,
            "PSUBB wraps each byte independently without cross-lane borrow");
    const auto subw=0x70000048u|(8u<<21u)|(9u<<16u)|(8u<<11u);
    require(decode_mips(0,subw).opcode==MipsOpcode::Psubw && decode_mips(0,subw).written_gpr()==8,
            "PSUBW decoder and destination metadata");
    state.set_reg128(8,0x8000000000000000ull,0xFFFFFFFF7FFFFFFFull);
    state.set_reg128(9,0x0000000100000001ull,0xFFFFFFFFFFFFFFFFull);
    ee::exec<MipsOpcode::Psubw>(state,memory,0,subw);
    require(state.reg128(8).low==0x7FFFFFFFFFFFFFFFull && state.reg128(8).high==0x0000000080000000ull,
            "PSUBW independent modulo-32 lanes, signed boundaries and aliased destination");
    ee::exec<MipsOpcode::Psubw>(state,memory,0,subw&~(31u<<11));
    require(state.reg128(0).low==0 && state.reg128(0).high==0,"PSUBW preserves zero register");
}
void leading_sign_count() {
    constexpr auto word=0x70000004u|(8u<<21)|(8u<<11);
    require(decode_mips(0,word).opcode==MipsOpcode::Plzcw && decode_mips(0,word).written_gpr()==8,
            "PLZCW decoder and destination metadata");
    require(!try_decode_r5900(0,word|(1u<<16)) && !try_decode_r5900(0,word|0x40),"PLZCW reserved fields rejected");
    Memory memory;EEState state;
    for(unsigned bit=0;bit<31;++bit) {
        const std::uint32_t lane=1u<<bit;
        state.set_reg128(8,std::uint64_t{lane^0xFFFFFFFFu}<<32|lane,0x0123456789ABCDEFull);
        ee::exec<MipsOpcode::Plzcw>(state,memory,0,word);
        const auto count=30u-bit;
        require(state.reg64(8)==(std::uint64_t{count}<<32|count) && state.reg128(8).high==0x0123456789ABCDEFull,
                "PLZCW every leading-sign transition, opposite signs, alias and upper-half preservation");
    }
    state.set_reg128(8,0xFFFFFFFF00000000ull,0xFEDCBA9876543210ull);
    ee::exec<MipsOpcode::Plzcw>(state,memory,0,word);
    require(state.reg64(8)==0x0000001F0000001Full,"PLZCW uniform zero/one words count 31");
    state.set_reg64(8,0x000FFF0FFF0FF00Full);
    ee::exec<MipsOpcode::Plzcw>(state,memory,0,word);
    require(state.reg64(8)==0x0000000B00000007ull,"PLZCW documented manual example");
    ee::exec<MipsOpcode::Plzcw>(state,memory,0,word&~(31u<<11));
    require(state.reg64(0)==0,"PLZCW cannot change zero register");
}
void disabled_cop0_context() {
    Memory memory;memory.reset_for_boot(m20::build_firmware().rom);
    auto state=ee_reset_state();state.pc=0x80001000u;state.set_reg32(16,0x80002000);
    std::uint64_t executed=0;ExecutionContext ctx{state,memory,executed,100,0xFFFFFFFFu};
    const auto step=[&](std::uint32_t word){memory.write32(state.pc,word);interpret_instruction(ctx);};
    step(0x4008C000);step(0x4009C800); // MFBPC/MFPS, disabled with undefined configuration
    require(state.unknown_gpr_bits[8]==0x17BF8007u && state.unknown_gpr_bits[9]==0xFFBFEu,
            "COP0 reset preserves undefined configuration but knows enables and reserved zeros");
    step(0x70000488u|(9u<<21)|(8u<<16)|(10u<<11)); // PEXTLW: BPC, PCCR
    step(0xFE0A0000u); // SD t2,0(s0)
    require(memory.load64_observed(0x80002000).unknown==0x000FFBFE17BF8007ull,
            "packed context save preserves exact unknown bits in RAM");
    step(0x8E0B0000);step(0x8E0C0004); // LW t3/t4 from saved fields
    step(0x408BC000);step(0x408CC800);
    require(state.bpc_unknown==0x17BF8007u && state.pccr_unknown==0xFFBFEu,"disabled COP0 roundtrip remains symbolic");
    state.set_reg32(11,0x80000000u);
    rejects([&]{step(0x408BC000);},"breakpoint enable");
    rejects([&]{step(0x408BC800);},"counter enable");
    require(state.bpc_unknown==0x17BF8007u && state.pccr_unknown==0xFFBFEu,"refused activation leaves configuration intact");
    state.set_reg32(11,0);step(0x408BC000);step(0x408BC800);
    step(0x4008C000);step(0x4009C800);
    require(state.reg64(8)==0 && state.reg64(9)==0,"explicit zero writes establish known disabled controls");
    state.set_reg128_observed(8,1,2,0xFF00,0xFF0000);
    state.set_reg128_observed(9,3,4,0xF0000000,0xF000000000000000ull);
    ee::exec<MipsOpcode::Pcpyud>(state,memory,0,0x700003A9u|(8u<<21)|(9u<<16)|(8u<<11));
    require(state.unknown_gpr_bits[8]==0xFF0000 && state.unknown_gpr_high_bits[8]==0xF000000000000000ull,
            "PCPYUD transports only selected halves, including alias and upper uncertainty");
    ee::exec<MipsOpcode::Ppacw>(state,memory,0,0x700004C8u|(8u<<21)|(9u<<16)|(8u<<11));
    require(state.unknown_gpr_bits[8]==0xF0000000 && state.unknown_gpr_high_bits[8]==0xFF0000,
            "PPACW discards unselected unknown lanes while preserving selected ones");
}
void symbolic_shifts() {
    EEState s;Memory memory;
    // Saved BPC/PCCR pair: restoring PCCR discards all BPC bits.
    s.set_reg128_observed(2,0,0x12345678,0x000FFBFE17BF8007ull,0xFF);
    ee::exec<MipsOpcode::Dsra32>(s,memory,0,0x0002103Fu);
    require(s.unknown_gpr_bits[2]==0xFFBFEu && s.gpr[2].low==0 &&
            s.gpr[2].high==0x12345600 && s.unknown_gpr_high_bits[2]==0xFF,
            "DSRA32 extracts symbolic PCCR without consuming discarded BPC or upper GPR bits");
    s.set_reg64_observed(2,0,1);
    ee::exec<MipsOpcode::Sll>(s,memory,0,(2u<<16)|(3u<<11)|(31u<<6));
    require(s.unknown_gpr_bits[3]==0xFFFFFFFF80000000ull,"SLL extends uncertainty when it reaches the sign bit");
    s.set_reg64_observed(2,0,0x8000000000000000ull);
    ee::exec<MipsOpcode::Dsra32>(s,memory,0,(2u<<16)|(3u<<11)|(31u<<6));
    require(s.unknown_gpr_bits[3]==~0ull,"DSRA32 replicates unknown sign across all 64 bits");
    ee::exec<MipsOpcode::Dsrl32>(s,memory,0,(2u<<16)|(3u<<11)|(31u<<6));
    require(s.unknown_gpr_bits[3]==1,"DSRL32 fills with known zeros");
    s.set_reg64_observed(2,0x80000000,1);
    s.set_reg64_observed(4,31,~31ull);
    ee::exec<MipsOpcode::Srav>(s,memory,0,(4u<<21)|(2u<<16)|(3u<<11));
    require(s.reg64(3)==~0ull,"SRAV uses only low five count bits and discards unknown payload bit");
    s.set_reg64_observed(4,0,1);
    rejects([&]{ee::exec<MipsOpcode::Dsllv>(s,memory,0,(4u<<21)|(2u<<16)|(3u<<11));},"unknown shift count");
    require(s.reg64(3)==~0ull,"refused unknown shift count leaves destination unchanged");
}
void register_context() {
    EEState state;Memory memory;
    memory.reset_for_boot(m20::build_firmware().rom);
    state.pc=0x80001000u;state.set_cp0(Cop0::Status,kStatusCU1);
    state.set_reg128(8,0xFEDCBA98DEADBEEFull,0x1122334455667788ull);
    state.set_reg32(10,0xFFFFFFFFu);
    state.shift_amount=72;
    const std::vector<std::uint32_t> words{
        0x44880800u,0x44090800u, // MTC1 t0,f1; MFC1 t1,f1
        0x44CAF800u,0x444BF800u, // CTC1 t2,FCR31; CFC1 t3,FCR31
        0x00004028u,0,0,0,      // MFSA t0; hazard spacing
        0x01000029u,0,0,0,      // MTSA t0; hazard spacing
        0x00006028u};           // MFSA t4
    for(std::size_t i=0;i<words.size();++i)memory.write32(state.pc+static_cast<std::uint32_t>(i*4u),words[i]);
    std::uint64_t executed=0;ExecutionContext ctx{state,memory,executed,100,0xFFFFFFFFu};
    for(std::size_t i=0;i<words.size();++i)interpret_instruction(ctx);
    require(state.fpr[1]==0xDEADBEEFu && state.reg64(9)==0xFFFFFFFFDEADBEEFull,"FPU raw transfers sign extend GPR results");
    require(state.fcr31==0x0183C079u && state.reg32(11)==0x0183C079u,"FCR31 writable and fixed fields");
    memory.write32(0x80003000u,0x46000886u); // MOV.S f2,f1
    const auto saved_pc=state.pc;state.pc=0x80003000u;interpret_instruction(ctx);state.pc=saved_pc;
    require(state.fpr[2]==0xDEADBEEFu && state.fcr31==0x0183C079u,"MOV.S preserves raw payload and all FCR31 flags");
    // Raw physical sign-operation vectors from ps2autotests arithmetic.expected.
    const std::uint32_t signs[][3]={{0,0,0x80000000u},{0x80000000u,0,0},
        {0x00001337u,0x00001337u,0x80001337u},{0x7F800001u,0x7F800001u,0xFF800001u},
        {0xFFFFFFFFu,0x7FFFFFFFu,0x7FFFFFFFu},{0xDEADBEEFu,0x5EADBEEFu,0x5EADBEEFu}};
    for(const auto& row:signs)for(unsigned op=0;op<2;++op) {
        state.pc=0x80003000u;state.fpr[1]=row[0];state.fcr31=0x0183C079u;
        memory.write32(state.pc,op ? 0x46000847u : 0x46000845u); // fd == fs
        interpret_instruction(ctx);
        require(state.fpr[1]==row[op+1] && state.fcr31==0x01830079u,
                "ABS/NEG preserve raw magnitude and sticky flags with aliased destination");
    }
    require(!try_decode_r5900(0,0x46010845u) && !try_decode_r5900(0,0x46010847u),
            "sign operations reject reserved ft fields");
    state.pc=0x80003000u;state.set_cp0(Cop0::Status,0);state.fpr[1]=0x1234;
    rejects([&]{interpret_instruction(ctx);},"Status.CU1 = 0");
    require(state.fpr[1]==0x1234,"disabled FPU sign operation preserves destination");
    state.set_cp0(Cop0::Status,kStatusCU1);state.pc=saved_pc;
    require(state.shift_amount==72 && state.reg64(12)==72 && state.reg128(8).high==0x1122334455667788ull,
            "SA context save/restore preserves full GPR upper half");
    require(!try_decode_r5900(0,0x444BF801u) && !try_decode_r5900(0,0x00004868u),"reserved COP1/SA transfer fields rejected");
    memory.write32(0x80002000u,0x4601009Cu); // MADD.S f2,f0,f1
    state.pc=0x80002000u;state.fpr[0]=0x80000000u;state.fpr[1]=0x40000000u;state.fpu_acc=0x3FA00000u;
    interpret_instruction(ctx);
    require(state.fpr[2]==state.fpu_acc && state.fpu_acc==0x3FA00000u,"zero product copies actual accumulator without changing it");
    state.pc=0x80002000u;state.fpr[0]=0x3F800000u;
    rejects([&]{interpret_instruction(ctx);},"multiplier rounding not modeled");
    require(state.fpr[2]==0x3FA00000u,"unsupported nonzero product cannot silently return a substitute value");
    memory.write32(0x80002000u,0x46010018u); // ADDA.S f0,f1
    state.pc=0x80002000u;state.fpr[0]=0xC0200000u;state.fpr[1]=0x80000000u;
    interpret_instruction(ctx);
    require(state.fpu_acc==0xC0200000u,"ADDA with negative zero restores the actual accumulator payload");
    state.pc=0x80002000u;state.fpr[0]=0x80000000u;
    interpret_instruction(ctx);require(state.fpu_acc==0x80000000u,"ADDA preserves two negative zeros");
    state.pc=0x80002000u;state.fpr[0]=0;
    interpret_instruction(ctx);require(state.fpu_acc==0,"ADDA opposite zero signs produce positive zero");
    state.set_reg128(8,0x0123456789ABCDEFull,0xFEDCBA9876543210ull);
    ee::exec<MipsOpcode::Pmthi>(state,memory,0,0x71000229u);
    ee::exec<MipsOpcode::Pmtlo>(state,memory,0,0x71000269u);
    ee::exec<MipsOpcode::Pmfhi>(state,memory,0,0x70004A09u);
    ee::exec<MipsOpcode::Pmflo>(state,memory,0,0x70005249u);
    require(state.reg128(9).low==state.reg128(8).low && state.reg128(9).high==state.reg128(8).high &&
            state.reg128(10).low==state.reg128(8).low && state.reg128(10).high==state.reg128(8).high,
            "128-bit HI/LO context moves preserve both pipelines");
}
void adder() {
    // Numeric observations in ps2autotests ee_fpu/arithmetic.expected.
    const std::uint32_t observed[][3]{
        {0,0x80000000,0},{0x80000000,0x80000000,0x80000000},
        {0x3F800000,0x3F800000,0x40000000},{0x40400000,0x3F800000,0x40800000},
        {0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF},{0x7FFFFFFF,0xFFFFFFFF,0},
        {0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF},{0,0x7F800000,0x7F800000},
        {0x7F800000,0x7F800000,0x7FFFFFFF},{0x3FFFFFFF,0x3F800000,0x403FFFFF},
        {0x7F800001,0x3F800000,0x7F800001},{1,0x3F800000,0x3F800000},
        {0x1337,0xDEADBEEF,0xDEADBEEF},{0,0xBF800000,0xBF800000},
        {0x3FFFFFFF,0xBF800000,0x3F7FFFFE},{0x3F800000,0xBFFFFFFF,0xBF7FFFFE},
    };
    for(const auto& c:observed){std::uint32_t flags=0;require(ee_fpu_add(c[0],c[1],flags)==c[2],"EE adder physical result vector");}
    std::uint32_t flags=0x0183C079u;
    require(ee_fpu_add(0x3F800000,0xBE800001,flags)==0x3F400000 && flags==0x01830079u,
            "one guard bit drops the small addend tail before cancellation, preserving I/D and sticky flags");
    require(ee_fpu_add(0x00800001,0x80800000,flags)==0 && (flags&0xC008u)==0x4008u,
            "a tiny nonzero difference flushes and sets underflow");
    require(ee_fpu_add(0x7FFFFFFF,0x7FFFFFFF,flags)==0x7FFFFFFF && (flags&0xC018u)==0x8018u,
            "extended exponent overflow saturates and retains sticky underflow");
    require(ee_fpu_add(0x80000001,0,flags)==0 && !(flags&0xC000u),"denormal operands are signed zeros");
    Memory memory;memory.reset_for_boot(m20::build_firmware().rom);
    EEState state;state.pc=0x80001000;state.set_cp0(Cop0::Status,kStatusCU1);
    state.fpr[0]=0x3F800000;state.fpr[1]=0x3E800001;
    memory.write32(state.pc,0x46010001u); // SUB.S f0,f0,f1, aliased destination
    memory.write32(state.pc+4,0x46010000u); // ADD.S f0,f0,f1
    std::uint64_t executed=0;ExecutionContext ctx{state,memory,executed,4,0};
    interpret_instruction(ctx);require(state.fpr[0]==0x3F400000,"SUB.S uses the EE adder with sign inversion");
    interpret_instruction(ctx);require(state.fpr[0]==0x3F800000,"ADD.S truncates without host nearest rounding");
    state.pc=0x80001000;state.set_cp0(Cop0::Status,0);
    rejects([&]{interpret_instruction(ctx);},"Status.CU1 = 0");
    require(state.fpr[0]==0x3F800000,"disabled COP1 cannot change the destination");
}
void divider() {
    // Numeric observations from unknownbrackets/ps2autotests, ee_fpu/muldiv.expected.
    // Test code/circuit implementation here is independently authored.
    const std::uint32_t cases[][3]={
        {0,0,0x7FFFFFFF},{0,0x80000000,0xFFFFFFFF},{0x80000000,0x80000000,0x7FFFFFFF},
        {0,0x3F800000,0},{0x3F800000,0,0x7FFFFFFF},{0x3F800000,0x3F800000,0x3F800000},
        {0x40000000,0x40000000,0x3F800000},{0x40400000,0x3F800000,0x40400000},
        {0x40400000,0x40400000,0x3F800000},{0x7FFFFFFF,0x7FFFFFFF,0x3F800000},
        {0x7FFFFFFF,0xFFFFFFFF,0xBF800000},{0xFFFFFFFF,0xFFFFFFFF,0x3F800000},
        {0x7F800000,0x7F800000,0x3F800000},{0xFF800000,0x7F800000,0xBF800000},
        {0x3F800000,0x3FFFFFFF,0x3F000001}};
    for(const auto& row:cases) {
        std::uint32_t fcr=0x0100C001u;
        require(ee_fpu_divide(row[0],row[1],fcr)==row[2],"SRT divider physical result vector");
        require((fcr&0xC000u)==0xC000u,"DIV preserves O/U cause flags");
    }
    std::uint32_t fcr=0x01000001;
    require(ee_fpu_divide(0,0,fcr)==0x7FFFFFFF && (fcr&0x30060)==0x20040,"zero over zero invalid/sticky flag");
    require(ee_fpu_divide(0xBF800000,0,fcr)==0xFFFFFFFF && (fcr&0x30060)==0x10060,"divide zero clears I and preserves sticky history");
    require(ee_fpu_divide(0x3F800000,0x40400000,fcr)==0x3EAAAAAB && (fcr&0x30000)==0,"SRT one-third differs from IEEE truncation");
    require(ee_fpu_divide(0x00800000,0x40000000,fcr)==0,"exponent underflow flushes to zero");
    require(ee_fpu_divide(0x80000001,0x3F800000,fcr)==0x80000000,"denormal input is signed zero");
    const std::uint32_t products[][3]={
        {0,0x80000000,0x80000000},{0x80000000,0x80000000,0},
        {0x40000000,0x40000000,0x40800000},{0x40400000,0x40400000,0x41100000},
        {0x3FFFFFFF,0x3F800000,0x3FFFFFFF},
        {0x7FFFFFFF,0x3F800000,0x7FFFFFFF},{0x7F800001,0x3F800000,0x7F800001}};
    for(const auto& row:products)require(ee_fpu_multiply(row[0],row[1],fcr)==row[2],"Booth multiplier physical result vector");
    const auto flags=fcr;
    require(ee_fpu_multiply(0x3F800000,0x48B33333,fcr)==0x48B33332 &&
            ee_fpu_multiply(0x48B33333,0x3F800000,fcr)==0x48B33333 &&
            ee_fpu_multiply(0xBF800000,0x48B33333,fcr)==0xC8B33332 &&
            ee_fpu_multiply(0x40800000,0x3F800003,fcr)==0x40800002 &&
            ee_fpu_multiply(0x3F800000,0x491EB852,fcr)==0x491EB851,
            "power multiplicand low negative Booth digit loses one retained ULP");
    require(!ee_fpu_multiply(0x3F800000,0x3F808801,fcr) &&
            !ee_fpu_multiply(0x3F800001,0x3F800003,fcr),
            "other negative digits and non-power multiplicands remain unproven");
    require(!ee_fpu_multiply(0x3F800001,0x3F800001,fcr) && fcr==flags,
            "general asymmetric Booth case cannot substitute host multiplication or change flags");
    // Published physical VU observation: 1 * 129.5 loses one output ULP.
    // The shared multiplier boundary is asymmetric; the operand order matters.
    require(ee_fpu_multiply(0x3F800000,0x43018000,fcr)==0x43017FFF &&
            ee_fpu_multiply(0x43018000,0x3F800000,fcr)==0x43018000,
            "column-15 Booth carry loss preserves physical operand asymmetry");
    require(ee_fpu_multiply(0xC31B0000,0x453B8000,fcr)==0xC8E30CFF &&
            ee_fpu_multiply(0x431B0000,0xC53B8000,fcr)==0xC8E30CFF,
            "column-15 proof applies across signs and non-unit multiplicands");
    require(ee_fpu_multiply(0x3F800001,0x3F808000,fcr)==0x3F808001,
            "a discarded carry need not change the retained output mantissa");
    require(ee_fpu_multiply(0x3F800000,0x3FFFFFFF,fcr)==0x3FFFFFFE &&
            ee_fpu_multiply(0x3FFFFFFF,0x3F800000,fcr)==0x3FFFFFFF &&
            ee_fpu_multiply(0x3F800000,0xFFFFFFFF,fcr)==0xFFFFFFFE,
            "physical EE maximum-mantissa products retain operand asymmetry");
    require(ee_fpu_multiply(0x4A800000,0x4B7FFFFF,fcr)==0x567FFFFE &&
            ee_fpu_multiply(0x3F800000,0x3FFFFFFE,fcr)==0x3FFFFFFD &&
            ee_fpu_multiply(0x3F800000,0x3FFFFFFC,fcr)==0x3FFFFFFB,
            "single negative Booth digit proof spans exponents and digit positions");
    require(ee_fpu_multiply(0x44FA0000,0x447A0000,fcr)==0x49F42400 &&
            ee_fpu_multiply(0x3F800001,0x3F810000,fcr)==0x3F810001 &&
            ee_fpu_multiply(0x3F800001,0x3F820000,fcr)==0x3F820001,
            "Booth digits entirely above discarded columns need no correction");
    // Exact integer mantissa product is 0x68DB8B5DCC64. Even the conservative
    // maximum low-column loss cannot cross the retained 24-bit boundary.
    require(ee_fpu_multiply(0x3C23D70A,0x3C23D70A,fcr)==0x38D1B716,
            "nonsparse multiply certified independently of low-column reduction order");
    require(ee_fpu_multiply(0xBC23D70A,0x3C23D70A,fcr)==0xB8D1B716,
            "certified multiply preserves sign xor");
}
void vector_q() {
    require(ee_fpu_sqrt_exact(0x3F800000u)==0x3F800000u &&
            ee_fpu_sqrt_exact(0x41C80000u)==0x40A00000u &&
            ee_fpu_sqrt_exact(0x7F800000u)==0x5F800000u &&
            ee_fpu_sqrt_exact(0x80000001u)==0u,"physical exact SQRT vectors, extended exponent and denormal zero");
    require(!ee_fpu_sqrt_exact(0x40000000u) && !ee_fpu_sqrt_exact(0xBF800000u),"unproved SQRT refuses");
    Memory memory;memory.reset_for_boot(m20::build_firmware().rom);
    EEState s;s.set_cp0(Cop0::Status,1u<<30u);s.pc=0x80001000u;
    memory.set_clock(&s.retired_instructions,&s.stall_cycles);
    memory.write32(0xB0000010u,0x80u); // T0 enable, BUSCLK = EE/2
    memory.write_vu_control(22,0x40000000u);
    memory.write_vu_vector(5,{{0x41C80000u,0},{0,0}});
    memory.write32(s.pc,0x4A0503BDu); // sqrt Q,VF5.x
    memory.write32(s.pc+4u,0x4A0003BFu); // waitq
    std::uint64_t executed=0;ExecutionContext ctx{s,memory,executed,100,0xFFFFFFFFu};
    s.compare_written=true;s.set_cp0(Cop0::Compare,4);
    interpret_instruction(ctx);
    require(s.retired_instructions==1 && s.cycles()==1 && memory.read_vu_control(22)==0x40000000u,"Q remains old before completion");
    interpret_instruction(ctx);
    require(s.retired_instructions==2 && executed==2 && s.stall_cycles==6 && s.count()==8,"WAITQ consumes cycles, not instructions");
    require((s.cp0(Cop0::Cause)&kStatusIM7) && memory.read32(0xB0000000u)==4,"stall crosses Compare and advances device timer");
    require(memory.read_vu_control(22)==0x40A00000u,"SQRT publishes exact result after seven cycles");
    ee::exec<MipsOpcode::VaddQ>(s,memory,0,0x4B000160u);
    require(memory.observe_vu_vector(5).low.value==0x40A00000u,"VADDq uses Q");
    memory.write_vu_vector(5,{{0x40000000u,0},{0,0}});
    ee::exec<MipsOpcode::Vdiv>(s,memory,0,0x4A6503BCu); // 1/2
    require(memory.read_vu_control(22)==0x40A00000u,"VDIV preserves Q until completion");
    s.stall(7);require(memory.read_vu_control(22)==0x3F000000u,"Q completion without WAITQ");
    memory.write_vu_vector(4,{{0x40A00000u,0},{0,0}});
    ee::exec<MipsOpcode::VmulQ>(s,memory,0,0x4B00219Cu);
    require(static_cast<std::uint32_t>(memory.observe_vu_vector(6).low.value)==0x40200000u,"VMULq uses completed Q");
    s.set_cp0(Cop0::Count,0xFFFFFFFEu);s.count_anchor=s.cycles();s.set_cp0(Cop0::Compare,1);s.set_cp0(Cop0::Cause,0);s.stall(4);
    require(s.count()==2 && (s.cp0(Cop0::Cause)&kStatusIM7),"Compare latches across Count wrap during stall");
    memory.write_vu_vector(5,{{0x40000000u,0},{0,0}});
    rejects([&]{ee::exec<MipsOpcode::Vsqrt>(s,memory,0,0x4A0503BDu);},"non-exact");
    require(memory.vu_q_ready_cycle()==0 && memory.read_vu_control(22)==0x3F000000u,"failed SQRT leaves pipeline unchanged");
    require(!try_decode_r5900(0,0x4A2503BDu) && !try_decode_r5900(0,0x4A000BBFu),"reserved SQRT/WAITQ fields refuse");
    VuControl vu;
    vu.issue_q(0,0,0,3,0,false); // 1/0
    require(vu.read(16)==0,"FDIV flags remain pending with Q");
    vu.advance_q(7);
    require(vu.read(22)==0x7FFFFFFFu && vu.read(16)==0x820u,"divide-by-zero cause and sticky map to VU STATUS");
    vu.issue_q(7,0,0,0,0,false);vu.advance_q(14); // 0/0
    require(vu.read(16)==0xC10u,"invalid replaces cause while preserving divide sticky");
    vu.issue_q(14,0,0,0,3,true);vu.advance_q(21); // sqrt(1)
    require(vu.read(16)==0xC00u && vu.read(17)==0,"SQRT clears I/D causes without changing MAC or stickies");
    rejects([&]{vu.issue_q(21,0,1,0,0,true);},"unknown operands");
    require(vu.q_ready_cycle()==0,"unknown operand refuses before starting FDIV");
    vu.issue_q(21,0,0,3,3,false);vu.write(28,2);vu.advance_q(28);
    require(vu.q_ready_cycle()==0 && vu.read(16)==0,"VU reset cancels pending FDIV flags");
}
void vector_setup() {
    Memory memory;memory.reset_for_boot(m20::build_firmware().rom);
    EEState s;s.pc=0x80001000;s.set_cp0(Cop0::Status,1u<<30);
    memory.write_vu_vector(2,{{0x2222222211111111ull,0x00000000000000FFull},{0x4444444433333333ull,0xFFFFFFFF00000000ull}});
    memory.write_vu_vector(3,{{0xBBBBBBBBAAAAAAAAull,0},{0xDDDDDDDDCCCCCCCCull,0}});
    const auto move_flags=memory.read_vu_control(16);
    const auto move=(0x12u<<26u)|(26u<<21u)|(3u<<16u)|(2u<<11u)|0x33Cu; // xz
    require(decode_r5900(0,move).opcode==MipsOpcode::Vmove,"VMOVE decoder");
    ee::exec<MipsOpcode::Vmove>(s,memory,0,move);
    const auto moved=memory.observe_vu_vector(3);
    require(moved.low.value==0xBBBBBBBB11111100ull && moved.low.unknown==0xFFu &&
            moved.high.value==0xDDDDDDDD33333333ull && !moved.high.unknown &&
            memory.read_vu_control(16)==move_flags,"VMOVE preserves unselected lanes, uncertainty and flags");
    ee::exec<MipsOpcode::Vmove>(s,memory,0,(move&~(31u<<11u))|(3u<<11u));
    require(memory.observe_vu_vector(3).low.value==moved.low.value,"VMOVE aliases source/destination");
    ee::exec<MipsOpcode::Vmove>(s,memory,0,move&~(31u<<16u));
    require(memory.observe_vu_vector(0).high.value==0x3F80000000000000ull,"VMOVE preserves VF00");
    memory.write_vu_vector(1,{});
    constexpr auto add=0x4A200868u; // VADD.w VF01,VF01,VF00
    require(try_decode_r5900(0,add)->opcode==MipsOpcode::Vadd,"VADD decodes independently of VMUL");
    memory.write32(s.pc,add);
    std::uint64_t executed=0;ExecutionContext ctx{s,memory,executed,100,~0u};
    interpret_instruction(ctx);
    const auto one=memory.observe_vu_vector(1);
    require(one.low.value==0 && one.high.value==0x3F80000000000000ull &&
            !one.high.unknown && memory.read_vu_control(17)==0,"masked zero addend constructs vector one");
    constexpr auto rotate=0x4BE10B3Du; // VMR32.xyzw VF01,VF01
    require(try_decode_r5900(0,rotate)->opcode==MipsOpcode::Vmr32,"VMR32 has the special2 encoding");
    ee::exec<MipsOpcode::Vmr32>(s,memory,0,rotate);
    require(memory.observe_vu_vector(1).high.value==0x3F800000u,"aliased rotation snapshots all source fields");
    memory.write_vu_vector(2,{{0xC0400000u,0},{0,0}});
    memory.vu_add(2,2,0,8);
    require(memory.read_vu_control(17)==0x80u && (memory.read_vu_control(16)&0xCFu)==0x82u,
            "negative nonzero result sets component sign and sticky sign");
    const auto flags=memory.read_vu_control(16);
    memory.write_vu_vector(3,{{0,0},{0,0xFFFFFFFF00000000ull}});
    rejects([&]{memory.vu_add(2,1,3,15);},"unknown operands");
    require(memory.observe_vu_vector(2).low.value==0xC0400000u && memory.read_vu_control(16)==flags,
            "unknown later component cannot partially change vector or flags");
    memory.write_vu_vector(2,{{0x1122334455667700ull,0xFFu},{0xAABBCCDDEEFF0000ull,0xFFFFu}});
    s.set_reg32(8,0x80002010u);
    constexpr auto store=0xF902FFF0u,load=0xD903FFF0u; // offset -16, VF02/VF03
    require(try_decode_r5900(0,store)->opcode==MipsOpcode::Sqc2 &&
            try_decode_r5900(0,load)->opcode==MipsOpcode::Lqc2,"COP2 quadword load/store decode");
    ee::exec<MipsOpcode::Sqc2>(s,memory,0,store);
    ee::exec<MipsOpcode::Lqc2>(s,memory,0,load);
    const auto copied=memory.observe_vu_vector(3);
    require(copied.low.value==0x1122334455667700ull && copied.low.unknown==0xFFu &&
            copied.high.value==0xAABBCCDDEEFF0000ull && copied.high.unknown==0xFFFFu,
            "COP2 quadword memory roundtrip preserves all known and unknown bits");
    memory.vu_rotate(3,3,8);
    require(memory.observe_vu_vector(3).low.value==0x1122334411223344ull &&
            memory.observe_vu_vector(3).low.unknown==0,"rotation mask selects destination lanes");
    s.set_reg32(8,0x80002018u);
    bus_fault([&]{ee::exec<MipsOpcode::Lqc2>(s,memory,0,load);},BusFault::Unaligned,"16-byte");
    bus_fault([&]{ee::exec<MipsOpcode::Sqc2>(s,memory,0,store);},BusFault::Unaligned,"16-byte");
    s.set_reg32(8,0x80002010u);s.set_cp0(Cop0::Status,0);
    rejects([&]{ee::exec<MipsOpcode::Sqc2>(s,memory,0,store);},"Status.CU2 = 0");
    memory.vu_rotate(0,3,15);
    require(memory.observe_vu_vector(0).high.value==0x3F80000000000000ull,"VF00 stays constant");
    // Distinct source lanes catch broadcast selection and accidental host FMA.
    memory.write_vu_vector(4,{{0x400000003F800000ull,0},{0x4080000040400000ull,0}});
    memory.write_vu_vector(5,{{0x3F00000040000000ull,0},{0x3F80000000000000ull,0}});
    rejects([&]{memory.vu_product(6,4,5,15,0,true,false);},"unknown ACC");
    s.set_cp0(Cop0::Status,1u<<30);
    constexpr auto mula=0x4BE521BCu,madda=0x4BE520BDu,madd=0x4BE5218Bu;
    require(try_decode_r5900(0,mula)->opcode==MipsOpcode::VmulaBc &&
            try_decode_r5900(0,madda)->opcode==MipsOpcode::VmaddaBc &&
            try_decode_r5900(0,madd)->opcode==MipsOpcode::VmaddBc,"broadcast accumulator opcode groups");
    ee::exec<MipsOpcode::VmulaBc>(s,memory,0,mula); // ACC = (1,2,3,4)*2
    ee::exec<MipsOpcode::VmaddaBc>(s,memory,0,madda); // ACC += (1,2,3,4)*0.5
    ee::exec<MipsOpcode::VmaddBc>(s,memory,0,madd); // VF06 = ACC + (1,2,3,4)*1
    const auto accumulated=memory.observe_vu_vector(6);
    require(accumulated.low.value==0x40E0000040600000ull && accumulated.high.value==0x4160000041280000ull,
            "broadcast matrix accumulation uses actual persistent ACC lanes");
    // MADD must leave ACC unchanged: a zero product exposes its prior value.
    memory.vu_product(6,4,5,15,2,true,false);
    require(memory.observe_vu_vector(6).low.value==0x40A0000040200000ull &&
            memory.observe_vu_vector(6).high.value==0x4120000040F00000ull,"MADD destination does not overwrite ACC");
    memory.write_vu_vector(7,{{0x7FFFFFFFu,0},{0,0}});
    const auto saved=memory.observe_vu_vector(6);
    rejects([&]{memory.vu_product(6,7,5,8,0,false,true);},"exception/ACC");
    memory.vu_product(6,4,5,15,2,true,false);
    require(memory.observe_vu_vector(6).low.value==saved.low.value,"refused multiplication preserves ACC");
    memory.write_vu_vector(7,{{0x3F800000u,0},{0,0}});
    memory.write_vu_vector(8,{{0xBE800001u,0},{0,0}});
    memory.vu_add(7,7,8,8);
    require(memory.observe_vu_vector(7).low.value==0x3F400000u,"VU addition shares the one-guard-bit circuit");
    memory.write_vu_vector(5,{{0x40A0000040800000ull,0},{0x40E0000040C00000ull,0}});
    memory.write_vu_vector(6,{{0,0},{0xDEADBEEF00000000ull,0}});
    constexpr auto outer=0x4BC522FEu,finish=0x4BC429AEu;
    require(try_decode_r5900(0,outer)->opcode==MipsOpcode::Vopmula &&
            try_decode_r5900(0,finish)->opcode==MipsOpcode::Vopmsub &&
            !try_decode_r5900(0,outer|(1u<<21)),"outer product requires fixed xyz mask");
    ee::exec<MipsOpcode::Vopmula>(s,memory,0,outer);
    ee::exec<MipsOpcode::Vopmsub>(s,memory,0,finish);
    require(memory.observe_vu_vector(6).low.value==0x40C00000C0400000ull &&
            memory.observe_vu_vector(6).high.value==0xDEADBEEFC0400000ull &&
            memory.read_vu_control(17)==0xA0u,"cross product cyclic lanes, reversed second operands and preserved w");
    constexpr auto add_y=0x4B852101u;
    require(try_decode_r5900(0,add_y)->opcode==MipsOpcode::VaddBc,"broadcast add opcode");
    ee::exec<MipsOpcode::VaddBc>(s,memory,0,add_y);
    require(memory.observe_vu_vector(4).low.value==0x40E0000040C00000ull &&
            memory.observe_vu_vector(4).high.value==0x4080000040400000ull,"broadcast source remains fixed across aliased destination lanes");
    memory.vu_subtract(4,4,5,15);
    require(memory.observe_vu_vector(4).low.value==0x4000000040000000ull &&
            memory.observe_vu_vector(4).high.value==0xC0400000C0400000ull,"general vector subtraction uses actual per-lane operands");
}
void dma_idle_knowledge() {
    Memory memory;memory.reset_for_boot(m20::build_firmware().rom);
    EEState s;s.pc=0x80001000;s.set_reg32(8,0xB000C000u);
    memory.set_clock(&s.retired_instructions);
    ee::exec<MipsOpcode::Lw>(s,memory,0,0x8D090000u);
    require(s.unknown_gpr_bits[9]!=0,"idle CHCR does not invent reset mode/tag fields");
    ee::exec<MipsOpcode::Andi>(s,memory,0,0x31290100u);
    require(s.reg32(9)==0 && s.unknown_gpr_bits[9]==0,"STR reset proves no transfer is running");
    memory.write32(0xB000C000u,0x84u);
    require(memory.read32(0xB000C000u)==0x84u,"programmed idle CHCR becomes fully known");
}
void conversions_and_partial_loads() {
    Memory memory;memory.reset_for_boot(m20::build_firmware().rom);
    EEState s;s.pc=0x80001000;s.set_cp0(Cop0::Status,kStatusCU1);s.fcr31=0x0183C079;
    std::uint64_t retired=0;ExecutionContext ctx{s,memory,retired,100,0xFFFFFFFF};
    const std::uint32_t to_integer[][2]={{0,0},{0x80000000,0},{0x3FC00000,1},{0xBFE00000,0xFFFFFFFF},
        {0x4F000000,0x7FFFFFFF},{0xCF000000,0x80000000},{0x7FFFFFFF,0x7FFFFFFF},{0xFFFFFFFF,0x80000000}};
    const std::uint32_t to_float[][2]={{0,0},{0xFFFFFFFF,0xBF800000},{0x80000000,0xCF000000},
        {0x7FFFFFFF,0x4EFFFFFF},{0x01000001,0x4B800000}};
    for(unsigned direction=0;direction<2;++direction) {
        const std::span<const std::uint32_t[2]> words=direction
            ? std::span<const std::uint32_t[2]>(to_float) : std::span<const std::uint32_t[2]>(to_integer);
        for(const auto& row:words) {
            s.pc=0x80001000;s.fpr[1]=row[0];
            memory.write32(s.pc,direction ? 0x468008A0u : 0x460008A4u);
            interpret_instruction(ctx);
            require(s.fpr[2]==row[1] && s.fcr31==0x0183C079,"FPU conversion truncates/clamps and preserves status");
        }
    }
    memory.write64(0x80002000,0x8776655443322110ull);
    s.set_reg32(8,0x80002000);
    constexpr std::uint64_t old=0xFFEEDDCCBBAA9988ull;
    constexpr std::uint64_t left[]={0x10EEDDCCBBAA9988ull,0x2110DDCCBBAA9988ull,
        0x322110CCBBAA9988ull,0x43322110BBAA9988ull,0x5443322110AA9988ull,
        0x6554433221109988ull,0x7665544332211088ull,0x8776655443322110ull};
    constexpr std::uint64_t right[]={0x8776655443322110ull,0xFF87766554433221ull,
        0xFFEE877665544332ull,0xFFEEDD8776655443ull,0xFFEEDDCC87766554ull,
        0xFFEEDDCCBB877665ull,0xFFEEDDCCBBAA8776ull,0xFFEEDDCCBBAA9987ull};
    for(unsigned offset=0;offset<8;++offset) {
        s.set_reg128(9,old,0x0123456789ABCDEFull);
        ee::exec<MipsOpcode::Ldl>(s,memory,0,(8u<<21u)|(9u<<16u)|offset);
        require(s.reg64(9)==left[offset] && s.reg128(9).high==0x0123456789ABCDEFull,"LDL partial merge edge");
        s.set_reg128(9,old,0x0123456789ABCDEFull);
        ee::exec<MipsOpcode::Ldr>(s,memory,0,(8u<<21u)|(9u<<16u)|offset);
        require(s.reg64(9)==right[offset] && s.reg128(9).high==0x0123456789ABCDEFull,"LDR partial merge edge");
    }
    s.set_reg64_observed(9,old,~0ull);
    ee::exec<MipsOpcode::Ldr>(s,memory,0,(8u<<21u)|(9u<<16u));
    require(s.reg64(9)==0x8776655443322110ull && s.unknown_gpr_bits[9]==0,"full LDR replaces unknown old bits");
    s.set_reg64_observed(9,old,~0ull);
    ee::exec<MipsOpcode::Ldl>(s,memory,0,(8u<<21u)|(9u<<16u));
    require(s.unknown_gpr_bits[9]==0x00FFFFFFFFFFFFFFull,"partial LDL retains only unknown untouched bytes");
    s.set_reg64_observed(9,old,~0ull);
    ee::exec<MipsOpcode::Lwl>(s,memory,0,(8u<<21u)|(9u<<16u)|3u);
    require(s.reg64(9)==0x43322110u && s.unknown_gpr_bits[9]==0,"full LWL replaces unknown bits including sign extension");
    s.set_reg64_observed(9,old,~0xFFull);
    ee::exec<MipsOpcode::Sdr>(s,memory,0,(8u<<21u)|(9u<<16u)|7u);
    require(memory.read8(0x80002007)==0x88 && memory.read8(0x80002006)==0x76,
            "partial SDR consumes only enabled source bytes");
}
}
int main() {
    try {privilege_and_cache();floating_comparisons();nested_exception();addi_exception();signed_arithmetic();packed_copy();leading_sign_count();disabled_cop0_context();symbolic_shifts();register_context();adder();divider();vector_q();vector_setup();dma_idle_knowledge();conversions_and_partial_loads();return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
