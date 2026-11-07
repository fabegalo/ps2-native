#pragma once
#include "ps2native/device_timing.hpp"
#include <optional>
#include <array>
namespace ps2native {
using FloatBounds=std::array<std::uint32_t,2>; // raw endpoints; inputs have one sign/exponent class
inline bool word_bounds_match(ObservedWord word,FloatBounds bounds) noexcept {
    return bounds[0]<=bounds[1] && bounds[0]>=(word.value&~word.unknown) && bounds[1]<=(word.value|word.unknown) &&
        !((bounds[0]^word.value)&~word.unknown) && !((bounds[1]^word.value)&~word.unknown);
}
struct VuFloatResult {
    ObservedWord word;std::uint32_t flags{};
    FloatBounds bounds{};bool bounded{};
};
// Sound overapproximations, never guessed low bits. Numeric certificates
// restrict normal word hulls and prove flags even across exponents.
// Unsupported uncertainty/cancellation/exception boundaries return nullopt.
std::optional<VuFloatResult> vu_float_add(ObservedWord a,ObservedWord b,
    std::optional<FloatBounds> a_bounds={},std::optional<FloatBounds> b_bounds={},bool allow_exponent_bounds=false);
// The opt-in exponent hull is for EE domain analysis. Default VU callers
// admit certified exponent crossings only when MAC Z/S and exceptions are
// proven from known bits or independently certified numeric bounds.
std::optional<VuFloatResult> vu_float_product(ObservedWord a,ObservedWord b,unsigned diagnostic_early_digit_carry=0,
    std::optional<FloatBounds> a_bounds={},std::optional<FloatBounds> b_bounds={},bool allow_exponent_bounds=false);
// Enumerate at most eight opaque fraction bits, or at most 65536 encodings
// in an independently certified numeric interval. Never assume SRT monotonicity.
std::optional<VuFloatResult> vu_float_sqrt(ObservedWord a,std::optional<FloatBounds> bounds={});
    // RUNTIME_HELPERS_BEGIN
inline bool vu_float_zero(ObservedWord word,std::optional<FloatBounds> bounds={}) {
    if(word.value&~word.unknown&0x7FFFFFFFu)return false;
    if(!(word.unknown&0x7FFFFFFFu))return true;
    if(bounds && word_bounds_match(word,*bounds) && !(((*bounds)[0]^(*bounds)[1])&0x80000000u) &&
        ((*bounds)[0]&0x7FFFFFFFu)>=0x00800000u)return false;
    throw DeviceRefusal("VU zero flag is not proven by the word or numeric bounds");
}
// Enumerate the same bounded domain through SQRT and DIV, retaining Q flags.
std::optional<VuFloatResult> vu_float_rsqrt(std::uint32_t numerator,ObservedWord denominator,
    std::optional<FloatBounds> bounds={});
    // RUNTIME_HELPERS_END
}
