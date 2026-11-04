// M2.5: what the real BIOS memory setup needs before its F410 operation, on
// synthetic inputs only (no Sony content): console hardware profiles and the
// D_ENABLER they carry, the MCH control registers (F400..F4C0 latches and the
// F410 operation), the MCH SIO pin initialization, RDRAM register programming
// that SETR keeps, the later serial opcode encoding (CLRR 1011), the BIOS's
// stale RICM bits and an SRD no device answers. The M2.5 variant of the
// synthetic ROM0 runs it interpreter-only and hybrid with a synthetic profile.
#include "bios_program.hpp"
#include "boot_checks.hpp"

#include "ps2native/aot_runtime.hpp"
#include "ps2native/core.hpp"
#include "ps2native/interpreter.hpp"
#include "ps2native/mch.hpp"
#include "ps2native/rdram.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ps2native;
using namespace boot_checks;

constexpr std::uint64_t kShortBusy = 4719u; // 16 SCK cycles at 1000 ns
constexpr std::uint64_t kLongBusy = 18875u; // 64 SCK cycles
constexpr std::uint32_t kSyntheticDEnabler = 0x00004111u;

// A profile for the synthetic firmware's ROMVER (test only).
const ConsoleHardware kSynthetic{"SYNTHETIC", "TEST-BOARD", m20::kRomver, kSyntheticDEnabler,
                                 ProfileEvidence::Measured, "boot_m25 synthetic profile"};

RdramSerialRequest request(std::uint8_t sop, bool broadcast, std::uint8_t sdev, std::uint16_t sa = 0u,
                           std::uint16_t sd = 0u) {
    return {sop, broadcast, sdev, sa, sd};
}

void refused_unchanged(RdramChain& chain, const RdramSerialRequest& r, const std::string& reason) {
    const auto before = chain.devices();
    rejects([&] { (void)chain.transact(r); }, reason);
    require(before == chain.devices(), "refusal changed the chain: " + reason);
}

void check_profiles(const std::vector<std::uint8_t>& rom) {
    require(!known_console_hardware().empty(), "known profiles");
    for (const auto& profile : known_console_hardware())
        require(profile.romver.size() == 14u && !profile.source.empty() &&
                profile.d_enabler.has_value() == (profile.evidence != ProfileEvidence::Unknown) &&
                (!profile.d_enabler || (*profile.d_enabler & 0x10000u) == 0u),
                "profile " + std::string(profile.model) + ": ROMVER, source, evidence and CPND clear");
    const auto* measured = find_console_hardware("SCPH-77008");
    require(measured != nullptr && measured->d_enabler == 0x4111u && measured->evidence == ProfileEvidence::Measured &&
            measured->romver == "0220EC20060210", "SCPH-77008 profile");
    require(find_console_hardware("SCPH-77008:GH-051") == measured, "MODEL:MAINBOARD lookup");
    require(find_console_hardware("SCPH-70012") == nullptr, "a model on several mainboards needs the mainboard");
    const auto* unknown = find_console_hardware("SCPH-70012:GH-035-11");
    require(unknown != nullptr && !unknown->d_enabler && unknown->evidence == ProfileEvidence::Unknown,
            "unmeasured mainboard");
    require(find_console_hardware("SCPH-77008:GH-999") == nullptr && find_console_hardware("") == nullptr &&
            find_console_hardware("SCPH-1") == nullptr, "unknown names");
    require(std::string(profile_evidence_name(ProfileEvidence::Measured)) !=
            profile_evidence_name(ProfileEvidence::SameChipRevision), "evidence names");

    // D_ENABLER comes from the selected profile, never from a global value.
    Memory m;
    m.reset_for_boot(rom);
    std::uint64_t cycles = 0u;
    m.set_clock(&cycles);
    device_refused([&] { (void)m.read32(0xB000F520u); }, "D_ENABLER read: no console hardware profile selected");
    m.reset_for_boot(rom, unknown);
    device_refused([&] { (void)m.read32(0xB000F520u); }, "SCPH-70012:GH-035-11 has not been measured");
    m.reset_for_boot(rom, &kSynthetic);
    require(m.read32(0xB000F520u) == kSyntheticDEnabler, "D_ENABLER of the profile");
    device_refused([&] { (void)m.read16(0xB000F520u); }, "D_ENABLER read of 2 byte(s) is not modeled");
    m.reset_for_boot(rom);
    device_refused([&] { (void)m.read32(0xB000F520u); }, "no console hardware profile selected");

    // A profile is tied to its BIOS revision.
    const auto bios = load_bios_rom0(rom);
    EEState state;
    rejects([&] { prepare_ee_boot(bios, nullptr, state, m, measured); },
            "console SCPH-77008:GH-051 ships with ROMVER 0220EC20060210, not " + std::string(m20::kRomver) +
                "; firmware revisions are never mixed");
}

void check_control() {
    MchRegisters mch;
    mch.reset();
    require(mch.read(kMchConfig, 4u, 0u) == 0u, "F400 reads 0 after reset (hypothesis)");
    for (const auto reg : {kMchControl, 0x1000F420u, 0x1000F450u, 0x1000F460u, 0x1000F480u, 0x1000F490u,
                           0x1000F4A0u, 0x1000F4C0u})
        rejects([&] { (void)mch.read(reg, 4u, 0u); }, "MCH 0x" + hex8(reg) + " read before any write");
    rejects([&] { mch.write(0x1000F480u, 2u, 0u, 0u); }, "MCH access of 2 byte(s) is not modeled");
    rejects([&] { (void)mch.read(0x1000F480u, 1u, 0u); }, "MCH access of 1 byte(s) is not modeled");
    mch.write(0x1000F480u, 4u, 0x00031234u, 0u);
    mch.write(0x1000F4C0u, 4u, 0xFFFFFFFFu, 0u);
    require(mch.read(0x1000F480u, 4u, 0u) == 0x00031234u && mch.read(0x1000F480u, 2u, 0u) == 0x1234u &&
            mch.read(0x1000F4C0u, 4u, 0u) == 0xFFFFFFFFu, "latches read back; LHU reads the low half");

    // F410: bit 31 alone starts the operation; other words are latched.
    mch.write(kMchControl, 4u, 0u, 10u);
    require(mch.read(kMchControl, 4u, 10u) == 0u, "F410 cleared");
    rejects([&] { mch.write(kMchControl, 4u, 0x80000001u, 10u); }, "bit 31 with other bits set is not verified");
    require(mch.read(kMchControl, 4u, 10u) == 0u, "refused F410 start changes nothing");
    mch.write(kMchControl, 4u, 0x80000000u, 100u);
    require(mch.read(kMchControl, 4u, 100u) == 0x80000000u &&
            mch.read(kMchControl, 4u, 100u + kShortBusy - 1u) == 0x80000000u, "F410 busy for 16 SCK cycles");
    rejects([&] { mch.write(0x1000F480u, 4u, 0u, 100u + kShortBusy - 1u); }, "while the F410 operation is in progress");
    rejects([&] { mch.write(kMchRicm, 4u, mch_ricm(kMchSioPinInit, true, 0u, 0u), 100u); },
            "while the F410 operation is in progress");
    require(mch.read(kMchControl, 4u, 100u + kShortBusy) == 0u, "F410 bit 31 clears");
    mch.write(kMchControl, 4u, 0x00000123u, 100u + kShortBusy);
    require(mch.read(kMchControl, 4u, 100u + kShortBusy) == 0x123u, "F410 timing word latched");

    // Serial busy blocks control writes too.
    auto t = 100u + kShortBusy;
    rejects([&] { mch.write(kMchRicm, 4u, mch_ricm(kMchSioPinInit, false, 0u, 0u), t); },
            "only verified in broadcast with SDEV and SA 0");
    rejects([&] { mch.write(kMchRicm, 4u, mch_ricm(kMchSioPinInit, true, 1u, 0u), t); }, "broadcast with SDEV and SA 0");
    rejects([&] { mch.write(kMchRicm, 4u, mch_ricm(kMchSioPinInit, true, 0u, 0x021u), t); }, "broadcast with SDEV and SA 0");
    mch.write(kMchRicm, 4u, mch_ricm(kMchSioPinInit, true, 0u, 0u), t);
    rejects([&] { mch.write(kMchControl, 4u, 0u, t + kShortBusy - 1u); }, "while a serial transaction is in progress");
    t += kShortBusy;
    require(mch.chain(t).devices()[0].sio_ready && mch.read(kMchRicm, 4u, t) == 0x000003A0u, "SIO pin initialization");

    // The BIOS SRD helper's stale bits (0x3000F000) are accepted and not kept; bits 11 and 30 are refused.
    mch.write(kMchDrd, 4u, 0x001Fu, t);
    mch.write(kMchRicm, 4u, mch_ricm(kRdramSwr, true, 0u, kRdramDevid), t);
    t += kLongBusy;
    rejects([&] { mch.write(kMchRicm, 4u, 0x40000000u | mch_ricm(kRdramSrd, false, 0u, kRdramDevid), t); },
            "outside the SRQ/SA fields");
    rejects([&] { mch.write(kMchRicm, 4u, 0x00000800u | mch_ricm(kRdramSrd, false, 0u, kRdramDevid), t); },
            "outside the SRQ/SA fields");
    // Before SETR every SDEVID is undefined: a non-broadcast SRD is refused.
    rejects([&] { mch.write(kMchRicm, 4u, 0x3000F000u | mch_ricm(kRdramSrd, false, 0x1Fu, kRdramDevid), t); },
            "while its SDEVID is undefined");
    mch.write(kMchRicm, 4u, mch_ricm(kRdramSetr, true, 0u, 0u), t);
    t += 2u * kShortBusy;
    mch.write(kMchRicm, 4u, mch_ricm(kRdramClrr, true, 0u, 0u), t);
    t += kShortBusy;
    mch.write(kMchDrd, 4u, 0x0080u, t); // INIT: SRP 1, SDEVID 0 for both
    mch.write(kMchRicm, 4u, mch_ricm(kRdramSwr, true, 0u, kRdramInit), t);
    t += kLongBusy;
    mch.write(kMchRicm, 4u, 0x3000F000u | mch_ricm(kRdramSrd, false, 1u, kRdramDevid), t);
    require(mch.read(kMchRicm, 4u, t) == 0x80400001u, "stale bits are not kept in RICM");
    t += kLongBusy;
    require(mch.read(kMchDrd, 4u, t) == 0u, "no device numbered 1: SRD reads 0");
    rejects([&] { mch.write(kMchRicm, 4u, mch_ricm(kRdramSrd, false, 0u, kRdramDevid), t); },
            "several devices driving SIO0");

    mch.reset();
    require(mch.read(kMchConfig, 4u, 0u) == 0u, "reset restores F400");
    rejects([&] { (void)mch.read(0x1000F480u, 4u, 0u); }, "read before any write");
    rejects([&] { mch.write(kMchRicm, 4u, mch_ricm(kRdramSetr, true, 0u, 0u), 0u); }, "SIO pin directions are undefined");
}

void check_chain() {
    RdramChain chain(2u);
    chain.power_on();
    chain.sio_pin_initialization();
    // Register layouts the BIOS writes before SETR (bits beyond the fields are 0).
    struct Write { std::uint16_t sa, value; const char* name; };
    const Write refused[] = {{kRdramDevid, 0x0020u, "DEVID"}, {kRdramCca, 0x0200u, "CCA"}, {kRdramCcb, 0x0200u, "CCB"},
                             {kRdramNapx, 0x0800u, "NAPX"}, {kRdramPdnxa, 0x2000u, "PDNXA"},
                             {kRdramPdnxb, 0x2000u, "PDNXB"}, {kRdramTparm, 0x0080u, "TPARM"},
                             {kRdramSkip, 0x0001u, "SKIP"}, {kRdramInit, 0x1000u, "INIT"}, {kRdramInit, 0x0020u, "INIT"}};
    for (const auto& write : refused)
        refused_unchanged(chain, request(kRdramSwr, true, 0u, write.sa, write.value),
                          std::string("RDRAM ") + write.name + " write");
    // Broadcast programming before the first SETR, as the BIOS does.
    const Write accepted[] = {{kRdramTest34, 0xFFFFu, "TEST34"}, {kRdramCca, 0x01FFu, "CCA"},
                              {kRdramNapx, 0x07FFu, "NAPX"},     {kRdramPdnxa, 0x1FFFu, "PDNXA"},
                              {kRdramTparm, 0x007Fu, "TPARM"},   {kRdramSkip, 0u, "SKIP"},
                              {kRdramTest78, 0x1234u, "TEST78"}, {kRdramDevid, 0x001Fu, "DEVID"}};
    for (const auto& write : accepted) (void)chain.transact(request(kRdramSwr, true, 0u, write.sa, write.value));
    for (const auto& device : chain.devices())
        require(!device.reset_applied && device.registers.size() == 8u && device.init_defined == 0u,
                "pre-SETR writes stored, INIT still undefined");
    // INIT written before SETR numbers the chain; bit 11 is stored.
    (void)chain.transact(request(kRdramSwr, true, 0u, kRdramInit, 0x081Fu));
    (void)chain.transact(request(kRdramSwr, false, 0x1Fu, kRdramInit, 0x0880u));
    (void)chain.transact(request(kRdramSwr, false, 0x1Fu, kRdramInit, 0x0881u));
    require(chain.transact(request(kRdramSrd, false, 0x1Fu, kRdramInit)) == std::optional<std::uint16_t>{0u},
            "enumeration ends on an SRD no device answers");
    require(chain.transact(request(kRdramSrd, false, 1u, kRdramInit)) == std::optional<std::uint16_t>{0x0881u} &&
            chain.transact(request(kRdramSrd, false, 0u, kRdramTparm)) == std::optional<std::uint16_t>{0x007Fu},
            "INIT bit 11 and TPARM read back");
    const auto before = chain.devices();
    (void)chain.transact(request(kRdramSetr, true, 0u));
    (void)chain.transact(request(kRdramClrr, true, 0u));
    for (std::size_t n = 0; n < before.size(); ++n)
        require(chain.devices()[n].registers == before[n].registers && chain.devices()[n].init == kRdramInitReset &&
                chain.devices()[n].init_defined == kRdramInitReset, "SETR resets INIT only");
}

void check_results(const EEState& state, const Memory& memory, const std::string& run) {
    require(memory.read32(m20::kM25DEnabler) == kSyntheticDEnabler, run + ": D_ENABLER from the profile");
    require(memory.read32(m20::kM25ConfigRead) == m20::kM25F400Bit18, run + ": F400 read-modify-write from 0");
    const auto busy = memory.read32(m20::kM25ControlBusy);
    require(busy >= kShortBusy && busy <= kShortBusy + 12u, run + ": F410 busy measured with Count: " + std::to_string(busy));
    require(memory.read32(m20::kM25F480Read) == (m20::kM25F480 & 0xFFFFu), run + ": LHU of F480");
    require(memory.read32(m20::kM25EndRead) == 0u && memory.read32(m20::kM25Init1Read) == 0x0881u,
            run + ": chain renumbered, enumeration end reads 0");
    require(memory.read32(m20::kM25TcycleRead) == m20::kM24Tcycle && memory.read32(m20::kM25TparmRead) == m20::kM25Tparm,
            run + ": TCYCLE kept across SETR, TPARM written");
    const auto& devices = memory.mch().chain(state.retired_instructions).devices();
    for (std::size_t n = 0; n < devices.size(); ++n)
        require(devices[n].init == (0x0880u | n) && !devices[n].reset_bit && devices[n].registers.size() == 5u,
                run + ": RDRAM " + std::to_string(n));
}

void check_boot(const std::vector<std::uint8_t>& rom, const m20::Firmware& fw) {
    const auto bios = load_bios_rom0(rom);
    const auto& module = aot_module();
    require(std::string(module.source_kind) == "bios-rom0" && bios.sha256 == module.source_sha256,
            "AOT module provenance");
    require(find_aot_block(module, fw.at("m25_control_wait")) != nullptr, "M2.5 block translated");
    BootOptions options;
    options.trace = TraceLevel::Full;
    auto run = [&](const AotModule* aot, EEState& state, Memory& memory, const ConsoleHardware* hardware) {
        prepare_ee_boot(bios, aot, state, memory, hardware);
        return run_ee_boot(state, memory, aot, options);
    };
    // Without a profile the firmware stops, named, at its D_ENABLER load.
    for (const AotModule* aot : {static_cast<const AotModule*>(nullptr), &module}) {
        EEState state;
        Memory memory;
        const auto report = run(aot, state, memory, nullptr);
        require(report.outcome == BootOutcome::BlockPoint && report.pc == fw.at("m25_control") &&
                report.reason.find("no console hardware profile selected") != std::string::npos,
                "block point at the D_ENABLER load without a profile: " + report.reason);
    }
    EEState interpreted_state, hybrid_state;
    Memory interpreted_memory, hybrid_memory;
    const auto interpreted = run(nullptr, interpreted_state, interpreted_memory, &kSynthetic);
    const auto hybrid = run(&module, hybrid_state, hybrid_memory, &kSynthetic);
    for (const auto* report : {&interpreted, &hybrid}) {
        const std::string name = report == &interpreted ? "interpreter" : "hybrid";
        require(report->outcome == BootOutcome::BlockPoint && report->pc == fw.at("mmio") + 4u &&
                report->reason.find("0xB000F020 (physical 0x1000F020, INTC)") != std::string::npos,
                name + " block point at the INTC load: " + report->reason);
        for (const char* line : {"  M R32 v=B000F520 p=1000F520 DMAC extended = 00004111",
                                 "  M W32 v=B000F410 p=1000F410 MCH (RDRAM controller) = 00000000",
                                 "  M R32 v=B000F400 p=1000F400 MCH (RDRAM controller) = 00000000",
                                 "  M W32 v=B000F400 p=1000F400 MCH (RDRAM controller) = 00040000",
                                 "  M W32 v=B000F410 p=1000F410 MCH (RDRAM controller) = 80000000",
                                 "  M R32 v=B000F410 p=1000F410 MCH (RDRAM controller) = 80000000", // polling
                                 "  M R32 v=B000F410 p=1000F410 MCH (RDRAM controller) = 00000000", // done
                                 "  M W32 v=B000F410 p=1000F410 MCH (RDRAM controller) = 00000123",
                                 "  M R16 v=B000F480 p=1000F480 MCH (RDRAM controller) = 1234",
                                 "  M W32 v=B000F430 p=1000F430 MCH (RDRAM controller) = 800002E0", // CLRR
                                 "  M W32 v=B000F430 p=1000F430 MCH (RDRAM controller) = 8021005F"})
            require(contains(report->trace, line), name + " trace: " + line);
    }
    check_results(interpreted_state, interpreted_memory, "interpreter");
    check_results(hybrid_state, hybrid_memory, "hybrid");
    require(interpreted.aot_instructions == 0u, "interpreter-only run");
    require(contains(hybrid.trace, " aot " + hex8(fw.at("m25_control_wait")) + " "), "hybrid ran the M2.5 block in AOT");
    require(hybrid.retired == interpreted.retired, "same retired instruction count");
    require(normalized(hybrid.trace) == normalized(interpreted.trace), "hybrid and interpreter traces agree");
    for (unsigned n = 0; n < 32; ++n)
        require(hybrid_state.gpr[n].low == interpreted_state.gpr[n].low &&
                hybrid_state.gpr[n].high == interpreted_state.gpr[n].high, "same GPRs");
    require(hybrid_state.cop0 == interpreted_state.cop0 && hybrid_state.count() == interpreted_state.count(),
            "same COP0 and Count");
    EEState again_state;
    Memory again_memory;
    require(run(&module, again_state, again_memory, &kSynthetic).trace == hybrid.trace, "deterministic trace");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: ps2n-boot-m25 <synthetic-rom0-m25.bin>");
        const auto rom = read_file(std::filesystem::path(argv[1]));
        const auto fw = m20::build_firmware(m20::Stage::M25);
        require(rom == fw.rom, "fixture file matches the synthetic M2.5 firmware definition");
        require(m20::build_firmware(m20::Stage::M24).rom != fw.rom, "M2.5 variant differs from the M2.4 firmware");
        check_profiles(rom);
        check_control();
        check_chain();
        check_boot(rom, fw);
        std::cout << "M2.5 boot: console profiles, MCH control registers, SIO pin initialization, "
                     "SETR-kept RDRAM registers and hybrid checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "boot_m25: " << error.what() << '\n';
        return 1;
    }
}
