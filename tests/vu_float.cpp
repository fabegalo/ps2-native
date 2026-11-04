#include "ps2native/vu_float.hpp"
#include "ps2native/vu_control.hpp"
#include "ps2native/ee_fpu.hpp"
#include <cstdio>
#include <stdexcept>
using namespace ps2native;
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
bool contains(ObservedWord word,std::uint32_t value) {return (value&~word.unknown)==word.value;}
void ranges() {
    std::uint32_t flags=0xC000;
    require(!ee_fpu_multiply(0x3F800001,0x3F800003,flags) && flags==0xC000,
            "strict scalar multiply keeps its explicit refusal");
    const auto product=vu_float_product({0x3F800001,0},{0x3F800003,0});
    require(product && product->word.unknown && !product->flags &&
            contains(product->word,0x3F800003) && contains(product->word,0x3F800004),
            "ambiguous product retains both possible chopped results");
    const auto absorbed=vu_float_add({0x50000000,0},product->word);
    require(absorbed && absorbed->word.value==0x50000000 && !absorbed->word.unknown && !absorbed->flags,
            "guard-column alignment proves insignificant uncertain product has no effect");
    const auto hull=vu_float_add({0x3F800000,3},{0x34000000,0});
    require(hull && hull->word.unknown==7 && contains(hull->word,0x3F800002),
            "range hull includes intermediate bits that agree at both endpoints");
    require(!vu_float_add({0x00800000,1},{0x80800001,0}),
            "interior cancellation can change underflow flags despite equal endpoints");
    require(!vu_float_product({0x3F800000,0x80000000},{0,0}) &&
            !vu_float_add({0x3F800000,0x00800000},{0,0}),
            "unknown signs/exponents cannot be hidden by zero operands");
    const auto zero=vu_float_product({0x80000000,0x7FFFFF},{0x3F900000,3});
    require(zero && zero->word.value==0x80000000 && !zero->word.unknown && !zero->flags,
            "known signed denormal class flushes all possible fractions to zero");
    const auto range=vu_float_product({0x3F8001F8,7},{0x3F900000,0});
    require(range && !range->flags,"bounded normal product exists");
    for(unsigned bits=0;bits<8;++bits) {
        flags=0;const auto exact=ee_fpu_multiply(0x3F8001F8|bits,0x3F900000,flags);
        require(exact && contains(range->word,*exact) && flags==range->flags,
                "every represented concrete product is contained with the same flags");
    }
    for(const auto a:{0x3F800000u,0xBF800000u,0x00800000u,0x7FFFFFF0u})
        for(const auto b:{0x30000000u,0xB0000000u,0x40000000u}) {
            const auto sum=vu_float_add({a,7},{b,3});
            if(!sum)continue;
            for(unsigned x=0;x<8;++x)for(unsigned y=0;y<4;++y) {
                flags=0;const auto exact=ee_fpu_add((a&~7u)|x,(b&~3u)|y,flags);
                require(contains(sum->word,exact) && flags==sum->flags,
                        "every represented sum preserves result containment and exception flags");
            }
        }
    const auto roots=vu_float_sqrt({0x3F800000,7});
    require(roots && roots->word.unknown && !roots->flags,"uncertain roots remain uncertain");
    for(unsigned value=0;value<8;++value)
        require(contains(roots->word,*ee_fpu_sqrt(0x3F800000|value)),"root enumeration contains every represented input");
    require(!vu_float_sqrt({0x3F800000,0x1FF}) && !vu_float_sqrt({0x3F800000,0x80000000}),
            "wide or sign-unknown root domains remain refused with bounded work");
}
void pipeline() {
    VuControl vu;
    vu.write_vector(1,{{0x3F800001,0},{0,0}});
    vu.write_vector(2,{{0x3F800003,0xFFFFFFFF00000000ull},{0,0}});
    vu.write_vector(3,{{0x1234567812345678ull,0},{0,0}});
    const auto before=vu.observe_vector(3);bool refused=false;
    try{vu.product(3,1,2,12,4,false,false);}catch(const DeviceRefusal&){refused=true;}
    require(refused && vu.observe_vector(3).low.value==before.low.value &&
            !vu.observe_vector(3).low.unknown && !vu.read(16) && !vu.read(17),
            "unknown later lane cannot commit earlier uncertain results or flags");
    vu.product(3,1,2,8,4,false,false);
    const auto uncertain=vu.observe_vector(3);
    require(uncertain.low.unknown && (uncertain.low.value>>32u)==0x12345678 &&
            !vu.read(16) && !vu.read(17),
            "masked VMUL preserves unknown fraction, normal flags and untouched lanes");
    vu.write(22,0x3F800000);
    vu.issue_q(0,0,3,0,0,true);vu.advance_q(6);
    require(vu.read(22)==0x3F800000,"uncertain Q still preserves the previous value before seven cycles");
    vu.advance_q(7);
    require(vu.observe(22).unknown && !vu.q_ready_cycle() && !vu.read(16),
            "completed Q propagates its uncertain fraction with known flags");
    refused=false;try{(void)vu.read(22);}catch(const DeviceRefusal&){refused=true;}
    require(refused,"a consumer requiring known Q cannot fabricate its uncertain fraction");
    vu.write_vector(4,{{0x50000000,0},{0,0}});
    vu.add(3,4,3,8);
    require(static_cast<std::uint32_t>(vu.observe_vector(3).low.value)==0x50000000 &&
            !vu.observe_vector(3).low.unknown,
            "VADD commits known bits only after proving every represented result agrees");
    vu.add(3,4,0,8,5);
    require(static_cast<std::uint32_t>(vu.observe_vector(3).low.value)==0x50000000 && !vu.observe_vector(3).low.unknown,
            "VADDq can prove Q uncertainty insignificant without erasing Q itself");
    vu.product(0,1,2,8,4,false,true);
    vu.write_vector(5,{{0x50000000,0},{0,0}});
    vu.write_vector(6,{{0x3F800000,0},{0,0}});
    vu.write_vector(7,{});
    vu.product(7,5,6,8,4,true,false);
    require(vu.observe_vector(7).low.value==0x50000000 && !vu.observe_vector(7).low.unknown,
            "MADD can prove uncertain ACC insignificant without overwriting ACC");
    vu.product(7,0,6,8,4,true,false);
    require(vu.observe_vector(7).low.unknown,"zero product exposes the still-uncertain persistent ACC");
}
}
int main()try {ranges();pipeline();std::puts("VU floating uncertainty remains explicit and sound");return 0;}
catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
