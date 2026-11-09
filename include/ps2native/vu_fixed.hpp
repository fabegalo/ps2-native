#pragma once
#include "ps2native/vu_float.hpp"
#include "ps2native/ee_fpu.hpp"
#include <bit>

namespace ps2native {
// ITOF0/4/12/15 chop the signed fixed-point operand; VU flags are unchanged.
inline ObservedWord vu_fixed_to_float(ObservedWord input,unsigned fraction) {
    if(fraction!=0u && fraction!=4u && fraction!=12u && fraction!=15u)
        throw DeviceRefusal("ITOF fractional width is not an instruction encoding");
    if(input.unknown)throw DeviceRefusal("ITOF consumes unknown fixed-point bits");
    std::uint32_t flags=0;
    return {ee_fpu_scaled_integer(static_cast<std::int32_t>(input.value),150u-fraction,flags),0};
}
// VU manual 2.1-2.3 and FTOI0/4/12/15, shared by micro and macro mode.
// Pure execution helper: no serialized state and no host FP operation.
inline ObservedWord vu_float_to_fixed(ObservedWord input,unsigned fraction,std::optional<FloatBounds> bounds={}) {
    if(fraction!=0u && fraction!=4u && fraction!=12u && fraction!=15u)
        throw DeviceRefusal("FTOI fractional width is not an instruction encoding");
    if(input.unknown&0xFF800000u)throw DeviceRefusal("FTOI consumes unknown VF sign/exponent bits");
    const auto convert=[fraction](std::uint32_t value) {
        const auto exponent=(value>>23u)&255u;
        const int scaled=static_cast<int>(exponent)-127+static_cast<int>(fraction);
        if(!exponent || scaled<0)return 0u;
        if(scaled>=31)return (value&0x80000000u) ? 0x80000000u : 0x7FFFFFFFu;
        const auto significand=0x800000u|(value&0x7FFFFFu);
        const auto magnitude=scaled>=23 ? significand<<static_cast<unsigned>(scaled-23) :
            significand>>static_cast<unsigned>(23-scaled);
        return (value&0x80000000u) ? 0u-magnitude : magnitude;
    };
    // Fixed sign/exponent makes conversion monotone. The common-bit hull
    // includes all possible integers, preserving lower carry uncertainty.
    auto minimum=input.value&~input.unknown,maximum=input.value|input.unknown;
    if(bounds) {
        if((*bounds)[0]>(*bounds)[1] || (*bounds)[0]<minimum || (*bounds)[1]>maximum ||
           (((*bounds)[0]^input.value)&~input.unknown) || (((*bounds)[1]^input.value)&~input.unknown))
            throw DeviceRefusal("FTOI numeric certificate differs from the operand");
        minimum=(*bounds)[0];maximum=(*bounds)[1];
    }
    const auto low=convert(minimum),high=convert(maximum);
    const auto different=low^high;
    const auto unknown=different ? ~0u>>(32u-std::bit_width(different)) : 0u;
    return {low&~unknown,unknown};
}
}
