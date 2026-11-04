// Byte-oracle coverage for firmware copies that carry opaque device replies.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include "ps2native/interpreter.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void step(Memory& m,EEState& s,MipsOpcode op,std::uint32_t word,bool interpreted) {
    if(interpreted) {
        s.pc=0x80001000u;m.write32(s.pc,word);std::uint64_t count=0;
        ExecutionContext ctx{s,m,count,1,0xFFFFFFFFu};interpret_instruction(ctx);require(count==1,"copy instruction retired");
    } else switch(op) {
        case MipsOpcode::Ldl:ee::exec<MipsOpcode::Ldl>(s,m,0,word);break;
        case MipsOpcode::Ldr:ee::exec<MipsOpcode::Ldr>(s,m,0,word);break;
        case MipsOpcode::Sdl:ee::exec<MipsOpcode::Sdl>(s,m,0,word);break;
        case MipsOpcode::Sdr:ee::exec<MipsOpcode::Sdr>(s,m,0,word);break;
        case MipsOpcode::Daddu:ee::exec<MipsOpcode::Daddu>(s,m,0,word);break;
        case MipsOpcode::Addu:ee::exec<MipsOpcode::Addu>(s,m,0,word);break;
        case MipsOpcode::Dsubu:ee::exec<MipsOpcode::Dsubu>(s,m,0,word);break;
        case MipsOpcode::Subu:ee::exec<MipsOpcode::Subu>(s,m,0,word);break;
        case MipsOpcode::Slt:ee::exec<MipsOpcode::Slt>(s,m,0,word);break;
        case MipsOpcode::Sltu:ee::exec<MipsOpcode::Sltu>(s,m,0,word);break;
        case MipsOpcode::Slti:ee::exec<MipsOpcode::Slti>(s,m,0,word);break;
        case MipsOpcode::Sltiu:ee::exec<MipsOpcode::Sltiu>(s,m,0,word);break;
        default:throw std::runtime_error("unexpected test operation");
    }
}
void doubleword_copies(Memory& m) {
    constexpr std::uint8_t data[]{0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
    for(bool interpreted:{false,true})for(unsigned source=0;source<8;++source)for(unsigned dest=0;dest<8;++dest)
    for(unsigned missing=0;missing<=8;++missing)for(bool reverse:{false,true})for(bool prior_unknown:{false,true}) {
        EEState s;s.set_reg64(8,0x80000300u+source);s.set_reg64(10,0x80000400u+dest);
        s.set_reg128_observed(9,0xDEADBEEFDEADBEEFull,0xAABBCCDD00000000ull,prior_unknown ? ~0ull : 0,0xFF);
        m.write128(0x80000300u,0,0);m.write128(0x80000400u,0xAAAAAAAAAAAAAAAAull,0xAAAAAAAAAAAAAAAAull);
        m.write8(0x800003FFu,0xAA);
        for(unsigned n=0;n<8;++n)m.write8(0x80000300u+source+n,data[n]);
        if(missing<8) {
            const auto at=0x80000300u+source+missing,aligned=at&~3u;
            m.store32_observed(aligned,{m.read32(aligned),0xFFu<<((at&3u)*8u)});
        }
        const auto load_left=[&]{step(m,s,MipsOpcode::Ldl,0x68000007u|(8u<<21)|(9u<<16),interpreted);};
        const auto load_right=[&]{step(m,s,MipsOpcode::Ldr,0x6C000000u|(8u<<21)|(9u<<16),interpreted);};
        if(reverse){load_right();load_left();}else{load_left();load_right();}
        const auto expected_unknown=missing<8 ? 0xFFull<<(missing*8u) : 0;
        require(s.unknown_gpr_bits[9]==expected_unknown && s.unknown_gpr_high_bits[9]==0xFF &&
                s.gpr[9].high==0xAABBCCDD00000000ull,"merge pairs replace exactly all low bytes and preserve the upper 64 bits");
        const auto store_left=[&]{step(m,s,MipsOpcode::Sdl,0xB0000007u|(10u<<21)|(9u<<16),interpreted);};
        const auto store_right=[&]{step(m,s,MipsOpcode::Sdr,0xB4000000u|(10u<<21)|(9u<<16),interpreted);};
        if(reverse){store_right();store_left();}else{store_left();store_right();}
        for(unsigned n=0;n<8;++n) {
            const auto at=0x80000400u+dest+n,aligned=at&~3u;const auto word=m.load32_observed(aligned);
            require(((word.unknown>>((at&3u)*8u))&0xFFu)==(n==missing ? 0xFFu : 0u),"each destination byte retains its source knowledge");
            if(n!=missing)require(m.read8(at)==data[n],"known bytes survive every source/destination alignment and merge order");
        }
        require(m.read8(0x800003FFu+dest)==0xAA && m.read8(0x80000408u+dest)==0xAA,"partial stores leave both neighboring bytes untouched");
        if(missing<8)rejects([&]{(void)s.reg64(9);},"unknown");
        else require(s.reg64(9)==0x8877665544332211ull,"complete known merge overwrites an initially opaque register");
    }
}
void narrow_copies(Memory& m) {
    EEState s;s.set_reg64(8,0x80000300);s.set_reg64(10,0x80000400);
    m.store32_observed(0x80000300,{0x80001234u,0x0000000Fu});
    ee::exec<MipsOpcode::Lb>(s,m,0,(8u<<21)|(9u<<16));
    require(s.unknown_gpr_bits[9]==0xF && s.gpr[9].low==0x30,"LB preserves exact data bits when the positive sign is known");
    ee::exec<MipsOpcode::Sb>(s,m,0,(10u<<21)|(9u<<16));
    require(m.load32_observed(0x80000400).unknown==0xF,"SB transports only its low eight bits");
    ee::exec<MipsOpcode::Lh>(s,m,0,(8u<<21)|(9u<<16)|2u);
    require(s.reg64(9)==0xFFFFFFFFFFFF8000ull,"known negative halfword sign extends");
    m.store32_observed(0x80000300,{0x80000000u,0x80008080u});
    ee::exec<MipsOpcode::Lb>(s,m,0,(8u<<21)|(9u<<16));
    require(s.unknown_gpr_bits[9]==0xFFFFFFFFFFFFFF80ull,"unknown byte sign extends uncertainty through bit 63");
    ee::exec<MipsOpcode::Lbu>(s,m,0,(8u<<21)|(9u<<16));
    require(s.unknown_gpr_bits[9]==0x80,"LBU establishes upper zeros");
    ee::exec<MipsOpcode::Lh>(s,m,0,(8u<<21)|(9u<<16)|2u);
    require(s.unknown_gpr_bits[9]==0xFFFFFFFFFFFF8000ull,"unknown halfword sign extends uncertainty");
    ee::exec<MipsOpcode::Lhu>(s,m,0,(8u<<21)|(9u<<16)|2u);
    require(s.unknown_gpr_bits[9]==0x8000,"LHU establishes upper zeros");
    s.set_reg64_observed(9,0xFEDCBA9876543210ull,0xFFFFFFFFFFFF0080ull);
    ee::exec<MipsOpcode::Sh>(s,m,0,(10u<<21)|(9u<<16)|2u);
    require((m.load32_observed(0x80000400).unknown>>16)==0x80 && m.read8(0x80000403)==0x32,
            "SH ignores unrelated high uncertainty and keeps low partial-bit knowledge");
    m.store32_observed(0x80000300,{0x76543210,0xF0000080});
    ee::exec<MipsOpcode::Lwl>(s,m,0,(8u<<21)|(9u<<16)|3u);
    require(s.unknown_gpr_bits[9]==0xFFFFFFFFF0000080ull,"word merge extends an unknown sign while replacing every prior low byte");
    ee::exec<MipsOpcode::Swl>(s,m,0,(10u<<21)|(9u<<16)|3u);
    require(m.load32_observed(0x80000400).unknown==0xF0000080u,"word partial store transports lower lanes without consuming sign extension");
    s.set_reg64(8,0xB000F200u);s.set_reg64_observed(9,0,1);
    device_refused([&]{ee::exec<MipsOpcode::Sb>(s,m,0,(8u<<21)|(9u<<16));},"unknown MMIO bits");
}
void neutral_arithmetic(Memory& memory) {
    for(const bool interpreted:{false,true})for(const bool wide:{false,true})
    for(const bool subtract:{false,true})for(const bool alias:{false,true})for(unsigned order=0;order<(subtract?1u:2u);++order) {
        EEState state;state.set_reg128_observed(8,0xFEDCBA9876543210ull,0xABCDEF0012345678ull,0x8000000Fu,0xFu);
        state.set_reg64(9,0);const auto dest=alias ? 8u : 10u;
        state.set_reg128_observed(dest,state.gpr[dest].low,0xABCDEF0012345678ull,state.unknown_gpr_bits[dest],0xFu);
        const auto op=wide ? subtract ? MipsOpcode::Dsubu : MipsOpcode::Daddu : subtract ? MipsOpcode::Subu : MipsOpcode::Addu;
        const auto word=((order ? 9u : 8u)<<21u)|((order ? 8u : 9u)<<16u)|(dest<<11u)|(wide ? subtract ? 0x2Fu : 0x2Du : subtract ? 0x23u : 0x21u);
        step(memory,state,op,word,interpreted);
        require(state.gpr[dest].low==(wide ? 0xFEDCBA9876543210ull : 0x76543210ull) &&
            state.unknown_gpr_bits[dest]==(wide ? 0x8000000Full : 0xFFFFFFFF8000000Full) &&
            state.gpr[dest].high==0xABCDEF0012345670ull && state.unknown_gpr_high_bits[dest]==0xFu,
            "zero arithmetic copies preserve opaque low bits, 32-bit sign extension, upper 128-bit lanes and aliased inputs in both engines");
    }
    EEState state;state.set_reg64_observed(8,0x12345678,1);state.set_reg64(9,1);
    rejects([&]{ee::exec<MipsOpcode::Daddu>(state,memory,0,(8u<<21u)|(9u<<16u)|(10u<<11u)|0x2Du);},"unknown carry");
    rejects([&]{ee::exec<MipsOpcode::Dsubu>(state,memory,0,(8u<<21u)|(9u<<16u)|(10u<<11u)|0x2Fu);},"unknown borrow");
}
void bounded_comparisons(Memory& memory) {
    for(const bool interpreted:{false,true})for(const bool signed_order:{false,true}) {
        for(const auto high:{0x3F490000ull,0xFFFFFFFFBF490000ull})for(const auto threshold:{0x31FFFFFFull,0xFFFFFFFFFFFFFFFFull}) {
            EEState state;state.set_reg64_observed(8,high,0x3FFFu);state.set_reg64(9,threshold);
            const auto expected=signed_order ? std::bit_cast<std::int64_t>(high)<std::bit_cast<std::int64_t>(threshold) : high<threshold;
            step(memory,state,signed_order ? MipsOpcode::Slt : MipsOpcode::Sltu,
                (8u<<21u)|(9u<<16u)|(10u<<11u)|(signed_order ? 0x2Au : 0x2Bu),interpreted);
            require(state.reg64(10)==static_cast<unsigned>(expected),"signed/unsigned comparisons prove separated operand intervals without consuming irrelevant fraction bits");
            for(unsigned low=0;low<16384;++low)require(expected==(signed_order ?
                std::bit_cast<std::int64_t>(high|low)<std::bit_cast<std::int64_t>(threshold) : (high|low)<threshold),
                "every concrete represented mantissa independently agrees with the proven comparison");
        }
        EEState state;state.set_reg64_observed(8,0,1);state.set_reg64(9,1);state.set_reg64(10,0x1234);
        rejects([&]{step(memory,state,signed_order ? MipsOpcode::Slt : MipsOpcode::Sltu,
            (8u<<21u)|(9u<<16u)|(10u<<11u)|(signed_order ? 0x2Au : 0x2Bu),interpreted);},"comparison depends");
        require(state.reg64(10)==0x1234,"an uncertain comparison refuses before replacing its destination");
        state.set_reg64_observed(8,0,~0ull);step(memory,state,signed_order ? MipsOpcode::Slt : MipsOpcode::Sltu,
            (8u<<21u)|(8u<<16u)|(10u<<11u)|(signed_order ? 0x2Au : 0x2Bu),interpreted);
        require(!state.reg64(10),"a register is never less than itself even when all raw bits remain opaque");
        state.set_reg64_observed(8,0xFFFFFFFFFFFF0000ull,15);
        step(memory,state,signed_order ? MipsOpcode::Slti : MipsOpcode::Sltiu,
            ((signed_order ? 0xAu : 0xBu)<<26u)|(8u<<21u)|(10u<<16u)|0xFFFFu,interpreted);
        require(state.reg64(10)==1,"SLTI/SLTIU compare against the actual sign-extended immediate");
    }
}
}
int main() try {
    Memory memory;memory.reset_for_boot(m20::build_firmware().rom);
    doubleword_copies(memory);narrow_copies(memory);neutral_arithmetic(memory);bounded_comparisons(memory);
    std::cout<<"EE opaque payload copies, all alignments/orders, template/interpreter and narrow sign extension passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
