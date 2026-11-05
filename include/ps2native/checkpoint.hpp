#pragma once
#include "ps2native/boot.hpp"
#include <filesystem>
#include <string>

namespace ps2native {
struct CheckpointContext {
    // SHA-256 of the explicit scenario, including input and experimental
    // configuration. Immutable firmware/NVM/disc bindings are checked too.
    std::string scenario_sha256;
    // Host script state belongs here, not in guest RAM. At most 64 KiB.
    std::string host_metadata;
    bool allow_runtime_change{};
    // Required when a disc is mounted. Pins the original input, rather than
    // treating a modified file as a new valid checkpoint dependency.
    std::string expected_disc_sha256;
};
struct CheckpointInfo {
    std::string schema_sha256, runtime_sha256, bindings_sha256;
    std::string scenario_sha256, host_metadata, payload_sha256;
    std::uint64_t retired{}, cycles{}, payload_bytes{};
    std::uint32_t pc{};
};
// Save only at a successful budget/stop boundary returned by run_ee_boot.
// Refused instructions can have partial device effects and are not save points.
void save_checkpoint(const std::filesystem::path& path, const EEState& state,
                     const Memory& memory, const BootReport& boundary,
                     const CheckpointContext& context);
// The caller first binds the same verified BIOS/profile/NVM/disc. Parsing,
// checksums and identities are validated before replacing its runtime state.
[[nodiscard]] CheckpointInfo load_checkpoint(const std::filesystem::path& path,
    EEState& state, Memory& memory, const CheckpointContext& context);
[[nodiscard]] CheckpointInfo inspect_checkpoint(const std::filesystem::path& path);
[[nodiscard]] std::string checkpoint_state_sha256(const EEState& state,const Memory& memory);
// Explicit external-reference scalar carry experiment, never an automatic load
// policy. Permitted only at a bound boundary with no uncertain numeric history:
// the experiment selects within strict product bounds, preserving that prefix.
// Record a new scenario identity before saving the experimental continuation.
void refine_checkpoint_scalar_carry(const EEState& state,Memory& memory);
// Explicit additive diagnostic precision experiment at a safe boundary.
// Accepted integral calculations are unchanged; formerly refused fractional
// coordinates/filters use the existing documented hypotheses. Save under a
// new scenario identity. This does not certify physical GS precision.
void refine_checkpoint_texture_precision(const EEState& state,Memory& memory,unsigned modes);
} // namespace ps2native
