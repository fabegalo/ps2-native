#include "ps2native/ee_fpu.hpp"
#include <array>
#include <bit>

namespace ps2native {
namespace {
using Rows=std::array<std::uint32_t,2>;
// A column of three bits produces a sum bit and a carry in the next column.
// Keep the two redundant rows: propagating carries here changes digit selection.
Rows compress(const std::array<std::uint32_t,3>& inputs) {
    Rows output{};
    for(unsigned column=0;column<32;++column) {
        unsigned count=0;
        for(const auto row:inputs)count+=(row>>column)&1u;
        output[0]|=(count&1u)<<column;
        if(column<31)output[1]|=(count>>1u)<<(column+1u);
    }
    return output;
}
// Prabhu/Zyner, ARITH 1995, pp.156-157, Fig.4 and Table III.
// Four high bits add without lower carry propagation; the fifth bits detect
// possible carry for the all-zero case. No IEEE final-remainder correction.
int digit(const Rows& remainder) {
    const auto high=((remainder[0]>>25u)+(remainder[1]>>25u))&15u;
    if(high&8u)return high==15u ? 0 : -1;
    return high || ((remainder[0]|remainder[1])&(1u<<24u)) ? 1 : 0;
}
}
std::uint32_t ee_fpu_add(std::uint32_t a,std::uint32_t b,std::uint32_t& fcr31) {
    fcr31&=~0xC000u; // O/U causes are replaced; I/D and sticky flags survive.
    const auto ea=(a>>23u)&255u,eb=(b>>23u)&255u;
    if(!ea)a&=0x80000000u;
    if(!eb)b&=0x80000000u;
    if(!ea && !eb)return a&b; // only two negative zeros produce negative zero
    if(!ea)return b;
    if(!eb)return a;
    const auto common=ea>eb ? ea : eb;
    const auto aligned=[&](std::uint32_t value,unsigned exponent) -> std::int64_t {
        const auto distance=common-exponent;
        // One retained guard column, no sticky-bit injection. Bits shifted
        // below it are lost BEFORE cancellation/normalization on the EE.
        const auto magnitude=distance>=25u ? 0u : (((value&0x7FFFFFu)|0x800000u)<<1u)>>distance;
        return (value&0x80000000u) ? -static_cast<std::int64_t>(magnitude) : magnitude;
    };
    const auto sum=aligned(a,ea)+aligned(b,eb);
    if(!sum)return 0;
    const std::uint32_t sign=sum<0 ? 0x80000000u : 0;
    const auto magnitude=static_cast<std::uint32_t>(sum<0 ? -sum : sum);
    const auto top=31u-std::countl_zero(magnitude);
    const auto exponent=static_cast<int>(common)+static_cast<int>(top)-24;
    if(exponent<=0){fcr31|=0x4008u;return sign;}
    if(exponent>255){fcr31|=0x8010u;return sign|0x7FFFFFFFu;}
    const auto significand=top>23u ? magnitude>>(top-23u) : magnitude<<(23u-top);
    return sign|(static_cast<std::uint32_t>(exponent)<<23u)|(significand&0x7FFFFFu);
}
std::uint32_t ee_fpu_divide(std::uint32_t a,std::uint32_t b,std::uint32_t& fcr31) {
    const auto sign=(a^b)&0x80000000u;
    const auto ea=(a>>23u)&255u,eb=(b>>23u)&255u;
    fcr31&=~0x30000u; // DIV modifies I/D, preserves O/U and sticky history.
    if(!eb) {
        fcr31|=ea ? 0x10020u : 0x20040u;
        return sign|0x7FFFFFFFu;
    }
    if(!ea)return sign; // denormal operands are signed zero on the EE
    const auto divisor=((b&0x7FFFFFu)|0x800000u)<<2u;
    Rows remainder{((a&0x7FFFFFu)|0x800000u)<<2u,0};
    std::int64_t quotient=0;
    int selected=1;
    // R5900 precision model: 25 signed radix-2 digits. Physical test vectors
    // distinguish this from either host nearest or IEEE toward-zero rounding.
    for(unsigned stage=0;stage<25;++stage) {
        quotient+=selected*(std::int64_t{1}<<(24u-stage));
        if(selected) {
            // Shifted rows always have a clear bit 0, so the subtraction carry
            // enters there without a carry-propagate addition.
            remainder=compress({remainder[0],remainder[1]|(selected>0 ? 1u : 0u),
                                selected>0 ? ~divisor : divisor});
        }
        // A zero digit bypasses the compressor, preserving row representation.
        for(auto& row:remainder)row<<=1u;
        selected=digit(remainder);
    }
    int exponent=static_cast<int>(ea)-static_cast<int>(eb)+126;
    if(quotient>=(1<<24)){quotient>>=1;++exponent;}
    if(exponent<=0)return sign;
    if(exponent>255)return sign|0x7FFFFFFFu;
    return sign|(static_cast<std::uint32_t>(exponent)<<23u)|(static_cast<std::uint32_t>(quotient)&0x7FFFFFu);
}
std::optional<std::uint32_t> ee_fpu_multiply(std::uint32_t a,std::uint32_t b,std::uint32_t& fcr31) {
    const auto ea=(a>>23u)&255u,eb=(b>>23u)&255u;
    const auto sign=(a^b)&0x80000000u;
    if(!ea || !eb){fcr31&=~0xC000u;return sign;}
    const auto product=static_cast<std::uint64_t>((a&0x7FFFFFu)|0x800000u)*((b&0x7FFFFFu)|0x800000u);
    if(b&0x3FFFFu) {
        // Certify a result without guessing the R5900's low-column tree.
        // A 24-bit radix-4 Booth array has 13 partial rows and at most 13
        // separate sign corrections. Reducing 26 rows to two needs at most
        // 24 three-to-two compressors. Dropping columns 0..14 on every input,
        // both outputs of every compressor, and both final rows loses less
        // than (26 + 2*24 + 2)*2^15. This deliberately includes redundant cuts.
        // Every cut subtracts a nonnegative residue; no host rounding is used.
        // See FIRST-GAME-STATUS.md for the hardware observations/limitations.
        constexpr std::uint64_t maximum_loss=76ull*(1ull<<15u);
        const auto lower=product-maximum_loss;
        // Compare BEFORE normalization, including bit 47. If all possible
        // retained products give the same chopped 24-bit result, the tree
        // cannot affect this instruction. Boundary cases remain explicit.
        const auto precision=(product&(1ull<<47u)) ? 24u : 23u;
        if((lower>>precision)!=(product>>precision))return std::nullopt;
    }
    fcr31&=~0xC000u;
    int exponent=static_cast<int>(ea)+static_cast<int>(eb)-127;
    const auto shift=(product&(1ull<<47u)) ? 24u : 23u;
    if(shift==24)++exponent;
    if(exponent<=0){fcr31|=0x4008u;return sign;}
    if(exponent>255){fcr31|=0x8010u;return sign|0x7FFFFFFFu;}
    return sign|(static_cast<std::uint32_t>(exponent)<<23u)|(static_cast<std::uint32_t>(product>>shift)&0x7FFFFFu);
}
}
