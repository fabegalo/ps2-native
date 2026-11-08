// M2.4: Direct RDRAM timing/test registers (TCYCLE, TFRM, TRDLY, TEST77 with
// the Rambus 64/72-Mbit layouts) and MCH serial transactions whose effects
// commit when they complete, demonstrated on synthetic inputs only (no Sony
// content). The M2.4 variant of the synthetic ROM0 runs InitDev and then the
// register writes the BIOS performs, sliced native and hybrid.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include "ee_native_fixture.hpp"

#include "ps2native/aot_runtime.hpp"
#include "ps2native/core.hpp"
#include "ps2native/execution.hpp"
#include "ps2native/mch.hpp"
#include "ps2native/rdram.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ps2native;
using namespace boot_checks;

constexpr std::uint64_t kShortBusy = 4719u; // 16 SCK cycles at 1000 ns
constexpr std::uint64_t kLongBusy = 18875u; // 64 SCK cycles

RdramSerialRequest request(std::uint8_t sop, bool broadcast, std::uint8_t sdev, std::uint16_t sa = 0u,
                           std::uint16_t sd = 0u) {
    return {sop, broadcast, sdev, sa, sd};
}

void refused_unchanged(RdramChain& chain, const RdramSerialRequest& r, const std::string& reason) {
    const auto before = chain.devices();
    rejects([&] { (void)chain.transact(r); }, reason);
    require(before == chain.devices(), "refusal changed the chain: " + reason);
}

// InitDev for `devices` RDRAMs, numbered 0..n-1.
void init_dev(RdramChain& chain) {
    chain.power_on();
    chain.sio_pin_initialization();
    (void)chain.transact(request(kRdramSetr, true, 0u));
    (void)chain.transact(request(kRdramClrr, true, 0u));
    (void)chain.transact(request(kRdramSetf, true, 0u));
    (void)chain.transact(request(kRdramSwr, true, 0u, kRdramInit, 0x001Fu));
    for (std::uint16_t n = 0; n < chain.devices().size(); ++n)
        (void)chain.transact(request(kRdramSwr, false, 0x1Fu, kRdramInit, static_cast<std::uint16_t>(0x0080u | n)));
}

void check_registers() {
    RdramChain chain(2u);
    init_dev(chain);
    for (const auto sa : {kRdramTcycle, kRdramTfrm, kRdramTrdly, kRdramTest77})
        refused_unchanged(chain, request(kRdramSrd, false, 1u, sa), "read before any write: its reset value is undefined");
    // Layouts: bits beyond each field are defined as 0, TRDLY stops at 2, TEST77 takes only zero.
    refused_unchanged(chain, request(kRdramSwr, true, 0u, kRdramTcycle, 0x4000u), "RDRAM TCYCLE write 0x4000 sets bits defined as 0");
    refused_unchanged(chain, request(kRdramSwr, true, 0u, kRdramTfrm, 0x0010u), "RDRAM TFRM write 0x0010 sets bits defined as 0");
    refused_unchanged(chain, request(kRdramSwr, true, 0u, kRdramTrdly, 0x0004u), "RDRAM TRDLY write 0x0004 sets bits");
    refused_unchanged(chain, request(kRdramSwr, true, 0u, kRdramTrdly, 0x0003u),
                      "RDRAM TRDLY write 0x0003 exceeds the largest value the data sheet allows (0x0002)");
    refused_unchanged(chain, request(kRdramSwr, true, 0u, kRdramTest77, 0x0001u), "RDRAM TEST77 write 0x0001 sets bits");
    // TPARM (TCAS/TCLS/TDAC, M2.5) has seven bits; the rest of the map is not modeled.
    refused_unchanged(chain, request(kRdramSwr, true, 0u, kRdramTparm, 0x0080u), "RDRAM TPARM write 0x0080 sets bits");
    refused_unchanged(chain, request(kRdramSwr, true, 0u, 0x041u, 0x0000u), "control register 0x041 is not modeled");
    refused_unchanged(chain, request(kRdramSrd, false, 0u, 0x035u), "control register 0x035 is not modeled");

    (void)chain.transact(request(kRdramSwr, true, 0u, kRdramTcycle, 0x3FFFu));
    (void)chain.transact(request(kRdramSwr, false, 1u, kRdramTcycle, 0x0027u)); // SDEV 1 only
    (void)chain.transact(request(kRdramSwr, true, 0u, kRdramTrdly, 0x0002u));
    (void)chain.transact(request(kRdramSwr, true, 0u, kRdramTest77, 0x0000u));
    require(chain.transact(request(kRdramSrd, false, 0u, kRdramTcycle)) == std::optional<std::uint16_t>{0x3FFFu} &&
            chain.transact(request(kRdramSrd, false, 1u, kRdramTcycle)) == std::optional<std::uint16_t>{0x0027u},
            "TCYCLE per device");
    require(chain.transact(request(kRdramSrd, false, 1u, kRdramTrdly)) == std::optional<std::uint16_t>{0x0002u} &&
            chain.transact(request(kRdramSrd, false, 0u, kRdramTest77)) == std::optional<std::uint16_t>{0x0000u},
            "TRDLY and TEST77 read back");
    refused_unchanged(chain, request(kRdramSrd, false, 0u, kRdramTfrm), "RDRAM 0 TFRM read before any write");
    // SETR resets INIT only; the timing registers keep their contents (hypothesis, rdram.hpp).
    const auto before_setr = chain.devices();
    (void)chain.transact(request(kRdramSetr, true, 0u));
    for (std::size_t n = 0; n < before_setr.size(); ++n)
        require(chain.devices()[n].registers == before_setr[n].registers && chain.devices()[n].registers.size() == 3u &&
                chain.devices()[n].init == kRdramInitReset, "SETR keeps the timing registers");
    (void)chain.transact(request(kRdramClrr, true, 0u));
    RdramChain single(1u);
    init_dev(single);
    (void)single.transact(request(kRdramSwr, true, 0u, kRdramTfrm, 0x0007u));
    (void)single.transact(request(kRdramSetr, true, 0u));
    (void)single.transact(request(kRdramClrr, true, 0u));
    require(single.transact(request(kRdramSrd, false, 0x1Fu, kRdramTfrm)) == std::optional<std::uint16_t>{0x0007u},
            "TFRM survives SETR/CLRR");
}

// The chain and the SRD data change when the transaction completes, not when it starts.
void check_commit() {
    MchRegisters mch;
    mch.reset();
    mch.write(kMchRicm, 4u, mch_ricm(kMchSioPinInit, true, 0u, 0u), 0u);
    const auto s = kShortBusy; // SIO pin initialization complete
    mch.write(kMchDrd, 4u, 0u, s);
    mch.write(kMchRicm, 4u, mch_ricm(kRdramSetr, true, 0u, 0u), s);
    require(!mch.chain(s).devices()[0].reset_applied && !mch.chain(s + kShortBusy - 1u).devices()[1].reset_applied,
            "SETR not applied while busy");
    require(mch.chain(s + kShortBusy).devices()[0].reset_bit && mch.chain(s + kShortBusy).devices()[1].reset_bit,
            "SETR applied when it completes");
    // No access since the SETR: the next start is still validated against the completed state.
    auto t = s + kShortBusy + kShortBusy;
    mch.write(kMchRicm, 4u, mch_ricm(kRdramClrr, true, 0u, 0u), t);
    require(mch.chain(t).devices()[0].reset_bit && !mch.chain(t + kShortBusy).devices()[0].reset_bit, "CLRR on completion");
    t += kShortBusy;
    mch.write(kMchDrd, 4u, 0x001Fu, t);
    mch.write(kMchRicm, 4u, mch_ricm(kRdramSwr, true, 0u, kRdramInit), t);
    require(mch.chain(t + kLongBusy - 1u).devices()[1].init == kRdramInitReset &&
            mch.chain(t + kLongBusy).devices()[1].init == 0x001Fu, "INIT write lands at the end of the SWR");
    t += kLongBusy;
    mch.write(kMchDrd, 4u, 0x0027u, t);
    mch.write(kMchRicm, 4u, mch_ricm(kRdramSwr, true, 0u, kRdramTcycle), t);
    require(mch.chain(t).devices()[0].registers.empty() &&
            mch.chain(t + kLongBusy).devices()[0].registers == std::map<std::uint16_t, std::uint16_t>{{kRdramTcycle, 0x0027u}},
            "TCYCLE write lands at the end of the SWR");
    t += kLongBusy;
    mch.write(kMchRicm, 4u, mch_ricm(kRdramSrd, false, 0x1Fu, kRdramTcycle), t);
    rejects([&] { (void)mch.read(kMchDrd, 4u, t + kLongBusy - 1u); }, "in progress");
    require(mch.read(kMchDrd, 4u, t + kLongBusy) == 0x0027u, "SRD data at completion");
    // A refused start keeps the completed state and data.
    t += kLongBusy;
    rejects([&] { mch.write(kMchRicm, 4u, mch_ricm(kRdramSrd, false, 0x1Fu, kRdramTfrm), t); }, "TFRM read before any write");
    require(mch.read(kMchDrd, 4u, t) == 0x0027u && mch.chain(t).devices()[0].registers.size() == 1u, "refusal keeps state");
    // Reset drops a transaction in flight.
    mch.write(kMchDrd, 4u, 0u, t);
    mch.write(kMchRicm, 4u, mch_ricm(kRdramSetr, true, 0u, 0u), t);
    mch.reset();
    require(mch.read(kMchRicm, 4u, t) == 0u && !mch.chain(t + kShortBusy).devices()[0].reset_applied,
            "reset drops the pending SETR");
}

void check_results(const EEState& state, const Memory& memory, const std::string& run) {
    require(memory.read32(m20::kM23Device0Read) == 0x0080u && memory.read32(m20::kM23Device1Read) == 0x0081u,
            run + ": InitDev numbered both devices");
    require(memory.read32(m20::kM24TcycleRead) == m20::kM24Tcycle && memory.read32(m20::kM24TfrmRead) == m20::kM24Tfrm &&
            memory.read32(m20::kM24TrdlyRead) == m20::kM24Trdly && memory.read32(m20::kM24Test77Read) == 0u,
            run + ": timing registers read back");
    const std::map<std::uint16_t, std::uint16_t> expected{
        {kRdramTfrm, m20::kM24Tfrm}, {kRdramTrdly, m20::kM24Trdly}, {kRdramTcycle, m20::kM24Tcycle}, {kRdramTest77, 0u}};
    for (const auto& device : memory.mch().chain(state.retired_instructions).devices())
        require(device.registers == expected && !device.reset_bit && device.fast_clock, run + ": RDRAM registers");
}

void check_boot(const std::vector<std::uint8_t>& rom, const m20::Firmware& fw) {
    const auto bios = load_bios_rom0(rom);
    NativeBootFixture native_fixture(aot_module());
    const auto& module = native_fixture.module;
    require(std::string(module.source_kind) == "bios-rom0" && bios.sha256 == module.source_sha256,
            "AOT module provenance");
    require(find_aot_block(module, fw.at("m24_timing")) != nullptr, "M2.4 block translated");
    BootOptions options;
    options.trace = TraceLevel::Full;
    auto run = [&](bool sliced, EEState& state, Memory& memory) {
        const auto* aot=&module;
        prepare_ee_boot(bios, aot, state, memory);
        seed_native_test_iop(memory);
        return run_native_sliced(state,memory,module,options,sliced ? 17u : options.max_instructions);
    };
    EEState sliced_state, hybrid_state;
    Memory sliced_memory, hybrid_memory;
    const auto sliced = run(true, sliced_state, sliced_memory);
    const auto hybrid = run(false, hybrid_state, hybrid_memory);
    for (const auto* report : {&sliced, &hybrid}) {
        const std::string name = report == &sliced ? "sliced native" : "hybrid";
        require(report->outcome == BootOutcome::BlockPoint && report->pc == fw.at("mmio") + 4u &&
                report->reason.find("0xB000F020 (physical 0x1000F020, INTC)") != std::string::npos,
                name + " block point at the INTC load: " + report->reason);
        for (const char* line : {"  M W32 v=B000F430 p=1000F430 MCH (RDRAM controller) = 804D0060", // SWR TEST77
                                 "  M W32 v=B000F440 p=1000F440 MCH (RDRAM controller) = 00000027",
                                 "  M W32 v=B000F430 p=1000F430 MCH (RDRAM controller) = 804C0060", // SWR TCYCLE
                                 "  M R32 v=B000F430 p=1000F430 MCH (RDRAM controller) = 804C0060", // busy
                                 "  M R32 v=B000F430 p=1000F430 MCH (RDRAM controller) = 004C0060", // done
                                 "  M W32 v=B000F430 p=1000F430 MCH (RDRAM controller) = 804C0001", // SRD TCYCLE SDEV 1
                                 "  M R32 v=B000F440 p=1000F440 MCH (RDRAM controller) = 00000027"})
            require(contains(report->trace, line), name + " trace: " + line);
    }
    check_results(sliced_state, sliced_memory, "sliced native");
    check_results(hybrid_state, hybrid_memory, "hybrid");
    require(sliced.interpreted_instructions == 0u && sliced.aot_instructions == sliced.retired, "sliced native run");
    require(contains(hybrid.trace, " aot " + hex8(fw.at("m24_timing")) + " "), "hybrid ran the M2.4 block in AOT");
    require(hybrid.retired == sliced.retired, "same retired instruction count");
    same_trace(hybrid.trace,sliced.trace);
    for (unsigned n = 0; n < 32; ++n)
        require(hybrid_state.gpr[n].low == sliced_state.gpr[n].low &&
                hybrid_state.gpr[n].high == sliced_state.gpr[n].high, "same GPRs");
    require(hybrid_state.cop0 == sliced_state.cop0 && hybrid_state.count() == sliced_state.count(),
            "same COP0 and Count");
    EEState again_state;
    Memory again_memory;
    require(run(false, again_state, again_memory).trace == hybrid.trace, "deterministic trace");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: ps2n-boot-m24 <synthetic-rom0-m24.bin>");
        const auto rom = read_file(std::filesystem::path(argv[1]));
        const auto fw = m20::build_firmware(m20::Stage::M24);
        require(rom == fw.rom, "fixture file matches the synthetic M2.4 firmware definition");
        require(m20::build_firmware(m20::Stage::M23).rom != fw.rom, "M2.4 variant differs from the M2.3 firmware");
        check_registers();
        check_commit();
        check_boot(rom, fw);
        std::cout << "M2.4 boot: RDRAM timing registers, completion-time commits and hybrid checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "boot_m24: " << error.what() << '\n';
        return 1;
    }
}
