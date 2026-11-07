#include "ps2native/ee_fpu.hpp"
#include <array>
#include <bit>
#include <stdexcept>

namespace ps2native {
bool ee_fpu_compare(std::uint32_t a,std::uint32_t b,unsigned condition) {
    if(condition&~6u)throw std::invalid_argument("reserved EE floating comparison condition");
    if(!(a&0x7F800000u))a=0;
    if(!(b&0x7F800000u))b=0;
    const auto ordered=[](std::uint32_t bits){return bits&0x80000000u ? ~bits : bits^0x80000000u;};
    return ((condition&2u) && a==b) || ((condition&4u) && ordered(a)<ordered(b));
}
std::optional<std::uint32_t> ee_fpu_sqrt_exact(std::uint32_t value) {
    const auto exponent=(value>>23u)&255u;
    if(!exponent)return 0u; // VU denormals and signed zero are zero.
    if(value&0x80000000u)return std::nullopt;
    const auto mantissa=(value&0x7FFFFFu)|0x800000u;
    const auto radicand=std::uint64_t{mantissa}<<(23u+((exponent+1u)&1u));
    std::uint64_t low=0,high=1ull<<24u;
    while(low+1u<high) {
        const auto middle=(low+high)/2u;
        if(middle*middle<=radicand)low=middle;else high=middle;
    }
    if(low*low!=radicand)return std::nullopt;
    return ((exponent+127u)/2u<<23u)|(static_cast<std::uint32_t>(low)&0x7FFFFFu);
}
namespace {
using Rows=std::array<std::uint32_t,2>;
// A column of three bits produces a sum bit and a carry in the next column.
// Keep the two redundant rows: propagating carries here changes digit selection.
Rows compress(const std::array<std::uint32_t,3>& inputs) {
    const auto a=inputs[0],b=inputs[1],c=inputs[2];
    return {a^b^c,((a&b)|(a&c)|(b&c))<<1u};
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
std::optional<std::uint32_t> ee_fpu_sqrt(std::uint32_t value) {
    // Preserve the cheaper exact-root path and its signed-zero convention.
    if(const auto exact=ee_fpu_sqrt_exact(value))return exact;
    if(value&0x80000000u)return std::nullopt;
    const auto exponent=(value>>23u)&255u;
    const auto mantissa=(value&0x7FFFFFu)|0x800000u;
    // Prabhu/Zyner eq.(2), with x in [1/4,1) and root in [1/2,1).
    // R uses 26 fractional bits; the partial root uses 25. For weight w,
    // (Q+d*w)^2-Q^2=d*w*(2*Q+d*w), giving the effective divisor below.
    Rows remainder{mantissa<<(1u+((exponent+1u)&1u)),0};
    int root=0,selected=1;
    for(unsigned stage=0;stage<25;++stage) {
        const auto step=selected*(1<<(24u-stage));
        const auto divisor=static_cast<std::uint32_t>(2*root+step);
        const auto previous=remainder;
        if(selected)
            remainder=compress({remainder[0],remainder[1]|(selected>0 ? 1u : 0u),
                                selected>0 ? ~divisor : divisor});
        else remainder=compress({remainder[0],remainder[1],0});
        // Fig.1/5's zero-divisor selection observes the old redundant rows;
        // the main compressor still advances. Collapsing or bypassing those
        // rows changes later digit decisions and the final low result bit.
        auto selection=selected ? remainder : previous;
        root+=step;
        for(auto& row:remainder)row<<=1u;
        for(auto& row:selection)row<<=1u;
        selected=digit(selection);
    }
    return ((exponent+127u)/2u<<23u)|((static_cast<std::uint32_t>(root)>>1u)&0x7FFFFFu);
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
std::uint32_t ee_fpu_scaled_integer(std::int64_t units,unsigned exponent,std::uint32_t& fcr31) {
    fcr31&=~0xC000u;
    if(!units)return 0;
    const auto sign=units<0 ? 0x80000000u : 0u;
    const auto magnitude=units<0 ? 0ull-static_cast<std::uint64_t>(units) : static_cast<std::uint64_t>(units);
    const auto top=63u-static_cast<unsigned>(std::countl_zero(magnitude));
    const auto result_exponent=static_cast<int>(exponent)+static_cast<int>(top)-23;
    if(result_exponent<=0){fcr31|=0x4008u;return sign;}
    if(result_exponent>255){fcr31|=0x8010u;return sign|0x7FFFFFFFu;}
    const auto mantissa=top>23u ? magnitude>>(top-23u) : magnitude<<(23u-top);
    return sign|(static_cast<std::uint32_t>(result_exponent)<<23u)|(static_cast<std::uint32_t>(mantissa)&0x7FFFFFu);
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
std::optional<std::uint32_t> ee_fpu_multiply(std::uint32_t a,std::uint32_t b,std::uint32_t& fcr31,unsigned diagnostic_early_digit_carry) {
    if(diagnostic_early_digit_carry>CoarseEarlyDigits)throw std::invalid_argument("unknown FPU carry experiment");
    const auto ea=(a>>23u)&255u,eb=(b>>23u)&255u;
    const auto sign=(a^b)&0x80000000u;
    if(!ea || !eb){fcr31&=~0xC000u;return sign;}
    const auto significand_a=(a&0x7FFFFFu)|0x800000u;
    auto product=static_cast<std::uint64_t>(significand_a)*((b&0x7FFFFFu)|0x800000u);
    const auto negative_digit=0x10000u-(b&0xFFFFu);
    const bool coarse=(significand_a&0x3FFFFu)==0;
    const bool assumed_early=(diagnostic_early_digit_carry==PowerTwoEarlyDigits && significand_a==0x800000u) ||
                             (diagnostic_early_digit_carry==CoarseEarlyDigits && coarse);
    if((coarse && (b&2u)) || (assumed_early && (b&0x2A8u))) {
        // For A divisible by 2^18, every Booth input below column 15 is
        // identical to A=2^23: positive rows have zero low payload and each
        // negative row has the same complement/correction bits. Carry loss
        // therefore depends only on B. The established lowest-negative-digit
        // class transfers to all these A values without commuting operands.
        // Loss is positive and <=8*2^15. The exact product is aligned to
        // 2^18, so subtracting its minimum positive loss gives the SAME packed
        // result for every possible loss in that range, including exponent
        // normalization. It does not claim a particular internal loss value.
        // Earlier negative digits remain an optional external-reference
        // hypothesis (8,126,464 B values checked for A=2^23 and A=3*2^22),
        // not a measured hardware tree. VU's old experiment keeps its A=2^23
        // restriction; the separate scalar experiment selects the coarse class.
        product-=std::uint64_t{1}<<15u;
    } else if(std::has_single_bit(negative_digit) && negative_digit<=0x8000u &&
       ((static_cast<std::uint64_t>(significand_a)*negative_digit)&0x7FFFu)==0) {
        // If the low 16 multiplier bits are -2^k, their Booth expansion
        // contains one negative digit; every later digit starts at >=16.
        // When A*2^k has 15 clear low bits, the complemented row plus its
        // separate sign correction produce a carry into column 15. Cutting
        // both low portions loses that carry. Covers k=15 for every A and
        // e.g. k=0 for A divisible by 2^15. See FIRST-GAME-STATUS.md.
        product-=std::uint64_t{1}<<15u;
    } else if(b&0xFFFFu) {
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
        if((lower>>precision)!=(product>>precision)) {
            // Refine only the formerly ambiguous slow path. Low-column weight
            // is conserved by a compressor; carries into column 15 and cuts
            // can only remove it. Therefore ALL cuts together cannot lose more
            // than the initial weight below column 15, regardless of the tree.
            // Only the first eight radix-4 digits reach those columns. A
            // negative row's complement plus its separate correction carries
            // weight 2^15-residue (also 2^15 when the residue is zero).
            std::uint64_t low_mass=0;
            const auto multiplier=(b&0x7FFFFFu)|0x800000u;
            for(unsigned stage=0;stage<8;++stage) {
                const auto pattern=((std::uint64_t{multiplier}<<1u)>>(2u*stage))&7u;
                const int booth=int(pattern&1u)+int((pattern>>1u)&1u)-2*int(pattern>>2u);
                if(!booth)continue;
                const auto magnitude=booth<0 ? -booth : booth;
                const auto residue=((std::uint64_t{significand_a}*magnitude)<<(2u*stage))&0x7FFFu;
                low_mass+=booth<0 ? 0x8000u-residue : residue;
            }
            if(((product-low_mass)>>precision)!=(product>>precision))return std::nullopt;
        }
    }
    // With at least 16 trailing multiplier zeroes, the first nonzero Booth
    // digit and its sign correction start at column 16 or above. No partial
    // row reaches the discarded columns, so the integer product is exact.
    fcr31&=~0xC000u;
    int exponent=static_cast<int>(ea)+static_cast<int>(eb)-127;
    const auto shift=(product&(1ull<<47u)) ? 24u : 23u;
    if(shift==24)++exponent;
    if(exponent<=0){fcr31|=0x4008u;return sign;}
    if(exponent>255){fcr31|=0x8010u;return sign|0x7FFFFFFFu;}
    return sign|(static_cast<std::uint32_t>(exponent)<<23u)|(static_cast<std::uint32_t>(product>>shift)&0x7FFFFFu);
}
}
