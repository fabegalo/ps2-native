#pragma once

#include "ps2native/elf.hpp"
#include "ps2native/mips.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace ps2native {
// Addresses supported for *entry* by the generated native dispatch table.
// The analyzer does not guess that arbitrary words in code/data are functions.
struct AotJumpTable {
    // Every table entry is a 32-bit absolute executable address in non-code
    // file-backed PT_LOAD data. Explicit hints are mandatory without ELF relocation evidence.
    std::uint32_t address{};
    std::uint32_t entries{};
};
struct AotResolvedTarget {
    std::uint32_t source{};
    std::uint32_t target{};
    std::string evidence;
};
struct AotRejectedTarget {
    std::uint32_t source{};
    std::uint32_t value{};
    std::string reason;
};
struct AotControlFlow {
    std::map<std::uint32_t, MipsInstruction> instructions;
    std::set<std::uint32_t> leaders;
    std::set<std::uint32_t> function_entries;
    std::vector<AotResolvedTarget> resolved_indirect_targets;
    std::vector<AotRejectedTarget> rejected_indirect_targets;
};

[[nodiscard]] AotControlFlow analyze_control_flow(
    const ElfImage& image, std::span<const std::uint32_t> additional_function_entries,
    std::span<const AotJumpTable> additional_jump_tables = {});
} // namespace ps2native
