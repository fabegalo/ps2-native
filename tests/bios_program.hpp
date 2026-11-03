// Synthetic, independently-authored boot firmware in the PS2 ROM0 container
// format (ROMDIR, EXTINFO, ROMVER). It contains no Sony code or data: the
// reset-vector program below only exercises the boot infrastructure (COP0,
// TLB, scratchpad, CACHE, ROM -> RAM copy, ERET, hybrid AOT/interpreter) and
// ends on an EE device access that is not modeled yet. Shared by the fixture
// generator and the M2.0 tests so that addresses and expectations live in one place.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace m20 {
constexpr std::uint32_t kResetVector = 0xBFC00000u;
constexpr std::uint32_t kRomBytes = 4u * 1024u * 1024u;
constexpr std::uint32_t kResetSize = 0x1000u;     // RESET file (code), ROMDIR follows
constexpr std::uint32_t kPayloadRam = 0x80200000u; // KSEG0 address the payload runs from
constexpr std::uint32_t kChecksumRam = 0x80000110u;
constexpr std::uint32_t kPayloadResultRam = 0x80000100u;
constexpr std::uint32_t kKusegWindow = 0x00080000u; // TLB entry 1 -> physical 0x00100000
constexpr std::uint32_t kScratchpadBase = 0x70000000u; // TLB entry 0 (S bit)
constexpr std::uint32_t kStatusAfterEret = 0x10400000u; // CU0 | BEV
constexpr char kRomver[] = "0100XD20261103";          // 1.00, Test region, Devel, 2026-11-03
constexpr std::array<std::uint32_t, 16> kTable{
    0x01234567u, 0x89ABCDEFu, 0x0F1E2D3Cu, 0x4B5A6978u, 0x8796A5B4u, 0xC3D2E1F0u, 0x00000001u, 0xFFFFFFFFu,
    0x13579BDFu, 0x2468ACE0u, 0x7FFFFFFFu, 0x80000000u, 0xDEADBEEFu, 0x0BADF00Du, 0x00C0FFEEu, 0x5A5A5A5Au};
inline std::uint32_t table_checksum() {
    std::uint32_t sum = 0;
    for (const auto word : kTable) sum += word;
    return sum;
}

// M2.1 variant: results of the MMI / Count / timer block, stored in RAM.
constexpr std::uint32_t kM21ResultRam = 0x80000200u;
constexpr std::uint32_t kM21PaddResult = kM21ResultRam;         // SQ of PADDUW (16 bytes)
constexpr std::uint32_t kM21MulDivResults = kM21ResultRam + 0x20u; // 12 doublewords (SD)
constexpr unsigned kM21MulDivCount = 12u;
constexpr std::uint32_t kM21CountResult = kM21ResultRam + 0x80u;   // Count after 5 NOPs
constexpr std::uint32_t kM21HblankResult = kM21ResultRam + 0x84u;  // Count over two HBLANK edges
constexpr std::uint32_t kM21T0Result = kM21ResultRam + 0x88u;      // T0_COUNT at the last edge
constexpr std::uint32_t kM21BoardIdResult = kM21ResultRam + 0x8Cu; // LBU of the tool board id
constexpr std::uint32_t kM21ScratchpadAlias = 0x70010000u;          // TLB entry 2: S bit, PageMask 0

// M2.2 variant: GS privileged registers, the GS line clock, DEV9, EE SIO, COP1 transfers.
constexpr std::uint32_t kM22ResultRam = 0x80000300u;
constexpr std::uint32_t kM22FirstLineResult = kM22ResultRam;        // Count from the sync restart to HBLANK
constexpr std::uint32_t kM22T0Result = kM22ResultRam + 4u;          // T0_COUNT at that edge
constexpr std::uint32_t kM22FpuResult = kM22ResultRam + 8u;         // SWC1 of the word LWC1 loaded
// Synthetic SMODE1 values: CMOD 2 (NTSC) with PRST | SINT (sync held), then CMOD 2 alone.
constexpr std::uint64_t kM22Smode1Held = 0x34000u;
constexpr std::uint64_t kM22Smode1Run = 0x4000u;
constexpr std::uint64_t kM22Synch1 = 0x1234u;
constexpr std::uint64_t kM22Synch2 = 0x0567u;
constexpr std::uint64_t kM22Syncv = 0x0000002400000789ull;
constexpr std::uint64_t kM22Smode2 = 3u;
constexpr std::uint64_t kM22Srfsh = 8u;
constexpr char kM22SioText[] = "M2.2 SIO\n";

// M2.3: Algorithm InitDev (Rambus Direct RDRAM data sheet) for the two RDRAMs
// through MCH_RICM/MCH_DRD, polling bit 31 like the BIOS serial helpers.
constexpr std::uint32_t kM23ResultRam = 0x80000400u;
constexpr std::uint32_t kM23SetrResult = kM23ResultRam;          // Count over the SETR transaction
constexpr std::uint32_t kM23SwrResult = kM23ResultRam + 4u;      // Count over the broadcast SWR of INIT
constexpr std::uint32_t kM23FirstRead = kM23ResultRam + 8u;      // SRD INIT of SDEV 0x1F before numbering
constexpr std::uint32_t kM23SecondRead = kM23ResultRam + 0xCu;   // the same after device 0 got SDEVID 0
constexpr std::uint32_t kM23Device0Read = kM23ResultRam + 0x10u; // SRD INIT of SDEV 0
constexpr std::uint32_t kM23Device1Read = kM23ResultRam + 0x14u; // SRD INIT of SDEV 1
// SETR-to-CLRR wait, in Count cycles from the SETR store: enough for the SETR
// transaction and the 16 SCK cycles after it.
constexpr std::uint32_t kM23ClrrWait = 9600u;

// M2.4: the timing/test registers the BIOS writes before enumerating (64/72-Mbit
// layouts), broadcast after InitDev and read back per device.
constexpr std::uint32_t kM24ResultRam = 0x80000500u;
constexpr std::uint32_t kM24TcycleRead = kM24ResultRam;     // SRD TCYCLE of SDEV 1
constexpr std::uint32_t kM24TfrmRead = kM24ResultRam + 4u;  // SRD TFRM of SDEV 0
constexpr std::uint32_t kM24TrdlyRead = kM24ResultRam + 8u; // SRD TRDLY of SDEV 1
constexpr std::uint32_t kM24Test77Read = kM24ResultRam + 0xCu; // SRD TEST77 of SDEV 0
constexpr std::uint16_t kM24Tcycle = 0x0027u;               // 39 x 64 ps = 2.5 ns (data sheet example)
constexpr std::uint16_t kM24Tfrm = 0x0007u;                 // 7 tCYCLE (data sheet value)
constexpr std::uint16_t kM24Trdly = 0x0002u;

enum class Stage { M20, M21, M22, M23, M24 };

enum Reg : unsigned {
    zero = 0, at = 1, v0 = 2, v1 = 3, a0 = 4, a1 = 5, a2 = 6, a3 = 7, t0 = 8, t1 = 9, t2 = 10, t3 = 11,
    t4 = 12, t5 = 13, t6 = 14, t7 = 15, s0 = 16, t9 = 25, k0 = 26, ra = 31
};
enum Cp0 : unsigned {
    Index = 0, EntryLo0 = 2, EntryLo1 = 3, PageMask = 5, Count = 9, EntryHi = 10, Status = 12, Cause = 13,
    PRid = 15, TagLo = 28, TagHi = 29, ErrorEPC = 30
};

class Assembler {
public:
    explicit Assembler(std::uint32_t base) : base_(base) {}
    [[nodiscard]] std::uint32_t here() const { return base_ + 4u * static_cast<std::uint32_t>(words_.size()); }
    void label(const std::string& name) {
        if (!labels_.emplace(name, here()).second) throw std::runtime_error("Duplicate label " + name);
    }
    [[nodiscard]] std::uint32_t at(const std::string& name) const {
        const auto found = labels_.find(name);
        if (found == labels_.end()) throw std::runtime_error("Unknown M2.0 label " + name);
        return found->second;
    }
    void raw(std::uint32_t word) { words_.push_back(word); }
    void nop() { raw(0u); }
    void i(unsigned op, unsigned rs, unsigned rt, std::uint32_t imm) {
        raw((op << 26) | (rs << 21) | (rt << 16) | (imm & 0xffffu));
    }
    void r(unsigned rs, unsigned rt, unsigned rd, unsigned sa, unsigned funct) {
        raw((rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | funct);
    }
    void lui(unsigned rt, std::uint32_t imm) { i(0x0fu, 0u, rt, imm); }
    void addiu(unsigned rt, unsigned rs, std::uint32_t imm) { i(0x09u, rs, rt, imm); }
    void ori(unsigned rt, unsigned rs, std::uint32_t imm) { i(0x0du, rs, rt, imm); }
    void andi(unsigned rt, unsigned rs, std::uint32_t imm) { i(0x0cu, rs, rt, imm); }
    void sltiu(unsigned rt, unsigned rs, std::uint32_t imm) { i(0x0bu, rs, rt, imm); }
    void lw(unsigned rt, std::int32_t offset, unsigned base) { i(0x23u, base, rt, static_cast<std::uint32_t>(offset)); }
    void lbu(unsigned rt, std::int32_t offset, unsigned base) { i(0x24u, base, rt, static_cast<std::uint32_t>(offset)); }
    void sb(unsigned rt, std::int32_t offset, unsigned base) { i(0x28u, base, rt, static_cast<std::uint32_t>(offset)); }
    void sh(unsigned rt, std::int32_t offset, unsigned base) { i(0x29u, base, rt, static_cast<std::uint32_t>(offset)); }
    void lwc1(unsigned ft, std::int32_t offset, unsigned base) { i(0x31u, base, ft, static_cast<std::uint32_t>(offset)); }
    void swc1(unsigned ft, std::int32_t offset, unsigned base) { i(0x39u, base, ft, static_cast<std::uint32_t>(offset)); }
    void sw(unsigned rt, std::int32_t offset, unsigned base) { i(0x2bu, base, rt, static_cast<std::uint32_t>(offset)); }
    void sd(unsigned rt, std::int32_t offset, unsigned base) { i(0x3fu, base, rt, static_cast<std::uint32_t>(offset)); }
    void addu(unsigned rd, unsigned rs, unsigned rt) { r(rs, rt, rd, 0u, 0x21u); }
    void dsll32(unsigned rd, unsigned rt, unsigned sa) { r(0u, rt, rd, sa, 0x3cu); }
    void jr(unsigned rs) { r(rs, 0u, 0u, 0u, 0x08u); }
    void jalr(unsigned rs) { r(rs, 0u, ra, 0u, 0x09u); }
    void sync_p() { raw(0x0000040Fu); }
    void brk() { raw(0x0000000Du); }
    // Absolute 32-bit address with %hi/%lo carry: LUI + ADDIU.
    void la(unsigned rt, std::uint32_t address) {
        lui(rt, (address + 0x8000u) >> 16);
        addiu(rt, rt, address & 0xffffu);
    }
    void mfc0(unsigned rt, unsigned rd) { raw((0x10u << 26) | (rt << 16) | (rd << 11)); }
    void mtc0(unsigned rt, unsigned rd) { raw((0x10u << 26) | (0x04u << 21) | (rt << 16) | (rd << 11)); }
    void tlbwi() { raw(0x42000002u); }
    void tlbp() { raw(0x42000008u); }
    void eret() { raw(0x42000018u); }
    void cache(unsigned op, std::int32_t offset, unsigned base) { i(0x2fu, base, op, static_cast<std::uint32_t>(offset)); }
    void lq(unsigned rt, std::int32_t offset, unsigned base) { i(0x1eu, base, rt, static_cast<std::uint32_t>(offset)); }
    void sq(unsigned rt, std::int32_t offset, unsigned base) { i(0x1fu, base, rt, static_cast<std::uint32_t>(offset)); }
    // SPECIAL MULT/DIV and MFLO/MFHI; MMI (opcode 0x1C) pipeline-1 forms and MMI1 (funct 0x28).
    void mult(unsigned rd, unsigned rs, unsigned rt) { r(rs, rt, rd, 0u, 0x18u); }
    void mflo(unsigned rd) { r(0u, 0u, rd, 0u, 0x12u); }
    void mfhi(unsigned rd) { r(0u, 0u, rd, 0u, 0x10u); }
    void mmi(unsigned rs, unsigned rt, unsigned rd, unsigned sa, unsigned funct) {
        raw((0x1cu << 26) | (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | funct);
    }
    void padduw(unsigned rd, unsigned rs, unsigned rt) { mmi(rs, rt, rd, 0x10u, 0x28u); }
    void multu1(unsigned rd, unsigned rs, unsigned rt) { mmi(rs, rt, rd, 0u, 0x19u); }
    void div1(unsigned rs, unsigned rt) { mmi(rs, rt, 0u, 0u, 0x1au); }
    void divu1(unsigned rs, unsigned rt) { mmi(rs, rt, 0u, 0u, 0x1bu); }
    void mfhi1(unsigned rd) { mmi(0u, 0u, rd, 0u, 0x10u); }
    void mthi1(unsigned rs) { mmi(rs, 0u, 0u, 0u, 0x11u); }
    void mflo1(unsigned rd) { mmi(0u, 0u, rd, 0u, 0x12u); }
    void mtlo1(unsigned rs) { mmi(rs, 0u, 0u, 0u, 0x13u); }
    void bne(unsigned rs, unsigned rt, const std::string& target) { branch(0x05u, rs, rt, target); }
    void bltz(unsigned rs, const std::string& target) { branch(0x01u, rs, 0u, target); }
    void beq(unsigned rs, unsigned rt, const std::string& target) { branch(0x04u, rs, rt, target); }
    void branch(unsigned op, unsigned rs, unsigned rt, const std::string& target) {
        fixups_.emplace_back(words_.size(), target);
        i(op, rs, rt, 0u);
    }
    std::vector<std::uint32_t> finish() {
        for (const auto& [index, target] : fixups_) {
            const auto site = base_ + 4u * static_cast<std::uint32_t>(index);
            const auto delta = static_cast<std::int64_t>(at(target)) - static_cast<std::int64_t>(site + 4u);
            if (delta % 4 != 0 || delta / 4 < -32768 || delta / 4 > 32767)
                throw std::runtime_error("M2.0 branch out of range");
            words_[index] |= static_cast<std::uint32_t>(delta / 4) & 0xffffu;
        }
        return words_;
    }

private:
    std::uint32_t base_;
    std::vector<std::uint32_t> words_;
    std::map<std::string, std::uint32_t> labels_;
    std::vector<std::pair<std::size_t, std::string>> fixups_;
};

struct File {
    std::string name;
    std::vector<std::uint8_t> data;
    std::vector<std::uint8_t> extinfo;
};
struct Firmware {
    std::vector<std::uint8_t> rom;
    std::map<std::string, std::uint32_t> labels; // reset-code labels
    std::uint32_t romdir_offset{};
    std::uint32_t payload_rom{};
    std::uint32_t table_rom{};
    std::uint32_t payload_words{};
    [[nodiscard]] std::uint32_t at(const std::string& name) const { return labels.at(name); }
};

inline std::vector<std::uint8_t> le_words(const std::vector<std::uint32_t>& words) {
    std::vector<std::uint8_t> bytes;
    for (const auto word : words)
        for (unsigned n = 0; n < 4; ++n) bytes.push_back(static_cast<std::uint8_t>(word >> (8u * n)));
    return bytes;
}
// EXTINFO field: value (u16), data length (u8), type (u8), data padded to 4 bytes.
inline void field(std::vector<std::uint8_t>& out, std::uint8_t type, std::uint16_t value,
                  const std::vector<std::uint8_t>& data) {
    auto padded = data;
    while (padded.size() % 4u != 0u) padded.push_back(0u);
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(padded.size()));
    out.push_back(type);
    out.insert(out.end(), padded.begin(), padded.end());
}
inline std::vector<std::uint8_t> text(const std::string& value) {
    std::vector<std::uint8_t> bytes(value.begin(), value.end());
    bytes.push_back(0u);
    return bytes;
}
inline std::uint32_t align16(std::uint32_t value) { return (value + 15u) & ~15u; }

inline std::vector<std::uint32_t> payload_code() {
    Assembler p(kPayloadRam);
    p.lui(t0, kScratchpadBase >> 16);
    p.lw(t1, 0, t0);              // checksum stored through the scratchpad mapping
    p.dsll32(v1, t1, 0);
    p.ori(v1, v1, 0x1234u);
    p.jr(ra);
    p.nop();
    return p.finish();
}

// M2.1, still at error level (KSEG0/KSEG1 only): PADDUW on two SYNDATA quads,
// the MMI pipeline-1 multiply/divide group next to pipeline 0, COP0 Count over
// five NOPs, two HBLANK edges of EE timer 0 measured with Count (as the real
// BIOS calibrates its clock), the 0x1000F500 store the real BIOS performs and
// its LBU of the tool board id.
inline void emit_m21_core(Assembler& a, std::uint32_t table_rom) {
    const auto low = [](std::uint32_t address) { return static_cast<std::int32_t>(address & 0xffffu); };
    a.label("m21_mmi");
    a.la(a0, table_rom);
    a.lq(t0, 0, a0);
    a.lq(t1, 16, a0);
    a.padduw(t2, t0, t1);
    a.lui(a1, 0x8000u);
    a.sq(t2, low(kM21PaddResult), a1);
    a.addiu(t0, zero, 0xFFF9u);      // -7
    a.addiu(t1, zero, 3u);
    a.mult(t3, t0, t1);              // pipeline 0: LO -21, HI -1
    a.multu1(t4, t0, t1);            // 0xFFFFFFF9 * 3 = 0x2_FFFFFFEB in LO1/HI1
    a.mfhi1(t5);
    a.div1(t0, t1);                  // -7 / 3: LO1 -2, HI1 -1
    a.mflo1(t6);
    a.mfhi1(t7);
    a.mflo(a2);                      // pipeline 0 untouched by pipeline 1
    a.mfhi(a3);
    unsigned slot = 0;
    for (const auto reg : {t3, t4, t5, t6, t7, a2, a3})
        a.sd(reg, low(kM21MulDivResults) + 8 * static_cast<std::int32_t>(slot++), a1);
    a.divu1(t0, zero);               // division by zero: LO1 -1, HI1 dividend
    a.mflo1(t6);
    a.mfhi1(t7);
    a.mtlo1(t1);
    a.mthi1(t0);
    a.mflo1(t2);
    a.mfhi1(t3);
    a.lui(t0, 0x8000u);              // INT32_MIN / -1: LO1 INT32_MIN, HI1 0
    a.addiu(t1, zero, 0xFFFFu);
    a.div1(t0, t1);
    a.mflo1(t4);
    for (const auto reg : {t6, t7, t2, t3, t4})
        a.sd(reg, low(kM21MulDivResults) + 8 * static_cast<std::int32_t>(slot++), a1);
    a.label("m21_count");
    a.mtc0(zero, Count);
    for (unsigned n = 0; n < 5u; ++n) a.nop();
    a.mfc0(t0, Count);               // 6: MTC0 and the five NOPs
    a.sw(t0, low(kM21CountResult), a1);
    a.lui(a0, 0xB000u);
    a.addiu(t0, zero, 0x83u);
    a.sw(t0, 0x10, a0);              // T0_MODE: HBLANK clock, counting
    a.lw(t1, 0, a0);
    a.label("m21_edge");
    a.lw(t2, 0, a0);                 // wait for an HBLANK edge
    a.beq(t2, t1, "m21_edge");
    a.nop();
    a.mtc0(zero, Count);
    a.addiu(t3, t2, 2u);
    a.label("m21_edges");
    a.lw(t1, 0, a0);                 // two more edges
    a.bne(t1, t3, "m21_edges");
    a.nop();
    a.mfc0(t4, Count);
    a.sw(t4, low(kM21HblankResult), a1);
    a.sw(t1, low(kM21T0Result), a1);
    a.addiu(t0, zero, 0xFFFFu);
    a.lui(t5, 0xB001u);
    a.sw(t0, -0x0B00, t5);           // 0x1000F500 <- 0xFFFFFFFF
    a.lui(t5, 0xBF80u);
    a.lbu(t0, 0x3204, t5);           // tool board id, as the RDRAM module reads it
    a.sw(t0, low(kM21BoardIdResult), a1);
}
// M2.2, still at error level: T0 stopped before the GS is retimed, a CSR
// reset, SMODE1 with sync held (PRST | SINT), the sync/refresh registers, the
// CSR event acknowledge bits, sync restarted (CMOD 2) and the first HBLANK
// measured with Count; a DEV9 latch pulse; text through the EE SIO TX FIFO
// (waiting on SIO_ISR like a polled putc); LWC1/SWC1 with Status.CU1 set.
inline void emit_m22_core(Assembler& a, std::uint32_t table_rom) {
    const auto low = [](std::uint32_t address) { return static_cast<std::int32_t>(address & 0xffffu); };
    a.label("m22_gs");
    a.lui(a0, 0xB000u);
    a.sw(zero, 0x10, a0);            // T0_MODE = 0: no HBLANK count while the GS is retimed
    a.sw(zero, 0, a0);               // T0_COUNT = 0
    a.lui(t5, 0xB200u);              // GS privileged registers through KSEG1
    a.addiu(t0, zero, 0x200u);
    a.sw(t0, 0x1000, t5);            // CSR.RESET (32-bit store)
    a.lui(t0, static_cast<std::uint32_t>(kM22Smode1Held >> 16));
    a.ori(t0, t0, static_cast<std::uint32_t>(kM22Smode1Held & 0xffffu));
    a.sd(t0, 0x10, t5);              // SMODE1: sync held
    a.addiu(t1, zero, static_cast<std::uint32_t>(kM22Synch1));
    a.sd(t1, 0x40, t5);
    a.addiu(t1, zero, static_cast<std::uint32_t>(kM22Synch2));
    a.sd(t1, 0x50, t5);
    a.addiu(t1, zero, static_cast<std::uint32_t>(kM22Syncv >> 32));
    a.dsll32(t1, t1, 0u);
    a.ori(t1, t1, static_cast<std::uint32_t>(kM22Syncv & 0xffffu));
    a.sd(t1, 0x60, t5);
    a.addiu(t1, zero, static_cast<std::uint32_t>(kM22Smode2));
    a.sd(t1, 0x20, t5);
    a.addiu(t1, zero, static_cast<std::uint32_t>(kM22Srfsh));
    a.sd(t1, 0x30, t5);
    a.addiu(t0, zero, 0x1Fu);
    a.sw(t0, 0x1000, t5);            // CSR: SIGNAL | FINISH | HSINT | VSINT | EDWINT
    a.ori(t0, zero, static_cast<std::uint32_t>(kM22Smode1Run));
    a.sd(t0, 0x10, t5);              // SMODE1: sync restarts, lines start now
    a.mtc0(zero, Count);
    a.addiu(t0, zero, 0x83u);
    a.sw(t0, 0x10, a0);              // T0: HBLANK clock, counting from 0
    a.label("m22_first_line");
    a.lw(t1, 0, a0);
    a.beq(t1, zero, "m22_first_line");
    a.nop();
    a.mfc0(t2, Count);
    a.lui(a1, 0x8000u);
    a.sw(t2, low(kM22FirstLineResult), a1);
    a.sw(t1, low(kM22T0Result), a1);
    a.sw(zero, 0x10, a0);            // T0 stopped again
    a.label("m22_dev9");
    a.lui(t5, 0xBF80u);
    a.addiu(t0, zero, 1u);
    a.sh(t0, 0x1472, t5);
    a.sh(zero, 0x1470, t5);
    a.label("m22_sio");
    a.lui(t5, 0xB001u);              // 0x1000F100.. through negative offsets
    a.addiu(t0, zero, 3u);
    a.sw(t0, -0x0F00, t5);           // LCR
    a.sw(zero, -0x0EC0, t5);         // FCR
    a.addiu(t0, zero, 0x10u);
    a.sw(t0, -0x0EB0, t5);           // BGR
    a.sw(zero, -0x0EE0, t5);         // IER = 0: polled
    for (std::size_t n = 0; kM22SioText[n] != '\0'; ++n) {
        const auto wait = "m22_tx" + std::to_string(n);
        a.label(wait);
        a.lw(t1, -0x0ED0, t5);       // SIO_ISR
        a.andi(t1, t1, 0x8000u);     // TX FIFO full
        a.bne(t1, zero, wait);
        a.nop();
        a.addiu(t0, zero, static_cast<std::uint8_t>(kM22SioText[n]));
        a.sb(t0, -0x0E80, t5);       // TX FIFO
    }
    a.label("m22_fpu");
    a.lui(t0, 0x3040u);
    a.ori(t0, t0, 0x0004u);          // CU1 | CU0 | BEV | ERL
    a.mtc0(t0, Status);
    a.la(a0, table_rom);
    a.lwc1(1u, 8, a0);               // $f1 = SYNDATA[2]
    a.swc1(1u, low(kM22FpuResult), a1);
    a.lui(t0, 0x1040u);
    a.ori(t0, t0, 0x0004u);          // CU1 cleared again
    a.mtc0(t0, Status);
    // Constant JR shapes for the AOT proof, all executed at run time. Only the
    // first is provable; the never-taken branches make the code after the
    // other three discoverable without that proof.
    a.label("m22_jumps");
    a.bne(zero, zero, "m22_rewritten_target");
    a.nop();
    a.bne(zero, zero, "m22_entered_target");
    a.nop();
    a.bne(zero, zero, "m22_slot_target");
    a.nop();
    const auto constant_target = a.here() + 20u;
    a.lui(t0, constant_target >> 16);
    a.ori(t0, t0, constant_target & 0xffffu);
    a.label("m22_const_jr");
    a.jr(t0);
    a.nop();
    a.brk();                         // never reached: the JR skips it
    a.label("m22_const_target");
    const auto rewritten_target = a.here() + 24u;
    a.lui(t1, rewritten_target >> 16);
    a.ori(t1, t1, rewritten_target & 0xffffu);
    a.addu(t1, t1, zero);            // same value, but not a LUI/ORI/ADDIU step
    a.label("m22_rewritten_jr");
    a.jr(t1);
    a.nop();
    a.brk();
    a.label("m22_rewritten_target");
    const auto entered_target = a.here() + 28u;
    a.bne(zero, zero, "m22_entered_jr"); // an entry between the LUI and the JR
    a.nop();
    a.lui(t2, entered_target >> 16);
    a.ori(t2, t2, entered_target & 0xffffu);
    a.label("m22_entered_jr");
    a.jr(t2);
    a.nop();
    a.brk();
    a.label("m22_entered_target");
    const auto slot_target = a.here() + 24u;
    a.bne(zero, zero, "fail");
    a.lui(t3, slot_target >> 16);    // LUI in a branch delay slot
    a.ori(t3, t3, slot_target & 0xffffu);
    a.label("m22_slot_jr");
    a.jr(t3);
    a.nop();
    a.brk();
    a.label("m22_slot_target");
    a.beq(zero, zero, "m22_jumps_done");
    a.nop();
    a.lui(t4, kPayloadRam >> 16);    // never executed: a constant target outside the ROM image
    a.ori(t4, t4, kPayloadRam & 0xffffu);
    a.label("m22_outside_jr");
    a.jr(t4);
    a.nop();
    a.label("m22_jumps_done");
}

// One MCH serial transaction as the BIOS helpers issue it (wait for bit 31
// clear, SD to MCH_DRD, start MCH_RICM, SYNC, wait again); t6 = 0xB0010000.
// SRD/SWR address `sa`; the other opcodes carry SA 0.
constexpr std::int32_t kMchRicmOffset = -0x0BD0; // 0x1000F430 from 0xB0010000
constexpr std::int32_t kMchDrdOffset = -0x0BC0;  // 0x1000F440
inline void emit_mch_serial(Assembler& a, const std::string& label, std::uint8_t sop, bool broadcast,
                            std::uint8_t sdev, std::uint16_t sa, std::uint16_t sd,
                            const std::function<void()>& before_start = {}) {
    const auto wait_idle = [&](const std::string& name) {
        a.label(name);
        a.lw(t1, kMchRicmOffset, t6);
        a.bltz(t1, name);
        a.nop();
    };
    wait_idle(label + "_pre");
    a.ori(t0, zero, sd);
    a.sw(t0, kMchDrdOffset, t6);
    const std::uint32_t address = sop <= 1u ? sa : 0u;
    const auto ricm = 0x80000000u | (address << 16) | (static_cast<std::uint32_t>(sop) << 6) | (broadcast ? 0x20u : 0u) | sdev;
    a.lui(t0, ricm >> 16);
    a.ori(t0, t0, ricm & 0xffffu);
    if (before_start) before_start();
    a.sw(t0, kMchRicmOffset, t6);
    a.sync_p();
    wait_idle(label + "_post");
}

// M2.3, before ERET (AOT-reachable): InitDev steps 2-10 for two devices through
// emit_mch_serial, with SRD reads of INIT along the way and the SETR and SWR
// durations measured with Count.
inline void emit_m23_core(Assembler& a) {
    const auto low = [](std::uint32_t address) { return static_cast<std::int32_t>(address & 0xffffu); };
    unsigned serial_count = 0;
    const auto serial = [&](std::uint8_t sop, bool broadcast, std::uint8_t sdev, std::uint16_t sd,
                            const std::function<void()>& before_start = {}) {
        emit_mch_serial(a, "m23_s" + std::to_string(serial_count++), sop, broadcast, sdev, 0x021u, sd, before_start);
    };
    const auto read_init = [&](std::uint8_t sdev, std::uint32_t result) {
        serial(0u, false, sdev, 0u);
        a.lw(t2, kMchDrdOffset, t6);
        a.sw(t2, low(result), a1);
    };
    a.label("m23_mch");
    a.lui(t6, 0xB001u);
    a.lui(a1, 0x8000u);
    serial(2u, true, 0u, 0u, [&] { a.mtc0(zero, Count); }); // SETR
    a.mfc0(t2, Count);
    a.sw(t2, low(kM23SetrResult), a1);
    a.label("m23_setr_wait");                               // step 3: 16 SCK cycles
    a.mfc0(t1, Count);
    a.sltiu(t1, t1, kM23ClrrWait);
    a.bne(t1, zero, "m23_setr_wait");
    a.nop();
    serial(3u, true, 0u, 0u);                               // CLRR
    serial(4u, true, 0u, 0u);                               // SETF
    serial(1u, true, 0u, 0x001Fu, [&] { a.mtc0(zero, Count); }); // SWR INIT: SRP 0, SDEVID 0x1F
    a.mfc0(t2, Count);
    a.sw(t2, low(kM23SwrResult), a1);
    read_init(0x1Fu, kM23FirstRead);                        // device 0 answers
    serial(1u, false, 0x1Fu, 0x0080u);                      // device 0: SRP 1, SDEVID 0
    read_init(0x1Fu, kM23SecondRead);                       // device 1 answers
    serial(1u, false, 0x1Fu, 0x0081u);                      // device 1: SRP 1, SDEVID 1
    read_init(0u, kM23Device0Read);
    read_init(1u, kM23Device1Read);
    a.label("m23_done");
}

// M2.4, after the M2.3 block: TEST77, TCYCLE, TFRM and TRDLY written in
// broadcast (the BIOS order, without TPARM) and read back from each device.
inline void emit_m24_core(Assembler& a) {
    const auto low = [](std::uint32_t address) { return static_cast<std::int32_t>(address & 0xffffu); };
    a.label("m24_timing");
    emit_mch_serial(a, "m24_test77", 1u, true, 0u, 0x04Du, 0u);
    emit_mch_serial(a, "m24_tcycle", 1u, true, 0u, 0x04Cu, kM24Tcycle);
    emit_mch_serial(a, "m24_tfrm", 1u, true, 0u, 0x049u, kM24Tfrm);
    emit_mch_serial(a, "m24_trdly", 1u, true, 0u, 0x04Au, kM24Trdly);
    const auto read = [&](const std::string& label, std::uint8_t sdev, std::uint16_t sa, std::uint32_t result) {
        emit_mch_serial(a, label, 0u, false, sdev, sa, 0u);
        a.lw(t2, kMchDrdOffset, t6);
        a.sw(t2, low(result), a1);
    };
    read("m24_read_tcycle", 1u, 0x04Cu, kM24TcycleRead);
    read("m24_read_tfrm", 0u, 0x049u, kM24TfrmRead);
    read("m24_read_trdly", 1u, 0x04Au, kM24TrdlyRead);
    read("m24_read_test77", 0u, 0x04Du, kM24Test77Read);
    a.label("m24_done");
}

// M2.1, after ERET: a scratchpad entry written like the real BIOS does
// (PageMask 0, S bit) still spans the whole 16 KiB.
inline void emit_m21_scratchpad(Assembler& a) {
    a.label("m21_scratchpad");
    a.addiu(t0, zero, 2u);
    a.mtc0(t0, Index);
    a.mtc0(zero, PageMask);
    a.lui(t0, kM21ScratchpadAlias >> 16);
    a.mtc0(t0, EntryHi);
    a.lui(t0, 0x8000u);
    a.ori(t0, t0, 7u);
    a.mtc0(t0, EntryLo0);
    a.addiu(t0, zero, 7u);
    a.mtc0(t0, EntryLo1);
    a.tlbwi();
    a.lui(t1, kM21ScratchpadAlias >> 16);
    a.sw(v0, 0x3FF0, t1);
    a.lui(t2, kScratchpadBase >> 16);
    a.lw(t3, 0x3FF0, t2);            // the same bytes through entry 0
    a.bne(t3, v0, "fail");
    a.nop();
}

// Stage::M21 adds the M2.1 blocks (MMI, COP0 Count, EE timers, 0x1000F500 before
// ERET, where the AOT reaches them; a PageMask-0 scratchpad entry after it);
// Stage::M22 also the M2.2 block before ERET; Stage::M23 adds the M2.3 block
// after it and Stage::M24 the M2.4 block after that. The default (M2.0) firmware is unchanged.
inline Firmware build_firmware(Stage stage = Stage::M20) {
    const bool m21 = stage != Stage::M20;
    const bool m22 = stage == Stage::M22 || stage == Stage::M23 || stage == Stage::M24;
    const bool m23 = stage == Stage::M23 || stage == Stage::M24;
    const bool m24 = stage == Stage::M24;
    std::vector<File> files;
    files.push_back({"RESET", {}, {}});
    files.push_back({"ROMDIR", {}, {}});
    files.push_back({"EXTINFO", {}, {}});
    files.push_back({"ROMVER", text(std::string(kRomver) + "\n"), {}});
    files.push_back({"SYNPAYLD", le_words(payload_code()), {}});
    std::vector<std::uint32_t> table(kTable.begin(), kTable.end());
    files.push_back({"SYNDATA", le_words(table), {}});
    field(files[0].extinfo, 0x01, 0, {0x03, 0x11, 0x26, 0x20});
    field(files[0].extinfo, 0x03, 0, text("ps2-native synthetic firmware (not Sony)"));
    field(files[3].extinfo, 0x02, 0x0100, {});
    field(files[4].extinfo, 0x03, 0, text("RAM payload"));
    field(files[5].extinfo, 0x7F, 0, {});
    std::vector<std::uint8_t> extinfo;
    for (const auto& file : files) extinfo.insert(extinfo.end(), file.extinfo.begin(), file.extinfo.end());
    files[2].data = extinfo;
    files[1].data.assign((files.size() + 1u) * 16u, 0u); // filled below

    // Layout: RESET has a fixed size, so every later file offset is known
    // before the reset code (which refers to them) is assembled.
    Firmware fw;
    std::vector<std::uint32_t> offsets;
    std::uint32_t offset = 0;
    for (std::size_t n = 0; n < files.size(); ++n) {
        offsets.push_back(offset);
        const auto size = n == 0 ? kResetSize : static_cast<std::uint32_t>(files[n].data.size());
        offset += align16(size);
    }
    fw.romdir_offset = offsets[1];
    fw.payload_rom = kResetVector + offsets[4];
    fw.table_rom = kResetVector + offsets[5];
    fw.payload_words = static_cast<std::uint32_t>(files[4].data.size() / 4u);

    Assembler a(kResetVector);
    a.label("reset");
    a.mfc0(k0, PRid);
    a.sltiu(at, k0, 0x59u);          // EE PRid (0x2E20) is not below 0x59: EE path.
    a.bne(at, zero, "iop_path");
    a.nop();
    a.lui(t0, 0x1040u);
    a.ori(t0, t0, 0x0004u);          // CU0 | BEV | ERL
    a.mtc0(t0, Status);
    a.mtc0(zero, Cause);
    a.sync_p();
    a.mtc0(zero, PageMask);
    a.mtc0(zero, EntryLo0);
    a.mtc0(zero, EntryLo1);
    a.lui(t1, 0xE000u);              // 48 distinct, invalid KSEG3 VPN2s
    a.addiu(t2, zero, 0u);
    a.addiu(t3, zero, 48u);
    a.label("tlb_loop");
    a.mtc0(t2, Index);
    a.mtc0(t1, EntryHi);
    a.tlbwi();
    a.addiu(t1, t1, 0x2000u);
    a.addiu(t2, t2, 1u);
    a.bne(t2, t3, "tlb_loop");
    a.nop();
    a.lui(t0, kScratchpadBase >> 16); // entry 0: scratchpad (S | D | V | G), 16 KiB pages
    a.mtc0(t0, EntryHi);
    a.addiu(t0, zero, 0x6000u);
    a.mtc0(t0, PageMask);
    a.lui(t0, 0x8000u);
    a.ori(t0, t0, 0x0007u);
    a.mtc0(t0, EntryLo0);
    a.addiu(t0, zero, 1u);
    a.mtc0(t0, EntryLo1);
    a.mtc0(zero, Index);
    a.tlbwi();
    a.lui(t0, kKusegWindow >> 16);   // entry 1: 0x00080000 -> physical 0x00100000 (4 KiB pages)
    a.mtc0(t0, EntryHi);
    a.mtc0(zero, PageMask);
    a.addiu(t0, zero, 0x401Fu);
    a.mtc0(t0, EntryLo0);
    a.addiu(t0, zero, 0x405Fu);
    a.mtc0(t0, EntryLo1);
    a.addiu(t0, zero, 1u);
    a.mtc0(t0, Index);
    a.tlbwi();
    a.tlbp();
    a.mfc0(s0, Index);               // 1
    a.mtc0(zero, TagLo);
    a.mtc0(zero, TagHi);
    a.lui(t0, 0x8000u);
    a.addiu(t1, zero, 8u);
    a.label("cache_loop");
    a.cache(0x07u, 0, t0);           // IXIN
    a.cache(0x16u, 0, t0);           // DXIN
    a.cache(0x12u, 0, t0);           // DXSTG with TagLo = 0
    a.addiu(t1, t1, 0xFFFFu);
    a.bne(t1, zero, "cache_loop");
    a.addiu(t0, t0, 64u);
    a.la(a0, fw.table_rom);          // checksum of SYNDATA, read from ROM through KSEG1
    a.addiu(a1, zero, static_cast<std::uint32_t>(kTable.size()));
    a.addu(v0, zero, zero);
    a.label("sum_loop");
    a.lw(t0, 0, a0);
    a.addu(v0, v0, t0);
    a.addiu(a1, a1, 0xFFFFu);
    a.bne(a1, zero, "sum_loop");
    a.addiu(a0, a0, 4u);
    a.lui(t0, 0x8000u);
    a.sw(v0, static_cast<std::int32_t>(kChecksumRam & 0xffffu), t0);
    a.la(a0, fw.payload_rom);        // copy SYNPAYLD to RAM
    a.lui(a1, kPayloadRam >> 16);
    a.addiu(a2, zero, fw.payload_words);
    a.label("copy_loop");
    a.lw(t0, 0, a0);
    a.sw(t0, 0, a1);
    a.addiu(a0, a0, 4u);
    a.addiu(a1, a1, 4u);
    a.addiu(a2, a2, 0xFFFFu);
    a.bne(a2, zero, "copy_loop");
    a.nop();
    if (m21) emit_m21_core(a, fw.table_rom);
    if (m22) emit_m22_core(a, fw.table_rom);
    if (m23) emit_m23_core(a);
    if (m24) emit_m24_core(a);
    a.label("leave_error_level");
    a.la(t0, 0u);                    // patched below: ErrorEPC = after_eret
    a.mtc0(t0, ErrorEPC);
    a.eret();
    a.label("after_eret");           // ERL = 0: kuseg now translates through the TLB
    a.lui(t0, kScratchpadBase >> 16);
    a.sw(v0, 0, t0);
    a.lui(t1, kKusegWindow >> 16);
    a.sw(v0, 0x10, t1);
    a.lui(t2, 0x8010u);              // same RAM through KSEG0
    a.lw(t3, 0x10, t2);
    a.bne(t3, v0, "fail");
    a.nop();
    a.lui(t9, kPayloadRam >> 16);
    a.jalr(t9);
    a.nop();
    a.lui(t0, 0x8000u);
    a.sd(v1, static_cast<std::int32_t>(kPayloadResultRam & 0xffffu), t0);
    if (m21) emit_m21_scratchpad(a);
    a.label("mmio");
    a.lui(t0, 0xB001u);
    a.lw(t1, -0x1000, t0);           // INTC_STAT (0x1000F000 through KSEG1): not modeled yet
    a.label("fail");
    a.brk();
    a.label("iop_path");
    a.brk();
    auto code = a.finish();
    // ErrorEPC load: LUI/ADDIU of after_eret.
    const auto patch = (a.at("leave_error_level") - kResetVector) / 4u;
    const auto target = a.at("after_eret");
    code[patch] = (code[patch] & 0xffff0000u) | ((target + 0x8000u) >> 16);
    code[patch + 1] = (code[patch + 1] & 0xffff0000u) | (target & 0xffffu);
    if (code.size() * 4u > kResetSize) throw std::runtime_error("M2.0 reset code exceeds RESET");
    files[0].data = le_words(code);
    fw.labels = {{"reset", a.at("reset")}, {"tlb_loop", a.at("tlb_loop")}, {"cache_loop", a.at("cache_loop")},
                 {"sum_loop", a.at("sum_loop")}, {"copy_loop", a.at("copy_loop")},
                 {"after_eret", a.at("after_eret")}, {"mmio", a.at("mmio")}, {"fail", a.at("fail")},
                 {"iop_path", a.at("iop_path")}};
    if (m21)
        for (const char* name : {"m21_mmi", "m21_count", "m21_edge", "m21_edges", "m21_scratchpad"})
            fw.labels.emplace(name, a.at(name));
    if (m22)
        for (const char* name : {"m22_gs", "m22_first_line", "m22_dev9", "m22_sio", "m22_fpu", "m22_const_jr",
                                 "m22_const_target", "m22_rewritten_jr", "m22_rewritten_target", "m22_entered_jr",
                                 "m22_entered_target", "m22_slot_jr", "m22_slot_target", "m22_outside_jr"})
            fw.labels.emplace(name, a.at(name));
    if (m23)
        for (const char* name : {"m23_mch", "m23_setr_wait", "m23_done"}) fw.labels.emplace(name, a.at(name));
    if (m24)
        for (const char* name : {"m24_timing", "m24_done"}) fw.labels.emplace(name, a.at(name));

    // ROMDIR: name[10], EXTINFO size (u16), file size (u32); zero terminator.
    auto& romdir = files[1].data;
    for (std::size_t n = 0; n < files.size(); ++n) {
        const auto base = n * 16u;
        for (std::size_t c = 0; c < files[n].name.size(); ++c) romdir[base + c] = static_cast<std::uint8_t>(files[n].name[c]);
        const auto ext = static_cast<std::uint16_t>(files[n].extinfo.size());
        romdir[base + 10] = static_cast<std::uint8_t>(ext);
        romdir[base + 11] = static_cast<std::uint8_t>(ext >> 8);
        const auto size = n == 0 ? kResetSize : static_cast<std::uint32_t>(files[n].data.size());
        for (unsigned b = 0; b < 4; ++b) romdir[base + 12 + b] = static_cast<std::uint8_t>(size >> (8u * b));
    }
    fw.rom.assign(kRomBytes, 0u);
    for (std::size_t n = 0; n < files.size(); ++n)
        std::copy(files[n].data.begin(), files[n].data.end(), fw.rom.begin() + offsets[n]);
    return fw;
}
} // namespace m20
