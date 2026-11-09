// Synthetic firmware/disc; checks the shared supervisor without Qt or user data.
#include "bios_program.hpp"
#include "iso_image.hpp"
#include "hle_fixture.hpp"
#include "ps2native/frontend/session.hpp"
#include "ps2native/frontend/content.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/io.hpp"
#include "ps2_runtime.h"
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
    require(&session.ee()==&session.runtime().cpu() && &session.memory()==&session.runtime().memory(),
            "Supervisor borrows the SDK-owned state without another console");
    auto* const canonical_cpu=&session.ee();auto* const canonical_memory=&session.memory();
    BootOptions options; options.max_instructions = 100; options.max_trace_events = 8;
    rejects([&] { (void)session.run(nullptr, options); }, "not prepared");
    session.prepare_original(request, nullptr);
    require(session.ee().cycles()==0 && session.memory().iop().cycle()==0,
            "Firmware preparation resets both CPU clocks before attaching input");
    session.memory().diagnostic_sio2_digital_pad(0,0xFFFFu,8192);
    auto reference = std::make_unique<Memory>(); EEState ee;
    prepare_ee_boot(bios, nullptr, ee, *reference);
    reference->diagnostic_sio2_digital_pad(0,0xFFFFu,8192);
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
    const auto assisted=parse_ee_elf(direct_test::elf(hle_test::program()));
    session.prepare_hle_elf(assisted,nullptr);
    require(session.memory().hle_kernel()!=nullptr,"Supervisor owns the explicit HLE session");
    require(session.ee().cp0(Cop0::Wired)==31 && session.ee().cp0(Cop0::Random)==47,
            "HLE installs the 32-MiB kernel TLB and protects its wired entries");
    auto& mapped=session.memory();
    require(mapped.read32(0x1000E010u)==mapped.read32(0xB000E010u),
            "HLE register mapping reaches the existing device bus");
    mapped.write32(0x20100000u,0x12345678u);
    require(mapped.read32(0x00100000u)==0x12345678u && mapped.read32(0x30100000u)==0x12345678u,
            "HLE RAM aliases share the canonical storage");
    require(((mapped.tlb_entry(*mapped.tlb_probe(0x20100000u)).entry_lo0>>3u)&7u)==2 &&
            ((mapped.tlb_entry(*mapped.tlb_probe(0x30100000u)).entry_lo0>>3u)&7u)==7,
            "HLE aliases retain their cache policies");
    mapped.set_translation_context(23,false,Memory::Privilege::Kernel);
    require(mapped.read32(0x1000E010u)==mapped.read32(0xB000E010u),
            "Kernel mappings are global across ASIDs");
    rejects([&]{mapped.write32(0x1000C000u,0);},"TLB modified");
    rejects([&]{(void)mapped.read32(0x12010000u);},"TLB invalid");
    rejects([&]{(void)mapped.read32(0x02000000u);},"TLB refill");
    mapped.tlb_write(2,{0,0x10000017u,0x00400016u,0x00400052u});
    require(mapped.read32(0x10000000u)==mapped.read32(0xB0000000u),
            "Guest TLB updates remain authoritative");
    mapped.set_translation_context(24,false,Memory::Privilege::Kernel);
    rejects([&]{(void)mapped.read32(0x10000000u);},"TLB refill");
    const auto ram_entry=*mapped.tlb_probe(0x00100000u);
    const auto original_ram=mapped.tlb_entry(ram_entry);
    mapped.tlb_write(ram_entry,{original_ram.page_mask,original_ram.entry_hi,
        original_ram.entry_lo0&~4u,original_ram.entry_lo1&~4u});
    rejects([&]{mapped.write32(0x00100000u,0);},"TLB modified");
    require(mapped.read32(0x20100000u)==0x12345678u,"TLB permission refusal preserves shared RAM");
    require(mapped.code_matches(0x00100000u,0x12345678u),"Native guard resolves the mapped code page");
    mapped.tlb_write(ram_entry,{original_ram.page_mask,original_ram.entry_hi,
        original_ram.entry_lo0&~2u,original_ram.entry_lo1&~2u});
    rejects([&]{(void)mapped.code_matches(0x00100000u,0x12345678u);},"TLB invalid");
    rejects([&]{(void)mapped.read32(0x00010000u);},"TLB refill");
    mapped.write32(0x70000000u,0xAABBCCDDu);
    require(mapped.read32(0x70000000u)==0xAABBCCDDu,"HLE scratchpad uses the canonical TLB mapping");
    {
        Iop romless;romless.reset({},nullptr);
        const auto stopped=romless.stop_reason();
        romless.mount_disc(std::make_shared<DiscImage>(fixture.disc),Iop::DiscMedium::Ps2Cd);
        require(romless.stopped() && romless.stop_reason()==stopped && !romless.cycle(),
                "ROM-less media attachment preserves the stopped IOP and its reset clock");
        rejects([&]{(void)romless.ee_cdvd_read(0x1F40200Fu,1);},"no ROM0 mapped");
        rejects([&]{romless.mount_disc({},Iop::DiscMedium::Ps2Cd);},"disc image");
        const std::array<std::uint8_t,4> incomplete_rom{};
        Iop invalid;invalid.reset(incomplete_rom,nullptr);
        rejects([&]{invalid.mount_disc(std::make_shared<DiscImage>(fixture.disc),Iop::DiscMedium::Ps2Cd);},"active reset state");
    }
    session.memory().mount_disc(std::make_shared<DiscImage>(fixture.disc),Iop::DiscMedium::Ps2Cd,true);
    require(session.memory().iop().hle_services_active() && !session.memory().iop().cycle() &&
            !session.memory().iop().native_instructions() && !session.memory().iop().interpreted_instructions(),
            "HLE mounts media in the canonical service state without claiming IOP firmware execution");
    session.prepare_original(request,nullptr);
    require(session.memory().hle_kernel()==nullptr && session.ee().pc==kEeResetVector,
            "Original boot detaches HLE before resetting the same console");
    require(!session.memory().tlb_probe(0x1000E000u),"Original reset does not inherit HLE mappings");
    require(!session.memory().iop().hle_services_active(),"Original reset removes IOP HLE services");
    require(canonical_cpu==&session.ee() && canonical_memory==&session.memory(),"SDK owner keeps state addresses stable across lifecycle transitions");
    session.runtime().invalidate_preparation();
    rejects([&]{(void)session.run(nullptr,options);},"not prepared");
    std::cout << "Session supervisor identity refusals, shared state and execution accounting passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
