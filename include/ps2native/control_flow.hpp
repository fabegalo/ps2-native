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
    // file-backed PT_LOAD data. Explicit hints are mandatory without other evidence.
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
// A JR whose every possible target was proven from the code itself:
//   SLTIU bound, BEQ guard, scaled index, table base, LW of the entry, JR.
struct AotRecognizedJumpTable {
    std::uint32_t jump_site{};
    std::uint32_t bound_site{};
    std::uint32_t table_address{};
    std::uint32_t entries{};
    std::string evidence;
};
// A JR whose register holds one constant on every path that reaches it:
//   LUI r ; [ORI r, r | ADDIU r, r] ; straight-line code not writing r ; JR r
// with no entry point between the LUI and the JR. The target is still checked
// against the verified indirect entries at runtime, like every JR.
struct AotConstantJump {
    std::uint32_t jump_site{};
    std::uint32_t lui_site{};
    std::uint32_t target{};
};
// A JR (other than JR $ra) whose targets could not be proven statically.
// It still runs, but only to verified indirect entries (checked at runtime).
struct AotUnresolvedJump {
    std::uint32_t site{};
    std::string reason;
};
// Hybrid discovery only: where static translation stops and execution must
// continue in the interpreter (system instruction, code outside the image).
struct AotUnsupportedBoundary {
    std::uint32_t address{};
    std::string reason;
};
struct AotControlFlow {
    std::map<std::uint32_t, MipsInstruction> instructions;
    std::set<std::uint32_t> leaders;
    std::set<std::uint32_t> function_entries;
    // Addresses a JR/JALR may legitimately reach: function roots, call
    // continuations and proven jump-table targets. Anything else fails closed.
    std::set<std::uint32_t> indirect_entries;
    std::vector<AotResolvedTarget> resolved_indirect_targets;
    std::vector<AotRejectedTarget> rejected_indirect_targets;
    std::vector<AotRecognizedJumpTable> jump_tables;
    std::vector<AotConstantJump> constant_jumps;
    std::vector<AotUnresolvedJump> unresolved_jumps;
    std::vector<AotUnsupportedBoundary> unsupported_boundaries;
};

[[nodiscard]] AotControlFlow analyze_control_flow(
    const ElfImage& image, std::span<const std::uint32_t> additional_function_entries,
    std::span<const AotJumpTable> additional_jump_tables = {},
    bool unsupported_boundaries = false);
} // namespace ps2native
