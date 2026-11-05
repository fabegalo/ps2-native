#pragma once
#include "ps2native/aot_runtime.hpp"
#include "ps2native/bios.hpp"
#include "ps2native/ee.hpp"
#include "ps2native/executable.hpp"
#include "ps2native/memory.hpp"
#include "ps2native/profile.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ps2native {
class DiagnosticRecorder;
// Direct ELF deliberately leaves the IOP dormant until a RAM payload is
// supplied. Its reset/no-ROM sentinel is not a refused guest instruction.
[[nodiscard]] inline bool iop_execution_refused(const Memory& memory) noexcept {
    const auto& iop=memory.iop();
    const bool dormant=memory.mapping()!=Memory::Mapping::HardwareReset &&
        iop.cycle()==0 && iop.pc()==0xBFC00000u;
    return iop.stop_reason().has_value() && !dormant;
}
enum class TraceLevel : std::uint8_t {
    Events, // COP0/TLB/CACHE events, engine changes and the stop (plus the instruction tail)
    Full    // additionally every instruction and every data bus access
};
struct BootOptions {
    std::uint64_t max_instructions{1'000'000};
    TraceLevel trace{TraceLevel::Events};
    std::size_t tail_instructions{32};
    // Optional bounded host diagnostics, shared by both execution engines.
    DiagnosticRecorder* diagnostics{};
    // Zero retains the original unlimited text trace. Diagnostic CLI runs use
    // a bound; the recorder remains the source of the circular event history.
    std::size_t max_trace_events{};
    // Interactive workers request short budgets frequently. Device histories
    // remain in the machine/checkpoint; copying their entire lifetime into
    // every transient report is optional and has no guest-visible effect.
    bool copy_device_histories{true};
    // Optional address at which to stop (diagnostics/tests); the BIOS never "returns".
    std::optional<std::uint32_t> stop_at;
    // Optional headless presentation hook, called at modeled VBLANK starts.
    // Frame ownership may be copied by the host. No scanout work when absent.
    std::function<void(const Framebuffer&)> on_frame;
    // Previously compiled, verified dynamic regions. Lifetimes of their modules
    // and the Memory used for verification must cover this run.
    std::vector<VerifiedAotOverlay> aot_overlays;
    // Optional host-only profiler; caller owns/reset its fixed-size counters.
    profiling::Profile* profile{};
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
    std::string sio_output; // EE SIO TX bytes since reset (BIOS console output)
    // IOP at the end of the run (run up to the EE time; boot mapping only).
    std::uint64_t iop_instructions{};
    std::uint32_t iop_pc{};
    std::optional<std::string> iop_stop;
    std::vector<std::string> iop_events;
    // Video encoder serial-bus transactions, in order.
    std::vector<std::string> video_encoder_transactions;
    std::vector<std::string> gif_packets;
    std::vector<std::string> trace;
    std::vector<std::string> tail;
    std::uint64_t frames_presented{};
    std::optional<std::string> frame_unavailable;
};

// Power-on state for ROM0: RAM/scratchpad cleared, ROM0 mapped, TLB undefined,
// EE at the reset vector. A hybrid AOT module must have been generated from
// this exact ROM0 (same SHA-256); a translation of another revision is refused.
// `hardware` is the console the ROM0 runs on (nullptr: none, so board-specific
// registers refuse reads); its ROMVER must be the ROM0's, or the boot is refused.
// `nvm` is that console's MECHACON NVM (load_nvm_image for `hardware`;
// nullptr: none, so NVM reads are refused); it must outlive `memory`.
// `assumptions` (nullptr: none) are diagnostic values for unmeasured device
// state; a run that uses them is an experiment, not a boot of a measured console.
void prepare_ee_boot(const BiosImage& bios, const AotModule* aot, EEState& state, Memory& memory,
                     const ConsoleHardware* hardware = nullptr, const NvmImage* nvm = nullptr,
                     const DiagnosticAssumptions* assumptions = nullptr, const Rom1Image* rom1 = nullptr);

// Bare-metal ELF development mode, not an emulation of ExecPS2 or Sony BIOS.
// Zero RAM/BSS/GPRs, kernel mode, interrupts disabled, entry from ELF. SP is
// explicit (16-byte aligned, in RAM, outside loaded segments); GP defaults to 0.
// No syscalls/services are replaced. Uses run_ee_boot and the shared devices.
struct DirectElfEnvironment {
    std::uint32_t stack_pointer{0x01FFF000u};
    std::uint32_t global_pointer{};
};
void prepare_direct_elf(const ElfImage& elf, const AotModule* aot, EEState& state, Memory& memory,
                        const DirectElfEnvironment& environment = {});

// Runs the EE from its current state: AOT blocks where the module has a block
// for the exact PC, the interpreter (explicitly traced) everywhere else. Never
// throws for guest behavior: the first fault or unmodeled behavior is the
// reported block point.
[[nodiscard]] BootReport run_ee_boot(EEState& state, Memory& memory, const AotModule* aot,
                                     const BootOptions& options);

[[nodiscard]] const char* boot_outcome_name(BootOutcome outcome) noexcept;
} // namespace ps2native
