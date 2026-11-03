#pragma once
#include "ps2native/ee_semantics.hpp"

#include <cstddef>
#include <cstdint>

namespace ps2native {
class Memory;
struct EEState;

// One host-compiled basic block of a generated translation unit.
struct AotBlock {
    std::uint32_t address;
    void (*execute)(ExecutionContext&);
};
// Everything a generated translation unit exports. Blocks and indirect entries
// are sorted by address. source_sha256 identifies the exact input image (ELF or
// BIOS ROM0) so a translation is never run against a different revision.
struct AotModule {
    const AotBlock* blocks;
    std::size_t block_count;
    const std::uint32_t* indirect_entries;
    std::size_t indirect_entry_count;
    const char* source_kind;
    const char* source_sha256;
};
[[nodiscard]] const AotBlock* find_aot_block(const AotModule& module, std::uint32_t pc) noexcept;
[[nodiscard]] bool is_verified_indirect_entry(const AotModule& module, std::uint32_t pc) noexcept;

// Per-instruction guard emitted at the head of every AOT instruction: returns
// false to leave the block (PC elsewhere, budget, stop address) and throws when
// the guest code word in memory no longer matches the translated one.
bool aot_enter(ExecutionContext& ctx, std::uint32_t pc, std::uint32_t word, bool has_delay_slot);

// Pure AOT dispatcher (no interpreter): JR/JALR may only reach verified entries;
// stops only at stop_pc; missing static targets and budget exhaustion throw.
std::uint64_t run_aot_module(const AotModule& module, EEState& state, Memory& memory,
                             std::uint32_t stop_pc, std::uint64_t max_instructions);

// Implemented by each generated, host-compiled translation unit.
const AotModule& aot_module();
std::uint64_t run_recompiled(EEState& state, Memory& memory,
                             std::uint32_t stop_pc, std::uint64_t max_instructions);
}  // namespace ps2native
