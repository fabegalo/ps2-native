#include "ps2native/vu_float.hpp"
#include "ps2native/vu_control.hpp"
#include "ps2native/ee_fpu.hpp"
#include <cstdio>
#include <stdexcept>
using namespace ps2native;
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
bool contains(ObservedWord word,std::uint32_t value) {return (value&~word.unknown)==word.value;}
void mac_boundaries() {
    const ObservedWord input{0xBF000000u,0xFFFFFFu};
    const FloatBounds domain{0xBF7FFFFDu,0xBF800000u};
    const auto sum=vu_float_add(input,input,domain,domain);
    require(sum && sum->bounded && sum->word.value==0x80000000u && sum->word.unknown==0x7FFFFFFFu &&
        sum->bounds==FloatBounds{0xBFFFFFFDu,0xC0000000u} && !sum->flags && !vu_float_zero(sum->word,sum->bounds),
        "certified sums around minus two prove nonzero even when no magnitude bit is common");
    require(!vu_float_add({0x3F800000u,0},{0xBF000000u,0xFFFFFFu},{},FloatBounds{0xBF7FFFFFu,0xBF800001u}),
        "the strict near-unit cancellation still refuses because actual results cross zero and sign");
    require(vu_float_zero({0x80000000u,0}) && !vu_float_zero({0x3F800000u,0}),"known signed zero and one classify independently");
    bool refused=false;try {(void)vu_float_zero({0xFFFFFFFFu,0x7FFFFFFFu});}catch(const DeviceRefusal&){refused=true;}
    require(refused,"unknown backing bits cannot fabricate zero or nonzero without numeric evidence");
    VuControl vu;vu.write_vector(1,{{input.value,input.unknown},{0,0}});vu.restore_vector_bounds(1,0,domain);
    vu.write_vector(2,{{input.value,input.unknown},{0,0}});vu.restore_vector_bounds(2,0,domain);
    vu.write_vector(3,{});vu.add(3,1,2,8);
    require(vu.observe_vector(3).low.value==sum->word.value && vu.observe_vector(3).low.unknown==sum->word.unknown &&
        vu.read(17)==0x80u && (vu.read(16)&15u)==2u,"macro ADD derives MAC Z from the certified normal range");
    vu.write_vector(4,{{0x3F800000u,0},{0,0}});vu.product(0,1,4,8,4,false,true);
    vu.product(3,2,4,8,4,true,false);
    require(vu.observe_vector_bounds(3,0)==std::optional<FloatBounds>{sum->bounds} && vu.read(17)==0x80u,
        "macro MADD retains an exponent-crossing ACC sum and its nonzero flag");
    constexpr auto lower_nop=0x8000033Cu,upper_nop=0x2FFu;
    const auto add=(8u<<21u)|(2u<<16u)|(1u<<11u)|(3u<<6u)|0x28u;
    vu.write_memory(0x11000000u,8,{lower_nop|(std::uint64_t{add}<<32u),0});
    vu.write_memory(0x11000008u,8,{lower_nop|(std::uint64_t{upper_nop|0x40000000u}<<32u),0});
    vu.write_memory(0x11000010u,8,{lower_nop|(std::uint64_t{upper_nop}<<32u),0});
    vu.start_micro(0,0,0);vu.advance_micro(20);
    require(!vu.micro_running(0) && vu.observe_vector_bounds(3,0)==std::optional<FloatBounds>{sum->bounds} &&
        vu.read(17)==0x80u && (vu.read(16)&15u)==2u,"micro ADD publishes the same numeric certificate and MAC class");
}
void rsqrt_domains() {
    const ObservedWord denominator{0x3F7FFFFEu,1u};
    const FloatBounds domain{0x3F7FFFFEu,0x3F7FFFFFu};
    const auto result=vu_float_rsqrt(0x3F800000u,denominator,domain);
    require(result && result->bounded && !result->flags,"RSQRT enumerates a certified near-unit denominator");
    for(auto input=domain[0];input<=domain[1];++input) {
        const auto root=ee_fpu_sqrt(input);std::uint32_t flags=0;
        const auto exact=ee_fpu_divide(0x3F800000u,*root,flags);
        require(contains(result->word,exact) && exact>=result->bounds[0] && exact<=result->bounds[1] && flags==result->flags,
            "every admissible RSQRT result and flag remains represented");
    }
    const auto zero=vu_float_rsqrt(0,denominator,domain);
    require(zero && !zero->word.value && !zero->word.unknown && !zero->flags,
        "zero divided by every positive root is independently exact zero");
    const auto divide_zero=vu_float_rsqrt(0x3F800000u,{0,3});
    require(divide_zero && divide_zero->word.value==0x7FFFFFFFu && !divide_zero->word.unknown &&
        divide_zero->flags==0x10020u,"all denormal roots preserve the established divide-by-zero value and flags");
    require(!vu_float_rsqrt(0x3F800000u,{0xBF7FFFFEu,1}) &&
        !vu_float_rsqrt(0x3F800000u,{0x3F000000u,0x1FF}) &&
        !vu_float_rsqrt(0x3F800000u,denominator,FloatBounds{0x3F800000u,0x3F800001u}),
        "RSQRT refuses negative, excessive opaque and invalid certified domains");
    VuControl vu;vu.write(22,0x40000000u);
    vu.write_vector(1,{{0xBF800000u,0},{0,0}});vu.add(2,1,0,8);
    vu.write_vector(15,{{denominator.value,denominator.unknown},{0,0}});vu.restore_vector_bounds(15,0,domain);
    vu.issue_q(10,0,15,3,0,2);vu.advance_q(22);
    require(vu.read(22)==0x40000000u && vu.q_ready_cycle()==23u && vu.read(17)==0x80u,
        "uncertain RSQRT preserves old Q and MAC for thirteen cycles");
    vu.advance_q(23);
    require(vu.observe(22).value==result->word.value && vu.observe(22).unknown==result->word.unknown &&
        !vu.q_ready_cycle() && !(vu.read(16)&0x30u) && vu.read(17)==0x80u,
        "RSQRT completion publishes the full uncertainty and proven I/D flags without changing MAC");
    vu.write_vector(15,{{0xBF7FFFFEu,1},{0,0}});const auto before=vu.observe(22);bool refused=false;
    try {vu.issue_q(24,0,15,3,0,2);}catch(const DeviceRefusal&){refused=true;}
    require(refused && !vu.q_ready_cycle() && vu.observe(22).value==before.value && vu.observe(22).unknown==before.unknown,
        "invalid RSQRT refuses before changing Q or scheduling a completion");
}
void ranges() {
    const ObservedWord crossing{0x3F000000u,0xFFFFFFu};
    const FloatBounds crossing_bounds{0x3F7FFFFEu,0x3F800000u};
    for(const auto unit:{0x3F800000u,0xBF800000u}) {
        const auto result=vu_float_product({unit,0},crossing,0,{},crossing_bounds);
        require(result && result->bounded && !result->flags && result->word.unknown &&
            (result->word.value&0x7FFFFFFFu),"small certified exponent crossings preserve a proven MAC class");
        for(auto value=crossing_bounds[0];value<=crossing_bounds[1];++value) {
            unsigned flags=0;const auto exact=ee_fpu_multiply(unit,value,flags);
            require(exact && contains(result->word,*exact) && *exact>=result->bounds[0] && *exact<=result->bounds[1] &&
                flags==result->flags,"enumeration preserves every directed circuit product and flag");
        }
        require(contains(result->word,unit),"the independent exact unit endpoint remains represented");
    }
    require(!vu_float_product({0x3F800000u,0},crossing) &&
        !vu_float_product({0x3F800000u,0},crossing,0,{},FloatBounds{0x3F7FFF00u,0x3F800000u}),
        "opaque and excessive exponent-crossing domains still refuse");
    for(const auto a_sign:{0u,0x80000000u})for(const auto b_sign:{0u,0x80000000u}) {
        const ObservedWord opaque{a_sign,0x7FFFFFFFu},zero{b_sign,0x7FFFFFu};
        for(bool swap:{false,true}) {
            const auto product=vu_float_product(swap ? zero : opaque,swap ? opaque : zero);
            require(product && product->word.value==(a_sign^b_sign) && !product->word.unknown && !product->flags,
                "zero/denormal multiplication has exact XOR sign and clear exception flags for every opaque magnitude");
        }
    }
    require(!vu_float_product({0,0xFFFFFFFFu},{0,0}) &&
        !vu_float_product({0x80000000u,0},{0,0xFFFFFFFFu}),
        "zero multiplication cannot fabricate MAC sign when an operand sign is unknown");
    const auto residual=vu_float_add({0x3F800000u,0},{0xBF7FFFF0u,3},
        {},FloatBounds{0xBF7FFFF1u,0xBF7FFFF2u});
    require(residual && residual->word.value==0x35600000u && residual->word.unknown==0x00100000u &&
        residual->bounds==FloatBounds{0x35600000u,0x35700000u},
        "cancellation of two adjacent inputs retains the known zero fraction columns");
    const auto sparse_root=vu_float_sqrt(residual->word,residual->bounds);
    require(sparse_root && sparse_root->bounded && !sparse_root->flags,
        "two sparse SQRT inputs do not become a million-encoding enumeration");
    for(const auto input:{0x35600000u,0x35700000u}) {
        const auto root=ee_fpu_sqrt(input);
        require(root && contains(sparse_root->word,*root) && *root>=sparse_root->bounds[0] &&
            *root<=sparse_root->bounds[1],"both actual SRT roots remain represented");
    }
    require(!vu_float_sqrt({0x35600000u,0x001FFFFFu},residual->bounds),
        "a genuinely dense wide SQRT domain still refuses instead of choosing sparse bits");
    std::uint32_t flags=0xC000;
    require(!ee_fpu_multiply(0x3F800001,0x3F800003,flags) && flags==0xC000,
            "strict scalar multiply keeps its explicit refusal");
    const auto product=vu_float_product({0x3F800001,0},{0x3F800003,0});
    for(const auto base:{0x00800000u,0x3F800000u,0xAB800000u,0x7F800000u}) {
        const ObservedWord symbolic{base,0x7FFFFFu};
        for(const auto factor:{0x3F800000u,0xBF800000u}) {
            const auto identity=vu_float_product(symbolic,{factor,0});
            require(identity && identity->word.value==(base^(factor&0x80000000u)) &&
                identity->word.unknown==symbolic.unknown && !identity->flags,
                "right-hand unit multiplication preserves every uncertain fraction even at exponent boundaries");
        }
    }
    for(const auto base:{0x20012340u,0xA0012340u,0x3F923450u})
        for(const auto factor:{0x3FC00000u,0x40010000u,0xBF900000u}) {
            const ObservedWord a{base,15},b{factor,0x10000u};
            const auto range=vu_float_product(a,b);
            require(range && range->bounded,"symbolic high multiplier bits with clear low columns permit an exact product bound");
            for(unsigned x=0;x<16;++x)for(const auto y:{0u,0x10000u}) {
                unsigned f=0;const auto concrete=ee_fpu_multiply(base|x,(factor&~0x10000u)|y,f);
                require(concrete && contains(range->word,*concrete) && *concrete>=range->bounds[0] &&
                    *concrete<=range->bounds[1] && f==range->flags,
                    "all represented products of the exact-column fixture stay within both result domains");
            }
        }
    const auto uncertain_multiplier=vu_float_product({0x3F800000u,0},{0xAB800000u,0x7FFFFFu});
    require(uncertain_multiplier && uncertain_multiplier->word.value==0xAB800000u &&
        uncertain_multiplier->word.unknown==0x7FFFFFu && !uncertain_multiplier->flags,
        "a clear-column minimum bounds all larger uncertain multipliers without an artificial exponent crossing");
    for(const auto base:{0x00800000u,0x3F800000u,0xAB800000u,0x7F800000u}) {
        const auto domain=vu_float_product({0x3F800000u,0},{base,0x1FFFFu});
        require(domain && domain->bounded && domain->bounds[0]==base && !domain->flags,
            "the exact minimum bounds a multiplier domain containing discarded-column digits");
        for(unsigned offset=0;offset<=0x1FFFFu;++offset) {
            unsigned f=0;const auto concrete=ee_fpu_multiply(0x3F800000u,base+offset,f);
            if(concrete)require(contains(domain->word,*concrete) && *concrete>=domain->bounds[0] &&
                *concrete<=domain->bounds[1] && f==domain->flags,
                "every certifiable multiplier remains above the clear-column endpoint with identical flags");
        }
    }
    require(product && product->word.unknown && !product->flags &&
            contains(product->word,0x3F800003) && contains(product->word,0x3F800004),
            "ambiguous product retains both possible chopped results");
    require(product->bounded && product->bounds[0]<=product->bounds[1] &&
            contains(product->word,product->bounds[0]) && contains(product->word,product->bounds[1]),
            "numeric endpoints survive independently of their coarser bit-mask hull");
    const ObservedWord broad_a{0x3F000000u,0x7FFFFFu},broad_b{0x37800000u,0x7FFFFFu};
    require(!vu_float_product(broad_a,broad_b),"a deliberately broad bit hull cannot certify the product exponent");
    const auto narrow=vu_float_product(broad_a,broad_b,false,FloatBounds{0x3F490000u,0x3F490003u},FloatBounds{0x37CE8000u,0x37CE8003u});
    require(narrow && narrow->bounded && !narrow->flags,"certified narrow numeric domains avoid artificial exponent uncertainty from a bit hull");
    unsigned certified=0;
    for(unsigned x=0;x<4;++x)for(unsigned y=0;y<4;++y) {
        std::uint32_t local_flags=0;const auto exact=ee_fpu_multiply(0x3F490000u+x,0x37CE8000u+y,local_flags);
        if(exact){++certified;require(contains(narrow->word,*exact) && *exact>=narrow->bounds[0] &&
            *exact<=narrow->bounds[1] && local_flags==narrow->flags,"every certified concrete pair remains within the precise numeric interval and bit hull");}
    }
    require(certified,"the bounded-domain test exercises certified concrete products");
    require(!vu_float_product(broad_a,broad_b,false,FloatBounds{0x3F800000u,0x3F800003u}),
            "supplied bounds outside the raw operand domain cannot invent a different exponent");
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
    const ObservedWord coarse{0x3AE00000u,0xFFFFFu};
    const FloatBounds tight{0x3AE7FFF0u,0x3AE80010u};
    const auto bounded_roots=vu_float_sqrt(coarse,tight);
    require(bounded_roots && bounded_roots->bounded,"a certified narrow interval avoids enumeration of its coarse twenty-bit hull");
    for(auto raw=tight[0];raw<=tight[1];++raw) {
        const auto exact=ee_fpu_sqrt(raw);
        require(exact && contains(bounded_roots->word,*exact) && *exact>=bounded_roots->bounds[0] &&
            *exact<=bounded_roots->bounds[1],"every SRT output belongs to both certified result domains");
    }
    require(!vu_float_sqrt(coarse) && !vu_float_sqrt(coarse,FloatBounds{0x3AF00000u,0x3AF00001u}) &&
            !vu_float_sqrt(coarse,FloatBounds{0x3AE80010u,0x3AE7FFF0u}) &&
            !vu_float_sqrt(coarse,FloatBounds{0x3AE00000u,0x3AE10000u}),
            "opaque, incompatible, reversed and excessive domains remain refused");

}
void pipeline() {
    VuControl vu;
    {
        VuControl zero;
        zero.write_vector(1,{{0x3F000000u,0xFFFFFFu},{0,0}});
        zero.restore_vector_bounds(1,0,{0x3F7FFFFEu,0x3F800000u});
        zero.write_vector(2,{{0x80000000u,0},{0,0}});
        zero.write_vector(3,{});
        zero.product(3,1,2,8,4,false,false);
        require(zero.observe_vector(3).low.value==0x80000000u && !zero.observe_vector(3).low.unknown &&
            zero.read(17)==0x88u && (zero.read(16)&15u)==3u,
            "macro zero product proves signed zero and both MAC Z/S across the input exponent boundary");
        constexpr auto lower_nop=0x8000033Cu,upper_nop=0x2FFu;
        const auto multiply=(8u<<21u)|(2u<<16u)|(1u<<11u)|(3u<<6u)|0x2Au;
        zero.write_memory(0x11000000u,8,{lower_nop|(std::uint64_t{multiply}<<32u),0});
        zero.write_memory(0x11000008u,8,{lower_nop|(std::uint64_t{upper_nop|0x40000000u}<<32u),0});
        zero.write_memory(0x11000010u,8,{lower_nop|(std::uint64_t{upper_nop}<<32u),0});
        zero.start_micro(0,0,0);zero.advance_micro(20);
        require(!zero.micro_running(0) && zero.observe_vector(3).low.value==0x80000000u &&
            !zero.observe_vector(3).low.unknown && zero.read(17)==0x88u,
            "micro VMUL uses the same zero/sign proof and MAC flags");
    }
    {
        // Adjacent encodings of sqrt(1/2) and sqrt(2) straddle a unit product.
        const ObservedWord a{0x3F3504F0u,7u},b{0x3FB504F0u,7u};
        const FloatBounds ab{0x3F3504F3u,0x3F3504F4u},bb{0x3FB504F3u,0x3FB504F4u};
        require(!vu_float_product(a,b),"an opaque fraction hull cannot borrow absent numeric certificates");
        const auto product=vu_float_product(a,b,0,ab,bb);
        require(product && product->bounded && !product->flags && product->word.value==0x3F000000u &&
            product->word.unknown==0xFFFFFFu && product->bounds==FloatBounds{0x3F7FFFFFu,0x3F800001u},
            "certified unit crossing retains both exponents without inventing a rounded product");
        for(const auto sign:{0u,0x80000000u}) {
            const auto identity=vu_float_product(product->word,{sign|0x3F800000u,0},0,product->bounds);
            require(identity && identity->word.value==(product->word.value^sign) && identity->word.unknown==product->word.unknown &&
                identity->bounds==FloatBounds{product->bounds[0]^sign,product->bounds[1]^sign} && !identity->flags,
                "right unit multiplication preserves a certified cross-exponent interval and sign");
            const auto sum=vu_float_add(product->word,{sign,0},product->bounds);
            const auto reverse=vu_float_add({sign,0},product->word,{},product->bounds);
            require(sum && reverse && sum->word.value==product->word.value && sum->word.unknown==product->word.unknown &&
                reverse->bounds==product->bounds && !sum->flags && !reverse->flags,
                "signed zero addition preserves normal numeric certificates in either operand order");
        }
        require(!vu_float_product(product->word,{0x3F800000u,0}) && !vu_float_add(product->word,{0,0}),
            "cross-exponent identities require independent numeric certificates");
        const auto backing=vu_float_product({0xFFFFFFFFu,0x7FFFFFFFu},{0x3F800000u,0},0,FloatBounds{0xBF7FFFFFu,0xBF800001u});
        require(backing && backing->word.value==0x80000000u && backing->word.unknown==0x7FFFFFFFu &&
            !vu_float_zero(backing->word,backing->bounds),
            "unknown backing bits remain masked; independent normal bounds prove nonzero MAC");
        vu.write_vector(1,{{a.value,a.unknown},{0,0}});vu.restore_vector_bounds(1,0,ab);
        vu.write_vector(2,{{b.value,b.unknown},{0,0}});vu.restore_vector_bounds(2,0,bb);
        vu.product(3,1,2,8,4,false,false);
        require(vu.observe_vector(3).low.value==product->word.value && static_cast<std::uint32_t>(vu.observe_vector(3).low.unknown)==product->word.unknown &&
            vu.observe_vector_bounds(3,0)==std::optional<FloatBounds>{product->bounds} && !vu.read(17) && !(vu.read(16)&15u),
            "macro VMUL commits the complete interval and known nonzero positive normal MAC/status flags");
        const auto unsafe=vu_float_product({0x3FC00000u,0},{0x3FAAAAAAu,1},0,{},FloatBounds{0x3FAAAAAAu,0x3FAAAAABu});
        require(!unsafe,"a crossing with zero common magnitude still refuses instead of fabricating MAC Z");
        constexpr auto lower_nop=0x8000033Cu,upper_nop=0x2FFu;
        const auto micro_product=(8u<<21u)|(2u<<16u)|(1u<<11u)|(3u<<6u)|0x2Au;
        vu.write_memory(0x11000000u,8,{lower_nop|(std::uint64_t{micro_product}<<32u),0});
        vu.write_memory(0x11000008u,8,{lower_nop|(std::uint64_t{upper_nop|0x40000000u}<<32u),0});
        vu.write_memory(0x11000010u,8,{lower_nop|(std::uint64_t{upper_nop}<<32u),0});
        vu.start_micro(0,0,0);vu.advance_micro(20);
        require(!vu.micro_running(0) && vu.observe_vector_bounds(3,0)==std::optional<FloatBounds>{product->bounds} &&
            !vu.read(17) && !(vu.read(16)&15u),"retired micro VMUL preserves the same interval and known MAC/status flags");
    }
    for(const auto sign:{0u,0x80000000u}) {
        VuControl clipped;clipped.write_vector(1,{{sign|0x00800000u,0},{0,0}});
        clipped.write_vector(2,{{0x3F000000u,0},{0,0}});
        clipped.product(3,1,2,8,4,false,false);
        require(clipped.observe_vector(3).low.value==sign && clipped.read(17)==(sign?0x888u:0x808u) &&
            clipped.read(16)==(sign?0x1C7u:0x145u),"plain VMUL flushes signed underflow with real MAC and sticky status flags");
        bool blocked=false;try{clipped.product(0,1,2,8,4,false,true);}catch(const DeviceRefusal&){blocked=true;}
        require(blocked,"exceptional ACC writes still require the hidden exception state");
    }
    {
        VuControl clipped;clipped.write_vector(1,{{0x7FFFFFFFu,0},{0,0}});clipped.write_vector(2,{{0x40000000u,0},{0,0}});
        clipped.product(3,1,2,8,4,false,false);
        require(clipped.observe_vector(3).low.value==0x7FFFFFFFu && clipped.read(17)==0x8000u && clipped.read(16)==0x208u,
            "plain VMUL clips overflow and reports overflow independently of zero/sign");
    }
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
    const auto certificate=vu.observe_vector_bounds(3,0);
    require(certificate.has_value(),"uncertain arithmetic retains its certified interval");
    vu.rotate(3,3,1);
    require(vu.observe_vector_bounds(3,3)==certificate,"alias-safe rotation carries the certificate with its lane");
    vu.write_vector(8,uncertain);
    require(!vu.observe_vector_bounds(8,0),"raw replacement cannot inherit an unrelated certificate with the same bit hull");
    vu.write_vector(3,uncertain);
    require(!vu.observe_vector_bounds(3,0),"raw overwrite invalidates even an identical old hull");
    vu.product(3,1,2,8,4,false,false);
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
    require(vu.observe_vector_bounds(7,0).has_value(),"the ACC certificate survives an independent destination write");
    vu.reset();
    require(!vu.observe_vector_bounds(3,0),"hardware reset discards all certificates");
    vu.write_vector(1,{{0x3F800001u,0},{0,0}});
    vu.write_vector(2,{{0x3F800003u,0},{0,0}});
    vu.product(3,1,2,8,4,false,false);
    require(vu.observe_vector_bounds(3,0).has_value(),"macro result begins with a live certificate");
    constexpr auto lower_nop=0x8000033Cu,upper_nop=0x2FFu;
    const auto micro_product=(8u<<21u)|(2u<<16u)|(1u<<11u)|(3u<<6u)|0x2Au;
    vu.write_memory(0x11000000u,8,{lower_nop|(std::uint64_t{micro_product}<<32u),0});
    vu.write_memory(0x11000008u,8,{lower_nop|(std::uint64_t{upper_nop|0x40000000u}<<32u),0});
    vu.write_memory(0x11000010u,8,{lower_nop|(std::uint64_t{upper_nop}<<32u),0});
    vu.start_micro(0,0,0);vu.advance_micro(20);
    require(!vu.micro_running(0) && vu.observe_vector_bounds(3,0).has_value(),
        "retired micro writes replace a macro certificate with their independently calculated range");

}
}
int main()try {ranges();rsqrt_domains();mac_boundaries();pipeline();std::puts("VU floating uncertainty remains explicit and sound");return 0;}
catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
