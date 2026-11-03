// M2.3: the Direct RDRAM serial control chain (Rambus data sheet: SETR, CLRR,
// SETF, SWR/SRD of INIT, SIO repeater daisy chain) behind the MCH serial
// interface MCH_RICM/MCH_DRD, demonstrated on synthetic inputs only (no Sony
// content). The M2.3 variant of the synthetic ROM0 runs Algorithm InitDev for
// two devices interpreter-only and hybrid, and both runs must agree.
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

constexpr std::uint64_t kShortBusy = 4719u; // 16 SCK cycles of 1000 ns at 294.912 MHz, rounded up
constexpr std::uint64_t kLongBusy = 18875u; // 64 SCK cycles

RdramSerialRequest request(std::uint8_t sop, bool broadcast, std::uint8_t sdev = 0u, std::uint16_t sd = 0u,
                           std::uint16_t sa = kRdramInit) {
    return {sop, broadcast, sdev, sop == kRdramSrd || sop == kRdramSwr ? sa : std::uint16_t{0}, sd};
}

bool same(const RdramChain::Device& a, const RdramChain::Device& b) {
    return a.reset_applied == b.reset_applied && a.reset_bit == b.reset_bit && a.fast_clock == b.fast_clock &&
           a.init == b.init && a.init_defined == b.init_defined;
}

// A refused transaction leaves every device as it was.
void refused_unchanged(RdramChain& chain, const RdramSerialRequest& r, const std::string& reason) {
    const auto before = chain.devices();
    rejects([&] { (void)chain.transact(r); }, reason);
    for (std::size_t n = 0; n < before.size(); ++n)
        require(same(before[n], chain.devices()[n]), "refusal changed RDRAM " + std::to_string(n) + ": " + reason);
}

void check_chain() {
    rejects([] { (void)RdramChain(0u); }, "1 to 32 devices");
    require(RdramChain::sck_cycles(kRdramSwr) == 64u && RdramChain::sck_cycles(kRdramSrd) == 64u &&
            RdramChain::sck_cycles(kRdramSetr) == 16u && RdramChain::sck_cycles(kRdramClrr) == 16u &&
            RdramChain::sck_cycles(kRdramSetf) == 16u, "transaction lengths in SCK cycles");
    rejects([] { (void)RdramChain::sck_cycles(0xEu); }, "no modeled transaction length");

    RdramChain chain(2u);
    chain.power_on();
    // Power-on: registers undefined until a broadcast SETR.
    refused_unchanged(chain, request(kRdramSwr, true, 0u, 0x1Fu), "RDRAM 0 has not received SETR since power-on");
    refused_unchanged(chain, request(kRdramClrr, true), "has not received SETR since power-on");
    refused_unchanged(chain, request(kRdramSetr, false, 0x1Fu), "non-broadcast SETR before any SETR");
    // Opcodes the 64/72-Mbit sheet does not define (the PS2 BIOS sends 0xB and 0xE).
    refused_unchanged(chain, request(0xEu, true), "serial opcode 0xE is reserved");
    refused_unchanged(chain, request(0xBu, true), "serial opcode 0xB is reserved");
    refused_unchanged(chain, request(kRdramTcen, true), "temperature calibration (TCEN/TCAL) is not modeled");

    require(!chain.transact(request(kRdramSetr, true)), "SETR returns no data");
    for (const auto& device : chain.devices())
        require(device.reset_applied && device.reset_bit && !device.fast_clock && device.init == kRdramInitReset &&
                device.init_defined == kRdramInitReset, "SETR: SDEVID 0x1F, SRP 1, other INIT fields undefined");
    refused_unchanged(chain, request(kRdramSrd, false, 0x1Fu), "access between SETR and CLRR is not defined");
    (void)chain.transact(request(kRdramClrr, true));
    (void)chain.transact(request(kRdramSetf, true));
    for (const auto& device : chain.devices())
        require(!device.reset_bit && device.fast_clock && device.init == kRdramInitReset, "CLRR and SETF");
    // Both devices share SDEVID 0x1F and repeat SIO: two would drive the read.
    refused_unchanged(chain, request(kRdramSrd, false, 0x1Fu), "several devices driving SIO0");
    refused_unchanged(chain, request(kRdramSwr, true, 0u, 0x0020u), "sets bits defined as 0");
    refused_unchanged(chain, request(kRdramSwr, true, 0u, 0x8000u), "sets bits defined as 0");
    refused_unchanged(chain, request(kRdramSwr, true, 0u, 0u, 0x040u), "control register 0x040 is not modeled");
    refused_unchanged(chain, request(kRdramSrd, false, 0x20u), "exceeds 5 bits");

    // InitDev step 6: broadcast INIT = SRP 0, SDEVID 0x1F reaches both devices
    // (repeating happens before the write takes effect), then the chain ends at device 0.
    (void)chain.transact(request(kRdramSwr, true, 0u, 0x001Fu));
    for (const auto& device : chain.devices())
        require(device.init == 0x001Fu && device.init_defined == kRdramInitFields, "broadcast INIT write");
    require(chain.transact(request(kRdramSrd, false, 0x1Fu)) == std::optional<std::uint16_t>{0x001Fu},
            "only device 0 answers SDEV 0x1F");
    (void)chain.transact(request(kRdramSwr, false, 1u, 0x0081u)); // SDEV 1 is nobody yet: no effect
    require(chain.devices()[0].init == 0x001Fu && chain.devices()[1].init == 0x001Fu, "write to absent SDEV");
    refused_unchanged(chain, request(kRdramSrd, false, 1u), "no device answered");
    // Steps 8-10.
    (void)chain.transact(request(kRdramSwr, false, 0x1Fu, 0x0080u));
    require(chain.devices()[0].init == 0x0080u && chain.devices()[1].init == 0x001Fu, "device 0 numbered");
    require(chain.transact(request(kRdramSrd, false, 0x1Fu)) == std::optional<std::uint16_t>{0x001Fu},
            "device 1 now reachable as SDEV 0x1F");
    (void)chain.transact(request(kRdramSwr, false, 0x1Fu, 0x0081u));
    require(chain.transact(request(kRdramSrd, false, 0u)) == std::optional<std::uint16_t>{0x0080u}, "SDEV 0");
    require(chain.transact(request(kRdramSrd, false, 1u)) == std::optional<std::uint16_t>{0x0081u}, "SDEV 1");
    refused_unchanged(chain, request(kRdramSrd, false, 0x1Fu), "SRD to SDEV 0x1F: no device answered");
    refused_unchanged(chain, request(kRdramSrd, true), "several devices driving SIO0");
    // A broadcast SETR resets every reachable device again.
    (void)chain.transact(request(kRdramSetr, true));
    require(chain.devices()[0].init == kRdramInitReset && chain.devices()[1].init == kRdramInitReset, "SETR again");

    // Undefined INIT fields after SETR make the read value unknown.
    RdramChain single(1u);
    single.power_on();
    (void)single.transact(request(kRdramSetr, true));
    (void)single.transact(request(kRdramClrr, true));
    refused_unchanged(single, request(kRdramSrd, false, 0x1Fu), "PSX/NSR/PSR/LSR are undefined");
    (void)single.transact(request(kRdramSwr, false, 0x1Fu, kRdramInitReset | kRdramInitPsx));
    require(single.transact(request(kRdramSrd, false, 0x1Fu)) == std::optional<std::uint16_t>{0x00DFu},
            "INIT read after write");
    single.power_on();
    require(!single.devices()[0].reset_applied, "power-on forgets everything");
}

void check_mch() {
    require(mch_busy_cycles(16u) == kShortBusy && mch_busy_cycles(64u) == kLongBusy, "busy durations");
    require(mch_ricm(kRdramSwr, true, 0u, kRdramInit) == 0x80210060u && mch_ricm(kRdramSetr, true, 0u, 0u) == 0x800000A0u,
            "RICM encoding");
    MchRegisters mch;
    mch.reset();
    require(mch.chain().devices().size() == kPs2RdramDevices, "two RDRAMs");
    require(mch.read(kMchRicm, 4u, 0u) == 0u, "idle after reset");
    rejects([&] { (void)mch.read(kMchDrd, 4u, 0u); }, "MCH_DRD read without a completed SRD");
    for (const auto reg : {0x1000F400u, 0x1000F410u, 0x1000F420u, 0x1000F450u, 0x1000F460u, 0x1000F480u,
                           0x1000F490u, 0x1000F4A0u, 0x1000F4C0u}) {
        rejects([&] { (void)mch.read(reg, 4u, 0u); }, "register 0x" + hex8(reg) + " is not modeled");
        rejects([&] { mch.write(reg, 4u, 0u, 0u); }, "register 0x" + hex8(reg) + " is not modeled");
    }
    rejects([&] { (void)mch.read(kMchRicm, 2u, 0u); }, "2 byte(s) is not modeled (32-bit only)");
    rejects([&] { mch.write(kMchDrd, 8u, 0u, 0u); }, "8 byte(s) is not modeled");
    rejects([&] { mch.write(kMchRicm, 4u, 0x000000A0u, 0u); }, "without bit 31");
    rejects([&] { mch.write(kMchRicm, 4u, 0x800008A0u, 0u); }, "outside the SRQ/SA fields");
    rejects([&] { mch.write(kMchRicm, 4u, 0x900000A0u, 0u); }, "outside the SRQ/SA fields");
    rejects([&] { mch.write(kMchRicm, 4u, 0x800004BFu, 0u); }, "SDEV5 is beyond");
    rejects([&] { mch.write(kMchRicm, 4u, mch_ricm(kRdramSwr, true, 0u, kRdramInit), 0u); }, "without serial data");
    rejects([&] { mch.write(kMchRicm, 4u, mch_ricm(0xEu, true, 0u, 0u), 0u); }, "serial opcode 0xE is reserved");
    rejects([&] { mch.write(kMchDrd, 4u, 0x10000u, 0u); }, "serial data is 16 bits");
    require(mch.read(kMchRicm, 4u, 0u) == 0u, "refused starts leave the MCH idle");

    // SETR: busy for 16 SCK cycles, fields readable with bit 31.
    mch.write(kMchDrd, 4u, 0u, 100u);
    mch.write(kMchRicm, 4u, mch_ricm(kRdramSetr, true, 0u, 0u), 100u);
    require(mch.read(kMchRicm, 4u, 100u) == 0x800000A0u, "busy at start");
    require(mch.read(kMchRicm, 4u, 100u + kShortBusy - 1u) == 0x800000A0u, "busy until the last cycle");
    rejects([&] { mch.write(kMchRicm, 4u, mch_ricm(kRdramClrr, true, 0u, 0u), 100u + kShortBusy - 1u); },
            "while a serial transaction is in progress");
    rejects([&] { mch.write(kMchDrd, 4u, 0u, 100u); }, "while a serial transaction is in progress");
    rejects([&] { (void)mch.read(kMchDrd, 4u, 100u); }, "MCH_DRD read while a serial transaction is in progress");
    const auto setr_done = 100u + kShortBusy;
    require(mch.read(kMchRicm, 4u, setr_done) == 0x000000A0u, "idle when the SETR completes");
    require(mch.chain().devices()[1].reset_bit, "SETR reached both devices");
    // InitDev step 3: 16 SCK cycles between SETR and CLRR.
    rejects([&] { mch.write(kMchRicm, 4u, mch_ricm(kRdramClrr, true, 0u, 0u), setr_done + kShortBusy - 1u); },
            "CLRR less than 16 SCK cycles after SETR");
    auto t = setr_done + kShortBusy;
    mch.write(kMchRicm, 4u, mch_ricm(kRdramClrr, true, 0u, 0u), t);
    t += kShortBusy;
    mch.write(kMchRicm, 4u, mch_ricm(kRdramSetf, true, 0u, 0u), t);
    t += kShortBusy;
    mch.write(kMchDrd, 4u, 0x001Fu, t);
    mch.write(kMchRicm, 4u, mch_ricm(kRdramSwr, true, 0u, kRdramInit), t);
    require(mch.read(kMchRicm, 4u, t + kLongBusy - 1u) == 0x80210060u, "SWR busy for 64 SCK cycles");
    t += kLongBusy;
    require(mch.read(kMchRicm, 4u, t) == 0x00210060u, "SWR complete");
    rejects([&] { (void)mch.read(kMchDrd, 4u, t); }, "without a completed SRD");
    mch.write(kMchRicm, 4u, mch_ricm(kRdramSrd, false, 0x1Fu, kRdramInit), t);
    rejects([&] { (void)mch.read(kMchDrd, 4u, t + kLongBusy - 1u); }, "in progress");
    t += kLongBusy;
    require(mch.read(kMchDrd, 4u, t) == 0x001Fu && mch.read(kMchDrd, 4u, t) == 0x001Fu, "SRD data in MCH_DRD");
    rejects([&] { mch.write(kMchRicm, 4u, mch_ricm(kRdramSrd, false, 3u, kRdramInit), t); }, "no device answered");
    require(mch.read(kMchRicm, 4u, t) == 0x0021001Fu && mch.read(kMchDrd, 4u, t) == 0x001Fu,
            "a refused SRD keeps the previous transaction");
    mch.write(kMchDrd, 4u, 0x0080u, t);
    rejects([&] { (void)mch.read(kMchDrd, 4u, t); }, "without a completed SRD");
    mch.reset();
    require(mch.read(kMchRicm, 4u, 0u) == 0u && !mch.chain().devices()[0].reset_applied, "reset");
}

void check_bus(const std::vector<std::uint8_t>& rom) {
    Memory m;
    m.reset_for_boot(rom);
    std::uint64_t cycles = 10u;
    m.set_clock(&cycles);
    require(m.read32(0xB000F430u) == 0u, "MCH_RICM through KSEG1");
    m.write32(0xB000F440u, 0u);
    m.write32(0xB000F430u, mch_ricm(kRdramSetr, true, 0u, 0u));
    require(m.read32(0xB000F430u) == 0x800000A0u, "busy through the bus");
    cycles += kShortBusy;
    require(m.read32(0xB000F430u) == 0x000000A0u && m.mch().chain().devices()[0].reset_bit, "SETR through the bus");
    device_refused([&] { m.write32(0xB000F410u, 0u); }, "MCH (RDRAM controller) register 0x1000F410 is not modeled");
    device_refused([&] { (void)m.read16(0xB000F480u); }, "register 0x1000F480 is not modeled");
    device_refused([&] { m.write32(0xB000F430u, mch_ricm(0xBu, true, 0u, 0u)); }, "serial opcode 0xB is reserved");
    device_refused([&] { (void)m.read16(0xB000F430u); }, "(32-bit only)");
    m.reset_for_boot(rom);
    require(!m.mch().chain().devices()[0].reset_applied && m.read32(0xB000F430u) == 0u, "boot reset powers RDRAM on");
}

void check_results(const EEState& state, const Memory& memory, const std::string& run) {
    (void)state;
    const auto setr = memory.read32(m20::kM23SetrResult);
    require(setr >= kShortBusy && setr <= kShortBusy + 12u, run + ": SETR busy measured with Count: " + std::to_string(setr));
    const auto swr = memory.read32(m20::kM23SwrResult);
    require(swr >= kLongBusy && swr <= kLongBusy + 12u, run + ": SWR busy measured with Count: " + std::to_string(swr));
    require(memory.read32(m20::kM23FirstRead) == 0x001Fu && memory.read32(m20::kM23SecondRead) == 0x001Fu,
            run + ": SDEV 0x1F answered by device 0, then device 1");
    require(memory.read32(m20::kM23Device0Read) == 0x0080u && memory.read32(m20::kM23Device1Read) == 0x0081u,
            run + ": devices numbered 0 and 1");
    const auto& devices = memory.mch().chain().devices();
    require(devices.size() == 2u, run + ": two devices");
    for (std::size_t n = 0; n < devices.size(); ++n)
        require(devices[n].reset_applied && !devices[n].reset_bit && devices[n].fast_clock &&
                devices[n].init == (0x0080u | n), run + ": RDRAM " + std::to_string(n) + " after InitDev");
}

void check_boot(const std::vector<std::uint8_t>& rom, const m20::Firmware& fw) {
    const auto bios = load_bios_rom0(rom);
    const auto& module = aot_module();
    require(std::string(module.source_kind) == "bios-rom0" && bios.sha256 == module.source_sha256,
            "AOT module provenance");
    require(find_aot_block(module, fw.at("m23_mch")) != nullptr, "M2.3 block translated");
    BootOptions options;
    options.trace = TraceLevel::Full;
    auto run = [&](const AotModule* aot, EEState& state, Memory& memory) {
        prepare_ee_boot(bios, aot, state, memory);
        return run_ee_boot(state, memory, aot, options);
    };
    EEState interpreted_state, hybrid_state;
    Memory interpreted_memory, hybrid_memory;
    const auto interpreted = run(nullptr, interpreted_state, interpreted_memory);
    const auto hybrid = run(&module, hybrid_state, hybrid_memory);
    const auto mmio = fw.at("mmio") + 4u;
    for (const auto* report : {&interpreted, &hybrid}) {
        const std::string name = report == &interpreted ? "interpreter" : "hybrid";
        require(report->outcome == BootOutcome::BlockPoint && report->pc == mmio &&
                report->reason.find("0xB000F000 (physical 0x1000F000, INTC)") != std::string::npos,
                name + " block point at the INTC load: " + report->reason);
        for (const char* line : {"  M W32 v=B000F430 p=1000F430 MCH (RDRAM controller) = 800000A0", // SETR
                                 "  M W32 v=B000F430 p=1000F430 MCH (RDRAM controller) = 800000E0", // CLRR
                                 "  M W32 v=B000F430 p=1000F430 MCH (RDRAM controller) = 80000120", // SETF
                                 "  M W32 v=B000F440 p=1000F440 MCH (RDRAM controller) = 0000001F",
                                 "  M W32 v=B000F430 p=1000F430 MCH (RDRAM controller) = 80210060", // SWR broadcast
                                 "  M W32 v=B000F430 p=1000F430 MCH (RDRAM controller) = 8021005F", // SWR SDEV 0x1F
                                 "  M R32 v=B000F430 p=1000F430 MCH (RDRAM controller) = 8021001F", // SRD busy
                                 "  M R32 v=B000F430 p=1000F430 MCH (RDRAM controller) = 0021001F", // SRD done
                                 "  M R32 v=B000F440 p=1000F440 MCH (RDRAM controller) = 00000081"})
            require(contains(report->trace, line), name + " trace: " + line);
    }
    check_results(interpreted_state, interpreted_memory, "interpreter");
    check_results(hybrid_state, hybrid_memory, "hybrid");
    require(interpreted.aot_instructions == 0u, "interpreter-only run");
    require(contains(hybrid.trace, " aot " + hex8(fw.at("m23_mch")) + " "), "hybrid ran the M2.3 block in AOT");
    require(hybrid.retired == interpreted.retired, "same retired instruction count");
    require(normalized(hybrid.trace) == normalized(interpreted.trace), "hybrid and interpreter traces agree");
    for (unsigned n = 0; n < 32; ++n)
        require(hybrid_state.gpr[n].low == interpreted_state.gpr[n].low &&
                hybrid_state.gpr[n].high == interpreted_state.gpr[n].high, "same GPRs");
    require(hybrid_state.cop0 == interpreted_state.cop0 && hybrid_state.count() == interpreted_state.count(),
            "same COP0 and Count");
    for (const auto address : {m20::kM23SetrResult, m20::kM23SwrResult})
        require(hybrid_memory.read32(address) == interpreted_memory.read32(address), "same MCH busy measurements");
    EEState again_state;
    Memory again_memory;
    require(run(&module, again_state, again_memory).trace == hybrid.trace, "deterministic trace");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: ps2n-boot-m23 <synthetic-rom0-m23.bin>");
        const auto rom = read_file(std::filesystem::path(argv[1]));
        const auto fw = m20::build_firmware(m20::Stage::M23);
        require(rom == fw.rom, "fixture file matches the synthetic M2.3 firmware definition");
        require(m20::build_firmware(m20::Stage::M22).rom != fw.rom, "M2.3 variant differs from the M2.2 firmware");
        check_chain();
        check_mch();
        check_bus(rom);
        check_boot(rom, fw);
        std::cout << "M2.3 boot: Direct RDRAM serial chain, MCH_RICM/MCH_DRD, InitDev and hybrid checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "boot_m23: " << error.what() << '\n';
        return 1;
    }
}
