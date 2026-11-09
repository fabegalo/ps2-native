// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ps2native/ee.hpp"
#include "ps2native/execution.hpp"
#include <bit>

// Use the SDK's original scalar operations without its SIMD/private CPU state.
#define PS2_SCALAR_FLOAT_ONLY
#include <ps2_runtime_macros.h>
#undef PS2_SCALAR_FLOAT_ONLY

namespace ps2native {
// A view into canonical registers, never a second register file or a snapshot.
struct SdkFloatContext {
    struct Register {
        std::uint32_t& bits;
        std::uint32_t* unknown{};
        FpuAffine* relation{};
        operator float() const {
            if(unknown && *unknown)throw GuestStop(StopKind::Undefined,"SDK FPU consumes unknown register bits");
            return std::bit_cast<float>(bits);
        }
        Register& operator=(float value) {
            bits=std::bit_cast<std::uint32_t>(value);
            if(unknown)*unknown=0;
            if(relation)*relation={};
            return *this;
        }
    };
    struct Registers {
        EEState& state;
        Register operator[](unsigned index) const {
            return {state.fpr[index],&state.unknown_fpr_bits[index],&state.fpr_affine[index]};
        }
    } f;
    Register f_acc;
    std::uint32_t& fcr31;
    explicit SdkFloatContext(EEState& state):f{state},f_acc{state.fpu_acc},fcr31(state.fcr31) {}
};
}
