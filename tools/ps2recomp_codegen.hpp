#pragma once
#include "ps2recomp/code_generator.h"
#include "ps2recomp/types.h"
#include <filesystem>

namespace ps2native {
// Coverage of the most recently generated offline module, not runtime state.
const std::vector<std::pair<std::uint32_t,std::uint32_t>>& sdk_native_missing_translations();
std::size_t sdk_native_instruction_count();
void reset_sdk_native_codegen(std::uint32_t execution_alias=0);
void write_sdk_native_sources(const std::filesystem::path& dispatcher);
// Offline code generation only: generated calls use the canonical console state.
std::string sdk_native_function(ps2recomp::CodeGenerator&, const ps2recomp::Function&,
    const std::vector<ps2recomp::Instruction>&, const std::string& standalone);
std::string sdk_native_registration(ps2recomp::CodeGenerator&, const std::vector<ps2recomp::Function>&,
    const std::unordered_map<std::uint32_t,std::vector<ps2recomp::Instruction>>&,
    const std::unordered_map<std::uint32_t,std::vector<std::uint32_t>>& resume_entries,
    const std::string& input_path, const std::string& standalone);
}
