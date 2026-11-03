#pragma once
#include <cstdint>

namespace ps2native {
class Memory;
struct EEState;

// Implemented by the generated, host-compiled translation unit.
// Stops only at stop_pc; reports missing static targets and budget exhaustion.
std::uint64_t run_recompiled(EEState& state, Memory& memory,
                             std::uint32_t stop_pc, std::uint64_t max_instructions);
}  // namespace ps2native
