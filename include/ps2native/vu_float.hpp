#pragma once
#include "ps2native/device_timing.hpp"
#include <optional>
namespace ps2native {
struct VuFloatResult { ObservedWord word; std::uint32_t flags{}; };
// Sound overapproximations, never guessed low bits. Uncertainty is confined
// to the fraction of a normal value with a known sign/exponent and flags.
// Unsupported uncertainty/cancellation/exception boundaries return nullopt.
std::optional<VuFloatResult> vu_float_add(ObservedWord a,ObservedWord b);
std::optional<VuFloatResult> vu_float_product(ObservedWord a,ObservedWord b,bool diagnostic_early_digit_carry=false);
// Enumerate at most eight uncertain fraction bits; larger/invalid domains
// remain refused. Never assume monotonic rounding of the SRT circuit.
std::optional<VuFloatResult> vu_float_sqrt(ObservedWord a);
}
