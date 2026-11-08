#pragma once
#include "ps2native/boot.hpp"
#include "ps2native/frontend/runtime.hpp"
#include "ps2native/hle.hpp"
#include <memory>

namespace ps2native::frontend {
// One console state for original boot and reviewed checkpoint sessions.
// Host threads, presentation, audio queues and input stay in the adapter.
class SessionSupervisor {
public:
    SessionSupervisor();
    SessionSupervisor(const SessionSupervisor&) = delete;
    SessionSupervisor& operator=(const SessionSupervisor&) = delete;
    SessionSupervisor(SessionSupervisor&&) = delete;
    SessionSupervisor& operator=(SessionSupervisor&&) = delete;

    void prepare_original(const SessionRequest& request, const AotModule* module);
    void prepare_hle_elf(const ElfImage& elf, const AotModule* module,
                         const DirectElfEnvironment& environment = {});
    void prepare_firmware(const BiosImage& bios, const AotModule* module,
                          const ConsoleHardware* hardware = nullptr, const NvmImage* nvm = nullptr,
                          const DiagnosticAssumptions* assumptions = nullptr, const Rom1Image* rom1 = nullptr);
    [[nodiscard]] BootReport run(const AotModule* module, const BootOptions& options);
    [[nodiscard]] EEState& ee() noexcept { return ee_; }
    [[nodiscard]] Memory& memory() noexcept { return *memory_; }

private:
    // IOP retains an NVM pointer; the owner cannot move and NVM outlives Memory.
    std::optional<NvmImage> nvm_;
    std::unique_ptr<Memory> memory_;
    EEState ee_;
    std::unique_ptr<HleKernel> hle_;
    bool prepared_{};
};
} // namespace ps2native::frontend
