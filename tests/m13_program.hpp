// Synthetic, independently-authored M1.3 MIPS program and ELF32 image.
// Shared by the fixture generator and the M1.3 test so that label addresses,
// relocation sites and expected values are defined in one place.
// No Sony, game or third-party binaries are involved.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace m13 {
constexpr std::uint32_t kTextBase = 0x00100000u;
// Fixed so that CMake can pass it to ps2n-aot as an --entry hint for the
// stripped variant (the callback is only discoverable through relocations).
constexpr std::uint32_t kCallback = 0x00100400u;
constexpr std::uint32_t kDataBase = 0x00110000u;        // .rodata
constexpr std::uint32_t kTableA = kDataBase + 0x00u;    // 4 entries, SLTIU-bounded
constexpr std::uint32_t kTableB = kDataBase + 0x10u;    // 3 entries, SLTIU-bounded
constexpr std::uint32_t kTableUnbounded = kDataBase + 0x20u;
constexpr std::uint32_t kTableEntered = kDataBase + 0x28u;
constexpr std::uint32_t kRodataSize = 0x30u;
constexpr std::uint32_t kResults = kDataBase + 0x100u;  // .data
constexpr std::uint32_t kResultsSize = 0x300u;
constexpr std::uint32_t kSlotBase = 0x20u;              // ALU results: kResults + 0x20 + 8 * slot
constexpr std::uint32_t kScratch = 0x280u;              // load/store scratch (16-byte aligned)
constexpr std::uint32_t kHostReturn = 0xfffffff0u;

enum Reg : unsigned {
    zero = 0, v0 = 2, a0 = 4, a1 = 5, a2 = 6, a3 = 7, t0 = 8, t1 = 9, t2 = 10,
    t3 = 11, t4 = 12, t5 = 13, t6 = 14, t7 = 15, s0 = 16, s1 = 17, s2 = 18,
    s6 = 22, s7 = 23, ra = 31
};
// .symtab layout of the fixture with sections.
enum Symbol : std::uint32_t {
    kSymNone, kSymText, kSymRodata, kSymData,
    kSymMain, kSymSum3, kSymSelectA, kSymSelectB, kSymAlu, kSymUnbounded, kSymEntered,
    kSymCount
};
inline constexpr std::array<const char*, kSymCount> kSymbolNames{
    "", "", "", "", "main", "sum3", "select_a", "select_b", "alu", "unbounded", "entered"};
constexpr std::uint32_t kRMips26 = 4, kRMipsHi16 = 5, kRMipsLo16 = 6, kRMipsGprel16 = 7;

// Expected 64-bit values of the ALU slots (R5900 semantics, see alu below).
inline constexpr std::array<std::uint64_t, 61> kAluExpected{
    0xFFFFFFFF80000001ull, 0xFFFFFFFFFFFFFFFFull, 0x00000000FFFFFFFFull, 0xFFFFFFF000000000ull, // 0-3
    0xFFFFFFFFFFFFFFF0ull, 0x0000000000000008ull, 0xFFFFFFFFFFFFFFF8ull, 0x000000FFFFFFFF00ull, // 4-7
    0xFFFFFFFFFF800000ull, 0x00FFFFFFFF800000ull, 0xFFFFFFFFFF800000ull, 0x0000000000800000ull, // 8-11
    0xFFFFFFFFFFFFFF00ull, 0x0000000FFFFFFFF0ull, 0x0FFFFFFFF8000000ull, 0xFFFFFFFFF8000000ull, // 12-15
    0x0000000080000001ull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFF7FFFFFFEull, 0x000000007FFFFFFEull, // 16-19
    0x0000000000000001ull, 0x00000000FFFFFF00ull, 0x0000000100000000ull, 0xFFFFFFFF00000000ull, // 20-23
    0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFEull, 0x0000000000000001ull, 0x0000000000000001ull, // 24-27
    0x0000000000000000ull, 0x0000000000000001ull, 0x0000000000000000ull, 0x0000000000000037ull, // 28-31
    0x0000000000000004ull, 0xFFFFFFFFFFFFFFD6ull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFEull, // 32-35
    0x0000000000000001ull, 0xFFFFFFFFFFFFFFFDull, 0xFFFFFFFFFFFFFFFFull, 0x000000007FFFFFFFull, // 36-39
    0x0000000000000001ull, 0x0000000000000001ull, 0xFFFFFFFFFFFFFFF9ull, 0xFFFFFFFFFFFFFFFFull, // 40-43
    0x0000000000000006ull, 0xFFFFFFFF80000000ull, 0x0000000000000000ull, 0x0000000000000006ull, // 44-47
    0xFFFFFFFFFFFFFFF9ull, 0xFFFFFFFFFFFFFFFEull, 0x00000000000000FEull, 0xFFFFFFFFFFFF8000ull, // 48-51
    0x0000000000008000ull, 0xFFFFFFFF80000001ull, 0x0000000080000001ull, 0xFFFFFFFF80000001ull, // 52-55
    0x80000001800000FEull, 0xFFFFFFFF80000001ull, 0xFFFFFFFF80000001ull, 0x80000001800000FFull, // 56-59
    0xFFFFFFFF80000001ull                                                                         // 60
};
// Slots 61..64 hold link registers (label-dependent); slot 65 the branch mask.
constexpr unsigned kLinkSlot = 61;
constexpr unsigned kBranchMaskSlot = 65;
constexpr std::uint64_t kBranchMask = 0xFFFu;

struct Relocation {
    std::uint32_t site{};
    std::uint32_t type{};
    std::uint32_t symbol{};
};
struct Program {
    std::vector<std::uint32_t> text;
    std::map<std::string, std::uint32_t> labels;
    std::vector<Relocation> relocations;
    std::vector<std::uint32_t> rodata; // words at kDataBase
    [[nodiscard]] std::uint32_t at(const std::string& name) const {
        const auto found = labels.find(name);
        if (found == labels.end()) throw std::runtime_error("Unknown M1.3 label " + name);
        return found->second;
    }
};

// Target of %hi/%lo or of a branch/jump: a code label or an absolute address.
struct Target {
    std::string label;
    std::uint32_t address{};
};
inline Target L(const std::string& label) { return {label, 0u}; }
inline Target A(std::uint32_t address) { return {std::string{}, address}; }

class Assembler {
public:
    [[nodiscard]] std::uint32_t here() const { return kTextBase + 4u * static_cast<std::uint32_t>(words_.size()); }
    void label(const std::string& name) {
        if (!labels_.emplace(name, here()).second) throw std::runtime_error("Duplicate label " + name);
    }
    void pad_to(std::uint32_t address) {
        if (address < here()) throw std::runtime_error("M1.3 fixture code overflows fixed address");
        while (here() < address) words_.push_back(0u);
    }
    void raw(std::uint32_t word) { words_.push_back(word); }
    void nop() { raw(0u); }
    void r(unsigned rs, unsigned rt, unsigned rd, unsigned sa, unsigned funct) {
        raw((rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | funct);
    }
    void i(unsigned op, unsigned rs, unsigned rt, std::uint32_t imm) {
        raw((op << 26) | (rs << 21) | (rt << 16) | (imm & 0xffffu));
    }
    void branch(unsigned op, unsigned rs, unsigned rt, const std::string& target) {
        fixups_.push_back({words_.size(), L(target), Fixup::Kind::Branch});
        i(op, rs, rt, 0u);
    }
    void regimm(unsigned code, unsigned rs, const std::string& target) { branch(1u, rs, code, target); }
    void jal(const std::string& target, std::uint32_t symbol) {
        relocations_.push_back({here(), kRMips26, symbol});
        fixups_.push_back({words_.size(), L(target), Fixup::Kind::Jump});
        raw(3u << 26);
    }
    void hi(unsigned rt, const Target& target, std::uint32_t symbol) {
        relocations_.push_back({here(), kRMipsHi16, symbol});
        fixups_.push_back({words_.size(), target, Fixup::Kind::High});
        i(0x0fu, 0u, rt, 0u);
    }
    void lo(unsigned op, unsigned rt, unsigned base, const Target& target, std::uint32_t symbol) {
        relocations_.push_back({here(), kRMipsLo16, symbol});
        fixups_.push_back({words_.size(), target, Fixup::Kind::Low});
        i(op, base, rt, 0u);
    }
    Program finish() {
        for (const auto& fixup : fixups_) {
            const auto value = fixup.target.label.empty() ? fixup.target.address : labels_.at(fixup.target.label);
            const auto site = kTextBase + 4u * static_cast<std::uint32_t>(fixup.index);
            auto& word = words_[fixup.index];
            switch (fixup.kind) {
                case Fixup::Kind::Branch: {
                    const auto delta = static_cast<std::int64_t>(value) - static_cast<std::int64_t>(site + 4u);
                    if (delta % 4 != 0 || delta / 4 < -32768 || delta / 4 > 32767)
                        throw std::runtime_error("Branch out of range");
                    word |= static_cast<std::uint32_t>(delta / 4) & 0xffffu;
                    break;
                }
                case Fixup::Kind::Jump:
                    if (((site + 4u) & 0xf0000000u) != (value & 0xf0000000u))
                        throw std::runtime_error("Jump out of region");
                    word |= (value >> 2) & 0x03ffffffu;
                    break;
                case Fixup::Kind::High:
                    word |= ((value + 0x8000u) >> 16) & 0xffffu; // %hi carries for signed %lo
                    break;
                case Fixup::Kind::Low:
                    word |= value & 0xffffu;
                    break;
            }
        }
        return {words_, labels_, relocations_, {}};
    }

private:
    struct Fixup {
        enum class Kind { Branch, Jump, High, Low };
        std::size_t index{};
        Target target;
        Kind kind{};
    };
    std::vector<std::uint32_t> words_;
    std::map<std::string, std::uint32_t> labels_;
    std::vector<Relocation> relocations_;
    std::vector<Fixup> fixups_;
};

inline Program build_program() {
    Assembler a;
    constexpr unsigned kAddiu = 0x09, kLw = 0x23, kSw = 0x2b, kSd = 0x3f, kLd = 0x37, kOri = 0x0d;
    auto addiu = [&](unsigned rt, unsigned rs, std::int32_t imm) { a.i(kAddiu, rs, rt, static_cast<std::uint32_t>(imm)); };
    auto daddu = [&](unsigned rd, unsigned rs, unsigned rt) { a.r(rs, rt, rd, 0u, 0x2du); };
    auto jr = [&](unsigned rs) { a.r(rs, 0u, 0u, 0u, 0x08u); };
    auto slot = [](unsigned index) { return kSlotBase + 8u * index; };
    auto sd = [&](unsigned rt, unsigned index) { a.i(kSd, s6, rt, slot(index)); };
    auto mark = [&](std::uint32_t bit) { a.i(kOri, s2, s2, bit); };

    // main: drives every scenario and writes word results to kResults.
    a.label("main");
    daddu(s0, ra, zero);                                  // save host return (64-bit move)
    a.hi(s1, A(kResults), kSymData);
    a.lo(kAddiu, s1, s1, A(kResults), kSymData);          // s1 = results
    addiu(a0, zero, 10);
    addiu(a1, zero, -3);
    a.label("jal_sum3");
    a.jal("sum3", kSymSum3);                              // R_MIPS_26 -> STT_FUNC
    addiu(a2, zero, 5);                                   // delay slot
    a.label("gprel_site");
    a.i(kSw, s1, v0, 0x00u);                              // results[0] = 12
    // Address-taken callback without STT_FUNC: only HI16/LO16 proves it is code.
    // Two HI16 share one LO16 (GNU); the second LO16 is a valid unpaired LO16.
    a.label("callback_hi_1");
    a.hi(t0, L("callback"), kSymText);
    a.label("callback_hi_2");
    a.hi(t1, L("callback"), kSymText);
    a.label("callback_lo");
    a.lo(kAddiu, t0, t0, L("callback"), kSymText);
    a.label("callback_lo_unpaired");
    a.lo(kAddiu, t1, t1, L("callback"), kSymText);
    a.label("callback_call");
    a.r(t0, 0u, ra, 0u, 0x09u);                           // jalr ra, t0
    addiu(a0, zero, 7);                                   // delay slot
    a.i(kSw, s1, v0, 0x04u);                              // results[1] = 21
    addiu(a0, zero, 2);
    a.jal("select_a", kSymSelectA);
    a.nop();
    a.i(kSw, s1, v0, 0x08u);                              // results[2] = 102
    addiu(a0, zero, 9);
    a.jal("select_a", kSymSelectA);
    a.nop();
    a.i(kSw, s1, v0, 0x0cu);                              // results[3] = -1 (default)
    addiu(a0, zero, 1);
    a.jal("select_b", kSymSelectB);
    a.nop();
    a.i(kSw, s1, v0, 0x10u);                              // results[4] = 201
    a.jal("alu", kSymAlu);
    daddu(a0, s1, zero);                                  // delay slot: results base
    // Statically discovered, never executed: JRs whose tables cannot be proven.
    a.branch(0x04u, zero, zero, "finish");
    a.nop();
    a.jal("unbounded", kSymUnbounded);
    a.nop();
    a.jal("entered", kSymEntered);
    a.nop();
    a.label("finish");
    daddu(ra, s0, zero);
    jr(ra);
    a.nop();

    a.label("sum3");
    a.r(a0, a1, v0, 0u, 0x21u);                           // addu v0, a0, a1
    a.r(v0, a2, v0, 0u, 0x21u);                           // addu v0, v0, a2
    jr(ra);
    a.nop();

    a.pad_to(kCallback);
    a.label("callback");
    a.r(0u, a0, v0, 1u, 0x00u);                           // sll v0, a0, 1
    a.r(v0, a0, v0, 0u, 0x21u);                           // addu v0, v0, a0 -> 3 * a0
    jr(ra);
    a.nop();

    // GCC shape: %lo folded into the LW.
    a.label("select_a");
    a.i(0x0bu, a0, t0, 4u);                               // sltiu t0, a0, 4
    a.branch(0x04u, t0, zero, "a_default");               // beq t0, zero, default
    a.r(0u, a0, t1, 2u, 0x00u);                           // delay: sll t1, a0, 2
    a.hi(t2, A(kTableA), kSymRodata);
    a.r(t1, t2, t1, 0u, 0x21u);                           // addu t1, t1, t2
    a.label("select_a_lw");
    a.lo(kLw, t1, t1, A(kTableA), kSymRodata);
    a.label("select_a_jr");
    jr(t1);
    a.nop();
    a.label("a_case0"); addiu(v0, zero, 100); jr(ra); a.nop();
    a.label("a_case1"); jr(ra); addiu(v0, zero, 101);     // result set in the delay slot
    a.label("a_case2"); addiu(v0, zero, 102); jr(ra); a.nop();
    a.label("a_case3"); addiu(v0, zero, 103); jr(ra); a.nop();
    a.label("a_default"); addiu(v0, zero, -1); jr(ra); a.nop();

    // ee-gcc shape: LUI/ADDIU table base, LW 0(index).
    a.label("select_b");
    a.i(0x0bu, a0, t0, 3u);
    a.branch(0x04u, t0, zero, "b_default");
    a.r(0u, a0, t1, 2u, 0x00u);
    a.hi(t2, A(kTableB), kSymRodata);
    a.label("select_b_addiu");
    a.lo(kAddiu, t2, t2, A(kTableB), kSymRodata);
    a.r(t1, t2, t1, 0u, 0x21u);
    a.i(kLw, t1, t1, 0u);
    a.label("select_b_jr");
    jr(t1);
    a.nop();
    a.label("b_case0"); addiu(v0, zero, 200); jr(ra); a.nop();
    a.label("b_case1"); addiu(v0, zero, 201); jr(ra); a.nop();
    a.label("b_case2"); addiu(v0, zero, 202); jr(ra); a.nop();
    a.label("b_default"); addiu(v0, zero, -2); jr(ra); a.nop();

    // No SLTIU bound: the table size cannot be proven.
    a.label("unbounded");
    a.r(0u, a0, t1, 2u, 0x00u);
    a.hi(t2, A(kTableUnbounded), kSymRodata);
    a.r(t1, t2, t1, 0u, 0x21u);
    a.lo(kLw, t1, t1, A(kTableUnbounded), kSymRodata);
    a.label("unbounded_jr");
    jr(t1);
    a.nop();
    a.label("u_case0"); jr(ra); a.nop();
    a.label("u_case1"); jr(ra); a.nop();

    // Bounded, but a branch enters the guarded window and bypasses the bound.
    a.label("entered");
    a.branch(0x05u, a1, zero, "entered_inside");          // bne a1, zero, inside
    a.nop();
    a.i(0x0bu, a0, t0, 2u);
    a.branch(0x04u, t0, zero, "e_default");
    a.r(0u, a0, t1, 2u, 0x00u);
    a.hi(t2, A(kTableEntered), kSymRodata);
    a.label("entered_inside");
    a.r(t1, t2, t1, 0u, 0x21u);
    a.lo(kLw, t1, t1, A(kTableEntered), kSymRodata);
    a.label("entered_jr");
    jr(t1);
    a.nop();
    a.label("e_default"); jr(ra); a.nop();
    a.label("e_case0"); jr(ra); a.nop();
    a.label("e_case1"); jr(ra); a.nop();

    // alu(a0 = results): R5900 64-bit integer semantics, loads/stores, branches.
    a.label("alu");
    daddu(s7, ra, zero);
    daddu(s6, a0, zero);
    a.i(0x0fu, 0u, t0, 0x8000u);                          // lui t0, 0x8000
    a.label("ori_site");
    a.i(kOri, t0, t0, 1u);                                // ori: 64-bit, keeps upper bits
    sd(t0, 0);
    a.r(0u, t0, t1, 0u, 0x3fu); sd(t1, 1);                // dsra32 t1, t0, 0
    a.r(0u, t0, t2, 0u, 0x3eu); sd(t2, 2);                // dsrl32 t2, t0, 0
    a.r(0u, t2, t3, 4u, 0x3cu); sd(t3, 3);                // dsll32 t3, t2, 4
    a.r(0u, t2, t4, 4u, 0x00u); sd(t4, 4);                // sll t4, t2, 4
    a.r(0u, t0, t5, 28u, 0x02u); sd(t5, 5);               // srl t5, t0, 28
    a.r(0u, t0, t6, 28u, 0x03u); sd(t6, 6);               // sra t6, t0, 28
    addiu(t7, zero, 8);
    a.r(t7, t2, t3, 0u, 0x14u); sd(t3, 7);                // dsllv t3, t2, t7
    a.r(t7, t0, t3, 0u, 0x17u); sd(t3, 8);                // dsrav
    a.r(t7, t0, t3, 0u, 0x16u); sd(t3, 9);                // dsrlv
    a.r(t7, t0, t3, 0u, 0x07u); sd(t3, 10);               // srav
    a.r(t7, t0, t3, 0u, 0x06u); sd(t3, 11);               // srlv
    a.r(t7, t2, t3, 0u, 0x04u); sd(t3, 12);               // sllv
    a.r(0u, t2, t3, 4u, 0x38u); sd(t3, 13);               // dsll
    a.r(0u, t0, t3, 4u, 0x3au); sd(t3, 14);               // dsrl
    a.r(0u, t0, t3, 4u, 0x3bu); sd(t3, 15);               // dsra
    a.r(t0, t2, t3, 0u, 0x24u); sd(t3, 16);               // and
    a.r(t0, t2, t3, 0u, 0x25u); sd(t3, 17);               // or
    a.r(t0, t2, t3, 0u, 0x26u); sd(t3, 18);               // xor
    a.r(t0, zero, t3, 0u, 0x27u); sd(t3, 19);             // nor
    a.i(0x0cu, t0, t3, 0xffffu); sd(t3, 20);              // andi
    a.i(0x0eu, t2, t3, 0x00ffu); sd(t3, 21);              // xori
    a.i(0x19u, t2, t3, 1u); sd(t3, 22);                   // daddiu
    a.r(zero, t3, t4, 0u, 0x2fu); sd(t4, 23);             // dsubu t4, zero, t3
    a.r(t4, t2, t4, 0u, 0x2du); sd(t4, 24);               // daddu
    a.r(t2, t2, t4, 0u, 0x21u); sd(t4, 25);               // addu (32-bit, sign-extended)
    a.r(zero, t2, t4, 0u, 0x23u); sd(t4, 26);             // subu
    a.r(t0, zero, t4, 0u, 0x2au); sd(t4, 27);             // slt (64-bit signed)
    a.r(t0, t2, t4, 0u, 0x2bu); sd(t4, 28);               // sltu (64-bit unsigned)
    a.i(0x0bu, t2, t4, 0xffffu); sd(t4, 29);              // sltiu, imm sign-extended
    a.i(0x0au, t2, t4, 0u); sd(t4, 30);                   // slti: 0xFFFFFFFF is positive in 64 bits
    addiu(t5, zero, 55);
    addiu(t6, zero, 3);
    a.r(t5, zero, t6, 0u, 0x0au); sd(t6, 31);             // movz t6, t5, zero
    addiu(t7, zero, 4);
    a.r(t5, zero, t7, 0u, 0x0bu); sd(t7, 32);             // movn t7, t5, zero (no move)
    addiu(a1, zero, -7);
    addiu(a2, zero, 6);
    a.r(a1, a2, t3, 0u, 0x18u); sd(t3, 33);               // mult t3, a1, a2 (R5900 3-operand)
    a.r(0u, 0u, t4, 0u, 0x10u); sd(t4, 34);               // mfhi
    addiu(a3, zero, -1);
    addiu(t5, zero, 2);
    a.r(a3, t5, 0u, 0u, 0x19u);                           // multu a3, t5
    a.r(0u, 0u, t4, 0u, 0x12u); sd(t4, 35);               // mflo
    a.r(0u, 0u, t4, 0u, 0x10u); sd(t4, 36);               // mfhi
    a.r(a1, t5, 0u, 0u, 0x1au);                           // div a1, t5
    a.r(0u, 0u, t4, 0u, 0x12u); sd(t4, 37);
    a.r(0u, 0u, t4, 0u, 0x10u); sd(t4, 38);
    a.r(a3, t5, 0u, 0u, 0x1bu);                           // divu a3, t5
    a.r(0u, 0u, t4, 0u, 0x12u); sd(t4, 39);
    a.r(0u, 0u, t4, 0u, 0x10u); sd(t4, 40);
    a.r(a1, zero, 0u, 0u, 0x1au);                         // div by zero
    a.r(0u, 0u, t4, 0u, 0x12u); sd(t4, 41);
    a.r(0u, 0u, t4, 0u, 0x10u); sd(t4, 42);
    a.r(a2, zero, 0u, 0u, 0x1bu);                         // divu by zero
    a.r(0u, 0u, t4, 0u, 0x12u); sd(t4, 43);
    a.r(0u, 0u, t4, 0u, 0x10u); sd(t4, 44);
    a.i(0x0fu, 0u, t3, 0x8000u);
    a.r(t3, a3, 0u, 0u, 0x1au);                           // INT32_MIN / -1
    a.r(0u, 0u, t4, 0u, 0x12u); sd(t4, 45);
    a.r(0u, 0u, t4, 0u, 0x10u); sd(t4, 46);
    a.r(a2, 0u, 0u, 0u, 0x11u);                           // mthi a2
    a.r(a1, 0u, 0u, 0u, 0x13u);                           // mtlo a1
    a.r(0u, 0u, t4, 0u, 0x10u); sd(t4, 47);
    a.r(0u, 0u, t4, 0u, 0x12u); sd(t4, 48);
    addiu(t3, zero, -2);
    a.i(0x28u, s6, t3, kScratch + 0u);                    // sb
    a.i(0x20u, s6, t4, kScratch + 0u); sd(t4, 49);        // lb
    a.i(0x24u, s6, t4, kScratch + 0u); sd(t4, 50);        // lbu
    addiu(t3, zero, -32768);
    a.i(0x29u, s6, t3, kScratch + 2u);                    // sh
    a.i(0x21u, s6, t4, kScratch + 2u); sd(t4, 51);        // lh
    a.i(0x25u, s6, t4, kScratch + 2u); sd(t4, 52);        // lhu
    a.i(kSw, s6, t0, kScratch + 4u);
    a.i(kLw, s6, t4, kScratch + 4u); sd(t4, 53);          // lw (sign-extended)
    a.i(0x27u, s6, t4, kScratch + 4u); sd(t4, 54);        // lwu
    a.i(kSd, s6, t0, kScratch + 8u);
    a.i(kLd, s6, t4, kScratch + 8u); sd(t4, 55);          // ld
    a.i(0x1eu, s6, t5, kScratch + 0x0cu);                 // lq: low 4 address bits ignored
    a.i(0x1fu, s6, t5, kScratch + 0x10u);                 // sq
    a.i(kLd, s6, t4, kScratch + 0x10u); sd(t4, 56);
    a.i(kLd, s6, t4, kScratch + 0x18u); sd(t4, 57);
    a.i(0x1fu, s6, t5, kScratch + 0x27u);                 // sq to 0x2A7 stores at 0x2A0
    a.i(kLd, s6, t4, kScratch + 0x28u); sd(t4, 58);
    a.i(0x19u, t5, t5, 1u);                               // daddiu keeps the upper 64 bits
    a.i(0x1fu, s6, t5, kScratch + 0x30u);
    a.i(kLd, s6, t4, kScratch + 0x30u); sd(t4, 59);
    a.i(kLd, s6, t4, kScratch + 0x38u); sd(t4, 60);
    // Branches: each correct outcome sets one bit; 0x4000/0x8000 must stay clear.
    daddu(s2, zero, zero);
    addiu(t3, zero, -5);
    a.regimm(0x00u, t3, "L1"); mark(0x1); mark(0x8000);           // bltz taken
    a.label("L1");
    a.regimm(0x01u, t3, "L2"); mark(0x2);                         // bgez not taken
    a.label("L2");
    a.branch(0x06u, zero, 0u, "L3"); a.nop(); mark(0x8000);       // blez taken
    a.label("L3");
    a.branch(0x07u, t2, 0u, "L4"); a.nop(); mark(0x8000);         // bgtz: 64-bit positive
    a.label("L4");
    a.branch(0x14u, zero, t3, "L5"); mark(0x4000); mark(0x4);     // beql not taken: nullified
    a.label("L5");
    a.branch(0x15u, zero, t3, "L6"); mark(0x8); mark(0x8000);     // bnel taken
    a.label("L6");
    a.regimm(0x03u, t3, "L7"); mark(0x4000); mark(0x10);          // bgezl not taken
    a.label("L7");
    a.regimm(0x02u, t3, "L8"); mark(0x20); mark(0x8000);          // bltzl taken
    a.label("L8");
    a.branch(0x16u, t2, 0u, "L9"); mark(0x4000); mark(0x40);      // blezl not taken
    a.label("L9");
    a.branch(0x17u, t2, 0u, "L10"); mark(0x80); mark(0x8000);     // bgtzl taken
    a.label("L10");
    a.label("bgezal_site");
    a.regimm(0x11u, zero, "L11"); mark(0x100); mark(0x8000);      // bgezal taken, links
    a.label("L11");
    sd(ra, kLinkSlot + 0);
    a.label("bltzal_site");
    a.regimm(0x10u, zero, "L12"); mark(0x200);                    // bltzal not taken, still links
    a.label("L12");
    sd(ra, kLinkSlot + 1);
    a.label("bltzall_site");
    a.regimm(0x12u, t3, "L13"); mark(0x400); mark(0x8000);        // bltzall taken
    a.label("L13");
    sd(ra, kLinkSlot + 2);
    a.label("bgezall_site");
    a.regimm(0x13u, t3, "L14"); mark(0x4000); mark(0x800);        // bgezall not taken: nullified
    a.label("L14");
    sd(ra, kLinkSlot + 3);
    a.raw(0x0000000fu);                                           // sync
    sd(s2, kBranchMaskSlot);
    daddu(ra, s7, zero);
    jr(ra);
    a.nop();

    auto program = a.finish();
    // R_MIPS_GPREL16 is counted as "not used as evidence", never applied.
    program.relocations.push_back({program.at("gprel_site"), kRMipsGprel16, kSymData});
    program.rodata = {program.at("a_case0"), program.at("a_case1"), program.at("a_case2"),
                      program.at("a_case3"), program.at("b_case0"), program.at("b_case1"),
                      program.at("b_case2"), 0u, program.at("u_case0"), program.at("u_case1"),
                      program.at("e_case0"), program.at("e_case1")};
    return program;
}

namespace detail {
inline void put16(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    b.at(at) = static_cast<std::uint8_t>(v);
    b.at(at + 1) = static_cast<std::uint8_t>(v >> 8);
}
inline void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (unsigned n = 0; n < 4; ++n) b.at(at + n) = static_cast<std::uint8_t>(v >> (8 * n));
}
inline std::size_t align16(std::size_t value) { return (value + 15u) & ~std::size_t{15u}; }
} // namespace detail

// ET_EXEC with two PT_LOADs (R+X text, R+W rodata/data). With sections it also
// carries .symtab/.strtab and a .rel.text that keeps the linked relocations
// (as `ld --emit-relocs` would); without sections it is fully stripped.
inline std::vector<std::uint8_t> build_elf(const Program& program, bool with_sections) {
    using detail::put16;
    using detail::put32;
    const std::size_t text_offset = 0x100u;
    const auto text_size = static_cast<std::uint32_t>(program.text.size() * 4u);
    const auto data_offset = detail::align16(text_offset + text_size);
    const std::uint32_t data_size = (kResults - kDataBase) + kResultsSize;
    std::size_t end = data_offset + data_size;

    std::vector<std::uint8_t> strtab{0};
    std::vector<std::uint32_t> name_offsets;
    for (const auto* name : kSymbolNames) {
        if (*name == 0) {
            name_offsets.push_back(0u); // null and STT_SECTION symbols are unnamed
            continue;
        }
        name_offsets.push_back(static_cast<std::uint32_t>(strtab.size()));
        for (const char* c = name; *c != 0; ++c) strtab.push_back(static_cast<std::uint8_t>(*c));
        strtab.push_back(0);
    }
    const std::string shstrtab_text = std::string("\0.text\0.rodata\0.data\0.symtab\0.strtab\0.rel.text\0.shstrtab\0", 57);
    const std::size_t symtab_offset = detail::align16(end);
    const std::size_t strtab_offset = symtab_offset + kSymCount * 16u;
    const std::size_t rel_offset = detail::align16(strtab_offset + strtab.size());
    const std::size_t shstrtab_offset = rel_offset + program.relocations.size() * 8u;
    const std::size_t sections_offset = detail::align16(shstrtab_offset + shstrtab_text.size());
    constexpr unsigned kSections = 8;
    if (with_sections) end = sections_offset + kSections * 40u;

    std::vector<std::uint8_t> b(end, 0);
    b[0] = 0x7f; b[1] = 'E'; b[2] = 'L'; b[3] = 'F'; b[4] = 1; b[5] = 1; b[6] = 1;
    put16(b, 16, 2); put16(b, 18, 8); put32(b, 20, 1);
    put32(b, 24, program.at("main")); put32(b, 28, 52); put16(b, 40, 52);
    put16(b, 42, 32); put16(b, 44, 2);
    // PT_LOAD #1: R+X text. PT_LOAD #2: R+W rodata/data, file-backed.
    put32(b, 52, 1); put32(b, 56, static_cast<std::uint32_t>(text_offset)); put32(b, 60, kTextBase);
    put32(b, 64, kTextBase); put32(b, 68, text_size); put32(b, 72, text_size);
    put32(b, 76, 5); put32(b, 80, 0x10);
    put32(b, 84, 1); put32(b, 88, static_cast<std::uint32_t>(data_offset)); put32(b, 92, kDataBase);
    put32(b, 96, kDataBase); put32(b, 100, data_size); put32(b, 104, data_size);
    put32(b, 108, 6); put32(b, 112, 0x10);
    for (std::size_t n = 0; n < program.text.size(); ++n) put32(b, text_offset + 4u * n, program.text[n]);
    for (std::size_t n = 0; n < program.rodata.size(); ++n) put32(b, data_offset + 4u * n, program.rodata[n]);
    if (!with_sections) return b;

    // .symtab: null, three STT_SECTION locals, then global STT_FUNC symbols.
    const std::array<std::uint32_t, 4> section_values{0u, kTextBase, kDataBase, kResults};
    for (std::uint32_t index = 1; index < kSymCount; ++index) {
        const auto at = symtab_offset + 16u * index;
        put32(b, at, name_offsets[index]);
        if (index <= kSymData) {
            put32(b, at + 4u, section_values[index]);
            b[at + 12u] = 0x03;                             // STB_LOCAL | STT_SECTION
            put16(b, at + 14u, index);                       // sections 1..3 match symbols 1..3
        } else {
            put32(b, at + 4u, program.at(kSymbolNames[index]));
            b[at + 12u] = 0x12;                             // STB_GLOBAL | STT_FUNC
            put16(b, at + 14u, 1u);
        }
    }
    for (std::size_t n = 0; n < strtab.size(); ++n) b[strtab_offset + n] = strtab[n];
    for (std::size_t n = 0; n < program.relocations.size(); ++n) {
        const auto& relocation = program.relocations[n];
        put32(b, rel_offset + 8u * n, relocation.site);
        put32(b, rel_offset + 8u * n + 4u, (relocation.symbol << 8) | relocation.type);
    }
    for (std::size_t n = 0; n < shstrtab_text.size(); ++n)
        b[shstrtab_offset + n] = static_cast<std::uint8_t>(shstrtab_text[n]);

    put32(b, 32, static_cast<std::uint32_t>(sections_offset));
    put16(b, 46, 40); put16(b, 48, kSections); put16(b, 50, 7);
    struct Header { std::uint32_t name, type, flags, addr, offset, size, link, info, align, entsize; };
    const std::array<Header, kSections> headers{{
        {},
        {1, 1, 6, kTextBase, static_cast<std::uint32_t>(text_offset), text_size, 0, 0, 16, 0},
        {7, 1, 2, kDataBase, static_cast<std::uint32_t>(data_offset), kRodataSize, 0, 0, 16, 0},
        {15, 1, 3, kResults, static_cast<std::uint32_t>(data_offset + (kResults - kDataBase)), kResultsSize, 0, 0, 16, 0},
        {21, 2, 0, 0, static_cast<std::uint32_t>(symtab_offset), kSymCount * 16u, 5, kSymMain, 4, 16},
        {29, 3, 0, 0, static_cast<std::uint32_t>(strtab_offset), static_cast<std::uint32_t>(strtab.size()), 0, 0, 1, 0},
        {37, 9, 0, 0, static_cast<std::uint32_t>(rel_offset),
         static_cast<std::uint32_t>(program.relocations.size() * 8u), 4, 1, 4, 8},
        {47, 3, 0, 0, static_cast<std::uint32_t>(shstrtab_offset),
         static_cast<std::uint32_t>(shstrtab_text.size()), 0, 0, 1, 0},
    }};
    for (unsigned index = 0; index < kSections; ++index) {
        const auto at = sections_offset + 40u * index;
        const auto& h = headers[index];
        put32(b, at, h.name); put32(b, at + 4u, h.type); put32(b, at + 8u, h.flags);
        put32(b, at + 12u, h.addr); put32(b, at + 16u, h.offset); put32(b, at + 20u, h.size);
        put32(b, at + 24u, h.link); put32(b, at + 28u, h.info); put32(b, at + 32u, h.align);
        put32(b, at + 36u, h.entsize);
    }
    return b;
}
} // namespace m13
