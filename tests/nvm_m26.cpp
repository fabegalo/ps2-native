// M2.6: external MECHACON NVM images and the S commands that read them, on a
// synthetic NVM authored for this test (no console's data): image validation
// (size, SHA-256, layout, model name, model ID and console model ID against
// the profile, configuration checksums reported), ReadNVM, and the
// OpenConfig / ReadConfig / CloseConfig sequence with its refusals, driven by
// a synthetic IOP program at the reset vector.
#include "bios_program.hpp"
#include "boot_checks.hpp"

#include "ps2native/core.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ps2native;
using namespace boot_checks;
using namespace m20;

constexpr unsigned s1 = 17;

const ConsoleHardware kProfile{"SYNTHETIC", "NVM-BOARD", "0000AC00000000", 0x00004111u,
                               ProfileEvidence::Measured, "nvm_m26 synthetic profile", 0x30u,
                               "nvm_m26 synthetic profile",
                               MechaconInfo{{0u, 6u, 0u, 0u}, {0x11u, 0x22u, 0x33u}, {0x44u, 0x55u}, NvmLayout::Dragon}};
const ConsoleHardware kOtherModel{"SYNTHETIC-B", "NVM-BOARD", "0000AC00000000", 0x00004111u,
                                  ProfileEvidence::Measured, "nvm_m26 synthetic profile", 0x30u,
                                  "nvm_m26 synthetic profile",
                                  MechaconInfo{{0u, 6u, 0u, 0u}, {0x11u, 0x22u, 0x33u}, {0x44u, 0x55u}, NvmLayout::Dragon}};
const ConsoleHardware kNoMechacon{"SYNTHETIC", "NVM-BOARD", "0000AC00000000", 0x00004111u,
                                  ProfileEvidence::Measured, "nvm_m26 synthetic profile", 0x30u,
                                  "nvm_m26 synthetic profile"};

// Every byte distinct from its neighbours (no zero fill), then the fields the
// validation reads and checksummed configuration blocks in area 1.
std::vector<std::uint8_t> synthetic_nvm() {
    std::vector<std::uint8_t> nvm(kNvmBytes);
    for (std::size_t i = 0; i < nvm.size(); ++i) nvm[i] = static_cast<std::uint8_t>(0xA5u ^ (i * 7u));
    const std::string name = "SYNTHETIC";
    for (std::size_t i = 0; i < 16u; ++i) nvm[0x1B0u + i] = i < name.size() ? static_cast<std::uint8_t>(name[i]) : 0u;
    nvm[0x1E1u] = 0x11u;
    nvm[0x1E2u] = 0x22u;
    nvm[0x1E3u] = 0x33u;
    nvm[0x1F0u] = 0x44u;
    nvm[0x1F1u] = 0x55u;
    for (unsigned block = 0; block < 2u; ++block) {
        std::uint8_t sum = 0;
        for (unsigned i = 0; i < 15u; ++i) {
            const auto value = static_cast<std::uint8_t>(0x30u + 16u * block + i);
            nvm[0x2B0u + 16u * block + i] = value;
            sum = static_cast<std::uint8_t>(sum + value);
        }
        nvm[0x2B0u + 16u * block + 15u] = sum;
    }
    return nvm;
}

void check_image() {
    const auto bytes = synthetic_nvm();
    const auto image = load_nvm_image(bytes, kProfile);
    require(image.sha256 == sha256_hex(bytes) && image.model_name == "SYNTHETIC", "NVM identity");
    require(image.word(0x1B0u / 2u) == 0x5953u, "word 0xD8 little-endian: 'S' 'Y'");
    require(image.config_checksum_ok[1].size() == 2u && image.config_checksum_ok[1][0] && image.config_checksum_ok[1][1],
            "area 1 checksums");
    require(image.config_checksum_ok[0].size() == 4u && image.config_checksum_ok[2].size() == 7u, "block counts");
    require(!image.config_checksum_ok[0][0], "patterned block 0 of area 0 has no valid checksum");
    auto upper = image.sha256;
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    (void)load_nvm_image(bytes, kProfile, {upper});

    auto shorter = bytes;
    shorter.pop_back();
    rejects([&] { (void)load_nvm_image(shorter, kProfile); }, "1023 bytes; a MECHACON NVM dump holds 1024");
    rejects([&] { (void)load_nvm_image(bytes, kProfile, {std::string(64, '0')}); }, "does not match the expected");
    rejects([&] { (void)load_nvm_image(bytes, kOtherModel); }, "the NVM belongs to a SYNTHETIC, not to the selected");
    rejects([&] { (void)load_nvm_image(bytes, kNoMechacon); }, "has no MECHACON data in its profile");
    auto other_id = bytes;
    other_id[0x1E2u] = 0x23u;
    rejects([&] { (void)load_nvm_image(other_id, kProfile); }, "model ID 112333 (i.LINK ID bytes 1-3) differs from 112233");
    auto other_console = bytes;
    other_console[0x1F1u] = 0x56u;
    rejects([&] { (void)load_nvm_image(other_console, kProfile); }, "console model ID 4456");
    auto no_name = bytes;
    for (std::size_t i = 0; i < 16u; ++i) no_name[0x1B0u + i] = 'X';
    rejects([&] { (void)load_nvm_image(no_name, kProfile); }, "no NUL-terminated model name");
    auto empty_name = bytes;
    empty_name[0x1B0u] = 0u;
    rejects([&] { (void)load_nvm_image(empty_name, kProfile); }, "no NUL-terminated model name");
}

struct Program {
    std::vector<std::uint8_t> rom;
    std::uint64_t checkpoint; // straight-line code: instructions before the "checkpoint" label
};

// t6 = CDVD base, t2 = IOP RAM (KSEG1); `body` emits the S commands.
template <typename Body>
Program program(Body&& body) {
    Assembler a(0xBFC00000u);
    a.lui(t6, 0xBF40u);
    a.lui(t2, 0xA000u);
    body(a);
    a.label("end");
    a.nop();
    const auto checkpoint = (a.at("checkpoint") - 0xBFC00000u) / 4u;
    const auto words = a.finish();
    Program p{std::vector<std::uint8_t>(4u * 1024u * 1024u), checkpoint};
    for (std::size_t i = 0; i < words.size(); ++i)
        for (unsigned b = 0; b < 4u; ++b) p.rom[4u * i + b] = static_cast<std::uint8_t>(words[i] >> (8u * b));
    return p;
}
void params(Assembler& a, std::initializer_list<std::uint32_t> bytes) {
    for (const auto byte : bytes) {
        a.ori(a0, zero, byte);
        a.sb(a0, 0x2017, t6);
    }
}
void command(Assembler& a, std::uint32_t number) {
    a.ori(a0, zero, number);
    a.sb(a0, 0x2016, t6);
}
// Copies `count` result bytes to IOP RAM at `offset`, then the status register.
void results(Assembler& a, unsigned count, std::int32_t offset) {
    for (unsigned i = 0; i < count; ++i) {
        a.lbu(s1, 0x2018, t6);
        a.nop();
        a.sb(s1, offset + static_cast<std::int32_t>(i), t2);
    }
    a.lbu(s1, 0x2017, t6);
    a.nop();
    a.sb(s1, offset + static_cast<std::int32_t>(count), t2);
}

void run(Iop& iop, const Program& p, const NvmImage* nvm, const ConsoleHardware& hardware = kProfile) {
    iop.reset(p.rom, &hardware, nvm);
    iop.run_until(4000u);
}
void stops_with(const Iop& iop, const std::string& text) {
    require(iop.stopped() && iop.stop_reason()->find(text) != std::string::npos,
            "IOP stop '" + text + "': " + iop.stop_reason().value_or("(running)"));
}

void check_commands() {
    const auto image = load_nvm_image(synthetic_nvm(), kProfile);
    // ReadNVM word 0xD8, then OpenConfig(read, area 1, 2 blocks), two
    // ReadConfig, CloseConfig, and a third ReadConfig with nothing open.
    const auto sequence = program([](Assembler& a) {
        params(a, {0x00u, 0xD8u});
        command(a, 0x0Au);
        results(a, 3u, 0x100);
        params(a, {0x00u, 0x01u, 0x02u});
        command(a, 0x40u);
        results(a, 1u, 0x110);
        command(a, 0x41u);
        results(a, 16u, 0x120);
        command(a, 0x41u);
        results(a, 16u, 0x140);
        command(a, 0x43u);
        results(a, 1u, 0x160);
        a.label("checkpoint");
        command(a, 0x41u);
    });
    Iop iop;
    iop.reset(sequence.rom, &kProfile, &image);
    iop.run_until(sequence.checkpoint);
    require(!iop.stopped(), "S command sequence: " + iop.stop_reason().value_or(""));
    auto byte = [&](std::uint32_t offset) { return static_cast<std::uint8_t>(iop.ee_read(offset, 1u)); };
    require(byte(0x100u) == 0u && byte(0x101u) == 0x59u && byte(0x102u) == 0x53u && byte(0x103u) == 0x40u,
            "ReadNVM: status, high, low, then no result left");
    require(byte(0x110u) == 0u && byte(0x111u) == 0x40u, "OpenConfig status 0");
    for (unsigned block = 0; block < 2u; ++block)
        for (unsigned i = 0; i < 16u; ++i)
            require(byte(0x120u + 0x20u * block + i) == image.config_block(1u, block)[i], "ReadConfig raw block bytes");
    require(byte(0x130u) == 0x40u && byte(0x150u) == 0x40u, "16 result bytes per ReadConfig");
    require(byte(0x160u) == 0u, "CloseConfig status 0");
    iop.run_until(4000u);
    stops_with(iop, "ReadConfig without an open configuration area");

    struct Case {
        std::vector<std::uint32_t> params;
        std::uint32_t command;
        unsigned reads; // ReadConfig commands after it
        std::string stop;
    };
    const std::vector<Case> refused{
        {{0x00u, 0x01u, 0x02u}, 0x40u, 3u, "ReadConfig past the 2 blocks opened"},
        {{0x01u, 0x01u, 0x02u}, 0x40u, 0u, "writing the configuration is not modeled"},
        {{0x00u, 0x01u, 0x03u}, 0x40u, 0u, "3 blocks requested, area 1 holds 2"},
        {{0x00u, 0x00u, 0x05u}, 0x40u, 0u, "5 blocks requested, area 0 holds 4"},
        {{0x00u, 0x03u, 0x01u}, 0x40u, 0u, "parameters outside the documented OpenConfig form"},
        {{0x02u, 0x00u}, 0x0Au, 0u, "NVM word address 0x0200 beyond the 512-word EEPROM"},
        {{0x00u}, 0x0Au, 0u, "ReadNVM takes a 2-byte word address"},
        {{}, 0x42u, 0u, "this MECHACON command is not modeled"},
    };
    for (const auto& c : refused) {
        const auto p = program([&](Assembler& a) {
            for (const auto byte_value : c.params) params(a, {byte_value});
            command(a, c.command);
            for (unsigned i = 0; i < c.reads; ++i) {
                results(a, i == 0u ? 1u : 16u, 0x100);
                command(a, 0x41u);
            }
            a.label("checkpoint");
        });
        Iop refused_iop;
        run(refused_iop, p, &image);
        stops_with(refused_iop, c.stop);
    }

    // Without an NVM image the reads stop; OpenConfig needs the profile's layout.
    const auto open_read = program([](Assembler& a) {
        params(a, {0x00u, 0x01u, 0x02u});
        command(a, 0x40u);
        results(a, 1u, 0x100);
        command(a, 0x41u);
        a.label("checkpoint");
    });
    Iop no_nvm;
    run(no_nvm, open_read, nullptr);
    stops_with(no_nvm, "no NVM image is supplied (--nvm)");
    Iop no_layout;
    run(no_layout, open_read, nullptr, kNoMechacon);
    stops_with(no_layout, "no console profile with MECHACON data");
    const auto read_nvm = program([](Assembler& a) {
        params(a, {0x00u, 0x10u});
        command(a, 0x0Au);
        a.label("checkpoint");
    });
    Iop no_nvm_word;
    run(no_nvm_word, read_nvm, nullptr);
    stops_with(no_nvm_word, "ReadNVM returns this console's MECHACON NVM");

    // A reset forgets the open area and pending results.
    Iop again;
    again.reset(sequence.rom, &kProfile, &image);
    again.run_until(sequence.checkpoint);
    again.reset(read_nvm.rom, &kProfile, &image);
    again.run_until(read_nvm.checkpoint);
    require(!again.stopped(), "reset clears the MECHACON state");
}

void check_region_parameters() {
    const auto image=load_nvm_image(synthetic_nvm(),kProfile);
    const auto read=program([](Assembler& a) {
        command(a,0x36u);results(a,15u,0x100);
        a.label("checkpoint");a.lbu(s1,0x101,t2);a.nop();a.addiu(s1,s1,1);
    });
    Iop iop;iop.reset(read.rom,&kProfile,&image);iop.run_until(read.checkpoint);
    require(!iop.stopped() && iop.ee_read(0x100,1)==0 && iop.ee_read(0x10F,1)==0x40u,
            "ReadRegionParams performs a read and drains exactly its 15-byte response");
    for(unsigned n=0;n<8;++n)require(iop.ee_read(0x103u+n,1)==image.bytes[0x180u+n],
        "region record is read from this NVM, preserving every byte without profile or region defaults");
    for(const unsigned offset:{1u,2u,11u,12u,13u,14u})rejects([&]{(void)iop.ee_read(0x100u+offset,1);},"has not been written");
    require(sha256_hex(image.bytes)==image.sha256,"region reads preserve the source EEPROM");
    iop.run_until(read.checkpoint+3u);stops_with(iop,"used while its value is unknown");
    const auto extra=program([](Assembler& a){params(a,{1u});command(a,0x36u);a.label("checkpoint");});
    Iop invalid;run(invalid,extra,&image);stops_with(invalid,"requires no parameter bytes");
    const auto request=program([](Assembler& a){command(a,0x36u);a.label("checkpoint");});
    Iop missing;run(missing,request,nullptr);stops_with(missing,"matching console NVM");
    auto old=kProfile;old.mechacon->version[1]=5u;
    Iop unsupported;run(unsupported,request,&image,old);stops_with(unsupported,"only established for a version-6");
}

void check_boot_plumbing() {
    const auto firmware = build_firmware(Stage::M20);
    const auto bios = load_bios_rom0(firmware.rom, {});
    const auto image = load_nvm_image(synthetic_nvm(), kProfile);
    EEState state;
    Memory memory;
    rejects([&] { prepare_ee_boot(bios, nullptr, state, memory, nullptr, &image); },
            "an NVM image needs the console profile it was validated for");
}
} // namespace

int main() {
    try {
        check_image();
        check_commands();
        check_region_parameters();
        check_boot_plumbing();
        std::cout << "M2.6 NVM checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
