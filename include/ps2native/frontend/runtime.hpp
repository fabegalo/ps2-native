#pragma once
// What the launcher can do with the execution layer (PS2Recomp, driven by
// ps2n). What is not available is reported with its reason so the UI disables
// it explicitly instead of faking it.
#include <cstdint>
#include <span>
#include <string_view>

namespace ps2native::frontend {
enum class Capability : std::uint8_t {
    FirmwareBrowser, // the original Browser / System Configuration
    DiscBoot,        // recompiling and running a game disc
    Controller,
    Audio,
    MemoryCards,
    GsFaithful,      // reference GS rendering
    GsNative         // enhanced Native rendering (same game logic)
};
struct CapabilityStatus {
    Capability capability;
    std::string_view name;
    bool available;
    std::string_view reason; // why it is unavailable (or what it does when available)
};
[[nodiscard]] std::span<const CapabilityStatus> runtime_capabilities() noexcept;
[[nodiscard]] const CapabilityStatus& capability_status(Capability capability) noexcept;
} // namespace ps2native::frontend
