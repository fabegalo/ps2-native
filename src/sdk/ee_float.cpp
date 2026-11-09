// SPDX-License-Identifier: GPL-3.0-only
// Canonical-register adapter; arithmetic is compiled from the pinned SDK.
#define PS2_SCALAR_FLOAT_ONLY
#include "ps2_runtime_macros.h"
#include "ps2native/hle.hpp"

namespace ps2native {
std::uint32_t sdk_float_add(std::uint32_t a,std::uint32_t b) {
    return std::bit_cast<std::uint32_t>(ps2_fadd(std::bit_cast<float>(a),std::bit_cast<float>(b)));
}
std::uint32_t sdk_float_sub(std::uint32_t a,std::uint32_t b) {
    return std::bit_cast<std::uint32_t>(ps2_fsub(std::bit_cast<float>(a),std::bit_cast<float>(b)));
}
std::uint32_t sdk_float_mul(std::uint32_t a,std::uint32_t b) {
    return std::bit_cast<std::uint32_t>(ps2_fmul(std::bit_cast<float>(a),std::bit_cast<float>(b)));
}
std::uint32_t sdk_float_div(std::uint32_t a,std::uint32_t b) {
    return std::bit_cast<std::uint32_t>(ps2_fdiv(std::bit_cast<float>(a),std::bit_cast<float>(b)));
}
std::uint32_t sdk_float_sqrt(std::uint32_t a) {
    return std::bit_cast<std::uint32_t>(ps2_fsqrt(std::bit_cast<float>(a)));
}
std::uint32_t sdk_float_rsqrt(std::uint32_t a,std::uint32_t b) {
    return std::bit_cast<std::uint32_t>(ps2_frsqrt(std::bit_cast<float>(a),std::bit_cast<float>(b)));
}
}
