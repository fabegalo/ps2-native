#pragma once
#include "ps2native/aot_runtime.hpp"
#include "ps2native/bios.hpp"
#include "ps2native/ee.hpp"
#include "ps2native/memory.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ps2native {
enum class TraceLevel : std::uint8_t {
    Events, // COP0/TLB/CACHE events, engine changes and the stop (plus the instruction tail)
    Full    // additionally every instruction and every data bus access
};
struct BootOptions {
    std::uint64_t max_instructions{1'000'000};
    TraceLevel trace{TraceLevel::Events};
    std::size_t tail_instructions{32};
    // Optional address at which to stop (diagnostics/tests); the BIOS never "returns".
    std::optional<std::uint32_t> stop_at;
};
enum class BootOutcome : std::uint8_t {
    BlockPoint,      // execution reached behavior that is not modeled (or a guest fault)
    BudgetExhausted, // still running when max_instructions retired
    StopAddress      // reached BootOptions::stop_at
};
struct BootReport {
    BootOutcome outcome{BootOutcome::BlockPoint};
    std::string category;   // e.g. "bus: device not implemented", "not modeled"
    std::string reason;     // full diagnostic
    std::uint32_t pc{};     // instruction at the block point (or next PC otherwise)
    std::optional<std::uint32_t> word;
    bool in_delay_slot{};
    std::uint64_t retired{};
    std::uint64_t aot_instructions{};
    std::uint64_t interpreted_instructions{};
    std::uint64_t engine_switches{};
    std::vector<std::string> trace;
    std::vector<std::string> tail;
};

// Power-on state for ROM0: RAM/scratchpad cleared, ROM0 mapped, TLB undefined,
// EE at the reset vector. A hybrid AOT module must have been generated from
// this exact ROM0 (same SHA-256); a translation of another revision is refused.
void prepare_ee_boot(const BiosImage& bios, const AotModule* aot, EEState& state, Memory& memory);

// Runs the EE from its current state: AOT blocks where the module has a block
// for the exact PC, the interpreter (explicitly traced) everywhere else. Never
// throws for guest behavior: the first fault or unmodeled behavior is the
// reported block point.
[[nodiscard]] BootReport run_ee_boot(EEState& state, Memory& memory, const AotModule* aot,
                                     const BootOptions& options);

[[nodiscard]] const char* boot_outcome_name(BootOutcome outcome) noexcept;
} // namespace ps2native
