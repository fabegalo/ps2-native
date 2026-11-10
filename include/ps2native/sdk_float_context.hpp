// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ps2native/ee.hpp"
#include "ps2native/execution.hpp"
#include <bit>
#include <memory>
#include <vector>

// Use SDK operations through canonical register views, without private CPU state.
#define PS2_SCALAR_FLOAT_ONLY
#include <ps2_runtime_macros.h>
#undef PS2_SCALAR_FLOAT_ONLY

namespace ps2native {
// A view into canonical registers, never a second register file or a snapshot.
struct SdkFloatContext {
    EEState& state;
    struct Word64 {
        std::uint64_t value{},unknown{};
        std::shared_ptr<const std::vector<std::uint64_t>> possible;
        std::array<std::optional<std::array<std::uint32_t,2>>,2> bounds;
        bool operator==(std::uint64_t other) const {
            if((value^other)&~unknown)return false;
            if(!unknown)return true;
            if(possible) {
                bool equal=false,different=false;
                for(auto candidate:*possible){equal|=candidate==other;different|=candidate!=other;}
                if(equal!=different)return equal;
            }
            throw GuestStop(StopKind::Undefined,"SDK conditional move consumes an unknown condition");
        }
        bool operator!=(std::uint64_t other) const { return !(*this==other); }
    };
    Word64 gpr_word(unsigned index) const {
        if(!index)return {};
        const auto& bounds=state.gpr_candidates[index].bounds;
        return {state.gpr[index].low,state.unknown_gpr_bits[index],state.reg64_candidates(index),{bounds[0],bounds[1]}};
    }
    void set_gpr_word(unsigned index,const Word64& word) {
        if(!index)return;
        if(word.possible)state.set_reg64_candidates(index,*word.possible);
        else state.set_reg64_observed(index,word.value,word.unknown);
        state.gpr_candidates[index].bounds[0]=word.bounds[0];
        state.gpr_candidates[index].bounds[1]=word.bounds[1];
    }
    struct Word { std::uint32_t value,unknown; };
    using Words=std::array<Word,4>;
    Words gpr_vec(unsigned index) const {
        if(!index)return {};
        const auto& r=state.gpr[index];
        const auto lo=state.unknown_gpr_bits[index],hi=state.unknown_gpr_high_bits[index];
        return {{{static_cast<std::uint32_t>(r.low),static_cast<std::uint32_t>(lo)},
                 {static_cast<std::uint32_t>(r.low>>32u),static_cast<std::uint32_t>(lo>>32u)},
                 {static_cast<std::uint32_t>(r.high),static_cast<std::uint32_t>(hi)},
                 {static_cast<std::uint32_t>(r.high>>32u),static_cast<std::uint32_t>(hi>>32u)}}};
    }
    void set_gpr_vec(unsigned index,const Words& words) {
        const auto pair=[&](unsigned n,bool mask){return std::uint64_t{mask?words[n].unknown:words[n].value}|
            (std::uint64_t{mask?words[n+1].unknown:words[n+1].value}<<32u);};
        state.set_reg128_observed(index,pair(0,false),pair(2,false),pair(0,true),pair(2,true));
    }
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
    explicit SdkFloatContext(EEState& state):state(state),f{state},f_acc{state.fpu_acc},fcr31(state.fcr31) {}
};
}
