#pragma once
#include "ps2native/boot.hpp"
#include "ps2native/frontend/runtime.hpp"
#include "ps2native/hle.hpp"
#include <memory>

class PS2Runtime;

namespace ps2native::frontend {
// One console state for original boot and reviewed checkpoint sessions.
// Host threads, presentation, audio queues and input stay in the adapter.
class SessionSupervisor {
public:
    SessionSupervisor();
    ~SessionSupervisor();
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
    [[nodiscard]] PS2Runtime& runtime() noexcept;
    [[nodiscard]] EEState& ee() noexcept;
    [[nodiscard]] Memory& memory() noexcept;

private:
    std::unique_ptr<PS2Runtime> runtime_;
};
} // namespace ps2native::frontend
