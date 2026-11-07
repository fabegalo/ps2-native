// Original BSD-3-Clause fixtures: no firmware or commercial microcode.
#pragma once
#include "ps2native/vu_control.hpp"
#include "ps2native/gif.hpp"
#include "ps2native/elf.hpp"
#include "ps2native/boot.hpp"
#include <array>
#include <vector>

namespace vu_aot_fixture {
using namespace ps2native;
inline constexpr std::uint32_t nop=0x8000033Cu, upper_nop=0x2FFu;
inline constexpr unsigned math_entry=0, draw_entry=64, indirect_entry=128, loop_entry=256, wrap_entry=2047;
inline constexpr std::uint32_t addiu(unsigned destination,unsigned source,unsigned immediate) {
    return (8u<<25u)|(destination<<16u)|(source<<11u)|(immediate&0x7FFu)|((immediate&0x7800u)<<10u);
}
inline constexpr std::uint32_t upper(unsigned operation,unsigned destination,unsigned left,unsigned right,unsigned mask=15u) {
    return (mask<<21u)|(right<<16u)|(left<<11u)|(destination<<6u)|operation;
}
inline std::vector<std::uint8_t> micro_image() {
    std::vector<std::uint8_t> bytes(16384u);
    const auto pair=[&](unsigned address,std::uint32_t low,std::uint32_t high=upper_nop) {
        const auto value=std::uint64_t{low}|(std::uint64_t{high}<<32u);
        for(unsigned n=0;n<8u;++n)bytes.at(address*8u+n)=static_cast<std::uint8_t>(value>>(8u*n));
    };
    for(unsigned at=0;at<2048u;++at)pair(at,nop);
    pair(0,addiu(1,0,1),upper(0x1BCu,0,1,2)); // MULAx ACC,VF1,VF2x
    pair(1,addiu(2,1,2),upper(0xBDu,0,1,2));  // MADDAy, accumulator bypass
    pair(2,0x22ABCDEFu,upper(0x29u,3,1,2)); // FCSET shares the FMAC issue without changing MAC/STATUS.
    pair(3,0x800003BCu|(2u<<16u)|(1u<<11u)); // DIV Q,VF1x,VF2x
    pair(4,0x800003BFu,upper(0x1Cu,4,1,0));   // WAITQ paired with MULq
    pair(5,(15u<<21u)|(5u<<16u)|1u,upper(0x17Du,0,3,6)); // LQ/FTOI4
    pair(6,(1u<<25u)|(15u<<21u)|(6u<<11u)|2u); // SQ VF6,2(VI0)
    pair(7,(0x1Au<<25u)|(7u<<16u),upper(0x28u,7,1,2));
    pair(8,(0x29u<<25u)|(2u<<16u)|(1u<<11u)|2u);
    pair(9,addiu(8,0,7),upper(0x1FFu,0,1,2,14));pair(10,addiu(8,0,99));
    pair(11,(5u<<25u)|(1u<<21u)|(1u<<16u)|3u,upper(0x2Fu,8,1,2));
    pair(12,0x24FFFFFFu);
    pair(13,0x40000000u,upper_nop|0x80000000u); // LOI 2.0; these bits must never decode as a branch.
    pair(14,0x40400000u,upper(0x22u,11,0,0,8)|0x80000000u); // ADDi sees old I=2 while LOI installs 3.
    pair(15,nop,upper(0x1Eu,12,1,0,8));
    pair(16,nop,upper(0x26u,13,2,0,8));
    pair(17,0x26FFFFFFu,upper_nop|0x40000000u);pair(18,nop);

    // FTOI computes a real sprite's opposite corner and SQ writes the
    // original PACKED XYZ2 packet before the independent XGKICK transfer.
    pair(64,addiu(1,0,32),upper(0x17Du,0,9,10));
    pair(65,(1u<<25u)|(15u<<21u)|(1u<<16u)|(10u<<11u)|2u);
    pair(66,nop);pair(67,nop);pair(68,nop);
    pair(69,0x800006FCu|(1u<<11u));
    pair(70,nop,upper_nop|0x40000000u);pair(71,nop);

    // The dynamic target deliberately has no static proof. Its first pair
    // remains native, and dispatch explicitly interprets the uncovered PC.
    pair(128,addiu(4,0,17));pair(129,(0x24u<<25u)|(3u<<11u));pair(130,addiu(4,0,18));
    pair(140,addiu(5,4,1),upper_nop|0x40000000u);pair(141,nop);

    // Finite workload with exact binary inputs, an integer branch gap,
    // vector dependency stalls and a genuine E delay/drain.
    pair(256,0x80000032u|(1u<<16u)|(1u<<11u)|(31u<<6u),upper(0x2Au,3,1,2));
    pair(257,nop,upper(0x28u,4,3,1));
    pair(258,(0x2Du<<25u)|(1u<<11u)|0x7FDu,upper(0x2Fu,5,1,2));
    pair(259,addiu(2,2,1));pair(260,nop,upper_nop|0x40000000u);pair(261,nop);
    // BAL crosses the 16 KiB micro-PC wrap. Its actual delay pair at PC0
    // executes once before the target at64; the link is the wrapped index1.
    pair(2047,(0x21u<<25u)|(15u<<16u)|64u);
    return bytes;
}
inline std::uint64_t instruction(const std::vector<std::uint8_t>& bytes,unsigned address) {
    std::uint64_t value=0;for(unsigned n=0;n<8u;++n)value|=std::uint64_t{bytes.at(address*8u+n)}<<(8u*n);return value;
}
inline void upload(VuControl& vu,Vif& vif,Gif& gif,GsRegisters& gs) {
    const auto bytes=micro_image();
    // Real MPG, eight 256-LIW chunks. The first qword contains a NOP then
    // the aligned MPG command, followed by the first actual 64-bit LIW.
    for(unsigned base=0;base<2048u;base+=256u) {
        vif.write_fifo(std::uint64_t{0x4A000000u|base}<<32u,instruction(bytes,base),gif,gs,&vu);
        for(unsigned n=1;n<255u;n+=2u)vif.write_fifo(instruction(bytes,base+n),instruction(bytes,base+n+1u),gif,gs,&vu);
        vif.write_fifo(instruction(bytes,base+255u),0,gif,gs,&vu);
    }
}
template<class Control> void seed_vector(Control& vu,unsigned index,const std::array<std::uint32_t,4>& words) {
    vu.write_vector(31,{{words[0]|(std::uint64_t{words[1]}<<32u),0},{words[2]|(std::uint64_t{words[3]}<<32u),0}});
    vu.write(2,0x400u+index);vu.vector_store_increment(31,2,15);
}
template<class Control> void seed_integer(Control& vu,unsigned index,std::uint32_t value) {
    vu.write(1,value);vu.write(2,0x420u+index);vu.integer_store(1,2,8);
}
template<class Control> void seed(Control& vu) {
    seed_vector(vu,1,{0x3F800000u,0x40000000u,0x40800000u,0x41000000u});
    seed_vector(vu,2,{0x40000000u,0x40800000u,0x41000000u,0x41800000u});
    seed_vector(vu,9,{0x41000000u,0x41000000u,0,0});
    seed_integer(vu,1,2000);seed_integer(vu,2,0);seed_integer(vu,3,140);
    for(unsigned lane=0;lane<4u;++lane)vu.write_memory(0x1100C010u+lane*4u,4,{0x3F800000u+lane,0});
}
template<class Control> void seed_graphics(Control& vu,GsRegisters& gs) {
    gs.general_write(0,6);gs.general_write(1,0x80402010u);gs.general_write(0x18,0);gs.general_write(0x1A,1);
    gs.general_write(0x4C,1u<<16u);gs.general_write(0x40,(7ull<<16u)|(7ull<<48u));
    gs.general_write(0x47,3u<<16u);gs.general_write(0x4E,1ull<<32u);gs.general_write(0x22,0);
    gs.general_write(0x4A,0);gs.general_write(0x45,0);gs.general_write(0x49,0);gs.general_write(0x46,1);
    gs.write(kGsPmode,8,0xFF25,0,{});gs.write(kGsSmode2,8,0,0,{});
    gs.write(kGsDispfb1,8,1u<<9u,0,{});gs.write(kGsDisplay1,8,(7ull<<32u)|(7ull<<44u),0,{});
    // Two vertices, PACKED XYZ2. The lower store overwrites qword34 with
    // (128,128,0,0), i.e. (8,8) in the GS's actual 12.4 coordinate format.
    const auto qword=[&](unsigned at,std::uint64_t lo,std::uint64_t hi) {
        vu.write_memory(0x1100C000u+at*16u,8,{lo,0});vu.write_memory(0x1100C008u+at*16u,8,{hi,0});
    };
    qword(32,(1ull<<60u)|0x8002u,5);qword(33,0,0);qword(34,0,0);
}
inline std::vector<std::uint8_t> ee_image() {
    std::vector<std::uint8_t> bytes(0x10Cu);
    const auto put=[&](unsigned at,std::uint32_t value,unsigned size=4u){for(unsigned n=0;n<size;++n)bytes.at(at+n)=static_cast<std::uint8_t>(value>>(8u*n));};
    bytes[0]=0x7F;bytes[1]='E';bytes[2]='L';bytes[3]='F';bytes[4]=1;bytes[5]=1;bytes[6]=1;
    put(16,2,2);put(18,8,2);put(20,1);put(24,0x100000);put(28,52);put(36,0x20924001);
    put(40,52,2);put(42,32,2);put(44,1,2);put(52,1);put(56,0x100);put(60,0x100000);
    put(64,0x100000);put(68,12);put(72,12);put(76,5);put(80,16);
    put(0x100,0x25080001u);put(0x104,0x08040000u);put(0x108,0);return bytes;
}
struct MemoryVu {
    Memory& memory;
    void write(unsigned index,std::uint32_t value){memory.write_vu_control(index,value);}
    void write_vector(unsigned index,ObservedQuadword value){memory.write_vu_vector(index,value);}
    void vector_store_increment(unsigned source,unsigned base,unsigned mask){memory.vu_vector_store_increment(source,base,mask);}
    void integer_store(unsigned source,unsigned base,unsigned mask){memory.vu_integer_store(source,base,mask);}
    void write_memory(std::uint32_t physical,std::size_t bytes,ObservedDoubleword value) {
        if(bytes==4u)memory.store32_observed(physical|0xA0000000u,{static_cast<std::uint32_t>(value.value),static_cast<std::uint32_t>(value.unknown)});
        else memory.store64_observed(physical|0xA0000000u,value);
    }
};
inline void initialize(Memory& memory,EEState& state,bool native,unsigned entry) {
    const auto elf=parse_ee_elf(ee_image());prepare_direct_elf(elf,nullptr,state,memory);memory.enable_vu1_aot(native);
    state.set_reg32_sign_extended(8,0);const auto bytes=micro_image();
    for(unsigned at=0;at<2048;++at)memory.write64(0xB1008000u+at*8u,instruction(bytes,at));
    MemoryVu adapter{memory};seed(adapter);auto& gs=const_cast<GsRegisters&>(memory.gs());seed_graphics(adapter,gs);
    memory.write128(0xB0005000u,0x14000000u|entry,0);
}
}
