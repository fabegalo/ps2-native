#include "ps2native/frontend/session.hpp"
#include "ps2native/frontend/content.hpp"
#include "ps2native/io.hpp"
#include "ps2native/hle.hpp"
#include "ps2_runtime.h"
#include "sdk/console_interface.hpp"
#include <stdexcept>
#include <chrono>

namespace ps2native::frontend {
SessionSupervisor::SessionSupervisor() : runtime_(std::make_unique<PS2Runtime>()) {}
SessionSupervisor::~SessionSupervisor() = default;
PS2Runtime& SessionSupervisor::runtime() noexcept { return *runtime_; }
EEState& SessionSupervisor::ee() noexcept { return runtime_->cpu(); }
Memory& SessionSupervisor::memory() noexcept { return runtime_->memory(); }

void SessionSupervisor::prepare_firmware(const BiosImage& bios, const AotModule* module,
                                        const ConsoleHardware* hardware, const NvmImage* nvm,
                                        const DiagnosticAssumptions* assumptions, const Rom1Image* rom1) {
    runtime_->prepare_firmware(bios,module,hardware,nvm,assumptions,rom1);
}

namespace {
struct GameInputs {
    BiosImage bios;
    const ConsoleHardware* hardware{};
    std::optional<NvmImage> nvm;
    std::shared_ptr<DiscImage> disc;
    Iop::DiscMedium medium{};
};
GameInputs validate_game(const SessionRequest& request) {
    if (!request.experimental_manifest.empty()) throw std::runtime_error("Original boot refuses an experimental manifest");
    if (request.renderer != RendererMode::GsFaithful) throw std::runtime_error("Native renderer is unavailable for original boot");
    if (request.expected_rom0_sha256.empty()) throw std::runtime_error("Original boot requires the identified ROM0 SHA-256");
    auto bios = load_bios_rom0(read_file(request.rom0), BiosLoadOptions{request.expected_rom0_sha256});
    const auto* hardware = request.console_profile.empty() ? nullptr : find_console_hardware(request.console_profile);
    if (!request.console_profile.empty() && !hardware) throw std::runtime_error("Unknown console profile");
    if(hardware && hardware->romver!=bios.version.romver)throw std::runtime_error("Console profile and BIOS ROMVER differ");
    std::optional<NvmImage> nvm;
    if (!request.nvm.empty()) {
        if (!hardware || request.expected_nvm_sha256.empty()) throw std::runtime_error("NVM requires an identified console profile and SHA-256");
        nvm = load_nvm_image(read_file(request.nvm), *hardware, NvmLoadOptions{request.expected_nvm_sha256});
    }
    if (!request.disc) throw std::runtime_error("No game disc selected");
    const auto probe = probe_disc(*request.disc);
    if (probe.status != ProbeStatus::Valid || !probe.info.contains("platform") || probe.info.at("platform") != "PlayStation 2")
        throw std::runtime_error("Disc refused: " + probe.error);
    if (request.expected_disc_boot.empty() || probe.info.at("boot") != request.expected_disc_boot)
        throw std::runtime_error("Disc BOOT2 differs from the identified library entry; reidentify the disc");
    auto disc_path = *request.disc;
    if (probe.info.contains("cue_tracks")) {
        const auto sheet = parse_cue_sheet(disc_path);
        if (!sheet.data_track()) throw std::runtime_error("CUE has no data track");
        disc_path = sheet.data_track()->file;
    }
    auto disc = std::make_shared<DiscImage>(disc_path);
    (void)disc->boot_path();
    const auto medium = probe.info.find("media");
    return {std::move(bios),hardware,std::move(nvm),std::move(disc),
        medium != probe.info.end() && medium->second == "DVD" ? Iop::DiscMedium::Ps2Dvd : Iop::DiscMedium::Ps2Cd};
}
}
void SessionSupervisor::prepare_original(const SessionRequest& request, const AotModule* module) {
    runtime_->invalidate_preparation();
    auto input=validate_game(request);
    prepare_firmware(input.bios,module,input.hardware,input.nvm ? &*input.nvm : nullptr);
    try {
        memory().mount_disc(std::move(input.disc),input.medium,true);
    } catch (...) {
        runtime_->invalidate_preparation();
        throw;
    }
}

void SessionSupervisor::prepare_game(const SessionRequest& request,const AotModule* module) {
    runtime_->invalidate_preparation();
    auto input=validate_game(request);
    if(!module)throw std::runtime_error("Game requires a verified offline native module");
    // This starts original game code with selective HLE, not a firmware boot.
    // Unimplemented firmware/IOP services remain explicit runtime refusals.
    prepare_hle_elf(input.disc->boot_elf(),module);
    try {
        memory().configure_rtc(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
        memory().hle_kernel()->configure_bios(std::make_shared<const BiosImage>(std::move(input.bios)));
        configure_hle_console(memory(),input.hardware,input.nvm?std::make_shared<const NvmImage>(std::move(*input.nvm)):nullptr);
        memory().mount_disc(std::move(input.disc),input.medium,true);
    }
    catch(...) {runtime_->invalidate_preparation();throw;}
}

void SessionSupervisor::prepare_hle_elf(const ElfImage& elf, const AotModule* module,
                                      const DirectElfEnvironment& environment) {
    runtime_->prepare_hle_elf(elf,module,environment);
}

BootReport SessionSupervisor::run(const AotModule* module, const BootOptions& options) {
    return runtime_->run(module,options);
}
} // namespace ps2native::frontend
