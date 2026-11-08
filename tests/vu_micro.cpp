// Original integer LIW fixtures, not firmware microprograms.
#include "boot_checks.hpp"
#include "ps2native/vu_control.hpp"
#include "ps2native/vu_fixed.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
constexpr std::uint32_t nop=0x8000033C,upper_nop=0x2FF;
std::uint32_t addiu(unsigned dest,unsigned source,unsigned value) {
    return (8u<<25u)|(dest<<16u)|(source<<11u)|(value&0x7FFu)|((value&0x7800u)<<10u);
}
void instruction(VuControl& vu,unsigned unit,unsigned address,std::uint32_t low,std::uint32_t upper=upper_nop) {
    vu.write_memory((unit ? 0x11008000u : 0x11000000u)+address*8u,8,{low|(std::uint64_t{upper}<<32u),0});
}
ObservedWord integer(const VuControl& vu,unsigned unit,unsigned index) {
    return unit ? vu.observe_data(0x4200u+index*16u) : vu.observe(index);
}
ObservedWord vector_lane(const VuControl& vu,unsigned unit,unsigned index,unsigned lane) {
    if(unit)return vu.observe_data(0x4000u+index*16u+lane*4u);
    const auto vector=vu.observe_vector(index);
    const auto half=lane<2u ? vector.low : vector.high;
    const auto shift=(lane&1u)*32u;
    return {static_cast<std::uint32_t>(half.value>>shift),static_cast<std::uint32_t>(half.unknown>>shift)};
}
void seed_vector(VuControl& vu,unsigned unit,unsigned index,const std::array<std::uint32_t,4>& words) {
    if(unit) {
        // Store raw lanes through the real VU0-to-VU1 register window.
        vu.write_vector(31,{{words[0]|(std::uint64_t{words[1]}<<32u),0},{words[2]|(std::uint64_t{words[3]}<<32u),0}});
        vu.write(2,0x400u+index);vu.vector_store_increment(31,2,15);
    } else vu.write_vector(index,{{words[0]|(std::uint64_t{words[1]}<<32u),0},{words[2]|(std::uint64_t{words[3]}<<32u),0}});
}
void seed_integer(VuControl& vu,unsigned unit,unsigned index,ObservedWord word) {
    if(!unit)vu.write_observed(index,word);
    else {vu.write_observed(1,word);vu.write(2,0x420u+index);vu.integer_store(1,2,8);}
}
std::uint32_t upper(unsigned op,unsigned destination,unsigned left,unsigned right,unsigned mask=15) {
    return (mask<<21)|(right<<16)|(left<<11)|(destination<<6)|op;
}
}
int main() try {
    {
        const ObservedWord word{0x44E58000u,0x3FFFu};
        const FloatBounds narrow{0x44E59100u,0x44E59101u};
        require(vu_float_to_fixed(word,4).unknown!=0,"a bare float hull still represents distinct coordinates");
        const auto result=vu_float_to_fixed(word,4,narrow);
        require(!result.unknown && result.value==vu_float_to_fixed({narrow[0],0},4).value,
            "a certified numeric range can prove fixed coordinates without choosing uncertain fraction bits");
        rejects([&]{(void)vu_float_to_fixed(word,4,FloatBounds{0x44E58000u,0x44E60000u});},"certificate differs");
    }

    for(unsigned unit=0;unit<2u;++unit)for(bool rotate:{false,true}) {
        VuControl vu;const auto data=unit?0x1100C000u:0x11004000u;
        vu.write_memory(data,4,{0x44E58000u,0x3FFFu});vu.restore_data_bounds(data,{0x44E59100u,0x44E59101u});
        instruction(vu,unit,0,0x01010000u);
        const auto mask=rotate?1u:8u;
        instruction(vu,unit,1,0x8000033Cu|unsigned(rotate)|(mask<<21u)|(2u<<16u)|(1u<<11u));
        instruction(vu,unit,2,nop,(mask<<21u)|(3u<<16u)|(2u<<11u)|0x17Du);
        instruction(vu,unit,3,nop,upper_nop|0x40000000u);instruction(vu,unit,4,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(24);
        const auto result=vector_lane(vu,unit,3,rotate?3u:0u);
        require(result.value==0x72C8u && !result.unknown,"VU0/VU1 MOVE and MR32 retain the numeric certificate through source stalls and delayed writeback");
    }
    for(unsigned unit=0;unit<2u;++unit) {
        VuControl vu;seed_vector(vu,unit,1,{0x3F800000u,0x40000000u,0x40800000u,0x41000000u});
        instruction(vu,unit,0,0x40000000u,upper_nop|0x80000000u);
        instruction(vu,unit,1,0x40400000u,upper(0x22u,2,0,0,8)|0x80000000u);
        instruction(vu,unit,2,nop,upper(0x1Eu,3,1,0,8)|0x40000000u);
        instruction(vu,unit,3,nop,upper(0x26u,4,1,0,8));
        vu.start_micro(unit,0,0);vu.advance_micro(20);
        require(!vu.micro_running(unit) && vu.diagnostic_micro_instructions(unit)==4u,"LOI branch-shaped constants issue once and preserve E delay/drain");
        for(const auto [reg,value]:{std::pair{2u,0x40000000u},std::pair{3u,0x40400000u},std::pair{4u,0xC0000000u}}){const auto word=vector_lane(vu,unit,reg,0);require(word.value==value && !word.unknown,"VU0/VU1 use the old paired I and the newly loaded I on following LIWs");}
        instruction(vu,unit,0,0x40000000u,upper(0x1BCu,0,1,1,8)|0x80000000u);
        instruction(vu,unit,1,nop,upper(0x23u,5,1,0,8)|0x40000000u);instruction(vu,unit,2,nop);
        vu.start_micro(unit,0,30);vu.advance_micro(50);
        const auto accumulated=vector_lane(vu,unit,5,0);require(accumulated.value==0x40400000u && !accumulated.unknown,"MADDi uses the new I and the real pending accumulator bypass");
    }
    for(unsigned unit=0;unit<2u;++unit) {
        VuControl vu;const auto data=unit?0x1100C000u:0x11004000u;
        const ObservedWord word{0x44E58000u,0x3FFFu};
        vu.write_memory(data,4,{word.value,word.unknown});vu.restore_data_bounds(data,{0x44E59100u,0x44E59101u});
        instruction(vu,unit,0,0x01010000u);
        instruction(vu,unit,1,nop,(8u<<21u)|(3u<<16u)|(1u<<11u)|0x17Du);
        instruction(vu,unit,2,nop,upper_nop|0x40000000u);instruction(vu,unit,3,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(20);
        require(vector_lane(vu,unit,3,0).value==0x72C8u && !vector_lane(vu,unit,3,0).unknown,
            "a real LQ carries its data certificate through the four-cycle FTOI pipeline");
        vu.write_memory(data,4,{word.value,word.unknown});vu.start_micro(unit,0,40);vu.advance_micro(60);
        require(vector_lane(vu,unit,3,0).unknown!=0,"raw VU data replacement invalidates a prior matching-byte numeric certificate");
        rejects([&]{vu.restore_data_bounds(data,{0x44E58000u,0x44E60000u});},"certificate differs");
    }
    for(unsigned unit=0;unit<2u;++unit) {
        VuControl vu;instruction(vu,unit,0,addiu(1,0,5));instruction(vu,unit,1,addiu(2,1,1));
        instruction(vu,unit,2,nop,upper_nop|0x40000000u);instruction(vu,unit,3,nop);
        vu.start_micro(unit,0,10);require(vu.read(29)==(unit ? 256u : 1u),"the genuine selected VU busy bit rises at start");
        vu.advance_micro(10);require(vu.diagnostic_micro_instructions(unit)==0,"start never executes instructions inline");
        vu.advance_micro(12);require(vu.diagnostic_micro_instructions(unit)==2 && integer(vu,unit,1).unknown,
            "IALU forwarding permits a dependent next instruction before external S writeback");
        vu.advance_micro(15);require(integer(vu,unit,1).value==5 && !integer(vu,unit,1).unknown && integer(vu,unit,2).unknown,
            "external integer visibility follows the documented four-stage writeback");
        vu.advance_micro(17);require(vu.micro_running(unit),"E delay instruction drains through the pipeline before Ready");
        vu.advance_micro(18);require(!vu.micro_running(unit) && vu.read(29)==0 && integer(vu,unit,2).value==6 &&
            vu.diagnostic_micro_instructions(unit)==4 && vu.diagnostic_micro_pc(unit)==32,"E retires its one delay pair, exact results and the next program counter");
        vu.reset();instruction(vu,unit,0,addiu(1,0,1));instruction(vu,unit,1,(0x21u<<25)|(15u<<16)|2u);
        instruction(vu,unit,2,addiu(1,1,2));instruction(vu,unit,3,addiu(1,0,99));instruction(vu,unit,4,addiu(2,1,4));
        instruction(vu,unit,5,nop,upper_nop|0x40000000u);instruction(vu,unit,6,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(20);
        require(integer(vu,unit,1).value==3 && integer(vu,unit,2).value==7 && integer(vu,unit,15).value==3 &&
            vu.diagnostic_micro_instructions(unit)==6,"BAL executes the true delay slot, saves the return index and skips untaken storage");
        vu.reset();instruction(vu,unit,0,addiu(1,0,0x7FFF));instruction(vu,unit,1,addiu(1,1,0x7FFF));
        instruction(vu,unit,2,addiu(1,1,3));instruction(vu,unit,3,nop,upper_nop|0x40000000u);instruction(vu,unit,4,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(20);require(integer(vu,unit,1).value==1,"Imm15 includes its upper four bits and arithmetic wraps to sixteen bits");
        vu.reset();instruction(vu,unit,0,addiu(1,2,1));vu.start_micro(unit,0,0);
        rejects([&]{vu.advance_micro(1);},"contains unknown bits");require(vu.micro_running(unit) && integer(vu,unit,1).unknown,
            "unknown operands cannot fabricate a result or a successful end");
        rejects([&]{vu.start_micro(unit,0,2);},"busy");vu.write(28,unit ? 0x200u : 2u);require(!vu.micro_running(unit),"selected VU reset cancels the failed pipeline");
        vu.reset();instruction(vu,unit,0,addiu(1,0,5),0x3A);vu.start_micro(unit,0,0);
        rejects([&]{vu.advance_micro(1);},"upper instruction");require(integer(vu,unit,1).unknown,"unsupported upper instruction cannot commit a valid lower half");
        vu.reset();instruction(vu,unit,0,0x81E1033Cu);
        instruction(vu,unit,1,nop,upper_nop|0x40000000u);instruction(vu,unit,2,nop);vu.start_micro(unit,0,0);vu.advance_micro(4);
        require(vector_lane(vu,unit,1,0).unknown==~0u,"MOVE never publishes its vector before S");
        vu.advance_micro(7);require(vector_lane(vu,unit,1,0).value==0 && !vector_lane(vu,unit,1,0).unknown && vector_lane(vu,unit,1,3).value==0x3F800000,
            "the real VF00 MOVE alias copies (0,0,0,1) instead of silently acting like the unmasked lower NOP");
        vu.reset();
        const auto data=unit ? 0x1100C000u : 0x11004000u;
        for(unsigned lane=0;lane<4u;++lane)vu.write_memory(data+16u+lane*4u,4,{0x12340000u+lane,lane==2u ? 0xFFu : 0u});
        vu.write_memory(data+32u,4,{0xCAFE1234,0});
        instruction(vu,unit,0,addiu(1,0,1));
        instruction(vu,unit,1,0x8000037Cu|(15u<<21u)|(2u<<16u)|(1u<<11u));
        instruction(vu,unit,2,0x8000037Cu|(8u<<21u)|(3u<<16u)|(1u<<11u));
        instruction(vu,unit,3,nop,upper_nop|0x40000000u);instruction(vu,unit,4,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(5);
        require(vector_lane(vu,unit,2,0).unknown==~0u,"LQI does not expose its result before the four-cycle load latency");
        vu.advance_micro(6);
        for(unsigned lane=0;lane<4u;++lane) {
            const auto word=vector_lane(vu,unit,2,lane);
            require(word.unknown==(lane==2u ? 0xFFu : 0u) && word.value==(lane==2u ? 0x12340000u : 0x12340000u+lane),
                "LQI copies the real data bits and their uncertainty rather than consuming or clearing them");
        }
        require(vector_lane(vu,unit,3,0).unknown==~0u,"consecutive loads keep separate S-stage visibility");
        vu.advance_micro(9);
        require(integer(vu,unit,1).value==3 && vector_lane(vu,unit,3,0).value==0xCAFE1234 &&
            vector_lane(vu,unit,3,1).unknown==~0u,"post-increment bypass feeds the next address and masked-off VF fields stay intact");
        vu.reset();instruction(vu,unit,0,nop,upper_nop|0x40000000u);
        instruction(vu,unit,1,0x81E1037Cu);vu.start_micro(unit,0,0);
        rejects([&]{vu.advance_micro(2);},"E delay slot");
        require(vu.diagnostic_micro_instructions(unit)==1,"invalid E-slot loads cannot modify registers or fabricate termination");
        vu.reset();seed_vector(vu,unit,1,{0x3F800000,0x40000000,0x40800000,0x41000000});
        seed_vector(vu,unit,2,{0x40000000,0x40800000,0x41000000,0x41800000});
        instruction(vu,unit,0,nop,upper(0x1BC,0,1,2));
        instruction(vu,unit,1,nop,upper(0xBD,0,1,2));
        instruction(vu,unit,2,nop,upper(0xA,5,1,2)|0x40000000u);instruction(vu,unit,3,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(6);
        require(vector_lane(vu,unit,5,0).unknown==~0u,"FMAC output is not externally visible before S");
        vu.advance_micro(8);
        const std::array<std::uint32_t,4> expected{0x41600000,0x41E00000,0x42600000,0x42E00000};
        for(unsigned lane=0;lane<4;++lane)require(vector_lane(vu,unit,5,lane).value==expected[lane] &&
            !vector_lane(vu,unit,5,lane).unknown,"consecutive MULA/MADDA/MADD use accumulator bypass and exact matrix arithmetic");
        vu.reset();seed_vector(vu,unit,1,{0x3F800000,0,0,0});seed_vector(vu,unit,2,{0x40000000,0,0,0});
        instruction(vu,unit,0,nop,upper(0x18,5,1,2,8));
        instruction(vu,unit,1,addiu(1,0,7),upper(0x18,6,5,2,8)|0x40000000u);instruction(vu,unit,2,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(4);
        require(vu.diagnostic_micro_instructions(unit)==1,"a pending VF source stalls both halves of the dependent LIW");
        vu.advance_micro(5);require(vu.diagnostic_micro_instructions(unit)==2,"the dependent LIW issues when its source reaches S");
        vu.advance_micro(10);require(vector_lane(vu,unit,6,0).value==0x40800000 && integer(vu,unit,1).value==7,
            "a stalled lower integer operation executes once and the dependent product is correct");
        vu.reset();seed_vector(vu,unit,1,{0x3F800000,0,0,0});seed_vector(vu,unit,2,{0x40000000,0x40800000,0,0});
        vu.write_memory(data,4,{0x41100000,0});vu.write_memory(data+4,4,{0x41200000,0});
        instruction(vu,unit,0,addiu(1,0,0));
        instruction(vu,unit,1,0x8000037Cu|(4u<<21u)|(2u<<16u)|(1u<<11u),upper(0x18,2,1,2,8)|0x40000000u);
        instruction(vu,unit,2,nop);vu.start_micro(unit,0,0);vu.advance_micro(7);
        require(vector_lane(vu,unit,2,0).value==0x40000000 && vector_lane(vu,unit,2,1).value==0x40800000 && integer(vu,unit,1).value==1,
            "upper reads the old VF value, discards the entire colliding LQI destination and preserves its VI increment");
        vu.reset();seed_vector(vu,unit,1,{0,0xC0000000,0x40800000,0x80000000});
        seed_vector(vu,unit,2,{0x40000000,0,0,0});instruction(vu,unit,0,nop,upper(0x18,6,1,2)|0x40000000u);
        instruction(vu,unit,1,nop);vu.start_micro(unit,0,0);vu.advance_micro(4);
        const auto mac_before=unit ? vu.observe_data(0x4310) : vu.observe(17);
        require(mac_before.value==0,"MAC flags retain their previous value until S");
        vu.advance_micro(6);const auto mac=unit ? vu.observe_data(0x4310) : vu.observe(17);
        const auto status=unit ? vu.observe_data(0x4300) : vu.observe(16);
        require(mac.value==0x59 && status.value==0xC3,"signed zero, sign/zero MAC fields and sticky STATUS flags commit with the actual product");
        vu.reset();instruction(vu,unit,0,addiu(1,0,7),upper(0x18,5,1,0,8));vu.start_micro(unit,0,0);
        rejects([&]{vu.advance_micro(1);},"unknown VF operand");
        require(integer(vu,unit,1).unknown && vu.diagnostic_micro_instructions(unit)==0,
            "unknown FMAC input refuses the whole pair without a lower integer side effect");
        vu.reset();seed_vector(vu,unit,1,{0x3F800000,0,0,0});
        instruction(vu,unit,0,addiu(1,0,7),upper(8,5,1,0,8));vu.start_micro(unit,0,0);
        rejects([&]{vu.advance_micro(1);},"unknown ACC");
        require(integer(vu,unit,1).unknown,"an unknown accumulator cannot be replaced by zero");
        vu.reset();seed_vector(vu,unit,1,{0x3F800000,0x40000000,0x40800000,0x41000000});
        seed_vector(vu,unit,2,{0x40000000,0,0,0});
        instruction(vu,unit,0,addiu(1,0,1));
        instruction(vu,unit,1,nop,upper(0x18,5,1,2,15));
        instruction(vu,unit,2,0x8000037Du|(10u<<21u)|(1u<<16u)|(5u<<11u));
        instruction(vu,unit,3,0x8000037Du|(4u<<21u)|(1u<<16u)|(5u<<11u));
        instruction(vu,unit,4,nop,upper_nop|0x40000000u);instruction(vu,unit,5,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(5);
        require(vu.diagnostic_micro_instructions(unit)==2,"SQI stalls on a pending FMAC source without incrementing its address twice");
        vu.advance_micro(9);require(vu.read_memory(data+16u,4).unknown==0xFFFFFFFFu,"SQI payload is not published before its writeback stage");
        vu.advance_micro(10);
        require(vu.read_memory(data+16u,4).value==0x40000000 && vu.read_memory(data+24u,4).value==0x41000000 &&
            vu.read_memory(data+20u,4).unknown==0xFFFFFFFFu,"SQI stores actual selected FMAC result fields and preserves other memory bits");
        vu.advance_micro(13);
        require(vu.read_memory(data+36u,4).value==0x40800000 && integer(vu,unit,1).value==3 && !vu.micro_running(unit),
            "consecutive SQI operations use VI post-increment bypass and drain their real stores before E completion");
        vu.reset();instruction(vu,unit,0,addiu(1,0,0));
        instruction(vu,unit,1,0x8000037Du|(8u<<21u)|(1u<<16u)|(7u<<11u));
        instruction(vu,unit,2,nop,upper_nop|0x40000000u);instruction(vu,unit,3,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(8);
        require(vu.read_memory(data,4).unknown==0xFFFFFFFFu && integer(vu,unit,1).value==1,
            "SQI transports an unknown payload without inventing its numeric value");
        vu.reset();instruction(vu,unit,0,addiu(1,0,5));instruction(vu,unit,1,nop);
        instruction(vu,unit,2,addiu(1,0,8));instruction(vu,unit,3,(0x24u<<25u)|(1u<<11u));
        instruction(vu,unit,4,addiu(2,0,7));instruction(vu,unit,5,nop,upper_nop|0x40000000u);
        instruction(vu,unit,6,nop);vu.start_micro(unit,0,0);vu.advance_micro(20);
        require(vu.diagnostic_micro_instructions(unit)==7 && integer(vu,unit,1).value==8 && integer(vu,unit,2).value==7,
            "JR uses the pre-gap integer target, executes its delay instruction and retains the newer VI writeback");
        vu.reset();instruction(vu,unit,0,addiu(1,0,7));instruction(vu,unit,1,nop);instruction(vu,unit,2,nop);
        instruction(vu,unit,3,(0x25u<<25u)|(1u<<16u)|(1u<<11u));instruction(vu,unit,4,addiu(2,0,8));
        instruction(vu,unit,7,nop,upper_nop|0x40000000u);instruction(vu,unit,8,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(20);
        require(vu.diagnostic_micro_instructions(unit)==7 && integer(vu,unit,1).value==5 && integer(vu,unit,2).value==8,
            "JALR snapshots an aliased target before saving its return index and executing its delay pair");
        vu.reset();vu.write_memory(data,4,{0x1234,0xFFFF0000u});
        instruction(vu,unit,0,0x8001037Cu|(8u<<21u));
        instruction(vu,unit,1,0x800103FCu|(1u<<11u));instruction(vu,unit,2,addiu(2,1,1));
        instruction(vu,unit,3,nop,upper_nop|0x40000000u);instruction(vu,unit,4,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(4);
        require(vu.diagnostic_micro_instructions(unit)==1,"MTIR stalls for the real selected VF source field");
        vu.advance_micro(12);
        require(integer(vu,unit,1).value==0x1234 && !integer(vu,unit,1).unknown && integer(vu,unit,2).value==0x1235,
            "MTIR transfers only low sixteen bits and forwards them to the next IALU operation without consuming unknown upper bits");
        vu.reset();vu.write_memory(data,4,{0x1234,0xF0});
        instruction(vu,unit,0,0x8001037Cu|(8u<<21u));instruction(vu,unit,1,0x800103FCu|(1u<<11u));
        instruction(vu,unit,2,nop,upper_nop|0x40000000u);instruction(vu,unit,3,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(11);
        require(integer(vu,unit,1).unknown==0xF0 && integer(vu,unit,1).value==0x1204,
            "MTIR preserves exact low-bit uncertainty rather than fabricating a numeric operand");
        vu.reset();seed_vector(vu,unit,1,{0x11223344,0,0,0});seed_vector(vu,unit,2,{0x3F800000,0,0,0});
        instruction(vu,unit,0,0x800103FCu|(1u<<11u),upper(0x1B,1,2,0,8)|0x40000000u);instruction(vu,unit,1,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(6);
        require(integer(vu,unit,1).value==0x3344 && vector_lane(vu,unit,1,0).value==0x3F800000,
            "MTIR and an upper write in the same pair read old data and commit their independent actual destinations");
        vu.reset();instruction(vu,unit,0,addiu(1,0,2));instruction(vu,unit,1,addiu(2,0,0));
        instruction(vu,unit,2,(9u<<25u)|(1u<<16u)|(1u<<11u)|1u);instruction(vu,unit,3,nop);
        instruction(vu,unit,4,(0x29u<<25u)|(1u<<16u)|0x7FDu);instruction(vu,unit,5,addiu(2,2,1));
        instruction(vu,unit,6,nop,upper_nop|0x40000000u);instruction(vu,unit,7,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(30);
        require(integer(vu,unit,1).value==0 && integer(vu,unit,2).value==2 && vu.diagnostic_micro_instructions(unit)==12,
            "IBNE takes a signed backward offset, observes the integer gap and executes both taken/untaken delay instructions");
        vu.reset();instruction(vu,unit,0,addiu(1,0,1));instruction(vu,unit,1,nop);instruction(vu,unit,2,addiu(1,0,0));
        instruction(vu,unit,3,(0x28u<<25u)|(1u<<16u)|2u);instruction(vu,unit,4,addiu(2,0,7));
        instruction(vu,unit,5,nop,upper_nop|0x40000000u);instruction(vu,unit,6,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(20);
        require(integer(vu,unit,1).value==0 && integer(vu,unit,2).value==7 && vu.diagnostic_micro_instructions(unit)==7,
            "IBEQ uses the pre-gap condition instead of a too-new adjacent IALU result");
        vu.reset();instruction(vu,unit,0,0x800106BDu);instruction(vu,unit,1,nop,upper_nop|0x40000000u);instruction(vu,unit,2,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(7);
        require(integer(vu,unit,1).unknown==(unit ? 0x3FFu : 0xFFu),
            "XITOP without established VIF input transfers unknown address bits and known zero-extension without fabricating a pointer");
        vu.reset();vu.write_memory(data+8u,4,{0x7ABC,0xFFFF0000u});
        instruction(vu,unit,0,0x800103FEu|(2u<<21u));
        instruction(vu,unit,1,addiu(2,1,1),upper(0x1B,5,0,0,8));
        instruction(vu,unit,2,nop,upper_nop|0x40000000u);instruction(vu,unit,3,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(4);
        require(vu.diagnostic_micro_instructions(unit)==1 && integer(vu,unit,1).unknown==0xFFFFu &&
            vector_lane(vu,unit,5,0).unknown==~0u,"an ILWR-dependent integer instruction interlocks both halves and cannot use the early IALU bypass");
        vu.advance_micro(11);
        require(integer(vu,unit,1).value==0x7ABC && !integer(vu,unit,1).unknown && integer(vu,unit,2).value==0x7ABD &&
            vector_lane(vu,unit,5,0).value==0 && !vector_lane(vu,unit,5,0).unknown,"the true S-stage load value releases the dependent pair exactly once");
        vu.reset();vu.write_memory(data+16u+4u,4,{0xABCD,0xFF00});
        instruction(vu,unit,0,(4u<<25u)|(4u<<21u)|(1u<<16u)|1u);
        instruction(vu,unit,1,nop,upper_nop|0x40000000u);instruction(vu,unit,2,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(7);
        require(integer(vu,unit,1).unknown==0xFF00 && integer(vu,unit,1).value==0xCD,"ILW offset selects the actual memory field and preserves low-bit uncertainty");
        vu.reset();vu.write_memory(data+16u,4,{0x11223344,0});vu.write_memory(data+28u,4,{0xFEDCBA98,0x00FF0000});
        for(unsigned lane=0;lane<4;++lane)vu.write_memory(data+48u+lane*4u,4,{0x55555555,0});
        instruction(vu,unit,0,addiu(1,0,2));instruction(vu,unit,1,(15u<<21u)|(6u<<16u)|(1u<<11u)|0x7FFu);
        instruction(vu,unit,2,(1u<<25u)|(9u<<21u)|(1u<<16u)|(6u<<11u)|1u);
        instruction(vu,unit,3,nop,upper_nop|0x40000000u);instruction(vu,unit,4,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(5);
        require(vu.diagnostic_micro_instructions(unit)==2 && vu.read_memory(data+48u,4).value==0x55555555,
            "offset SQ waits for the real LQ vector fields before issuing");
        vu.advance_micro(12);
        require(vu.read_memory(data+48u,4).value==0x11223344 && vu.read_memory(data+52u,4).value==0x55555555 &&
            vu.read_memory(data+60u,4).value==0xFE00BA98 && vu.read_memory(data+60u,4).unknown==0x00FF0000 && integer(vu,unit,1).value==2,
            "signed LQ/SQ offsets transfer exact selected bits while retaining masks and the unmodified VI base");
        vu.reset();seed_vector(vu,unit,1,{0xBF800000,0x3F800000,0x80000000,0x7FFFFFFF});
        seed_vector(vu,unit,2,{0,0x40000000,0,0xFF7FFFFF});
        instruction(vu,unit,0,nop,upper(0x18,5,0,0));
        instruction(vu,unit,1,nop,upper(0x10,3,1,0));
        instruction(vu,unit,2,nop,upper(0x2F,4,1,2)|0x40000000u);instruction(vu,unit,3,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(8);
        const std::array<std::uint32_t,4> max_expected{0,0x3F800000,0,0x7FFFFFFF};
        const std::array<std::uint32_t,4> min_expected{0xBF800000,0x3F800000,0,0xFF7FFFFF};
        for(unsigned lane=0;lane<4u;++lane)require(vector_lane(vu,unit,3,lane).value==max_expected[lane] &&
            vector_lane(vu,unit,4,lane).value==min_expected[lane],"MAX broadcast and MINI vector forms use PS2 finite sign/magnitude ordering and FT ties");
        const auto mac_after=unit ? vu.observe_data(0x4310) : vu.observe(17);
        const auto status_after=unit ? vu.observe_data(0x4300) : vu.observe(16);
        require(mac_after.value==15 && status_after.value==0x41,"MAX/MINI preserve the prior arithmetic MAC and sticky STATUS flags");
        vu.reset();seed_vector(vu,unit,1,{0x3F800000,0xC0000000,0x40800000,0});
        seed_vector(vu,unit,2,{0x40000000,0x40800000,0xC1000000,0});
        instruction(vu,unit,0,nop,upper(0x2C,3,1,2));instruction(vu,unit,1,nop,upper(1,4,1,2));
        instruction(vu,unit,2,nop,upper_nop|0x40000000u);instruction(vu,unit,3,nop);vu.start_micro(unit,0,0);vu.advance_micro(5);
        const std::array<std::uint32_t,4> sub_expected{0xBF800000,0xC0C00000,0x41400000,0};
        for(unsigned lane=0;lane<4u;++lane)require(vector_lane(vu,unit,3,lane).value==sub_expected[lane],"SUB vector fields have the exact signed arithmetic results");
        require((unit ? vu.observe_data(0x4310) : vu.observe(17)).value==0xC1,"SUB publishes its sign/zero MAC fields at S");
        vu.advance_micro(8);const std::array<std::uint32_t,4> add_expected{0x40A00000,0x40000000,0x41000000,0x40800000};
        for(unsigned lane=0;lane<4u;++lane)require(vector_lane(vu,unit,4,lane).value==add_expected[lane],"ADD broadcast selects the requested FT component independently in each result lane");
        require((unit ? vu.observe_data(0x4300) : vu.observe(16)).value==0xC0,"later positive ADD clears current Z/S and retains the true sticky flags");
        vu.reset();seed_integer(vu,unit,1,{0xFFFE,0});
        instruction(vu,unit,0,0x800303FDu|(5u<<21u)|(1u<<11u));
        instruction(vu,unit,1,0x8004033Du|(12u<<21u)|(3u<<11u));
        instruction(vu,unit,2,nop,upper_nop|0x40000000u);instruction(vu,unit,3,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(11);
        require(vector_lane(vu,unit,3,0).unknown==~0u && vector_lane(vu,unit,3,1).value==0xFFFFFFFEu &&
            vector_lane(vu,unit,3,3).value==0xFFFFFFFEu && vector_lane(vu,unit,4,0).value==0xFFFFFFFEu && vector_lane(vu,unit,4,1).unknown==~0u,
            "MFIR sign-extends actual VI bits and MR32 rotates the selected old VF fields after the true source latency");
        vu.reset();seed_integer(vu,unit,1,{0x12,0x8000});
        instruction(vu,unit,0,0x800303FDu|(15u<<21u)|(1u<<11u));
        instruction(vu,unit,1,nop,upper_nop|0x40000000u);instruction(vu,unit,2,nop);vu.start_micro(unit,0,0);vu.advance_micro(7);
        require(vector_lane(vu,unit,3,0).value==0x12 && vector_lane(vu,unit,3,0).unknown==0xFFFF8000u,
            "an unknown VI sign bit propagates through all sixteen sign-extension bits in MFIR");
        for(unsigned format=0;format<4u;++format) {
            vu.reset();seed_vector(vu,unit,1,{0xBEE66666,0x3EE66666,0x3F0CCCCD,0x42F6E666});
            instruction(vu,unit,0,nop,upper(0x18,5,0,0));
            instruction(vu,unit,1,nop,(15u<<21u)|(3u<<16u)|(1u<<11u)|(0x17Cu+format)|0x40000000u);
            instruction(vu,unit,2,nop);vu.start_micro(unit,0,0);vu.advance_micro(5);
            require(vector_lane(vu,unit,3,0).unknown==~0u,"FTOI output retains the four-cycle vector latency");
            vu.advance_micro(8);
            constexpr std::uint32_t expected[4][4]{{0,0,0,123},{0u-7u,7,8,1975},{0u-1843u,1843,2252,505651},{0u-14745u,14745,18022,4045209}};
            for(unsigned lane=0;lane<4u;++lane)require(vector_lane(vu,unit,3,lane).value==expected[format][lane],"FTOI formats truncate signed fractional values toward zero with exact fixed-point scaling");
            require((unit ? vu.observe_data(0x4310) : vu.observe(17)).value==15 && (unit ? vu.observe_data(0x4300) : vu.observe(16)).value==0x41,
                "FTOI preserves prior MAC and STATUS flags");
        }
        vu.reset();seed_vector(vu,unit,1,{0x4F000000,0xCF000000,0x7FFFFFFF,0xFFFFFFFF});
        instruction(vu,unit,0,nop,(15u<<21u)|(3u<<16u)|(1u<<11u)|0x17Cu|0x40000000u);instruction(vu,unit,1,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(6);
        require(vector_lane(vu,unit,3,0).value==0x7FFFFFFF && vector_lane(vu,unit,3,1).value==0x80000000 &&
            vector_lane(vu,unit,3,2).value==0x7FFFFFFF && vector_lane(vu,unit,3,3).value==0x80000000,
            "FTOI clamps signed overflow including finite PS2 exponent-255 values instead of using host NaN conversions");
        vu.reset();vu.write_memory(data,4,{0x3FF80000,0});seed_vector(vu,unit,3,{0,0x12345678,0,0});
        instruction(vu,unit,0,0x8101037Cu);instruction(vu,unit,1,addiu(1,0,7),(8u<<21u)|(3u<<16u)|(1u<<11u)|0x17Du);
        instruction(vu,unit,2,nop,upper_nop|0x40000000u);instruction(vu,unit,3,nop);vu.start_micro(unit,0,0);vu.advance_micro(4);
        require(vu.diagnostic_micro_instructions(unit)==1 && integer(vu,unit,1).unknown,"FTOI interlocks its actual selected loaded source and stalls the paired lower operation");
        vu.advance_micro(8);require(vector_lane(vu,unit,3,0).value==0,"a dependent FTOI cannot publish before its own S stage");
        vu.advance_micro(11);require(vector_lane(vu,unit,3,0).value==31 && vector_lane(vu,unit,3,1).value==0x12345678 && integer(vu,unit,1).value==7,
            "masked FTOI uses the true loaded coordinate, ignores unselected unknown operands, and preserves other destination fields");
        vu.reset();instruction(vu,unit,0,addiu(1,0,9),(8u<<21u)|(3u<<16u)|(1u<<11u)|0x17Du);vu.start_micro(unit,0,0);
        rejects([&]{vu.advance_micro(1);},"FTOI consumes unknown");require(integer(vu,unit,1).unknown,"unknown FTOI input refuses both halves without inventing a vertex");
        vu.reset();seed_vector(vu,unit,1,{0x3F800001,0x3F800001,0xBF800001,0xBF800001});
        seed_vector(vu,unit,2,{0x3F800003,0x3F800003,0x3F800003,0x3F800003});
        instruction(vu,unit,0,nop,upper(0x2A,5,1,2));
        instruction(vu,unit,1,nop,(15u<<21u)|(3u<<16u)|(5u<<11u)|0x17Du|0x40000000u);
        instruction(vu,unit,2,nop);vu.start_micro(unit,0,0);vu.advance_micro(5);
        const auto bounded=vector_lane(vu,unit,5,0);
        require(bounded.unknown && !(bounded.unknown&0xFF800000u),"FMAC keeps an uncertain fraction and certified sign/exponent through the real S-stage writeback");
        vu.advance_micro(12);
        for(unsigned lane=0;lane<4;++lane)require(vector_lane(vu,unit,3,lane).value==(lane<2u ? 16u : 0u-16u) &&
            !vector_lane(vu,unit,3,lane).unknown,"FTOI proves the same fixed-point coordinate for every bounded product candidate");
        vu.reset();
        // Both adjacent representable floats immediately below 2.0 truncate
        // to the same 15-fractional-bit integer.
        vu.write_memory(data,4,{0x3FFFFFFEu,1});
        instruction(vu,unit,0,0x8101037Cu);
        instruction(vu,unit,1,nop,(8u<<21u)|(3u<<16u)|(1u<<11u)|0x17Fu|0x40000000u);
        instruction(vu,unit,2,nop);vu.start_micro(unit,0,0);vu.advance_micro(12);
        require(vector_lane(vu,unit,3,0).value==65535u && !vector_lane(vu,unit,3,0).unknown,
            "FTOI discards all unobservable mantissa bits at the final truncation boundary");
        vu.reset();vu.write_memory(data,4,{0x3F800000u,0x100u});
        instruction(vu,unit,0,0x8101037Cu);
        instruction(vu,unit,1,nop,(8u<<21u)|(3u<<16u)|(1u<<11u)|0x17Fu|0x40000000u);
        instruction(vu,unit,2,nop);vu.start_micro(unit,0,0);vu.advance_micro(12);
        require(vector_lane(vu,unit,3,0).value==32768u && vector_lane(vu,unit,3,0).unknown==1u,
            "FTOI preserves a genuinely observable integer difference instead of selecting a guessed vertex coordinate");
        vu.reset();seed_vector(vu,unit,1,{0x3F800000,0xBF800000,0,0x3F800000});seed_integer(vu,unit,1,{});
        instruction(vu,unit,0,nop,upper(0x1B,5,1,0));instruction(vu,unit,1,addiu(2,0,0x40));
        instruction(vu,unit,2,nop);instruction(vu,unit,3,nop);
        instruction(vu,unit,4,(0x1Au<<25u)|(1u<<16u)|(2u<<11u));
        instruction(vu,unit,5,(0x29u<<25u)|(1u<<11u)|2u);instruction(vu,unit,6,addiu(3,0,7));
        instruction(vu,unit,7,addiu(3,0,99));instruction(vu,unit,8,nop,upper_nop|0x40000000u);instruction(vu,unit,9,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(6);require(integer(vu,unit,1).value==0,"flag check bypass does not publish external VI before S");
        vu.advance_micro(16);require(integer(vu,unit,1).value==0x40 && integer(vu,unit,3).value==7 && vu.diagnostic_micro_instructions(unit)==9,
            "FMAND reads actual retired arithmetic MAC bits and supplies the next conditional branch without the ordinary integer gap");
        require((unit ? vu.observe_data(0x4310) : vu.observe(17)).value==0x42,"MAC checks preserve the actual flags");
        vu.reset();seed_integer(vu,unit,2,{0,0xFFFFu});instruction(vu,unit,0,(0x1Au<<25u)|(1u<<16u)|(2u<<11u));
        instruction(vu,unit,1,(0x1Bu<<25u)|(3u<<16u)|(2u<<11u));instruction(vu,unit,2,(0x18u<<25u)|(4u<<16u)|(2u<<11u)|0u,upper_nop|0x40000000u);
        instruction(vu,unit,3,nop);vu.start_micro(unit,0,0);vu.advance_micro(8);
        require(integer(vu,unit,1).value==0 && !integer(vu,unit,1).unknown && integer(vu,unit,3).unknown==0xFFFFu && integer(vu,unit,4).unknown==1,
            "MAC flag checks preserve uncertain source bits, prove AND with zero, and cannot fabricate an equality result");
        for(unsigned op=0x2Cu;op<=0x2Fu;++op)for(const auto input : {0u,1u,0x7FFFu,0x8000u,0xFFFFu}) {
            vu.reset();seed_integer(vu,unit,1,{input,0});
            instruction(vu,unit,0,(op<<25u)|(1u<<11u)|2u);instruction(vu,unit,1,addiu(2,0,7));
            instruction(vu,unit,2,addiu(2,0,99));instruction(vu,unit,3,nop,upper_nop|0x40000000u);instruction(vu,unit,4,nop);
            vu.start_micro(unit,0,0);vu.advance_micro(12);
            const auto signed_input=input<0x8000u ? static_cast<int>(input) : static_cast<int>(input)-65536;
            const bool taken=op==0x2Cu ? signed_input<0 : op==0x2Du ? signed_input>0 : op==0x2Eu ? signed_input<=0 : signed_input>=0;
            require(integer(vu,unit,2).value==(taken ? 7u : 99u),"signed VU branches distinguish negative, positive, zero and both sixteen-bit boundaries and execute the true delay slot");
        }
        vu.reset();seed_integer(vu,unit,1,{0xFFFFu,0});instruction(vu,unit,0,addiu(1,0,1));
        instruction(vu,unit,1,(0x2Cu<<25u)|(1u<<11u)|2u);instruction(vu,unit,2,addiu(2,0,7));
        instruction(vu,unit,3,addiu(2,0,99));instruction(vu,unit,4,nop,upper_nop|0x40000000u);instruction(vu,unit,5,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(14);require(integer(vu,unit,1).value==1 && integer(vu,unit,2).value==7,
            "signed branch operands retain the documented one-instruction integer gap");
        vu.reset();seed_integer(vu,unit,2,{3,0});seed_integer(vu,unit,3,{});
        instruction(vu,unit,0,0x80000032u|(2u<<16u)|(2u<<11u)|(31u<<6u));instruction(vu,unit,1,nop);
        instruction(vu,unit,2,(0x2Du<<25u)|(2u<<11u)|0x7FDu);instruction(vu,unit,3,addiu(3,3,1));
        instruction(vu,unit,4,nop,upper_nop|0x40000000u);instruction(vu,unit,5,nop);vu.start_micro(unit,0,0);vu.advance_micro(22);
        require(integer(vu,unit,2).value==0 && integer(vu,unit,3).value==3 && vu.diagnostic_micro_instructions(unit)==14,
            "IBGTZ sign-extends a backward displacement, tests updated loop data after its required gap, and retains each delay instruction");
        vu.reset();instruction(vu,unit,0,(0x2Eu<<25u)|(1u<<11u)|2u,upper(0x1B,5,0,0));vu.start_micro(unit,0,0);
        rejects([&]{vu.advance_micro(1);},"contains unknown bits");require(vector_lane(vu,unit,5,0).unknown==~0u && vu.diagnostic_micro_instructions(unit)==0,
            "an unknown signed branch condition cannot fabricate a direction or commit the paired upper result");
    }
    for(unsigned unit=0;unit<2;++unit) {
        VuControl vu;instruction(vu,unit,0,0x22ABCDEFu);instruction(vu,unit,1,0x22000000u);
        instruction(vu,unit,2,nop,upper_nop|0x40000000u);instruction(vu,unit,3,nop);
        const auto clip=[&]{return unit ? vu.observe_data(0x4320u) : vu.observe(18);};
        vu.start_micro(unit,0,0);vu.advance_micro(4);
        require(clip().value==0 && !clip().unknown,"FCSET cannot publish before its four-stage writeback");
        vu.advance_micro(5);require(clip().value==0xABCDEFu && !clip().unknown,"FCSET publishes all 24 immediate bits at S");
        vu.advance_micro(6);require(clip().value==0 && !clip().unknown,"successive FCSET writes preserve pipeline ordering");
        vu.advance_micro(8);require(!vu.micro_running(unit) && clip().value==0,"FCSET drains through a genuine E/Ready termination");
        vu.reset();instruction(vu,unit,0,0x23000000u);vu.start_micro(unit,0,0);
        rejects([&]{vu.advance_micro(1);},"FCSET reserved bit");
        require(vu.diagnostic_micro_instructions(unit)==0,"invalid FCSET cannot retire or fabricate a clip flag");
    }
    for(unsigned unit=0;unit<2;++unit) {
        VuControl vu;seed_vector(vu,unit,1,{0x40000000u,0xC0000000u,0x3F800000u,0});
        seed_vector(vu,unit,2,{0,0,0,0xBF800000u});
        const auto clip_upper=(14u<<21u)|(2u<<16u)|(1u<<11u)|0x1FFu;
        const auto clip=[&]{return unit ? vu.observe_data(0x4320u) : vu.observe(18);};
        instruction(vu,unit,0,0x22ABCDEFu);instruction(vu,unit,1,addiu(1,0,7),clip_upper);
        instruction(vu,unit,2,nop,clip_upper);instruction(vu,unit,3,nop,upper_nop|0x40000000u);instruction(vu,unit,4,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(5);
        require(clip().value==0xABCDEFu,"CLIP cannot overwrite a preceding FCSET before its S stage");
        vu.advance_micro(6);require(clip().value==0xF37BC9u && integer(vu,unit,1).value==7,
            "CLIP uses absolute w, strict boundaries and six-bit history alongside the independent lower integer write");
        vu.advance_micro(7);require(clip().value==0xDEF249u,"successive CLIP operations forward the complete pending history");
        vu.advance_micro(9);require(!vu.micro_running(unit),"clipping flag writes drain through E/Ready");
        vu.reset();seed_vector(vu,unit,1,{0x3F800000u,0xC0000000u,0,0});seed_vector(vu,unit,2,{0,0,0,0x3F800000u});
        const ObservedQuadword uncertain{{0xC00000003F800000ull,0xFFFFu},{0,0}};
        if(unit){vu.write_vector(31,uncertain);vu.write(2,0x401u);vu.vector_store_increment(31,2,15);}else vu.write_vector(1,uncertain);
        instruction(vu,unit,0,nop,clip_upper);instruction(vu,unit,1,nop,upper_nop|0x40000000u);instruction(vu,unit,2,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(7);
        require(clip().value==8u && clip().unknown==1u,"only an unproved boundary comparison remains unknown");
        vu.reset();seed_vector(vu,unit,1,{0x80000000u,1u,0x80000001u,0});seed_vector(vu,unit,2,{0,0,0,1u});
        instruction(vu,unit,0,nop,clip_upper);instruction(vu,unit,1,0x22123456u,clip_upper);
        instruction(vu,unit,2,nop,upper_nop|0x40000000u);instruction(vu,unit,3,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(5);require(clip().value==0,"signed zeros and denormals do not clip against zero");
        vu.advance_micro(6);require(clip().value==0x123456u && !clip().unknown,"a paired FCSET overrides the upper clipping flag");
    }
    for(unsigned unit=0;unit<2;++unit)for(unsigned mask:{0u,1u,2u,9u}) {
        VuControl vu;seed_vector(vu,unit,2,{0,0,0,0x3F800000u});
        const ObservedQuadword uncertain{{0xC00000003F800000ull,0xFFFFu},{0,0}};
        if(unit){vu.write_vector(31,uncertain);vu.write(2,0x401u);vu.vector_store_increment(31,2,15);}else vu.write_vector(1,uncertain);
        const auto clip_upper=(14u<<21u)|(2u<<16u)|(1u<<11u)|0x1FFu;
        instruction(vu,unit,0,nop,clip_upper);for(unsigned n=1;n<4;++n)instruction(vu,unit,n,nop);
        instruction(vu,unit,4,0x24000000u|mask);
        instruction(vu,unit,5,mask==9u ? addiu(2,1,5) : nop);
        instruction(vu,unit,6,nop,upper_nop|0x40000000u);instruction(vu,unit,7,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(12);const auto predicate=integer(vu,unit,1);
        require(predicate.value==(mask==9u ? 1u : 0u) && predicate.unknown==(mask==1u ? 1u : 0u),
            "FCAND ignores unselected uncertainty and lets an established one prove its predicate");
        if(mask==9u)require(integer(vu,unit,2).value==6 && !integer(vu,unit,2).unknown,
            "a flag-check predicate uses its documented one-cycle bypass");
    }
    for(unsigned unit=0;unit<2;++unit)for(unsigned mask:{0xFFFFFFu,0xFFFFFEu,0xFFFFF7u,0xFFFFFDu}) {
        VuControl vu;seed_vector(vu,unit,2,{0,0,0,0x3F800000u});
        const ObservedQuadword uncertain{{0xC00000003F800000ull,0xFFFFu},{0,0}};
        if(unit){vu.write_vector(31,uncertain);vu.write(2,0x401u);vu.vector_store_increment(31,2,15);}else vu.write_vector(1,uncertain);
        const auto clip_upper=(14u<<21u)|(2u<<16u)|(1u<<11u)|0x1FFu;
        instruction(vu,unit,0,nop,clip_upper);for(unsigned n=1;n<4;++n)instruction(vu,unit,n,nop);
        instruction(vu,unit,4,0x26000000u|mask);
        instruction(vu,unit,5,mask==0xFFFFF7u ? addiu(2,1,5) : nop);
        instruction(vu,unit,6,nop,upper_nop|0x40000000u);instruction(vu,unit,7,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(12);const auto predicate=integer(vu,unit,1);
        require(predicate.value==((mask==0xFFFFFFu || mask==0xFFFFF7u) ? 1u : 0u) && predicate.unknown==(mask==0xFFFFFEu ? 1u : 0u),
            "FCOR proves all-one, unresolved and known-zero predicates without inventing clipping bits");
        if(mask==0xFFFFF7u)require(integer(vu,unit,2).value==6 && !integer(vu,unit,2).unknown,
            "a flag-check predicate uses its documented one-cycle bypass");
    }
    for(unsigned unit=0;unit<2;++unit)for(const bool direct:{false,true}) {
        VuControl vu;seed_integer(vu,unit,1,{0xFEDCu,1});seed_integer(vu,unit,2,{direct ? 3u : 4u,0});
        const auto base=(unit ? 0x1100C000u : 0x11004000u)+48u;
        for(unsigned lane=0;lane<4;++lane)vu.write_memory(base+lane*4u,4,{0xFFFFFFFFu,0});
        const auto store=(direct ? 0x800003FFu : (5u<<25u)|0x7FFu)|(10u<<21u)|(1u<<16u)|(2u<<11u);
        instruction(vu,unit,0,store);instruction(vu,unit,1,nop,upper_nop|0x40000000u);instruction(vu,unit,2,nop);
        vu.start_micro(unit,0,0);vu.advance_micro(4);
        require(vu.read_memory(base,4).value==0xFFFFFFFFu,"ISW/ISWR cannot publish before the S stage");
        vu.advance_micro(5);
        for(unsigned lane=0;lane<4;++lane){const auto word=vu.read_memory(base+lane*4u,4);const bool selected=lane==0 || lane==2;
            require(word.value==(selected ? 0xFEDCu : 0xFFFFFFFFu) && word.unknown==(selected ? 1u : 0u),
                "integer stores sign-extend offsets, zero-extend VI data and retain unselected lanes and source uncertainty");}
        vu.advance_micro(7);require(!vu.micro_running(unit),"integer stores drain before Ready");
        vu.reset();instruction(vu,unit,0,nop,upper_nop|0x40000000u);instruction(vu,unit,1,store);
        vu.start_micro(unit,0,0);rejects([&]{vu.advance_micro(2);},"store in the E delay slot");
    }
    VuControl cold;cold.start_micro(1,0,0);rejects([&]{cold.advance_micro(1);},"missing VU1 AOT");
    std::cout<<"Clocked VU integer LIW, bypass/S visibility, BAL delay, E drain, reset and refusals passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
