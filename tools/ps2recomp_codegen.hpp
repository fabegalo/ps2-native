#pragma once
#include "ps2recomp/code_generator.h"
#include "ps2recomp/types.h"

namespace ps2native {
// Offline code generation only: generated calls use the canonical console state.
std::string sdk_native_function(ps2recomp::CodeGenerator&, const ps2recomp::Function&,
    const std::vector<ps2recomp::Instruction>&, const std::string& standalone);
std::string sdk_native_registration(ps2recomp::CodeGenerator&, const std::vector<ps2recomp::Function>&,
    const std::unordered_map<std::uint32_t,std::vector<ps2recomp::Instruction>>&,
    const std::unordered_map<std::uint32_t,std::vector<std::uint32_t>>& resume_entries,
    const std::string& input_path, const std::string& standalone);
}
