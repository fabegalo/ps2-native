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
// Sound overapproximations, never guessed low bits. Uncertainty is confined
// to the fraction of a normal value with a known sign/exponent and flags.
// Unsupported uncertainty/cancellation/exception boundaries return nullopt.
std::optional<VuFloatResult> vu_float_add(ObservedWord a,ObservedWord b,
    std::optional<FloatBounds> a_bounds={},std::optional<FloatBounds> b_bounds={},bool allow_exponent_bounds=false);
// The opt-in exponent hull is for EE domain analysis only. Default VU
// callers retain a proven single exponent for their separate MAC flag model.
std::optional<VuFloatResult> vu_float_product(ObservedWord a,ObservedWord b,unsigned diagnostic_early_digit_carry=0,
    std::optional<FloatBounds> a_bounds={},std::optional<FloatBounds> b_bounds={},bool allow_exponent_bounds=false);
// Enumerate at most eight opaque fraction bits, or at most 65536 encodings
// in an independently certified numeric interval. Never assume SRT monotonicity.
std::optional<VuFloatResult> vu_float_sqrt(ObservedWord a,std::optional<FloatBounds> bounds={});
}
