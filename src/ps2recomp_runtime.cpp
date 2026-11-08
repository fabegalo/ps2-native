#include "ps2_runtime.h"
#include "ps2native/hle.hpp"
#include <stdexcept>

#if !defined(PS2N_SDK_NATIVE_BACKEND) || !defined(PS2N_NATIVE_ONLY)
#error The integrated PS2Runtime requires the exclusive native AOT backend
#endif

struct PS2Runtime::NativeState {
    // IOP retains an NVM pointer; NVM outlives Memory and the owner cannot move.
    std::optional<ps2native::NvmImage> nvm;
    std::unique_ptr<ps2native::Memory> memory = std::make_unique<ps2native::Memory>();
    ps2native::EEState ee;
    std::unique_ptr<ps2native::HleKernel> hle;
    bool prepared{};
};

PS2Runtime::PS2Runtime() : m_native(std::make_unique<NativeState>()) {}
PS2Runtime::~PS2Runtime() = default;

ps2native::EEState& PS2Runtime::cpu() noexcept { return m_native->ee; }
ps2native::Memory& PS2Runtime::memory() noexcept { return *m_native->memory; }
void PS2Runtime::invalidate_preparation() noexcept { m_native->prepared = false; }

void PS2Runtime::prepare_firmware(const ps2native::BiosImage& bios, const ps2native::AotModule* module,
                                 const ps2native::ConsoleHardware* hardware, const ps2native::NvmImage* nvm,
                                 const ps2native::DiagnosticAssumptions* assumptions, const ps2native::Rom1Image* rom1) {
    invalidate_preparation();
    m_native->hle.reset();
    m_native->nvm = nvm ? std::optional<ps2native::NvmImage>{*nvm} : std::nullopt;
    ps2native::prepare_ee_boot(bios,module,cpu(),memory(),hardware,
        m_native->nvm ? &*m_native->nvm : nullptr,assumptions,rom1);
    m_native->prepared = true;
}

void PS2Runtime::prepare_direct_elf(const ps2native::ElfImage& elf, const ps2native::AotModule* module,
                                   const ps2native::DirectElfEnvironment& environment) {
    invalidate_preparation();
    m_native->hle.reset();
    ps2native::prepare_direct_elf(elf,module,cpu(),memory(),environment);
    m_native->nvm.reset();
    m_native->prepared = true;
}

void PS2Runtime::prepare_hle_elf(const ps2native::ElfImage& elf, const ps2native::AotModule* module,
                                const ps2native::DirectElfEnvironment& environment) {
    prepare_direct_elf(elf,module,environment);
    invalidate_preparation();
    m_native->hle = std::make_unique<ps2native::HleKernel>(cpu(),memory());
    m_native->prepared = true;
}

ps2native::BootReport PS2Runtime::run(const ps2native::AotModule* module, const ps2native::BootOptions& options) {
    if (!m_native->prepared) throw std::runtime_error("Console session is not prepared");
    return ps2native::run_ee_boot(cpu(),memory(),module,options);
}
