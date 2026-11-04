#pragma once
#include <cstdint>
#include <optional>
namespace ps2native {
// Integer circuit model; independent of the host FPU/rounding environment.
// Addition aligns 24-bit significands with one guard bit, then chops.
std::uint32_t ee_fpu_add(std::uint32_t a,std::uint32_t b,std::uint32_t& fcr31);
std::uint32_t ee_fpu_divide(std::uint32_t numerator,std::uint32_t denominator,std::uint32_t& fcr31);
// Condition bits 0/2/4/6: false/equal/less/less-or-equal. Denormals are
// zero and exponent 255 remains finite; host IEEE comparisons are not used.
bool ee_fpu_compare(std::uint32_t a,std::uint32_t b,unsigned condition);
// Only nonnegative inputs with an exactly representable square root.
// Non-square rounding and invalid-input behavior remain explicit refusals.
std::optional<std::uint32_t> ee_fpu_sqrt_exact(std::uint32_t value);
// SRT square root for nonnegative inputs, including inexact roots. Invalid
// negative inputs remain refused until their exception flags are modeled.
std::optional<std::uint32_t> ee_fpu_sqrt(std::uint32_t value);
// Exact sparse or truncation-interval-certified product. Ambiguous low-column
// Booth cases return nullopt without modifying flags.
std::optional<std::uint32_t> ee_fpu_multiply(std::uint32_t a,std::uint32_t b,std::uint32_t& fcr31);
}
