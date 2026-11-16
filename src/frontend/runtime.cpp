#include "ps2native/frontend/runtime.hpp"

#include <array>

namespace ps2native::frontend {
namespace {
constexpr std::array kCapabilities{
    CapabilityStatus{Capability::FirmwareBrowser, "PS2 Browser", false,
                     "In progress: the engine starts games with its kernel HLE; booting the original OSDSYS is not implemented yet."},
    CapabilityStatus{Capability::DiscBoot, "Game boot", true,
                     "Play recompiles the game with the native engine and runs it; code met later is translated during play and kept."},
    CapabilityStatus{Capability::Controller, "Controller input", true,
                     "Keyboard, mouse and game controllers; F1 shows the keys."},
    CapabilityStatus{Capability::Audio, "Audio", true, "SPU2 emulated by the engine, stretched instead of chopped below full speed."},
    CapabilityStatus{Capability::MemoryCards, "Memory cards", true, "One console memory card shared by every game (slot 1)."},
    CapabilityStatus{Capability::GsFaithful, "GS renderer", true,
                     "The engine's GS on the GPU (WebGPU on Direct3D 12 or Vulkan); equivalence is still being validated."},
    CapabilityStatus{Capability::GsNative, "Native renderer", false,
                     "Not available yet: higher resolutions exist in the engine but are not validated as equivalent."},
};
} // namespace

std::span<const CapabilityStatus> runtime_capabilities() noexcept { return kCapabilities; }
const CapabilityStatus& capability_status(Capability capability) noexcept {
    for (const auto& status : kCapabilities)
        if (status.capability == capability) return status;
    return kCapabilities.front();
}
} // namespace ps2native::frontend
