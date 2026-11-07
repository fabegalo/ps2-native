#include "ps2native/vu_float.hpp"
#include "ps2native/ee_fpu.hpp"
#include "ps2native/profile.hpp"
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
std::optional<VuFloatResult> hull(std::uint32_t low,std::uint32_t high,std::uint32_t flags,bool allow_exponent=false) {
    const auto different=low^high;
    if(different&(allow_exponent ? 0x80000000u : 0xFF800000u))return std::nullopt;
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
std::optional<VuFloatResult> normal_identity(ObservedWord word,std::optional<FloatBounds> bounds) {
    if(!bounds || !word_bounds_match(word,*bounds) || (word.unknown&0x80000000u) ||
       !(word.value&~word.unknown&0x7FFFFFFFu) || (((*bounds)[0]^(*bounds)[1])&0x80000000u) ||
       ((*bounds)[0]&0x7FFFFFFFu)<0x00800000u)return {};
    return VuFloatResult{{word.value&~word.unknown,word.unknown},0,*bounds,true};
}
}
std::optional<VuFloatResult> vu_float_add(ObservedWord a,ObservedWord b,
    std::optional<FloatBounds> a_bounds,std::optional<FloatBounds> b_bounds,bool allow_exponent) {
    if(!a.unknown && !b.unknown) {
        std::uint32_t flags=0;const auto value=ee_fpu_add(a.value,b.value,flags);
        return VuFloatResult{{value,0},flags};
    }
    if(!b.unknown && !(b.value&0x7FFFFFFFu))if(auto value=normal_identity(a,a_bounds))return value;
    if(!a.unknown && !(a.value&0x7FFFFFFFu))if(auto value=normal_identity(b,b_bounds))return value;
    profiling::Scope profile(profiling::Phase::Bounds);
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
    auto result=hull(low,high,common_flags,allow_exponent);
    if(result && std::popcount(result->word.unknown)>8 &&
       std::popcount(a.unknown)+std::popcount(b.unknown)<=8) {
        // Cancellation can turn adjacent inputs into widely separated float
        // encodings. Their intervening low bits are not thereby unknown.
        // Evaluate at most 256 concrete pairs in the existing integer adder;
        // retain common bits as well as the numeric bounds, without selecting
        // an input or allocating another symbolic representation.
        std::uint32_t all=~0u,any=0,minimum=~0u,maximum=0;
        auto ax=a.unknown;
        for(;;) {
            const auto av=lower(a)|ax;
            if(av>=amin && av<=amax) {
                auto bx=b.unknown;
                for(;;) {
                    const auto bv=lower(b)|bx;
                    if(bv>=bmin && bv<=bmax) {
                        std::uint32_t flags=0;const auto value=ee_fpu_add(av,bv,flags);
                        if(flags!=common_flags)return {};
                        all&=value;any|=value;minimum=std::min(minimum,value);maximum=std::max(maximum,value);
                    }
                    if(!bx)break;
                    bx=(bx-1u)&b.unknown;
                }
            }
            if(!ax)break;
            ax=(ax-1u)&a.unknown;
        }
        if(minimum>maximum)return {};
        return VuFloatResult{{all,all^any},common_flags,{minimum,maximum},true};
    }
    return result;
}
std::optional<VuFloatResult> vu_float_product(ObservedWord a,ObservedWord b,unsigned diagnostic,
    std::optional<FloatBounds> a_bounds,std::optional<FloatBounds> b_bounds,bool allow_exponent) {
    if(!a.unknown && !b.unknown) {
        std::uint32_t flags=0;
        if(const auto value=ee_fpu_multiply(a.value,b.value,flags,diagnostic))
            return VuFloatResult{{*value,0},flags};
    }
    // A zero/denormal operand annihilates every magnitude; MAC still needs both signs.
    if(!((a.unknown|b.unknown)&0x80000000u) &&
       ((!((a.value|a.unknown)&0x7F800000u)) || (!((b.value|b.unknown)&0x7F800000u))))
        return VuFloatResult{{(a.value^b.value)&0x80000000u,0},0};
    // A right-hand unit has no partial rows in discarded Booth columns.
    if(!b.unknown && (b.value&0x7FFFFFFFu)==0x3F800000u)if(auto value=normal_identity(a,a_bounds)) {
        const auto sign=b.value&0x80000000u;value->word.value^=sign;
        for(auto& endpoint:value->bounds)endpoint^=sign;
        return value;
    }
    profiling::Scope profile(profiling::Phase::Bounds);
    if(!fraction_only(a) || !fraction_only(b)) {
        const auto certified=[](ObservedWord word,std::optional<FloatBounds> bounds)->std::optional<FloatBounds> {
            if(!word.unknown)return FloatBounds{word.value,word.value};
            const auto value=normal_identity(word,bounds);
            return value ? std::optional<FloatBounds>{value->bounds} : std::nullopt;
        };
        const auto ar=certified(a,a_bounds),br=certified(b,b_bounds);
        if(!ar || !br || (std::uint64_t{(*ar)[1]}-(*ar)[0]+1u)*
            (std::uint64_t{(*br)[1]}-(*br)[0]+1u)>256u)return {};
        std::uint32_t low=~0u,high=0,common_flags=0;bool first=true;
        // Small certified exponent crossings can use the existing circuit
        // directly, in operand order, without assuming rounded monotonicity.
        for(auto av=(*ar)[0];; ++av) {
            for(auto bv=(*br)[0];; ++bv) {
                std::uint32_t flags=0;const auto value=ee_fpu_multiply(av,bv,flags,diagnostic);
                if(!value || (!first && flags!=common_flags))return {};
                first=false;common_flags=flags;low=std::min(low,*value);high=std::max(high,*value);
                if(bv==(*br)[1])break;
            }
            if(av==(*ar)[1])break;
        }
        const auto result=hull(low,high,common_flags,true);
        return result && (!result->word.unknown || (result->word.value&0x7FFFFFFFu)) ? result : std::nullopt;
    }
    const auto ar=input_bounds(a,a_bounds),br=input_bounds(b,b_bounds);
    if(!ar || !br)return {};
    const auto [amin,amax]=*ar;const auto [bmin,bmax]=*br;
    const auto ea=(amin>>23u)&255u,eb=(bmin>>23u)&255u,sign=(amin^bmin)&0x80000000u;
    if(!ea || !eb)return VuFloatResult{{sign,0},0};
    const auto mantissa=[](std::uint32_t v){return (v&0x7FFFFFu)|0x800000u;};
    // Exact products are monotone in positive significands. The Booth circuit
    // need not be: bound its loss over ALL input values instead of assuming
    // monotonic rounded products. At most eight low rows, each <=2^15 weight.
    // B is the Booth-recoded multiplier. If its minimum has sixteen clear
    // low bits, that endpoint has no rows/corrections in discarded columns.
    // Every larger B increases the exact product by at least 2^23, exceeding
    // the entire possible loss (2^18). Thus even an uncertain B cannot make
    // the product smaller than that exact endpoint. This is directional.
    const auto maximum_loss=(bmin&0xFFFFu) ? 8ull*(1ull<<15u) : 0ull;
    const auto low=packed(std::uint64_t{mantissa(amin)}*mantissa(bmin)-maximum_loss,
                          static_cast<int>(ea)+static_cast<int>(eb)-127,sign);
    const auto high=packed(std::uint64_t{mantissa(amax)}*mantissa(bmax),
                           static_cast<int>(ea)+static_cast<int>(eb)-127,sign);
    if(low.flags!=high.flags)return std::nullopt;
    auto result=hull(low.word.value,high.word.value,low.flags,allow_exponent);
    if(!result && !allow_exponent && !low.flags && (!a.unknown || a_bounds) && (!b.unknown || b_bounds)) {
        // Certified domains may cross an exponent boundary while every MAC
        // class remains known. A nonzero common magnitude proves Z=0; the
        // hull already proves the sign and the endpoint flags prove U/O.
        auto expanded=hull(low.word.value,high.word.value,low.flags,true);
        if(expanded && (expanded->word.value&0x7FFFFFFFu))result=expanded;
    }
    return result;
}
static std::optional<VuFloatResult> root_domain(ObservedWord a,std::optional<FloatBounds> bounds,
    std::optional<std::uint32_t> numerator) {
    profiling::Scope profile(profiling::Phase::Bounds);
    if(!fraction_only(a))return std::nullopt;
    const auto domain=input_bounds(a,bounds);
    if(!domain)return {};
    std::uint32_t common_flags=0;bool first=true;
    const auto evaluate=[&](std::uint32_t input)->std::optional<std::uint32_t> {
        const auto root=ee_fpu_sqrt(input);
        if(!root)return {};
        std::uint32_t flags=0;
        const auto value=numerator ? ee_fpu_divide(*numerator,*root,flags) : *root;
        if(!first && flags!=common_flags)return {};
        first=false;common_flags=flags;return value;
    };
    if(std::popcount(a.unknown)>8) {
        if(!bounds || (*domain)[1]-(*domain)[0]>=65536u)return std::nullopt;
        std::uint32_t low=~0u,high=0;
        for(auto value=(*domain)[0];; ++value) {
            const auto root=evaluate(value);
            if(!root)return std::nullopt;
            low=std::min(low,*root);high=std::max(high,*root);
            if(value==(*domain)[1])break;
        }
        return hull(low,high,common_flags,numerator.has_value());
    }
    std::uint32_t low=~0u,high=0;
    auto subset=a.unknown;
    for(;;) {
        const auto value=lower(a)|subset;
        if(value>=(*domain)[0] && value<=(*domain)[1]) {
            const auto root=evaluate(value);
            if(!root)return std::nullopt;
            low=std::min(low,*root);high=std::max(high,*root);
        }
        if(!subset)break;
        subset=(subset-1u)&a.unknown;
    }
    return low<=high ? hull(low,high,common_flags,numerator.has_value()) : std::nullopt;
}
std::optional<VuFloatResult> vu_float_sqrt(ObservedWord a,std::optional<FloatBounds> bounds) {
    return root_domain(a,bounds,{});
}
std::optional<VuFloatResult> vu_float_rsqrt(std::uint32_t numerator,ObservedWord denominator,
    std::optional<FloatBounds> bounds) {
    return root_domain(denominator,bounds,numerator);
}
}
