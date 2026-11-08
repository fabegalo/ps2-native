#include "ps2native/frontend/session.hpp"
#include "ps2native/frontend/content.hpp"
#include "ps2native/io.hpp"
#include <stdexcept>

namespace ps2native::frontend {
SessionSupervisor::SessionSupervisor() : memory_(std::make_unique<Memory>()) {}

void SessionSupervisor::prepare_firmware(const BiosImage& bios, const AotModule* module,
                                        const ConsoleHardware* hardware, const NvmImage* nvm,
                                        const DiagnosticAssumptions* assumptions, const Rom1Image* rom1) {
    prepared_ = false;
    hle_.reset();
    nvm_ = nvm ? std::optional<NvmImage>{*nvm} : std::nullopt;
    prepare_ee_boot(bios, module, ee_, *memory_, hardware, nvm_ ? &*nvm_ : nullptr, assumptions, rom1);
    prepared_ = true;
}

void SessionSupervisor::prepare_original(const SessionRequest& request, const AotModule* module) {
    prepared_ = false;
    if (!request.experimental_manifest.empty()) throw std::runtime_error("Original boot refuses an experimental manifest");
    if (request.renderer != RendererMode::GsFaithful) throw std::runtime_error("Native renderer is unavailable for original boot");
    if (request.expected_rom0_sha256.empty()) throw std::runtime_error("Original boot requires the identified ROM0 SHA-256");
    const auto bios = load_bios_rom0(read_file(request.rom0), BiosLoadOptions{request.expected_rom0_sha256});
    const auto* hardware = request.console_profile.empty() ? nullptr : find_console_hardware(request.console_profile);
    if (!request.console_profile.empty() && !hardware) throw std::runtime_error("Unknown console profile");
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
    prepare_firmware(bios, module, hardware, nvm ? &*nvm : nullptr);
    prepared_ = false;
    const auto medium = probe.info.find("media");
    memory_->mount_disc(std::move(disc), medium != probe.info.end() && medium->second == "DVD" ? Iop::DiscMedium::Ps2Dvd : Iop::DiscMedium::Ps2Cd, true);
    prepared_ = true;
}

void SessionSupervisor::prepare_hle_elf(const ElfImage& elf, const AotModule* module,
                                      const DirectElfEnvironment& environment) {
    prepared_ = false;
    hle_.reset();
    prepare_direct_elf(elf,module,ee_,*memory_,environment);
    hle_=std::make_unique<HleKernel>(ee_,*memory_);
    prepared_ = true;
}

BootReport SessionSupervisor::run(const AotModule* module, const BootOptions& options) {
    if (!prepared_) throw std::runtime_error("Console session is not prepared");
    return run_ee_boot(ee_, *memory_, module, options);
}
} // namespace ps2native::frontend
