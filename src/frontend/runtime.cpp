#include "ps2native/frontend/runtime.hpp"

#include <array>

namespace ps2native::frontend {
namespace {
constexpr std::array kCapabilities{
    CapabilityStatus{Capability::FirmwareBrowser, "PS2 Browser", false,
                     "PS2Recomp runs games directly with its kernel implementation; the original firmware is not executed."},
    CapabilityStatus{Capability::DiscBoot, "Game boot", true,
                     "Play recompiles the game with PS2Recomp and runs it natively; compatibility depends on the game."},
    CapabilityStatus{Capability::Controller, "Controller input", true,
                     "Keyboard and gamepad through the PS2Recomp runtime."},
    CapabilityStatus{Capability::Audio, "Audio", true, "SPU2 through the PS2Recomp runtime."},
    CapabilityStatus{Capability::MemoryCards, "Memory cards", true, "Memory cards emulated by the PS2Recomp runtime."},
    CapabilityStatus{Capability::GsFaithful, "GS renderer", true,
                     "The PS2Recomp software GS; some games still miss textures or effects."},
    CapabilityStatus{Capability::GsNative, "Native renderer", false,
                     "Not available: games use the PS2Recomp GS."},
};
} // namespace

std::span<const CapabilityStatus> runtime_capabilities() noexcept { return kCapabilities; }
const CapabilityStatus& capability_status(Capability capability) noexcept {
    for (const auto& status : kCapabilities)
        if (status.capability == capability) return status;
    return kCapabilities.front();
}
} // namespace ps2native::frontend
