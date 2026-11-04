// M2.8: the video encoder serial bus, its two bus masters, GIF PATH3 PACKED
// A+D packets into the GS general registers, 128-bit stores, and the
// SCPH-70004 console profiles, on synthetic inputs only (no Sony content).
// Each test drives the device registers the way the firmware does and checks
// the transactions that reach the slave, the refusals for what is not
// modeled, and that no read value is ever invented without an explicit
// diagnostic assumption.
#include "boot_checks.hpp"

#include "ps2native/core.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ps2native;
using namespace boot_checks;

const ConsoleHardware kProfile{"SYNTHETIC", "TEST-BOARD", "0000AC00000000", 0x00004111u,
                               ProfileEvidence::Measured, "m28 synthetic profile", 0x30u, "m28 synthetic profile",
                               MechaconInfo{{0u, 6u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u}, NvmLayout::Dragon}, 0x35u};

// The slave on its own: register pointer, write and read transactions, the
// refusals and the diagnostic assumptions.
void check_bus() {
    VideoEncoderBus bus;
    bus.reset(nullptr);
    rejects([&] { (void)bus.read(0x43u, 1u); }, "before any register pointer was written");
    const std::array<std::uint8_t, 1> pointer{0x7Eu};
    bus.write(0x42u, pointer);
    rejects([&] { (void)bus.read(0x43u, 1u); }, "register 0x7E read: its value has not been measured");
    rejects([&] { (void)bus.read(0x43u, 1u); }, "--assume-video-encoder=0x7E:VALUE");
    rejects([&] { (void)bus.read(0x43u, 2u); }, "only single-byte reads");
    rejects([&] { (void)bus.read(0x45u, 1u); }, "no device other than the video encoder");
    rejects([&] { bus.write(0x44u, pointer); }, "no device other than the video encoder");
    const std::array<std::uint8_t, 3> three{0xB1u, 0u, 0u};
    rejects([&] { bus.write(0x42u, three); }, "auto-increment is not established");
    rejects([&] { bus.write(0x42u, std::span<const std::uint8_t>{}); }, "write of 0 byte(s)");
    const std::array<std::uint8_t, 2> value{0xB1u, 0x00u};
    bus.write(0x42u, value);
    require(bus.written(0xB1u) == 0u && !bus.written(0x7Eu), "written register values");
    require(bus.transactions() == std::vector<std::string>{"write 0x42 reg 0x7E", "write 0x42 reg 0xB1 = 0x00"},
            "bus transaction log");

    DiagnosticAssumptions assumed;
    assumed.video_encoder_registers = {{0x7Eu, 0x1Cu}};
    bus.reset(&assumed);
    require(bus.transactions().empty() && !bus.written(0xB1u), "reset clears the log and the written values");
    bus.write(0x42u, pointer);
    require(bus.read(0x43u, 1u) == std::vector<std::uint8_t>{0x1Cu}, "assumed register value");
    require(bus.transactions().back() == "read 0x43 reg 0x7E -> 0x1C (diagnostic assumption)",
            "an assumed read is labelled in the log");
    const std::array<std::uint8_t, 1> other{0x7Fu};
    bus.write(0x42u, other);
    rejects([&] { (void)bus.read(0x43u, 1u); }, "register 0x7F read");
}

// The 16-bit master at 0x1A000000, in the order the ROMVER 0200 kernel uses it.
void check_master_1a() {
    VideoEncoderBus bus;
    DiagnosticAssumptions assumed;
    assumed.video_encoder_registers = {{0x7Eu, 0x1Du}};
    bus.reset(&assumed);
    VideoEncoderMaster master(VideoEncoderMaster::Layout::Halfword1A);
    master.reset();
    rejects([&] { (void)master.read(0x06u, 2u, bus); }, "before it was cleared: power-on value unknown");
    rejects([&] { master.write(0x00u, 2u, 0x81u, bus); }, "the master is not enabled");
    master.write(0x0Au, 2u, 0u, bus);
    master.write(0x0Au, 2u, 1u, bus);
    master.write(0x04u, 2u, 1u, bus);
    master.write(0x08u, 2u, 3u, bus);
    rejects([&] { master.write(0x00u, 2u, 0x81u, bus); }, "no address byte written");
    master.write(0x02u, 2u, 0x42u, bus);
    rejects([&] { master.write(0x00u, 2u, 0x81u, bus); }, "status not cleared");
    master.write(0x06u, 2u, 3u, bus);
    require(master.read(0x06u, 2u, bus) == 0u, "cleared status");
    rejects([&] { master.write(0x00u, 2u, 0x81u, bus); }, "data byte 0 not written");
    master.write(0x10u, 2u, 0x7Eu, bus);
    master.write(0x00u, 2u, 0x81u, bus); // register pointer 0x7E
    require((master.read(0x06u, 2u, bus) & 2u) != 0u, "done bit after the transfer");
    master.write(0x06u, 2u, 3u, bus);
    master.write(0x02u, 2u, 0x43u, bus);
    master.write(0x00u, 2u, 0x81u, bus); // read one byte
    require(master.read(0x10u, 2u, bus) == 0x1Du && (master.read(0x06u, 2u, bus) & 2u) != 0u,
            "received byte in the first data halfword");
    require(bus.transactions() == std::vector<std::string>{"write 0x42 reg 0x7E",
                                                           "read 0x43 reg 0x7E -> 0x1D (diagnostic assumption)"},
            "0x1A000000 transactions");
    master.write(0x06u, 2u, 3u, bus);
    master.write(0x02u, 2u, 0x42u, bus);
    master.write(0x10u, 2u, 0xB1u, bus);
    master.write(0x12u, 2u, 0x00u, bus);
    master.write(0x00u, 2u, 0x82u, bus);
    require(bus.written(0xB1u) == 0u, "two-byte register write");

    rejects([&] { (void)master.read(0x04u, 2u, bus); }, "reading this register back is not modeled");
    rejects([&] { (void)master.read(0x06u, 1u, bus); }, "only 16-bit accesses");
    rejects([&] { master.write(0x00u, 2u, 0x01u, bus); }, "only start commands");
    rejects([&] { master.write(0x00u, 2u, 0x80u, bus); }, "byte count outside 1-8");
    rejects([&] { master.write(0x00u, 2u, 0x89u, bus); }, "byte count outside 1-8");
    rejects([&] { master.write(0x06u, 2u, 4u, bus); }, "only the done/error bits");
    rejects([&] { master.write(0x0Au, 2u, 2u, bus); }, "only bit 0 (enable)");
    rejects([&] { master.write(0x0Cu, 2u, 0u, bus); }, "register not used by the firmware");
    rejects([&] { master.write(0x10u, 2u, 0x100u, bus); }, "data halfwords carry one byte");
    master.write(0x0Au, 2u, 0u, bus);
    rejects([&] { (void)master.read(0x06u, 2u, bus); }, "before it was cleared");
    rejects([&] { (void)master.read(0x10u, 2u, bus); }, "no byte written or received there");
}

// The 8-bit master at 0x1F801900, in the order the ROMVER 0220 kernel uses it.
void check_master_190x() {
    VideoEncoderBus bus;
    DiagnosticAssumptions assumed;
    assumed.video_encoder_registers = {{0x7Eu, 0x1Cu}};
    bus.reset(&assumed);
    VideoEncoderMaster master(VideoEncoderMaster::Layout::Byte190x);
    master.reset();
    master.write(0x18u, 1u, 0u, bus);
    rejects([&] { master.write(0x06u, 1u, 1u, bus); }, "held in reset");
    master.write(0x18u, 2u, 1u, bus);
    for (const std::uint32_t offset : {0x07u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du}) master.write(offset, 1u, 0x10u, bus);
    rejects([&] { master.write(0x06u, 1u, 1u, bus); }, "not cleared (0x1F801908 = 8)");
    master.write(0x08u, 1u, 8u, bus);
    require(master.read(0x08u, 1u, bus) == 0u, "idle, no error");
    rejects([&] { master.write(0x06u, 1u, 1u, bus); }, "no address byte written");
    master.write(0x04u, 1u, 0x42u, bus);
    rejects([&] { master.write(0x06u, 1u, 1u, bus); }, "0x1F801905 other than 0");
    master.write(0x05u, 1u, 0u, bus);
    master.write(0x00u, 1u, 0x7Eu, bus);
    master.write(0x06u, 1u, 1u, bus); // send the pointer
    master.write(0x08u, 1u, 8u, bus);
    rejects([&] { (void)master.read(0x00u, 1u, bus); }, "no received byte is pending");
    master.write(0x06u, 1u, 3u, bus); // receive one byte
    require(master.read(0x08u, 1u, bus) == 0u && master.read(0x00u, 1u, bus) == 0x1Cu, "received byte");
    master.write(0x08u, 1u, 8u, bus);
    master.write(0x00u, 1u, 0xB1u, bus);
    master.write(0x00u, 1u, 0x00u, bus);
    master.write(0x06u, 1u, 0x11u, bus); // checked register write
    require((master.read(0x08u, 1u, bus) & 4u) == 0u && bus.written(0xB1u) == 0u, "checked write acknowledged");
    require(bus.transactions() == std::vector<std::string>{"write 0x42 reg 0x7E",
                                                           "read 0x43 reg 0x7E -> 0x1C (diagnostic assumption)",
                                                           "write 0x42 reg 0xB1 = 0x00"},
            "0x1F801900 transactions");

    master.write(0x00u, 1u, 0x7Fu, bus);
    rejects([&] { master.write(0x06u, 1u, 3u, bus); }, "receive with bytes still queued");
    rejects([&] { master.write(0x06u, 1u, 2u, bus); }, "only commands 0x01, 0x03 and 0x11");
    rejects([&] { master.write(0x08u, 1u, 1u, bus); }, "only 0x08");
    rejects([&] { master.write(0x10u, 1u, 0u, bus); }, "register not used by the firmware");
    rejects([&] { master.write(0x00u, 2u, 0u, bus); }, "only 8-bit accesses");
    rejects([&] { (void)master.read(0x08u, 2u, bus); }, "only 8-bit reads");
    rejects([&] { (void)master.read(0x04u, 1u, bus); }, "reading this register back is not modeled");
    rejects([&] { master.write(0x18u, 1u, 2u, bus); }, "only 0 and 1");
    for (unsigned i = 0; i < 7u; ++i) master.write(0x00u, 1u, i, bus);
    rejects([&] { master.write(0x00u, 1u, 0u, bus); }, "queue depth beyond 8 bytes");
    master.write(0x18u, 1u, 0u, bus);
    rejects([&] { (void)master.read(0x08u, 1u, bus); }, "before it was cleared");
}

// Both masters through the EE register map, with and without assumptions.
void check_ee_routing() {
    EeRegisters registers;
    std::uint64_t ee = 0;
    registers.set_clock(&ee);
    registers.reset(&kProfile);
    registers.write(kVideoEncoderMaster190x + 0x18u, 1u, 0u);
    registers.write(kVideoEncoderMaster190x + 0x18u, 2u, 1u);
    registers.write(kVideoEncoderMaster190x + 0x08u, 1u, 8u);
    registers.write(kVideoEncoderMaster190x + 0x04u, 1u, 0x42u);
    registers.write(kVideoEncoderMaster190x + 0x05u, 1u, 0u);
    registers.write(kVideoEncoderMaster190x + 0x00u, 1u, 0x7Eu);
    registers.write(kVideoEncoderMaster190x + 0x06u, 1u, 1u);
    registers.write(kVideoEncoderMaster190x + 0x08u, 1u, 8u);
    rejects([&] { registers.write(kVideoEncoderMaster190x + 0x06u, 1u, 3u); }, "register 0x7E read");
    require(registers.video_encoder().transactions() == std::vector<std::string>{"write 0x42 reg 0x7E"},
            "no transaction is logged for a refused read");
    rejects([&] { registers.write(kVideoEncoderMaster1A + 0x1Eu, 4u, 0u); }, "crosses the end");

    DiagnosticAssumptions assumed;
    assumed.video_encoder_registers = {{0x7Eu, 0x1Cu}};
    registers.reset(&kProfile, {}, nullptr, &assumed);
    require(registers.video_encoder().transactions().empty(), "reset clears the bus log");
    registers.write(kVideoEncoderMaster1A + 0x0Au, 2u, 1u);
    registers.write(kVideoEncoderMaster1A + 0x06u, 2u, 3u);
    registers.write(kVideoEncoderMaster1A + 0x02u, 2u, 0x42u);
    registers.write(kVideoEncoderMaster1A + 0x10u, 2u, 0x7Eu);
    registers.write(kVideoEncoderMaster1A + 0x00u, 2u, 0x81u);
    registers.write(kVideoEncoderMaster1A + 0x06u, 2u, 3u);
    registers.write(kVideoEncoderMaster1A + 0x02u, 2u, 0x43u);
    registers.write(kVideoEncoderMaster1A + 0x00u, 2u, 0x81u);
    require(registers.read(kVideoEncoderMaster1A + 0x10u, 2u) == 0x1Cu, "assumed ID through the EE map");
}

constexpr std::uint64_t giftag(std::uint32_t nloop, bool eop, unsigned nreg, unsigned flg = 0u) {
    return std::uint64_t{nloop} | (eop ? 1ull << 15 : 0u) | (std::uint64_t{flg} << 58) |
           (std::uint64_t{nreg & 0xFu} << 60);
}
constexpr std::uint64_t kAllAd = 0xEEEEEEEEEEEEEEEEull;

// GIF PATH3 through 128-bit stores to the FIFO.
void check_gif() {
    EeRegisters registers;
    std::uint64_t ee = 0;
    registers.set_clock(&ee);
    registers.reset(&kProfile);
    require(registers.read(kGifStat, 4u) == 0u, "GIF_STAT idle after reset");
    // NLOOP 2, NREG 2: four A+D writes. The packet ends with the last one.
    registers.write128(kGifFifo, giftag(2u, true, 2u), kAllAd);
    require((registers.read(kGifStat, 4u) & 0xE00u) == 0xE00u, "PATH3 active while the packet expects data");
    registers.write128(kGifFifo, 0x1234u, 0x06u);         // TEX0_1
    registers.write128(kGifFifo, 0x0000000100000002ull, 0x52u); // TRXREG 2x1
    registers.write128(kGifFifo, 0x00010001u, 0x4Cu);     // FRAME_1
    require(registers.gif().packets().empty(), "packet still open");
    registers.write128(kGifFifo, 0x55u, 0x18u);           // XYOFFSET_1
    require(registers.read(kGifStat, 4u) == 0u && registers.gif().packets() ==
                std::vector<std::string>{"PATH3 packet: 4 register/image writes"},
            "packet complete");
    require(registers.gs().general(0x06u) == 0x1234u && registers.gs().general(0x18u) == 0x55u &&
                registers.gs().general(0x52u) == 0x0000000100000002ull,
            "GS general registers written through A+D");

    // NREG 0 means 16; a chained tag without EOP keeps the packet open.
    registers.write128(kGifFifo, giftag(1u, false, 0u), kAllAd);
    for (unsigned i = 0; i < 16u; ++i) registers.write128(kGifFifo, i, 0x00u);
    require(registers.gif().packets().size() == 1u && registers.read(kGifStat, 4u) != 0u, "no EOP: still open");
    registers.write128(kGifFifo, giftag(0u, true, 1u), kAllAd); // empty closing tag
    require(registers.gif().packets().back() == "PATH3 packet: 16 A+D writes", "EOP tag closes the packet");
    require(registers.gs().general(0x00u) == 15u, "last PRIM value");

    registers.write128(kGifFifo, giftag(1u, true, 1u) | (1ull << 46), kAllAd);
    require(registers.gs().general(0) == 0u, "PRE writes PRIM");
    registers.write128(kGifFifo, 0u, 0x01u);
    rejects([&] { registers.write128(kGifFifo, giftag(1u, true, 1u, 1u), kAllAd); }, "invalid register descriptor");
    rejects([&] { registers.write128(kGifFifo, giftag(1u, true, 1u) | (1ull << 20), kAllAd); }, "bits 16-45");
    rejects([&] { registers.write128(kGifFifo, giftag(1u, true, 2u), 0xBEu); }, "invalid register descriptor");
    registers.write128(kGifFifo, giftag(1u, true, 1u), kAllAd);
    rejects([&] { registers.write128(kGifFifo, 0u, 0x105u); }, "A+D bits 72-127");
    rejects([&] { registers.write128(kGifFifo, 0u, 0x05u); }, "has not been written since reset");
    const auto revision = registers.gs().vram().revision();
    registers.write128(kGifFifo, 0u, 0x54u);
    require(registers.gs().vram().revision() == revision, "inactive HWREG is a documented NOP");
    registers.write128(kGifFifo, giftag(1u, true, 1u), kAllAd);
    rejects([&] { registers.write128(kGifFifo, 0u, 0x63u); }, "no GS general register");
    rejects([&] { registers.write128(kGifFifo, 0u, 0x30u); }, "no GS general register");
    rejects([&] { registers.write128(kGifFifo, 0x0000000100000002ull, 0x53u); }, "TRXDIR bits above 1");
    rejects([&] { registers.write128(kGifFifo, 0u, 0x53u); }, "has not been written since reset");
    registers.write128(kGifFifo, 3u, 0x53u); // XDIR 3: off
    registers.write128(kGifFifo, giftag(2u, true, 1u), kAllAd);
    registers.write128(kGifFifo, 0u, 0x52u); // TRXREG 0x0
    registers.write128(kGifFifo, 0u, 0x53u); // empty host->local transmission (hypothesis)
    require(registers.gs().general(0x53u) == 0u, "empty transmission accepted");

    // Reset, stop and mask.
    registers.write128(kGifFifo, giftag(1u, true, 1u), kAllAd);
    registers.write(kGifCtrl, 4u, 1u);
    require(registers.read(kGifStat, 4u) == 0u, "GIF_CTRL reset drops the packet");
    registers.write(kGifMode, 4u, 5u);
    require(registers.read(kGifStat, 4u) == 5u, "GIF_MODE M3R and IMT in GIF_STAT");
    rejects([&] { registers.write128(kGifFifo, giftag(1u, true, 1u), kAllAd); }, "PATH3 masked");
    registers.write(kGifMode, 4u, 0u);
    registers.write(kGifCtrl, 4u, 8u);
    require(registers.read(kGifStat, 4u) == 8u, "PSE");
    rejects([&] { registers.write128(kGifFifo, giftag(1u, true, 1u), kAllAd); }, "stops the GIF");
    rejects([&] { registers.write(kGifMode, 4u, 2u); }, "only bits 0 and 2");
    rejects([&] { registers.write(kGifFifo, 8u, 0u); }, "the FIFO takes quadwords");
    rejects([&] { (void)registers.read(kGifStat, 2u); }, "GIF_STAT read of 2 byte(s)");
    rejects([&] { registers.write128(0x10003020u, 0u, 0u); }, "128-bit store: no register model");

    // A GS CSR reset clears the general registers (hypothesis).
    registers.write(kGifCtrl, 4u, 0u);
    registers.write128(kGifFifo, giftag(1u, true, 1u), kAllAd);
    registers.write128(kGifFifo, 0x77u, 0x06u);
    require(registers.gs().general(0x06u) == 0x77u, "TEX0 written");
    registers.write(0x12001000u, 8u, 0x200u); // CSR RESET
    require(!registers.gs().general(0x06u), "CSR reset clears the general registers");
}

// SQ to RAM keeps the two doublewords in order; to a register without a
// 128-bit model it is refused.
void check_write128() {
    Memory memory;
    memory.write128(0x00100000u, 0x1122334455667788ull, 0x99AABBCCDDEEFF00ull);
    require(memory.read64(0x00100000u) == 0x1122334455667788ull && memory.read64(0x00100008u) == 0x99AABBCCDDEEFF00ull,
            "128-bit store to RAM");
    memory.write128(0x80100010u, 1u, 2u); // kseg0 alias
    require(memory.read64(0x00100010u) == 1u && memory.read64(0x00100018u) == 2u, "128-bit store through kseg0");
}

void check_profiles() {
    const auto* first = find_console_hardware("SCPH-70004:GH-032-11");
    const auto* second = find_console_hardware("SCPH-70004:GH-032-32");
    require(first != nullptr && second != nullptr, "both SCPH-70004 boards are known");
    require(find_console_hardware("SCPH-70004") == nullptr, "SCPH-70004 alone is ambiguous (two boards)");
    for (const auto* profile : {first, second}) {
        require(profile->romver == "0200EC20040614" && profile->d_enabler == 0x3101u &&
                    profile->evidence == ProfileEvidence::SameChipRevision,
                "SCPH-70004 ROMVER and D_ENABLER (same chip revision, hypothesis)");
        require(profile->mechacon && profile->mechacon->model_id == std::array<std::uint8_t, 3>{0x27u, 0xD4u, 0x20u} &&
                    profile->mechacon->con_model_id == std::array<std::uint8_t, 2>{0x26u, 0xD4u} &&
                    profile->mechacon->layout == NvmLayout::Dragon,
                "SCPH-70004 model IDs");
    }
    require(first->mechacon->version[2] == 2u && second->mechacon->version[2] == 4u, "MECHACON 6.2 and 6.4");

    // A synthetic NVM with the SCPH-70004 identification fields passes; the
    // same image under another model is refused.
    std::vector<std::uint8_t> nvm(kNvmBytes);
    for (std::size_t i = 0; i < nvm.size(); ++i) nvm[i] = static_cast<std::uint8_t>(0x5Au ^ (i * 3u));
    const std::string name = "SCPH-70004";
    for (std::size_t i = 0; i < 16u; ++i) nvm[0x1B0u + i] = i < name.size() ? static_cast<std::uint8_t>(name[i]) : 0u;
    nvm[0x1E1u] = 0x27u;
    nvm[0x1E2u] = 0xD4u;
    nvm[0x1E3u] = 0x20u;
    nvm[0x1F0u] = 0x26u;
    nvm[0x1F1u] = 0xD4u;
    require(load_nvm_image(nvm, *first).model_name == "SCPH-70004", "SCPH-70004 NVM accepted");
    rejects([&] { (void)load_nvm_image(nvm, *find_console_hardware("SCPH-77008")); }, "not to the selected");
}
} // namespace

int main() {
    try {
        check_bus();
        check_master_1a();
        check_master_190x();
        check_ee_routing();
        check_gif();
        check_write128();
        check_profiles();
        std::cout << "M2.8 checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
