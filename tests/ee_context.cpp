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
    require(!ee_fpu_multiply(0x3F800000,0x3FFFFFFF,fcr) && fcr==flags,
            "general asymmetric Booth case cannot substitute host multiplication or change flags");
    // Exact integer mantissa product is 0x68DB8B5DCC64. Even the conservative
    // maximum low-column loss cannot cross the retained 24-bit boundary.
    require(ee_fpu_multiply(0x3C23D70A,0x3C23D70A,fcr)==0x38D1B716,
            "nonsparse multiply certified independently of low-column reduction order");
    require(ee_fpu_multiply(0xBC23D70A,0x3C23D70A,fcr)==0xB8D1B716,
            "certified multiply preserves sign xor");
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
    try {privilege_and_cache();nested_exception();packed_copy();register_context();divider();dma_idle_knowledge();conversions_and_partial_loads();return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
