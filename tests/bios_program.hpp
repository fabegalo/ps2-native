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

enum Reg : unsigned {
    zero = 0, at = 1, v0 = 2, v1 = 3, a0 = 4, a1 = 5, a2 = 6, t0 = 8, t1 = 9, t2 = 10, t3 = 11,
    s0 = 16, t9 = 25, k0 = 26, ra = 31
};
enum Cp0 : unsigned {
    Index = 0, EntryLo0 = 2, EntryLo1 = 3, PageMask = 5, EntryHi = 10, Status = 12, Cause = 13,
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
    void sltiu(unsigned rt, unsigned rs, std::uint32_t imm) { i(0x0bu, rs, rt, imm); }
    void lw(unsigned rt, std::int32_t offset, unsigned base) { i(0x23u, base, rt, static_cast<std::uint32_t>(offset)); }
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
    void bne(unsigned rs, unsigned rt, const std::string& target) { branch(0x05u, rs, rt, target); }
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

inline Firmware build_firmware() {
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
