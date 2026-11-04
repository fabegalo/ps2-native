#include "boot_checks.hpp"
#include "bios_program.hpp"
#include "ps2native/interpreter.hpp"
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
}
}
int main() {
    try {privilege_and_cache();nested_exception();packed_copy();return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
