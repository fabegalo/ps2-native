// Synthetic firmware/disc; checks the shared supervisor without Qt or user data.
#include "bios_program.hpp"
#include "iso_image.hpp"
#include "ps2native/frontend/session.hpp"
#include "ps2native/frontend/content.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/io.hpp"
#include <iostream>
#include <stdexcept>

using namespace ps2native;
using namespace ps2native::frontend;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F&& operation, const char* reason) {
    try { operation(); }
    catch (const std::exception& error) {
        require(std::string(error.what()).find(reason) != std::string::npos, error.what());
        return;
    }
    throw std::runtime_error(std::string("Expected refusal: ") + reason);
}
struct Fixture {
    std::filesystem::path rom, disc;
    ~Fixture() {
        std::error_code error;
        std::filesystem::remove(rom, error);
        std::filesystem::remove(disc, error);
    }
};
}
int main(int argc, char** argv) try {
    require(argc == 2, "Expected fixture directory");
    const std::filesystem::path directory(argv[1]);
    ps2native::create_directories(directory);
    const auto rom_path = directory / "session-synthetic.rom0", disc_path = directory / "session-synthetic.iso";
    require(!std::filesystem::exists(rom_path) && !std::filesystem::exists(disc_path), "Fixture paths already exist");
    Fixture fixture{rom_path, disc_path};
    const auto bios = load_bios_rom0(m20::build_firmware().rom);
    write_file_atomically(fixture.rom, bios.rom0);
    write_file_atomically(fixture.disc, iso_test::build_iso({}));
    SessionRequest request;
    request.rom0 = fixture.rom;
    request.expected_rom0_sha256 = bios.sha256;
    request.disc = fixture.disc;
    request.expected_disc_boot = probe_disc(fixture.disc).info.at("boot");
    SessionSupervisor session;
    BootOptions options; options.max_instructions = 100; options.max_trace_events = 8;
    rejects([&] { (void)session.run(nullptr, options); }, "not prepared");
    session.prepare_original(request, nullptr);
    auto reference = std::make_unique<Memory>(); EEState ee;
    prepare_ee_boot(bios, nullptr, ee, *reference);
    reference->mount_disc(std::make_shared<DiscImage>(fixture.disc), Iop::DiscMedium::Ps2Cd, true);
    require(checkpoint_state_sha256(session.ee(), session.memory()) == checkpoint_state_sha256(ee, *reference),
            "Original preparation preserves complete console state");
    const auto actual = session.run(nullptr, options);
    const auto expected = run_ee_boot(ee, *reference, nullptr, options);
    require(actual.outcome == expected.outcome && actual.pc == expected.pc && actual.reason == expected.reason &&
            actual.retired == expected.retired && actual.aot_instructions == expected.aot_instructions &&
            actual.interpreted_instructions == expected.interpreted_instructions,
            "Supervisor preserves execution counters and refusals");
    require(checkpoint_state_sha256(session.ee(), session.memory()) == checkpoint_state_sha256(ee, *reference),
            "Supervisor preserves complete state and cycles after execution");
#ifdef PS2N_NATIVE_ONLY
    require(actual.retired == 0 && actual.interpreted_instructions == 0 && actual.reason.find("missing EE AOT") != std::string::npos,
            "Native session refuses absent AOT without interpreting");
#else
    require(actual.interpreted_instructions > 0 && actual.aot_instructions == 0,
            "Reference session reports interpretation honestly");
#endif
    auto invalid = request; invalid.expected_rom0_sha256 = std::string(64, '0');
    rejects([&] { session.prepare_original(invalid, nullptr); }, "SHA-256");
    rejects([&] { (void)session.run(nullptr, options); }, "not prepared");
    invalid = request; invalid.renderer = RendererMode::Native;
    rejects([&] { session.prepare_original(invalid, nullptr); }, "Native renderer");
    invalid = request; invalid.expected_disc_boot = "changed";
    rejects([&] { session.prepare_original(invalid, nullptr); }, "BOOT2 differs");
    invalid = request; invalid.disc.reset();
    rejects([&] { session.prepare_original(invalid, nullptr); }, "No game disc");
    invalid = request; invalid.console_profile = "unknown";
    rejects([&] { session.prepare_original(invalid, nullptr); }, "Unknown console profile");
    invalid = request; invalid.nvm = directory / "unused.nvm";
    rejects([&] { session.prepare_original(invalid, nullptr); }, "NVM requires");
    invalid = request; invalid.experimental_manifest = directory / "unused.json";
    rejects([&] { session.prepare_original(invalid, nullptr); }, "experimental manifest");
    session.prepare_original(request, nullptr);
    require(session.ee().pc == kEeResetVector, "Session can restart after refused preparation");
    std::cout << "Session supervisor identity refusals, shared state and execution accounting passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
