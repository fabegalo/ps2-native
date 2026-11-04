#include "ps2native/vu_float.hpp"
#include "ps2native/ee_fpu.hpp"
#include <algorithm>
#include <bit>
namespace ps2native {
namespace {
bool fraction_only(ObservedWord a) {return !(a.unknown&0xFF800000u);}
std::uint32_t lower(ObservedWord a) {return a.value&~a.unknown;}
std::uint32_t upper(ObservedWord a) {return a.value|a.unknown;}
std::optional<FloatBounds> input_bounds(ObservedWord word,std::optional<FloatBounds> bounds) {
    if(!bounds)return FloatBounds{lower(word),upper(word)};
    const auto [lo,hi]=*bounds;
    if(lo>hi || ((lo^hi)&0xFF800000u) || lo<lower(word) || hi>upper(word) ||
       ((lo^word.value)&~word.unknown) || ((hi^word.value)&~word.unknown))return {};
    return bounds;
}
std::optional<VuFloatResult> hull(std::uint32_t low,std::uint32_t high,std::uint32_t flags) {
    const auto different=low^high;
    if(different&0xFF800000u)return std::nullopt;
    // Endpoint XOR alone would omit intermediate values. All less significant
    // bits must be uncertain too, even when the endpoints agree there.
    const auto mask=different ? (1u<<std::bit_width(different))-1u : 0u;
    return VuFloatResult{{low&~mask,mask},flags,{low,high},true};
}
std::uint32_t magnitude(std::uint32_t a) {return a&0x7F800000u ? a&0x7FFFFFFFu : 0u;}
VuFloatResult packed(std::uint64_t product,int exponent,std::uint32_t sign) {
    // The conservative lower bound can fall just below 1.0 even though
    // both input significands were normalized. Normalize the bound itself;
    // otherwise borrowing across bit 46 would incorrectly keep its exponent.
    if(product<(1ull<<46u)){product<<=1u;--exponent;}
    const auto shift=product&(1ull<<47u) ? 24u : 23u;
    if(shift==24u)++exponent;
    if(exponent<=0)return {{sign,0},0x4008u};
    if(exponent>255)return {{sign|0x7FFFFFFFu,0},0x8010u};
    return {{sign|(static_cast<std::uint32_t>(exponent)<<23u)|
             (static_cast<std::uint32_t>(product>>shift)&0x7FFFFFu),0},0};
}
}
std::optional<VuFloatResult> vu_float_add(ObservedWord a,ObservedWord b,
    std::optional<FloatBounds> a_bounds,std::optional<FloatBounds> b_bounds) {
    if(!a.unknown && !b.unknown) {
        std::uint32_t flags=0;const auto value=ee_fpu_add(a.value,b.value,flags);
        return VuFloatResult{{value,0},flags};
    }
    if(!fraction_only(a) || !fraction_only(b))return std::nullopt;
    const auto ar=input_bounds(a,a_bounds),br=input_bounds(b,b_bounds);
    if(!ar || !br)return {};
    const auto [amin,amax]=*ar;const auto [bmin,bmax]=*br;
    // Opposite signs with overlapping magnitudes can cancel inside the
    // interval. An exact zero there may have different underflow flags from
    // both endpoints; endpoint agreement alone is insufficient.
    if(((amin^bmin)&0x80000000u) && magnitude(amin)<=magnitude(bmax) &&
       magnitude(bmin)<=magnitude(amax))return std::nullopt;
    std::uint32_t low=~0u,high=0,common_flags=0;bool first=true;
    // With fixed input exponents, alignment and signed integer addition are
    // monotone. After excluding cancellation, the four corners bound every
    // result and exception class, including discarded guard-column bits.
    for(const auto av:{amin,amax})for(const auto bv:{bmin,bmax}) {
        std::uint32_t flags=0;const auto value=ee_fpu_add(av,bv,flags);
        if(!first && flags!=common_flags)return std::nullopt;
        first=false;common_flags=flags;low=std::min(low,value);high=std::max(high,value);
    }
    return hull(low,high,common_flags);
}
std::optional<VuFloatResult> vu_float_product(ObservedWord a,ObservedWord b,bool diagnostic,
    std::optional<FloatBounds> a_bounds,std::optional<FloatBounds> b_bounds) {
    if(!a.unknown && !b.unknown) {
        std::uint32_t flags=0;
        if(const auto value=ee_fpu_multiply(a.value,b.value,flags,diagnostic))
            return VuFloatResult{{*value,0},flags};
    }
    if(!fraction_only(a) || !fraction_only(b))return std::nullopt;
    const auto ar=input_bounds(a,a_bounds),br=input_bounds(b,b_bounds);
    if(!ar || !br)return {};
    const auto [amin,amax]=*ar;const auto [bmin,bmax]=*br;
    const auto ea=(amin>>23u)&255u,eb=(bmin>>23u)&255u,sign=(amin^bmin)&0x80000000u;
    if(!ea || !eb)return VuFloatResult{{sign,0},0};
    const auto mantissa=[](std::uint32_t v){return (v&0x7FFFFFu)|0x800000u;};
    // Exact products are monotone in positive significands. The Booth circuit
    // need not be: bound its loss over ALL input values instead of assuming
    // monotonic rounded products. At most eight low rows, each <=2^15 weight.
    constexpr auto maximum_loss=8ull*(1ull<<15u);
    const auto low=packed(std::uint64_t{mantissa(amin)}*mantissa(bmin)-maximum_loss,
                          static_cast<int>(ea)+static_cast<int>(eb)-127,sign);
    const auto high=packed(std::uint64_t{mantissa(amax)}*mantissa(bmax),
                           static_cast<int>(ea)+static_cast<int>(eb)-127,sign);
    if(low.flags!=high.flags)return std::nullopt;
    return hull(low.word.value,high.word.value,low.flags);
}
std::optional<VuFloatResult> vu_float_sqrt(ObservedWord a) {
    if(!fraction_only(a) || std::popcount(a.unknown)>8)return std::nullopt;
    std::uint32_t low=~0u,high=0;
    auto subset=a.unknown;
    for(;;) {
        const auto root=ee_fpu_sqrt(lower(a)|subset);
        if(!root)return std::nullopt;
        low=std::min(low,*root);high=std::max(high,*root);
        if(!subset)break;
        subset=(subset-1u)&a.unknown;
    }
    return hull(low,high,0);
}
}
