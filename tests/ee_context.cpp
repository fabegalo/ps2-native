#include "boot_checks.hpp"
#include "bios_program.hpp"
#include "ee_native_fixture.hpp"
#include "ps2native/ee_fpu.hpp"
#include <iostream>

using namespace ps2native;
using namespace boot_checks;
namespace {
void native_fixture_identity() {
    auto owner=std::make_unique<Memory>();auto& memory=*owner;
    reset_native_ee_fixture(memory,m20::build_firmware().rom);
    auto state=ee_reset_state();std::uint64_t executed=0;
    ExecutionContext ctx{state,memory,executed,100,0xFFFFFFFFu};
    const auto step=[&](std::uint32_t pc){state.pc=pc;compiled_fixture_step(ctx);};
    memory.write32(0x80001000u,0x24080001u);
    step(0x80001000u);step(0x80001000u);
    require(state.reg32(8)==1 && executed==2,"compiled_fixture retires repeated instructions");
    memory.write32(0x80001000u,0x24080002u);
    step(0x80001000u);
    require(state.reg32(8)==2,"changed executable bytes select their matching compiled_fixture");
    memory.write32(0x80001400u,0x24090009u);
    step(0x80001400u);step(0x80001000u);
    require(state.reg32(8)==2 && state.reg32(9)==9,"colliding decode slots retain each instruction's operands");
    const auto retired=state.retired_instructions;
    memory.store32_observed(0x80001000u,{0x24080002u,1u});
    device_refused([&]{step(0x80001000u);},"unknown RAM bits");
    require(state.retired_instructions==retired,"warm decode still refuses an unknown instruction before retirement");
    memory.write32(0x80001000u,0x4C000000u);
    rejects([&]{step(0x80001000u);},"missing synthetic EE AOT");
    memory.write32(0x80001000u,0x24080002u);step(0x80001000u);
    require(state.reg32(8)==2,"missing fixture does not poison later valid execution");

    state.set_cp0(Cop0::Status,0);
    memory.write32(0x80000000u,0x24080003u);
    memory.write32(0x80002000u,0x24080007u);
    memory.tlb_write(0,{0,0x00100000u,0x1Fu,0x5Fu});
    step(0x00100000u);
    require(state.reg32(8)==3,"cached compiled_fixture uses the current TLB mapping");
    memory.tlb_write(0,{0,0x00100000u,0x9Fu,0xDFu});
    step(0x00100000u);
    require(state.reg32(8)==7,"TLB remap fetches a new word before consulting decode metadata");
    state.set_cp0(Cop0::Status,0x10u);
    bus_fault([&]{step(0x80001000u);},BusFault::Privilege,"privilege mode");
}

void privilege_and_cache() {
    Memory memory;
    const auto rom=m20::build_firmware().rom;
    reset_native_ee_fixture(memory,rom);
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
        compiled_fixture_step(ctx);
    }
    require(executed==6,"cache indices do not look up a TLB tag");
    for(const auto op:{0x18u,0x1Au,0x1Cu}) {
        memory.write32(state.pc,0xBC000000u|(op<<16u));
        bus_fault([&]{compiled_fixture_step(ctx);},BusFault::TlbRefill,"no entry");
        state.pc=0x80001000u;
    }
    state.pc=0x00100000u;state.set_cp0(Cop0::Status,0x10u);
    // Restore the word via a kernel alias before attempting the user MFC0.
    memory.set_cpu_context(0,0);memory.write32(0x80000000u,0x40026000u);
    rejects([&]{compiled_fixture_step(ctx);},"COP0 unusable");
    memory.set_cpu_context(0,0);memory.write32(0x80000000u,0xBC140000u);
    state.pc=0x00100000u;
    rejects([&]{compiled_fixture_step(ctx);},"COP0 unusable");
    require(executed==6,"privileged COP0 cannot silently execute in user mode");
}
void floating_comparisons() {
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
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
        compiled_fixture_step(ctx);
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
        rejects([&]{compiled_fixture_step(ctx);},"Status.CU1 = 0");
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
    Memory memory; reset_native_ee_fixture(memory,m20::build_firmware().rom);
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
        require(decode_r5900(pc,word).opcode==MipsOpcode::Addi && mips_aot_supported(MipsOpcode::Addi),
                "ADDI has a native exception exit");
        ExecutionContext ctx{state,memory,executed,100,0xFFFFFFFFu};
        compiled_fixture_step(ctx);
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
    rejects([&]{compiled_fixture_step(ctx);},"consumes unknown MMIO bits");
    require(state.pc==pc && state.retired_instructions==0,"unknown arithmetic does not fabricate a sum or exception");
}
void signed_arithmetic() {
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
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
        require(instruction.written_gpr()==8u && mips_aot_supported(instruction.opcode),
                "signed arithmetic decode records the correct destination and native exception exit");
        compiled_fixture_step(ctx);
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
void micro_subroutine() {
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);EEState state=ee_reset_state();
    memory.set_clock(&state.retired_instructions,&state.stall_cycles);
    state.set_cp0(Cop0::Status,1u<<30);
    constexpr auto add=(8u<<21u)|(2u<<16u)|(1u<<11u)|(3u<<6u)|0x28u;
    memory.write64(0xB1000000u,(std::uint64_t{add}<<32u)|0x8000033Cu);
    memory.write64(0xB1000008u,0x400002FF8000033Cull);
    memory.write64(0xB1000010u,0x000002FF8000033Cull);
    memory.write_vu_vector(1,{{0x3F800000u,0},{0,0}});
    memory.write_vu_vector(2,{{0x40000000u,0},{0,0}});
    memory.write_vu_vector(3,{{0x40800000u,0},{0,0}});
    require(decode_mips(0,0x4A000038u).opcode==MipsOpcode::Vcallms && !try_decode_r5900(0,0x4A200038u),
            "VCALLMS accepts its immediate and rejects reserved macro fields");
    ee::exec<MipsOpcode::Vcallms>(state,memory,0,0x4A000038u);
    require(memory.vu0_micro_running() && state.stall_cycles==0,"VCALLMS starts real VU0 code without completing it inline");
    constexpr auto read=0x48281800u; // QMFC2 r8,VF3
    ee::exec<MipsOpcode::Qmfc2>(state,memory,0,read);
    require(state.reg32(8)==0x40800000u && state.stall_cycles==0 && memory.vu0_micro_running(),
            "NI transfer reads committed VU data without waiting for the micro subroutine");
    ee::exec<MipsOpcode::Qmfc2>(state,memory,0,read|1u);
    require(state.reg32(8)==0x40400000u && state.stall_cycles==7 && !memory.vu0_micro_running(),
            "interlocked transfer waits through actual FMAC writeback and E-bit Ready");
    ee::exec<MipsOpcode::Vcallms>(state,memory,0,0x4A000038u);
    ee::exec<MipsOpcode::Vcallms>(state,memory,0,0x4A000038u);
    require(state.stall_cycles==14 && memory.vu0_micro_running(),"a second call waits for the first subroutine before starting again");
    ee::exec<MipsOpcode::Vnop>(state,memory,0,0x4A0002FFu);
    require(!memory.vu0_micro_running() && state.stall_cycles==21,"a following macro instruction waits for Ready");
    state.set_cp0(Cop0::Status,0);
    rejects([&]{ee::exec<MipsOpcode::Vcallms>(state,memory,0,0x4A000038u);},"Status.CU2 = 0");
    require(!memory.vu0_micro_running(),"disabled COP2 cannot launch microcode");
}
void funnel_shift() {
    Memory memory;EEState state;
    constexpr auto byte=0x0518FFF5u,half=0x0519FFFDu;
    require(decode_mips(0,byte).opcode==MipsOpcode::Mtsab && !decode_mips(0,byte).written_gpr() &&
            decode_mips(0,half).opcode==MipsOpcode::Mtsah,"SA setup decodes without a GPR destination or branch");
    for(unsigned value=0;value<32;++value) {
        state.set_reg64_observed(8,value,~15ull);
        ee::exec<MipsOpcode::Mtsab>(state,memory,0,byte);
        require(state.shift_amount==((value^5u)&15u)*8u,"MTSAB XORs only the low four bits");
        state.set_reg64_observed(8,value,~7ull);
        ee::exec<MipsOpcode::Mtsah>(state,memory,0,half);
        require(state.shift_amount==((value^5u)&7u)*16u,"MTSAH XORs only the low three bits");
    }
    state.shift_amount=88;state.set_reg64_observed(8,0,1);
    rejects([&]{ee::exec<MipsOpcode::Mtsab>(state,memory,0,byte);},"unknown shift count");
    require(state.shift_amount==88,"unproved shift input cannot mutate SA");
    constexpr auto op=0x712A5EE8u; // QFSRV r11,r9,r10
    require(decode_mips(0,op).opcode==MipsOpcode::Qfsrv && decode_mips(0,op).written_gpr()==11,"QFSRV decode and AOT metadata");
    for(unsigned bytes=0;bytes<16;++bytes)for(unsigned destination:{9u,10u,11u,0u}) {
        state.set_reg128(9,0x1716151413121110ull,0x1F1E1D1C1B1A1918ull);
        state.set_reg128(10,0x0706050403020100ull,0x0F0E0D0C0B0A0908ull);state.shift_amount=bytes*8u;
        const auto word=(op&~(31u<<11))|(destination<<11);
        ee::exec<MipsOpcode::Qfsrv>(state,memory,0,word);
        std::uint64_t low=0,high=0;
        for(unsigned n=0;n<8;++n){low|=std::uint64_t{bytes+n}<<(8u*n);high|=std::uint64_t{bytes+n+8u}<<(8u*n);}
        require(state.reg128(destination).low==(destination ? low : 0) && state.reg128(destination).high==(destination ? high : 0),
                "every byte alignment matches independent ordered bytes, including 64-bit edge, aliases and zero");
    }
    state.set_reg128(9,0,0);state.set_reg64_observed(9,0,1);
    state.set_reg128(10,0,0);state.shift_amount=0;
    ee::exec<MipsOpcode::Qfsrv>(state,memory,0,op);
    require(!state.unknown_gpr_bits[11] && !state.unknown_gpr_high_bits[11],"SA zero does not consume the discarded source");
    state.shift_amount=8;ee::exec<MipsOpcode::Qfsrv>(state,memory,0,op);
    require(!state.unknown_gpr_bits[11] && state.unknown_gpr_high_bits[11]==(1ull<<56),"funnel shift propagates only selected unknown bits");
    for(auto invalid:{1u,128u}){state.shift_amount=invalid;rejects([&]{ee::exec<MipsOpcode::Qfsrv>(state,memory,0,op);},"byte/halfword shift");}
    reset_native_ee_fixture(memory,m20::build_firmware().rom);
    memory.write32(0x80001000u,0x04180008u);memory.write32(0x80001004u,op);
    state=ee_reset_state();state.pc=0x80001000u;state.set_reg128(9,1,2);state.set_reg128(10,3,4);
    std::uint64_t count=0;ExecutionContext ctx{state,memory,count,2,0xFFFFFFFFu};compiled_fixture_step(ctx);compiled_fixture_step(ctx);
    require(state.shift_amount==64 && state.reg128(11).low==4 && state.reg128(11).high==1 && state.retired_instructions==2,
            "production compiled_fixture dispatch uses the same SA and funnel semantics");
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
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
    auto state=ee_reset_state();state.pc=0x80001000u;state.set_reg32(16,0x80002000);
    std::uint64_t executed=0;ExecutionContext ctx{state,memory,executed,100,0xFFFFFFFFu};
    const auto step=[&](std::uint32_t word){memory.write32(state.pc,word);compiled_fixture_step(ctx);};
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
void integer_multiply_add() {
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
    struct Row {std::uint32_t a,b,hi,lo,signed_hi,unsigned_hi,result;};
    constexpr Row rows[]{
        {3,7,0,5,0,0,26},
        {1,1,0,0xFFFFFFFFu,1,1,0},
        {0xFFFFFFFFu,2,0,0,0xFFFFFFFFu,1,0xFFFFFFFEu},
        {0x80000000u,0xFFFFFFFFu,0,0,0,0x7FFFFFFFu,0x80000000u},
        {1,1,0xFFFFFFFFu,0xFFFFFFFFu,0,0,0},
        {0x80000000u,0x80000000u,0xC0000000u,0,0,0,0}};
    for(bool second:{false,true})for(bool unsigned_product:{false,true})for(bool retired_path:{false,true})
    for(unsigned dest:{0u,8u,9u,10u})for(const auto& row:rows) {
        EEState state;state.pc=0x80001000;
        state.set_reg128(8,ee::sx32(row.a),0x123456789ABCDEFull);
        state.set_reg128(9,ee::sx32(row.b),0x223456789ABCDEFull);
        state.set_reg128(10,0x11223344,0x323456789ABCDEFull);
        state.lo=0x1122334455667788ull;state.hi=0x2233445566778899ull;
        state.lo1=0x33445566778899AAull;state.hi1=0x445566778899AABBull;
        auto& lo=second ? state.lo1 : state.lo;auto& hi=second ? state.hi1 : state.hi;
        lo=0x8765432100000000ull|row.lo;hi=0x1234567800000000ull|row.hi;
        const auto other_lo=second ? state.lo : state.lo1,other_hi=second ? state.hi : state.hi1;
        const auto upper=state.gpr[dest].high;
        const auto word=0x70000000u|(8u<<21u)|(9u<<16u)|(dest<<11u)|(second ? 0x20u : 0u)|(unsigned_product ? 1u : 0u);
        const auto decoded=decode_mips(state.pc,word);
        const auto expected=second ? (unsigned_product ? MipsOpcode::Maddu1 : MipsOpcode::Madd1) :
            (unsigned_product ? MipsOpcode::Maddu : MipsOpcode::Madd);
        require(decoded.opcode==expected && decoded.written_gpr()==(dest ? std::optional<unsigned>{dest} : std::nullopt),
            "MADD family decodes all pipelines, signedness and destination metadata for the AOT");
        if(retired_path) {memory.write32(state.pc,word);std::uint64_t count=0;ExecutionContext context{state,memory,count,1,0xFFFFFFFFu};compiled_fixture_step(context);}
        else if(second) {
            if(unsigned_product)ee::exec<MipsOpcode::Maddu1>(state,memory,0,word);else ee::exec<MipsOpcode::Madd1>(state,memory,0,word);
        } else {
            if(unsigned_product)ee::exec<MipsOpcode::Maddu>(state,memory,0,word);else ee::exec<MipsOpcode::Madd>(state,memory,0,word);
        }
        require(lo==ee::sx32(row.result) && hi==ee::sx32(unsigned_product ? row.unsigned_hi : row.signed_hi),
            "MADD accumulates true signed/unsigned products, propagates carry and wraps without signed overflow");
        require((second ? state.lo : state.lo1)==other_lo && (second ? state.hi : state.hi1)==other_hi &&
            state.reg64(dest)==(dest ? lo : 0) && state.gpr[dest].high==upper,
            "MADD preserves the other pipeline and destination upper half, including aliases and zero destination");
        require(!try_decode_r5900(0,word|0x40u),"MADD reserved shift field refuses");
    }
    EEState state;state.set_reg64(8,2);state.set_reg64_observed(9,0,1);state.lo=5;state.hi=7;
    constexpr auto word=0x71095000u;
    rejects([&]{ee::exec<MipsOpcode::Madd>(state,memory,0,word);},"unknown MMIO bits");
    require(state.lo==5 && state.hi==7 && state.reg64(10)==0,"unknown MADD input cannot mutate either destination");
    state.set_reg64(9,0xFFFFFFFFu);
    rejects([&]{ee::exec<MipsOpcode::Maddu>(state,memory,0,word|1u);},"sign-extended word operands");
    require(state.lo==5 && state.hi==7,"undefined non-word MADD operands refuse before commit");
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
    reset_native_ee_fixture(memory,m20::build_firmware().rom);
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
    for(std::size_t i=0;i<words.size();++i)compiled_fixture_step(ctx);
    require(state.fpr[1]==0xDEADBEEFu && state.reg64(9)==0xFFFFFFFFDEADBEEFull,"FPU raw transfers sign extend GPR results");
    require(state.fcr31==0x0183C079u && state.reg32(11)==0x0183C079u,"FCR31 writable and fixed fields");
    memory.write32(0x80003000u,0x46000886u); // MOV.S f2,f1
    const auto saved_pc=state.pc;state.pc=0x80003000u;compiled_fixture_step(ctx);state.pc=saved_pc;
    require(state.fpr[2]==0xDEADBEEFu && state.fcr31==0x0183C079u,"MOV.S preserves raw payload and all FCR31 flags");
    // Raw physical sign-operation vectors from ps2autotests arithmetic.expected.
    const std::uint32_t signs[][3]={{0,0,0x80000000u},{0x80000000u,0,0},
        {0x00001337u,0x00001337u,0x80001337u},{0x7F800001u,0x7F800001u,0xFF800001u},
        {0xFFFFFFFFu,0x7FFFFFFFu,0x7FFFFFFFu},{0xDEADBEEFu,0x5EADBEEFu,0x5EADBEEFu}};
    for(const auto& row:signs)for(unsigned op=0;op<2;++op) {
        state.pc=0x80003000u;state.fpr[1]=row[0];state.fcr31=0x0183C079u;
        memory.write32(state.pc,op ? 0x46000847u : 0x46000845u); // fd == fs
        compiled_fixture_step(ctx);
        require(state.fpr[1]==row[op+1] && state.fcr31==0x01830079u,
                "ABS/NEG preserve raw magnitude and sticky flags with aliased destination");
    }
    require(!try_decode_r5900(0,0x46010845u) && !try_decode_r5900(0,0x46010847u),
            "sign operations reject reserved ft fields");
    state.pc=0x80003000u;state.set_cp0(Cop0::Status,0);state.fpr[1]=0x1234;
    rejects([&]{compiled_fixture_step(ctx);},"Status.CU1 = 0");
    require(state.fpr[1]==0x1234,"disabled FPU sign operation preserves destination");
    state.set_cp0(Cop0::Status,kStatusCU1);state.pc=saved_pc;
    require(state.shift_amount==72 && state.reg64(12)==72 && state.reg128(8).high==0x1122334455667788ull,
            "SA context save/restore preserves full GPR upper half");
    require(!try_decode_r5900(0,0x444BF801u) && !try_decode_r5900(0,0x00004868u),"reserved COP1/SA transfer fields rejected");
    memory.write32(0x80002000u,0x4601009Cu); // MADD.S f2,f0,f1
    state.pc=0x80002000u;state.fpr[0]=0x80000000u;state.fpr[1]=0x40000000u;state.fpu_acc=0x3FA00000u;
    compiled_fixture_step(ctx);
    require(state.fpr[2]==state.fpu_acc && state.fpu_acc==0x3FA00000u,"zero product copies actual accumulator without changing it");
    state.pc=0x80002000u;state.fpr[0]=0x3F800000u;
    compiled_fixture_step(ctx);
    require(state.fpr[2]==0x40500000u && state.fpu_acc==0x3FA00000u,
        "certified MADD computes 1*2+1.25 exactly and preserves the accumulator");
    memory.write32(0x80002000u,0x46010018u); // ADDA.S f0,f1
    state.pc=0x80002000u;state.fpr[0]=0xC0200000u;state.fpr[1]=0x80000000u;
    compiled_fixture_step(ctx);
    require(state.fpu_acc==0xC0200000u,"ADDA with negative zero restores the actual accumulator payload");
    state.pc=0x80002000u;state.fpr[0]=0x80000000u;
    compiled_fixture_step(ctx);require(state.fpu_acc==0x80000000u,"ADDA preserves two negative zeros");
    state.pc=0x80002000u;state.fpr[0]=0;
    compiled_fixture_step(ctx);require(state.fpu_acc==0,"ADDA opposite zero signs produce positive zero");
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
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
    EEState state;state.pc=0x80001000;state.set_cp0(Cop0::Status,kStatusCU1);
    state.fpr[0]=0x3F800000;state.fpr[1]=0x3E800001;
    memory.write32(state.pc,0x46010001u); // SUB.S f0,f0,f1, aliased destination
    memory.write32(state.pc+4,0x46010000u); // ADD.S f0,f0,f1
    std::uint64_t executed=0;ExecutionContext ctx{state,memory,executed,4,0};
    compiled_fixture_step(ctx);require(state.fpr[0]==0x3F400000,"SUB.S uses the EE adder with sign inversion");
    compiled_fixture_step(ctx);require(state.fpr[0]==0x3F800000,"ADD.S truncates without host nearest rounding");
    state.pc=0x80001000;state.set_cp0(Cop0::Status,0);
    rejects([&]{compiled_fixture_step(ctx);},"Status.CU1 = 0");
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
    require(!ee_fpu_multiply(0x3F800002,0x3F800003,fcr) && fcr==flags,
            "ambiguous low-column mass cannot substitute host multiplication or change flags");
    require(ee_fpu_multiply(0x3F800001,0x3F800001,fcr)==0x3F800002,
            "one unit of initial low-column weight cannot cross the retained boundary");
    // Independently checked against an external circuit model. These require
    // the initial-weight bound; the coarser compressor-count bound refused.
    require(ee_fpu_multiply(0x37292FF7,0x401DE9E6,fcr)==0x37D0B9FE &&
            ee_fpu_multiply(0xBA493EFB,0x401DE9E6,fcr)==0xBAF846FE &&
            ee_fpu_multiply(0x3D0449DE,0x401DE9E6,fcr)==0x3DA33456,
            "initial low-column mass certifies nonsparse products across signs and exponents");
    fcr=0x0183FFFF;
    require(ee_fpu_multiply(0x3F800001,0x3F800001,fcr)==0x3F800002 && fcr==0x01833FFF,
            "certified product clears only overflow/underflow causes");
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
    // Numeric physical observations, plus low-bit circuit boundary cases.
    const std::uint32_t roots[][2]={
        {0,0},{0x80000000,0},{0x80000001,0},{0x3F800000,0x3F800000},
        {0x40000000,0x3FB504F3},{0x40400000,0x3FDDB3D7},
        {0x3FFFFFFF,0x3FB504F3},{0x7FFFFFFF,0x5FB504F3},
        {0x7F800000,0x5F800000},{0x41C80000,0x40A00000},
        {0x6EB8B10D,0x5719C138},{0x57E03710,0x4BA968CC},
        {0x4CBFAB1C,0x461CA1C5},{0x1201BC71,0x28B63E29}};
    for(const auto& row:roots)require(ee_fpu_sqrt(row[0])==row[1],"SRT SQRT physical/circuit result vector");
    require(!ee_fpu_sqrt(0xBF800000) && !ee_fpu_sqrt(0xFFFFFFFF),
            "negative SQRT exception behavior remains explicitly refused");
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
    EEState s;s.set_cp0(Cop0::Status,1u<<30u);s.pc=0x80001000u;
    memory.set_clock(&s.retired_instructions,&s.stall_cycles);
    memory.write32(0xB0000010u,0x80u); // T0 enable, BUSCLK = EE/2
    memory.write_vu_control(22,0x40000000u);
    memory.write_vu_vector(5,{{0x41C80000u,0},{0,0}});
    memory.write32(s.pc,0x4A0503BDu); // sqrt Q,VF5.x
    memory.write32(s.pc+4u,0x4A0003BFu); // waitq
    std::uint64_t executed=0;ExecutionContext ctx{s,memory,executed,100,0xFFFFFFFFu};
    s.compare_written=true;s.set_cp0(Cop0::Compare,4);
    compiled_fixture_step(ctx);
    require(s.retired_instructions==1 && s.cycles()==1 && memory.read_vu_control(22)==0x40000000u,"Q remains old before completion");
    compiled_fixture_step(ctx);
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
    {
        constexpr auto sub_q=0x4A8004A4u; // VSUBq.y VF18,VF0,Q; FT is reserved.
        require(try_decode_r5900(0,sub_q)->opcode==MipsOpcode::VsubQ && !try_decode_r5900(0,sub_q|(1u<<16u)),
            "VSUBq decoding preserves the reserved FT field");
        const ObservedQuadword initial{{0x404000003F800000ull,0},{0x40A0000040800000ull,0}};
        memory.write_vu_vector(18,initial);memory.write32(0x80001100u,sub_q);s.pc=0x80001100u;
        const auto before=s.cycles();compiled_fixture_step(ctx);const auto retired_path=memory.observe_vu_vector(18);
        require(retired_path.low.value==0xBF0000003F800000ull && retired_path.high.value==initial.high.value &&
            !retired_path.low.unknown && !retired_path.high.unknown && memory.read_vu_control(22)==0x3F000000u &&
            memory.read_vu_control(17)==0x40u && (memory.read_vu_control(16)&15u)==2u && s.cycles()==before+1u,
            "retired_path VSUBq subtracts completed Q, preserves other lanes and Q, and publishes masked MAC/status flags");
        memory.write_vu_vector(18,initial);ee::exec<MipsOpcode::VsubQ>(s,memory,0,sub_q);
        const auto native=memory.observe_vu_vector(18);
        require(native.low.value==retired_path.low.value && native.high.value==retired_path.high.value &&
            native.low.unknown==retired_path.low.unknown && native.high.unknown==retired_path.high.unknown,
            "native VSUBq helper matches the independent retired_path result");
    }
    {
        constexpr auto rsqrt=0x4A6F03BEu; // Q = VF0.w / sqrt(VF15.x).
        require(try_decode_r5900(0,rsqrt)->opcode==MipsOpcode::Vrsqrt,"VRSQRT decodes its two component selectors");
        memory.write_vu_vector(15,{{0x40800000u,0},{0,0}});memory.write_vu_control(22,0x3E800000u);
        memory.write32(0x80001120u,rsqrt);s.pc=0x80001120u;const auto issued=s.cycles();
        compiled_fixture_step(ctx);
        require(memory.vu_q_ready_cycle()==issued+13u && memory.read_vu_control(22)==0x3E800000u,
            "retired_path VRSQRT selects VF0.w/VF15.x and schedules thirteen cycles without publishing early");
        ee::exec<MipsOpcode::Vwaitq>(s,memory,0,0x4A0003BFu);
        require(s.cycles()==issued+13u && memory.read_vu_control(22)==0x3F000000u && !memory.vu_q_ready_cycle(),
            "VRSQRT completes exact 1/sqrt(4) through the existing Q stall pipeline");
    }
    s.set_cp0(Cop0::Count,0xFFFFFFFEu);s.count_anchor=s.cycles();s.set_cp0(Cop0::Compare,1);s.set_cp0(Cop0::Cause,0);s.stall(4);
    require(s.count()==2 && (s.cp0(Cop0::Cause)&kStatusIM7),"Compare latches across Count wrap during stall");
    memory.write_vu_vector(5,{{0x40000000u,0},{0,0}});
    const auto before_root=s.cycles();
    ee::exec<MipsOpcode::Vsqrt>(s,memory,0,0x4A0503BDu);
    require(memory.vu_q_ready_cycle()==before_root+7 && memory.read_vu_control(22)==0x3F000000u,
            "inexact SQRT preserves seven-cycle pending Q");
    s.stall(7);
    require(memory.read_vu_control(22)==0x3FB504F3u,"inexact SQRT publishes its SRT result");
    memory.write_vu_vector(5,{{0xBF800000u,0},{0,0}});
    rejects([&]{ee::exec<MipsOpcode::Vsqrt>(s,memory,0,0x4A0503BDu);},"negative input");
    require(memory.vu_q_ready_cycle()==0 && memory.read_vu_control(22)==0x3FB504F3u,"failed SQRT leaves pipeline unchanged");
    // Manual (fsf=0) and EE assembler/ROM (fsf=1) encodings use ftf,
    // not fsf, as the input selector. Unknown unselected fields are harmless.
    for(unsigned lane=0;lane<4u;++lane)for(unsigned fsf=0;fsf<2u;++fsf)for(bool retired_path:{false,true}) {
        std::array<std::uint32_t,4> values{},unknowns{};unknowns.fill(~0u);
        values[lane]=0x41C80000u;unknowns[lane]=0;
        memory.write_vu_vector(5,{{values[0]|(std::uint64_t{values[1]}<<32u),unknowns[0]|(std::uint64_t{unknowns[1]}<<32u)},
            {values[2]|(std::uint64_t{values[3]}<<32u),unknowns[2]|(std::uint64_t{unknowns[3]}<<32u)}});
        const auto word=0x4A0503BDu|(lane<<23u)|(fsf<<21u);
        const auto decoded=try_decode_r5900(s.pc,word);
        require(decoded && decoded->opcode==MipsOpcode::Vsqrt,"both established VSQRT encodings decode for every ftf lane");
        memory.write_vu_control(22,0x40000000u);
        const auto cycle=s.cycles();
        if(retired_path) {memory.write32(s.pc,word);compiled_fixture_step(ctx);}
        else ee::exec<MipsOpcode::Vsqrt>(s,memory,0,word);
        require(memory.vu_q_ready_cycle()==cycle+7 && memory.read_vu_control(22)==0x40000000u,
            "both shared execution paths preserve the old Q and the true seven-cycle SQRT latency");
        s.stall(7);
        require(memory.read_vu_control(22)==0x40A00000u,"VSQRT reads the selected known ftf lane with no dependency on fsf");
    }
    require(!try_decode_r5900(0,0x4A4503BDu) && !try_decode_r5900(0,0x4A250BBDu) &&
        !try_decode_r5900(0,0x4A000BBFu),"remaining reserved SQRT/WAITQ fields refuse");
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
void vector_nop() {
    for(bool retired_path:{false,true}) {
        Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);EEState state;
        state.pc=0x80001000;state.set_cp0(Cop0::Status,1u<<30u);
        memory.set_clock(&state.retired_instructions,&state.stall_cycles);
        memory.write_vu_control(22,0x40000000u);memory.vu_issue_q(0,0,0,3,3,false);
        memory.write_vu_vector(1,{{0x12345678u,0xFFFF000000000000ull},{0,~0ull}});
        const auto before=memory.observe_vu_vector(1);const auto status=memory.read_vu_control(16);
        const auto decoded=decode_mips(state.pc,0x4A0002FFu);
        require(decoded.opcode==MipsOpcode::Vnop && !decoded.written_gpr() && !decoded.has_delay_slot(),
            "VNOP has an exact COP2 encoding and no invented destination or control-flow edge");
        if(retired_path) {
            memory.write32(state.pc,0x4A0002FFu);std::uint64_t count=0;ExecutionContext context{state,memory,count,1,0xFFFFFFFFu};
            compiled_fixture_step(context);require(count==1 && state.retired_instructions==1,"retired_path VNOP retires one real instruction");
        } else ee::exec<MipsOpcode::Vnop>(state,memory,0,0x4A0002FFu);
        const auto after=memory.observe_vu_vector(1);
        require(state.stall_cycles==0 && memory.vu_q_ready_cycle()==7 && memory.read_vu_control(22)==0x40000000u &&
            memory.read_vu_control(16)==status && before.low.value==after.low.value && before.low.unknown==after.low.unknown &&
            before.high.value==after.high.value && before.high.unknown==after.high.unknown,
            "VNOP preserves status and opaque vectors, and does not turn its ordinary slot into WAITQ");
        state.set_cp0(Cop0::Status,0);
        rejects([&]{ee::exec<MipsOpcode::Vnop>(state,memory,0,0x4A0002FFu);},"Status.CU2 = 0");
    }
    for(auto word:{0x4A2002FFu,0x4A0102FFu,0x4A000AFFu})require(!try_decode_r5900(0,word),"reserved VNOP operand fields refuse");
}
void vector_fixed() {
    constexpr unsigned fractions[]{0,4,12,15};
    constexpr std::uint32_t positive[]{1,24,6144,49152},below_one[]{0,15,4095,32767};
    for(unsigned kind=0;kind<4u;++kind) {
        require(vu_float_to_fixed({0x3FC00000u,0},fractions[kind]).value==positive[kind] &&
            vu_float_to_fixed({0xBFC00000u,0},fractions[kind]).value==0u-positive[kind] &&
            vu_float_to_fixed({0x3F7FFFFFu,0},fractions[kind]).value==below_one[kind],
            "FTOI scales each fixed-point format and truncates either sign toward zero");
        for(auto input:{0x007FFFFFu,0x80000001u})require(vu_float_to_fixed({input,0},fractions[kind]).value==0,
            "FTOI flushes denormal inputs and negative zero without host FP behavior");
        require(vu_float_to_fixed({0x7F800000u,0},fractions[kind]).value==0x7FFFFFFFu &&
            vu_float_to_fixed({0xFF800000u,0},fractions[kind]).value==0x80000000u,
            "finite PS2 exponent 255 clamps to signed integer limits");
        for(bool retired_path:{false,true})for(unsigned destination:{4u,5u,0u}) {
            Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);EEState state;
            state.pc=0x80001000;state.set_cp0(Cop0::Status,1u<<30u);state.fcr31=0x0183FFFFu;
            memory.write_vu_vector(4,{{0xBFC000003FC00000ull,0},{0x000000017F800000ull,0}});
            const auto status=memory.read_vu_control(16),mac=memory.read_vu_control(17);
            const auto word=0x4BE0017Cu|(4u<<11u)|(destination<<16u)|kind;
            const auto decoded=decode_mips(state.pc,word);
            require(decoded.opcode==MipsOpcode::Vftoi && !decoded.written_gpr(),"all VFTOI encodings use the shared AOT vector operation");
            if(retired_path) {memory.write32(state.pc,word);std::uint64_t count=0;ExecutionContext context{state,memory,count,1,0xFFFFFFFFu};compiled_fixture_step(context);}
            else ee::exec<MipsOpcode::Vftoi>(state,memory,0,word);
            const auto result=memory.observe_vu_vector(destination);
            require(!result.low.unknown && !result.high.unknown &&
                result.low.value==(destination ? positive[kind]|(std::uint64_t{0u-positive[kind]}<<32u) : 0u) &&
                result.high.value==(destination ? 0x7FFFFFFFu : 0x3F80000000000000ull),
                "macro FTOI agrees on all lanes, aliases and immutable VF00 through both execution paths");
            require(memory.read_vu_control(16)==status && memory.read_vu_control(17)==mac && state.fcr31==0x0183FFFFu,
                "fixed conversion changes neither VU flags nor the separate EE FCR31");
        }
    }
    const auto dropped=vu_float_to_fixed({0x3FC00000u,0xFFFu},4);
    require(dropped.value==24 && !dropped.unknown,"discarded subpixel fraction uncertainty cannot affect the integer");
    const auto bounded=vu_float_to_fixed({0x3F800000u,0x7FFFFFu},4);
    require(bounded.value==16 && bounded.unknown==15,"FTOI preserves the full uncertain fixed integer range");
    rejects([&]{(void)vu_float_to_fixed({0,0x80000000u},4);},"sign/exponent");
    rejects([&]{(void)vu_float_to_fixed({0,0},1);},"fractional width");
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);EEState state;state.set_cp0(Cop0::Status,1u<<30u);
    memory.write_vu_vector(4,{{0x3FC00000u,0xFFFFFFFF00000000ull},{0,~0ull}});
    memory.write_vu_vector(5,{{0x1234567800000000ull,0},{0xAABBCCDDEEFF0011ull,0}});
    ee::exec<MipsOpcode::Vftoi>(state,memory,0,0x4B05217Du);
    const auto masked=memory.observe_vu_vector(5);
    require(masked.low.value==0x1234567800000018ull && masked.high.value==0xAABBCCDDEEFF0011ull,
        "VFTOI ignores opaque unselected inputs and preserves the destination mask");
    memory.write_vu_vector(4,{{0,0x80000000u},{0,0}});
    rejects([&]{ee::exec<MipsOpcode::Vftoi>(state,memory,0,0x4B05217Du);},"sign/exponent");
    require(memory.observe_vu_vector(5).low.value==masked.low.value,"unproven conversion refuses transactionally");
}
void vector_cpu_bounds() {
    for(bool retired_path:{false,true})for(bool cached:{false,true}) {
        Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);EEState state;
        state.pc=0x80001000;state.set_cp0(Cop0::Status,(1u<<30)|(1u<<29));
        if(cached){memory.diagnostic_data_cache(true);state.set_cp0(Cop0::Config,0x10003);memory.set_cache_config(0x10003);}
        const FloatBounds range{0x44E59100u,0x44E594FFu};
        memory.write_vu_vector(2,{{0x44E5800044E58000ull,0x3FFF00003FFFull},{0x44E5800044E58000ull,0x3FFF00003FFFull}});
        for(unsigned lane=0;lane<4;++lane)memory.restore_vu_vector_bounds(2,lane,range);
        state.set_reg32(9,0x80002000u);
        const auto execute=[&](MipsOpcode opcode,std::uint32_t word){
            if(retired_path){memory.write32(0xA0000000u|(state.pc&0x1FFFFFFFu),word);std::uint64_t count=0;ExecutionContext ctx{state,memory,count,1,~0u};compiled_fixture_step(ctx);}
            else switch(opcode){
            case MipsOpcode::Qmfc2:ee::exec<MipsOpcode::Qmfc2>(state,memory,0,word);break;
            case MipsOpcode::Qmtc2:ee::exec<MipsOpcode::Qmtc2>(state,memory,0,word);break;
            case MipsOpcode::Lq:ee::exec<MipsOpcode::Lq>(state,memory,0,word);break;
            case MipsOpcode::Sq:ee::exec<MipsOpcode::Sq>(state,memory,0,word);break;
            case MipsOpcode::Ld:ee::exec<MipsOpcode::Ld>(state,memory,0,word);break;
            case MipsOpcode::Sd:ee::exec<MipsOpcode::Sd>(state,memory,0,word);break;
            case MipsOpcode::Lw:ee::exec<MipsOpcode::Lw>(state,memory,0,word);break;
            case MipsOpcode::Sw:ee::exec<MipsOpcode::Sw>(state,memory,0,word);break;
            case MipsOpcode::Ldl:ee::exec<MipsOpcode::Ldl>(state,memory,0,word);break;
            case MipsOpcode::Ldr:ee::exec<MipsOpcode::Ldr>(state,memory,0,word);break;
            case MipsOpcode::Lwl:ee::exec<MipsOpcode::Lwl>(state,memory,0,word);break;
            case MipsOpcode::Lwr:ee::exec<MipsOpcode::Lwr>(state,memory,0,word);break;
            case MipsOpcode::Sdl:ee::exec<MipsOpcode::Sdl>(state,memory,0,word);break;
            case MipsOpcode::Sdr:ee::exec<MipsOpcode::Sdr>(state,memory,0,word);break;
            case MipsOpcode::Swl:ee::exec<MipsOpcode::Swl>(state,memory,0,word);break;
            case MipsOpcode::Swr:ee::exec<MipsOpcode::Swr>(state,memory,0,word);break;
            default:throw std::runtime_error("unexpected copy fixture opcode");}
        };
        execute(MipsOpcode::Qmfc2,0x48281000u);execute(MipsOpcode::Sq,0x7D280000u);
        execute(MipsOpcode::Lq,0x792A0000u);execute(MipsOpcode::Qmtc2,0x48AA1800u);
        for(unsigned lane=0;lane<4;++lane)require(memory.observe_vu_vector_bounds(3,lane)==range,
            "QMFC2/SQ/LQ/QMTC2 preserve all four certified lanes beyond the finite enumeration limit");
        memory.set_cpu_context(state.cp0(Cop0::Status),0);memory.tlb_write(0,{0,0x70000000u,0x80000007u,7u});
        state.set_reg32(16,0x70000000u);execute(MipsOpcode::Sq,0x7E0A0000u);
        const auto scratch=memory.fpu_relation(0x70000004u,{0x44E58000u,0x3FFFu});
        require(scratch.origin && scratch.minimum==range[0] && scratch.maximum==range[1],"cached RAM to scratchpad preserves the VU certificate");
        execute(MipsOpcode::Ld,0xDD2B0000u);execute(MipsOpcode::Sd,0xFD2B0020u);
        execute(MipsOpcode::Lw,0x8D2C0024u);execute(MipsOpcode::Sw,0xAD2C0030u);
        const auto relation=memory.fpu_relation(0x80002030u,{0x44E58000u,0x3FFFu});
        require(relation.origin && relation.minimum==range[0] && relation.maximum==range[1],
            "LD/SD and LW/SW retain the same interval without selecting a float encoding");
        execute(MipsOpcode::Sdl,0xB12A0047u);execute(MipsOpcode::Sdr,0xB52A0040u);
        execute(MipsOpcode::Ldl,0x692D0047u);execute(MipsOpcode::Ldr,0x6D2D0040u);
        require(state.gpr_candidates[13].bounds[0]==range && state.gpr_candidates[13].bounds[1]==range,"SDL/SDR and LDL/LDR preserve both full copied words");
        execute(MipsOpcode::Swl,0xA92D0063u);execute(MipsOpcode::Swr,0xB92D0060u);
        execute(MipsOpcode::Lwl,0x892E0063u);execute(MipsOpcode::Lwr,0x992E0060u);
        require(state.gpr_candidates[14].bounds[0]==range,"SWL/SWR and LWL/LWR preserve full float words");
        execute(MipsOpcode::Ldl,0x692D0043u);
        require(state.gpr_candidates[13].bounds[0]==range && state.gpr_candidates[13].bounds[1]==range,"partial LDL retains the untouched word and certifies the copied high word");
        execute(MipsOpcode::Sdr,0xB52D006Cu);
        const auto partial=memory.fpu_relation(0x8000206Cu,{0x44E58000u,0x3FFFu});
        require(partial.origin && partial.minimum==range[0] && partial.maximum==range[1],"partial SDR certifies only its fully copied aligned word");
        state.set_reg64_candidates(10,{1,2});
        require(!state.gpr_candidates[10].bounds[0] && !state.gpr_candidates[10].bounds[1] && state.gpr_candidates[10].bounds[2]==range,
            "finite low-half replacement invalidates only the affected certificates");
        state.set_reg128(10,0,0);for(const auto& bounds:state.gpr_candidates[10].bounds)require(!bounds,"128-bit replacement invalidates every lane");
        memory.write32(0x80002000u,0);execute(MipsOpcode::Ld,0xDD2B0000u);
        require(!state.gpr_candidates[11].bounds[0] && state.gpr_candidates[11].bounds[1]==range,"overwritten memory cannot reintroduce its old certificate");
        execute(MipsOpcode::Qmfc2,0x48201000u);for(const auto& bounds:state.gpr_candidates[0].bounds)require(!bounds,"QMFC2 preserves immutable zero metadata");
    }
}
void vector_setup() {
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
    EEState s;s.pc=0x80001000;s.set_cp0(Cop0::Status,1u<<30);
    memory.write_vu_vector(2,{{0x2222222211111111ull,0x00000000000000FFull},{0x4444444433333333ull,0xFFFFFFFF00000000ull}});
    memory.write_vu_vector(3,{{0xBBBBBBBBAAAAAAAAull,0},{0xDDDDDDDDCCCCCCCCull,0}});
    memory.restore_vu_vector_bounds(2,0,{0x11111180u,0x11111181u});
    memory.restore_vu_vector_bounds(3,1,{0xBBBBBBBBu,0xBBBBBBBBu});
    const auto move_flags=memory.read_vu_control(16);
    const auto move=(0x12u<<26u)|(26u<<21u)|(3u<<16u)|(2u<<11u)|0x33Cu; // xz
    require(decode_r5900(0,move).opcode==MipsOpcode::Vmove,"VMOVE decoder");
    ee::exec<MipsOpcode::Vmove>(s,memory,0,move);
    const auto moved=memory.observe_vu_vector(3);
    require(memory.observe_vu_vector_bounds(3,0)==FloatBounds{0x11111180u,0x11111181u} &&
        memory.observe_vu_vector_bounds(3,1)==FloatBounds{0xBBBBBBBBu,0xBBBBBBBBu},"VMOVE transfers selected numeric bounds and preserves unselected certificates");
    require(moved.low.value==0xBBBBBBBB11111100ull && moved.low.unknown==0xFFu &&
            moved.high.value==0xDDDDDDDD33333333ull && !moved.high.unknown &&
            memory.read_vu_control(16)==move_flags,"VMOVE preserves unselected lanes, uncertainty and flags");
    ee::exec<MipsOpcode::Vmove>(s,memory,0,(move&~(31u<<11u))|(3u<<11u));
    require(memory.observe_vu_vector(3).low.value==moved.low.value,"VMOVE aliases source/destination");
    require(memory.observe_vu_vector_bounds(3,0)==FloatBounds{0x11111180u,0x11111181u},"VMOVE aliases preserve their input certificate");
    ee::exec<MipsOpcode::Vmove>(s,memory,0,move&~(31u<<16u));
    require(memory.observe_vu_vector(0).high.value==0x3F80000000000000ull,"VMOVE preserves VF00");
    memory.write_vu_vector(1,{});
    constexpr auto add=0x4A200868u; // VADD.w VF01,VF01,VF00
    require(try_decode_r5900(0,add)->opcode==MipsOpcode::Vadd,"VADD decodes independently of VMUL");
    memory.write32(s.pc,add);
    std::uint64_t executed=0;ExecutionContext ctx{s,memory,executed,100,~0u};
    compiled_fixture_step(ctx);
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
void vector_accumulator_add() {
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
    EEState s;s.pc=0x80001000;s.set_cp0(Cop0::Status,1u<<30);
    const ObservedQuadword source{{0x400000003F800000ull,0},{0x4080000040400000ull,0}};
    memory.write_vu_vector(1,source);
    memory.write_vu_vector(2,{{0xBF80000000000000ull,0},{0x4080000040000000ull,0}});
    constexpr std::uint32_t expected[4][4]{
        {0x3F800000,0x40000000,0x40400000,0x40800000},
        {0,0x3F800000,0x40000000,0x40400000},
        {0x40400000,0x40800000,0x40A00000,0x40C00000},
        {0x40A00000,0x40C00000,0x40E00000,0x41000000}};
    constexpr std::uint32_t original[]{0x3F800000,0x40000000,0x40400000,0x40800000};
    for(bool retired_path:{false,true})for(unsigned bc=0;bc<4;++bc)for(unsigned mask=0;mask<16;++mask) {
        memory.vu_product(0,1,0,15,3,false,true);
        const auto code=0x4A00003Cu|(mask<<21u)|(2u<<16u)|(1u<<11u)|bc;
        require(decode_mips(0,code).opcode==MipsOpcode::VaddaBc,"VADDA broadcast is supported by AOT decode");
        if(retired_path) {
            memory.write32(s.pc,code);std::uint64_t executed=0;
            ExecutionContext ctx{s,memory,executed,1,~0u};compiled_fixture_step(ctx);
        } else ee::exec<MipsOpcode::VaddaBc>(s,memory,s.pc,code);
        require(memory.read_vu_control(17)==((bc==1 && (mask&8u)) ? 8u : 0u),"VADDA MAC flags use selected results");
        require(memory.observe_vu_vector(1).low.value==source.low.value &&
                memory.observe_vu_vector(2).low.value==0xBF80000000000000ull,"VADDA does not write a vector register");
        memory.vu_product(3,0,0,15,0,true,false);
        const auto result=memory.observe_vu_vector(3);
        for(unsigned lane=0;lane<4;++lane) {
            const auto half=lane<2 ? result.low : result.high;
            require(!half.unknown && static_cast<std::uint32_t>(half.value>>((lane&1u)*32u))==
                ((mask&(8u>>lane)) ? expected[bc][lane] : original[lane]),"VADDA preserves masked accumulator lanes");
        }
    }
    require(decode_r5900(0,0x4BE0083Cu).opcode==MipsOpcode::VaddaBc,"observed VADDAx encoding");
    memory.vu_product(0,1,0,15,3,false,true);
    memory.write_vu_vector(1,{{source.low.value,0},{0x40400000u,0xFFFFFFFF00000000ull}});
    const auto status=memory.read_vu_control(16),mac=memory.read_vu_control(17);
    rejects([&]{ee::exec<MipsOpcode::VaddaBc>(s,memory,0,0x4BE2083Cu);},"unknown operands");
    require(memory.read_vu_control(16)==status && memory.read_vu_control(17)==mac,"refused VADDA preserves flags");
    memory.vu_product(3,0,0,15,0,true,false);
    require(memory.observe_vu_vector(3).low.value==source.low.value &&
            memory.observe_vu_vector(3).high.value==source.high.value,"refused VADDA preserves ACC");
    s.set_cp0(Cop0::Status,0);
    rejects([&]{ee::exec<MipsOpcode::VaddaBc>(s,memory,0,0x4BE0083Cu);},"Status.CU2 = 0");
}
void immediate_add_knowledge() {
    auto owner=std::make_unique<Memory>();auto& memory=*owner;reset_native_ee_fixture(memory,m20::build_firmware().rom);
    for(const auto mask:{0xFull,0xF00ull,0x1E000000000ull,0x8000000000000000ull})
    for(const auto raw:{0ull,0x7Full,0xFFFFFFFFFFFFFFF0ull,0x1921FA0000000000ull})
    for(const unsigned immediate:{0u,1u,0x7Fu,0xFFFFu,0x8000u})for(const unsigned destination:{0u,5u,9u}) {
        std::vector<unsigned> positions;for(unsigned n=0;n<64;++n)if(mask&(1ull<<n))positions.push_back(n);
        std::uint64_t ones=~0ull,any=0;
        for(unsigned choice=0;choice<(1u<<positions.size());++choice) {
            auto value=raw&~mask;for(unsigned n=0;n<positions.size();++n)if(choice&(1u<<n))value|=1ull<<positions[n];
            const auto result=value+static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int16_t>(immediate)));
            ones&=result;any|=result;
        }
        const auto unknown=any^ones;const auto word=0x64000000u|(5u<<21)|(destination<<16)|immediate;
        for(const bool compiled_fixture:{false,true}) {
            auto state=ee_reset_state();state.pc=0x80001000u;state.set_reg128(5,0,0x123456789ABCDEFull);
            state.set_reg64_observed(5,raw,mask);state.gpr[destination].high=destination ? 0x123456789ABCDEFull : 0;
            if(compiled_fixture) {memory.write32(state.pc,word);std::uint64_t count=0;ExecutionContext context{state,memory,count,1,0xFFFFFFFFu};compiled_fixture_step(context);}
            else ee::exec<MipsOpcode::Daddiu>(state,memory,state.pc,word);
            require(state.gpr[destination].low==(destination ? ones : 0) && state.unknown_gpr_bits[destination]==(destination ? unknown : 0),
                "DADDIU known bits match all independent concrete sums, including sign extension, wrap, aliases and zero");
            require(state.gpr[destination].high==(destination ? 0x123456789ABCDEFull : 0),"DADDIU preserves the upper 64-bit lane");
            if(destination && unknown)rejects([&]{(void)state.reg64(destination);},"unknown MMIO bits");
        }
    }
}
void multiply_zero_knowledge() {
    auto owner=std::make_unique<Memory>();auto& memory=*owner;reset_native_ee_fixture(memory,m20::build_firmware().rom);
    for(const auto op:{MipsOpcode::Mult,MipsOpcode::Multu,MipsOpcode::Mult1,MipsOpcode::Multu1})
    for(const bool swap:{false,true})for(const unsigned destination:{0u,4u,5u,6u})for(const bool compiled_fixture:{false,true}) {
        auto s=ee_reset_state();s.pc=0x80001000u;s.lo=11;s.hi=12;s.lo1=13;s.hi1=14;
        s.set_reg128(4,0,0x1234);s.set_reg128(5,0,0x5678);s.set_reg128(6,9,0x9ABC);
        s.set_reg64_observed(swap?4u:5u,0,~0ull);
        const bool second=op==MipsOpcode::Mult1 || op==MipsOpcode::Multu1;
        const auto word=(second ? 0x70000000u : 0u)|(4u<<21)|(5u<<16)|(destination<<11)|
            ((op==MipsOpcode::Mult || op==MipsOpcode::Mult1) ? 0x18u : 0x19u);
        const auto upper=s.gpr[destination].high;
        if(compiled_fixture) {memory.write32(s.pc,word);std::uint64_t count=0;ExecutionContext context{s,memory,count,1,0xFFFFFFFFu};compiled_fixture_step(context);}
        else switch(op) {
            case MipsOpcode::Mult:ee::exec<MipsOpcode::Mult>(s,memory,s.pc,word);break;
            case MipsOpcode::Multu:ee::exec<MipsOpcode::Multu>(s,memory,s.pc,word);break;
            case MipsOpcode::Mult1:ee::exec<MipsOpcode::Mult1>(s,memory,s.pc,word);break;
            case MipsOpcode::Multu1:ee::exec<MipsOpcode::Multu1>(s,memory,s.pc,word);break;
            default:throw std::runtime_error("test opcode");
        }
        require((second ? s.lo1==0 && s.hi1==0 && s.lo==11 && s.hi==12 : s.lo==0 && s.hi==0 && s.lo1==13 && s.hi1==14),"known-zero products establish only the selected LO/HI pipeline");
        require(!s.gpr[destination].low && !s.unknown_gpr_bits[destination] && s.gpr[destination].high==upper,"known-zero product resolves rd, preserves its upper lane and honors aliases/zero");
    }
    auto s=ee_reset_state();s.lo=11;s.hi=12;s.set_reg64(4,1);s.set_reg64_observed(5,0,1);
    rejects([&]{ee::exec<MipsOpcode::Mult>(s,memory,0,0x00853018u);},"unknown MMIO bits");
    require(s.lo==11 && s.hi==12,"an unproved nonzero product refuses before changing LO/HI");
}
void dma_idle_knowledge() {
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
    EEState s;s.pc=0x80001000;s.set_reg32(8,0xB000C000u);
    memory.set_clock(&s.retired_instructions);
    ee::exec<MipsOpcode::Lw>(s,memory,0,0x8D090000u);
    require(s.unknown_gpr_bits[9]!=0,"idle CHCR does not invent reset mode/tag fields");
    ee::exec<MipsOpcode::Andi>(s,memory,0,0x31290100u);
    require(s.reg32(9)==0 && s.unknown_gpr_bits[9]==0,"STR reset proves no transfer is running");
    memory.write32(0xB000C000u,0x84u);
    require(memory.read32(0xB000C000u)==0x84u,"programmed idle CHCR becomes fully known");
}
void dma_cop0_branches() {
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
    EEState s;s.pc=0x80001000u;memory.set_clock(&s.retired_instructions);
    rejects([&]{(void)memory.dma_cop0_condition();},"unknown D_PCR.CPC");
    for(const bool condition:{false,true}) {
        memory.write32(0xB000E020u,condition ? 0u : 4u);
        for(unsigned form=0;form<4;++form) {
            const auto word=0x41000003u|(form<<16u);
            const auto decoded=decode_r5900(0x80001000u,word);
            require(decoded.is_conditional_branch() && decoded.is_likely_branch()==(form>=2u),
                "all BC0 forms retain control-flow and delay-slot classification");
            s.pc=0x80001000u;s.delay_pending=false;
            auto native=s;
            switch(form) {
                case 0:ee::exec<MipsOpcode::Bc0f>(native,memory,native.pc,word);break;
                case 1:ee::exec<MipsOpcode::Bc0t>(native,memory,native.pc,word);break;
                case 2:ee::exec<MipsOpcode::Bc0fl>(native,memory,native.pc,word);break;
                case 3:ee::exec<MipsOpcode::Bc0tl>(native,memory,native.pc,word);break;
            }
            memory.write32(s.pc,word);
            std::uint64_t executed=0;ExecutionContext ctx{s,memory,executed,100,0xFFFFFFFF};
            compiled_fixture_step(ctx);
            const bool taken=condition==((form&1u)!=0u);
            require(s.delay_pending==native.delay_pending && (!s.delay_pending || s.delay_target==native.delay_target),
                "native branch semantics and compiled_fixture agree on the DMA condition and delay slot");
            require(s.delay_pending==!(form>=2u && !taken) &&
                (s.delay_pending ? s.delay_target==(taken ? 0x80001010u : 0x80001008u) : s.pc==0x80001008u),
                "BC0F/T and likely variants follow CPCOND0 and preserve or annul the delay slot");
        }
    }
    memory.write32(0xB000E020u,4u);memory.write32(0xB000E000u,1);
    memory.write32(0xB000A010u,0);memory.write32(0xB000A020u,0);memory.write32(0xB000A000u,0x101);
    require(!memory.dma_cop0_condition(),"idle/uncompleted status is not fabricated by STR");
    s.retired_instructions+=2;
    require(memory.dma_cop0_condition(),"actual GIF DMA completion establishes CPCOND0 without an interrupt mask");
    memory.write32(0xB000E010u,4);
    require(!memory.dma_cop0_condition(),"acknowledging CIS clears CPCOND0 even though STR remains idle");
    s.delay_pending=false;s.set_cp0(Cop0::Status,0x10u);
    rejects([&]{ee::exec<MipsOpcode::Bc0f>(s,memory,0,0x41000000u);},"COP0 branch unusable");
}
void conversions_and_partial_loads() {
    Memory memory;reset_native_ee_fixture(memory,m20::build_firmware().rom);
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
            compiled_fixture_step(ctx);
            require(s.fpr[2]==row[1] && s.fcr31==0x0183C079,"FPU conversion truncates/clamps and preserves status");
        }
    }
    for(const auto bits:{0x4329B5C0u,0xC329B5C0u}) {
        s.pc=0x80001000;s.fpr[1]=bits;s.unknown_fpr_bits[1]=0x3Fu;
        memory.write32(s.pc,0x460008A4u);compiled_fixture_step(ctx);
        require(s.fpr[2]==(bits&0x80000000u ? 0xFFFFFF57u : 169u) && s.unknown_fpr_bits[2]==0 && s.fcr31==0x0183C079,
            "CVT.W.S proves discarded uncertain fractional bits cannot change the integer or status");
    }
    s.pc=0x80001000;s.fpr[1]=0x3F000000u;s.unknown_fpr_bits[1]=0x807FFFFFu;
    memory.write32(s.pc,0x460008A4u);compiled_fixture_step(ctx);
    require(s.fpr[2]==0,"uncertain sub-unit magnitude and sign still truncate to a proven zero");
    s.pc=0x80001000;s.fpr[1]=0x3F000000u;s.unknown_fpr_bits[1]=0x00800000u;
    compiled_fixture_step(ctx);
    require(s.fpr[2]==0 && s.unknown_fpr_bits[2]==1 && *s.fpr_affine[2].candidates==std::vector<std::uint32_t>{0,1},
        "CVT.W.S retains both possible integers instead of selecting one across an uncertain exponent");
    s.pc=0x80001000;s.fpr[1]=0x45000000u;s.unknown_fpr_bits[1]=0x00FFFFFFu;
    s.fpr_affine[1]={memory.allocate_fpu_origin(),0,0,false,s.fpr[1],s.unknown_fpr_bits[1],0x457FFCC2u,0x4580019Fu,{},
        std::make_shared<const std::vector<FloatBounds>>(std::initializer_list<FloatBounds>{{0x457FFCC2u,0x457FFFFFu},{0x45800000u,0x4580019Fu}})};
    compiled_fixture_step(ctx);
    require(s.fpr[2]==0 && s.unknown_fpr_bits[2]==0x1FFFu && *s.fpr_affine[2].candidates==std::vector<std::uint32_t>{4095,4096},
        "CVT.W.S uses certified numeric ranges instead of widening to every value in their raw bit hull");
    s.pc=0x80001000;s.fpr[1]=0;s.unknown_fpr_bits[1]=~0u;s.fpr_affine[1]={};
    rejects([&]{compiled_fixture_step(ctx);},"bounded integer domain");s.unknown_fpr_bits[1]=0;
    s.pc=0x80001000;s.fpr[1]=0x28080404u;s.unknown_fpr_bits[1]=0x03E7F9FBu;s.fpr[2]=0;s.unknown_fpr_bits[2]=0;
    memory.write32(s.pc,0x46020834u);compiled_fixture_step(ctx);
    require(!(s.fcr31&(1u<<23)),"C.LT.S proves a positive uncertain magnitude cannot be less than zero");
    s.pc=0x80001000;ee::exec<MipsOpcode::CLessS>(s,memory,s.pc,0x46020834u);
    require(!(s.fcr31&(1u<<23)),"shared AOT comparison uses the same certified condition");
    s.pc=0x80001000;s.fpr[1]=0x3F800000u;s.unknown_fpr_bits[1]=0x007FFFFFu;s.fpr[2]=0x3FC00000u;
    memory.write32(s.pc,0x46020832u);rejects([&]{compiled_fixture_step(ctx);},"unknown FPR bits");s.unknown_fpr_bits[1]=0;
    s.pc=0x80001000;s.fpr[1]=0x448619CCu;s.unknown_fpr_bits[1]=3;s.fpr[2]=0x40000000u;s.unknown_fpr_bits[2]=0;
    s.fcr31=0x0183C079;memory.write32(s.pc,0x460208C3u);compiled_fixture_step(ctx);
    require(s.fpr[3]==0x440619CCu && s.unknown_fpr_bits[3]==3 && s.fcr31==0x0180C079 && s.fpr_affine[3].candidates->size()==4,
        "DIV.S preserves every uncertain quotient and agrees on I/D causes without clearing O/U history");
    s.pc=0x80001000;memory.write32(s.pc,0x460108C3u);compiled_fixture_step(ctx);
    require(s.fpr[3]==0x3F800000u && s.unknown_fpr_bits[3]==0,"DIV.S preserves correlated identical input domains");
    s.pc=0x80001000;s.fpr[1]=0x3F800000u;s.unknown_fpr_bits[1]=0;s.fpr[2]=0;s.unknown_fpr_bits[2]=0x40000000u;
    memory.write32(s.pc,0x460208C3u);rejects([&]{compiled_fixture_step(ctx);},"cannot certify");s.unknown_fpr_bits[2]=0;
    s.pc=0x80001000;s.fpr[1]=0x44400000u;s.unknown_fpr_bits[1]=0x003FFFFFu;s.fpr_affine[1]={};s.fpr[2]=0x40000000u;s.fcr31=0x0183C079;
    memory.write32(s.pc,0x460208C3u);compiled_fixture_step(ctx);
    require(s.fpr[3]==0x43C00000u && s.unknown_fpr_bits[3]==0x003FFFFFu && s.fpr_affine[3].ranges && s.fcr31==0x0180C079,
        "power-of-two DIV.S preserves a large certified significand interval with common I/D causes");
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
    try {native_fixture_identity();privilege_and_cache();floating_comparisons();nested_exception();addi_exception();signed_arithmetic();packed_copy();micro_subroutine();funnel_shift();leading_sign_count();disabled_cop0_context();integer_multiply_add();symbolic_shifts();register_context();adder();divider();vector_q();vector_nop();vector_fixed();vector_cpu_bounds();vector_setup();vector_accumulator_add();immediate_add_knowledge();multiply_zero_knowledge();dma_idle_knowledge();dma_cop0_branches();conversions_and_partial_loads();return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
