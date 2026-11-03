#pragma once

#include "ps2native/elf.hpp"
#include "ps2native/mips.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <span>

namespace ps2native {
// Addresses supported for *entry* by the generated native dispatch table.
// The analyzer does not guess that arbitrary words in code/data are functions.
struct AotControlFlow {
    std::map<std::uint32_t, MipsInstruction> instructions;
    std::set<std::uint32_t> leaders;
    std::set<std::uint32_t> function_entries;
};

[[nodiscard]] AotControlFlow analyze_control_flow(
    const ElfImage& image, std::span<const std::uint32_t> additional_function_entries);
} // namespace ps2native
