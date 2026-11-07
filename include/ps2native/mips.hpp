#pragma once

#include <cstdint>
#include <optional>

namespace ps2native {
// R5900 integer subset with lower 64-bit GPR semantics, plus the MMI
// pipeline-1 multiply/divide group, PADDUW and the 128-bit logical
// operations PAND/PXOR/POR/PNOR. These are the only
// instructions the AOT translates; their semantics live in ee_semantics.hpp
// and are shared with the interpreter.
#define PS2N_R5900_AOT_OPCODES(X) \
    X(Nop) X(Sync) X(Cfc2) X(Ctc2) X(Qmfc2) X(Qmtc2) X(Viswr) X(Vsub) X(Vsqi) X(Viadd) \
    X(Sll) X(Srl) X(Sra) X(Sllv) X(Srlv) X(Srav) \
    X(Dsll) X(Dsrl) X(Dsra) X(Dsll32) X(Dsrl32) X(Dsra32) X(Dsllv) X(Dsrlv) X(Dsrav) \
    X(Addu) X(Subu) X(Daddu) X(Dsubu) X(And) X(Or) X(Xor) X(Nor) X(Slt) X(Sltu) X(Movz) X(Movn) \
    X(Mult) X(Multu) X(Madd) X(Maddu) X(Madd1) X(Maddu1) X(Div) X(Divu) X(Mfhi) X(Mflo) X(Mthi) X(Mtlo) \
    X(Addiu) X(Daddiu) X(Slti) X(Sltiu) X(Andi) X(Ori) X(Xori) X(Lui) \
    X(Lb) X(Lbu) X(Lh) X(Lhu) X(Lw) X(Lwu) X(Ld) X(Lq) X(Sb) X(Sh) X(Sw) X(Sd) X(Sq) \
    X(Lwl) X(Lwr) X(Ldl) X(Ldr) X(Swl) X(Swr) X(Sdl) X(Sdr) \
    X(Beq) X(Bne) X(Blez) X(Bgtz) X(Bltz) X(Bgez) X(Bltzal) X(Bgezal) \
    X(Beql) X(Bnel) X(Blezl) X(Bgtzl) X(Bltzl) X(Bgezl) X(Bltzall) X(Bgezall) \
    X(J) X(Jal) X(Jr) X(Jalr) \
    X(Mult1) X(Multu1) X(Div1) X(Divu1) X(Mfhi1) X(Mflo1) X(Mthi1) X(Mtlo1) X(Mfsa) X(Mtsa) X(Mtsab) X(Mtsah) X(Qfsrv) \
    X(Pmfhi) X(Pmflo) X(Pmthi) X(Pmtlo) X(Padduw) X(Pcpyh) X(Pcpyld) X(Pcpyud) \
    X(Pextlw) X(Pextuw) X(Ppacw) X(Psubb) X(Psubw) X(Pand) X(Pxor) X(Por) X(Pnor) X(Plzcw) \
    X(Vadd) X(Vmr32) X(Lqc2) X(Sqc2) \
    X(Vmul) X(VmulBc) X(Vmadd) X(VmaddBc) X(Vmula) X(VmulaBc) X(Vmadda) X(VmaddaBc) \
    X(VaddBc) X(VsubBc) X(Vopmula) X(Vopmsub) \
    X(Vcallms) X(Vsqrt) X(Vdiv) X(Vwaitq) X(VaddQ) X(VmulQ) X(Vmove) X(Vnop) X(Vftoi) \
    X(CFalseS) X(CEqualS) X(CLessS) X(CLessEqualS) X(Bc1f) X(Bc1t) X(Bc1fl) X(Bc1tl) \
    X(Bc0f) X(Bc0t) X(Bc0fl) X(Bc0tl)
// System encodings retain stable enum values. CACHE can be emitted by AOT
// through the shared system helper; the remaining instructions execute in
// the interpreter (boot path): COP0 moves, TLB maintenance, ERET, EI/DI,
// PREF, the COP1 word loads and
// stores LWC1/SWC1 (FPU arithmetic is not decoded) and the trap-style
// exceptions and signed arithmetic with overflow, which enter the guest's
// level-1 handler. Signed arithmetic remains an explicit AOT boundary.
#define PS2N_R5900_SYSTEM_OPCODES(X) \
    X(Mfc0) X(Mtc0) X(Tlbr) X(Tlbwi) X(Tlbwr) X(Tlbp) X(Eret) X(Ei) X(Di) \
    X(Cache) X(Pref) X(Syscall) X(Break) X(Lwc1) X(Swc1) X(Mfc1) X(Mtc1) X(Cfc1) X(Ctc1) X(MovS) X(DivS) X(MulS) X(CvtWS) X(CvtSW) X(MaddS) X(AddaS) X(Addi) \
    X(Add) X(Sub) X(Dadd) X(Dsub) X(Daddi) X(AddS) X(SubS) X(AbsS) X(NegS) X(SqrtS) X(MulaS) X(MsubaS) X(MaddaS) X(MsubS)

enum class MipsOpcode : std::uint8_t {
#define PS2N_OPCODE_ENUMERATOR(name) name,
    PS2N_R5900_AOT_OPCODES(PS2N_OPCODE_ENUMERATOR)
    PS2N_R5900_SYSTEM_OPCODES(PS2N_OPCODE_ENUMERATOR)
#undef PS2N_OPCODE_ENUMERATOR
};

struct MipsInstruction {
    std::uint32_t pc{};
    std::uint32_t word{};
    MipsOpcode opcode{};
    std::uint8_t rs{};
    std::uint8_t rt{};
    std::uint8_t rd{};
    std::uint8_t sa{};

    [[nodiscard]] bool has_delay_slot() const noexcept;
    // BEQ..BGEZALL: PC-relative, may fall through to pc + 8.
    [[nodiscard]] bool is_conditional_branch() const noexcept;
    // Branch-likely: the delay slot is nullified when the branch is not taken.
    [[nodiscard]] bool is_likely_branch() const noexcept;
    // JAL, JALR and BxxZAL(L): writes a return address (pc + 8).
    [[nodiscard]] bool links() const noexcept;
    // GPR written by this instruction (never $zero), if any.
    [[nodiscard]] std::optional<unsigned> written_gpr() const noexcept;
};

// Enumerator spelling ("Addu"), used by generated code and traces.
[[nodiscard]] const char* mips_opcode_name(MipsOpcode opcode) noexcept;
[[nodiscard]] bool mips_aot_supported(MipsOpcode opcode) noexcept;
// Any implemented encoding (integer subset and system instructions), or nullopt.
[[nodiscard]] std::optional<MipsInstruction> try_decode_r5900(std::uint32_t pc, std::uint32_t word) noexcept;
// Interpreter decoder: throws "R5900 instruction ... not implemented" with the
// encoding class (COP1, COP2/VU0 macro, MMI, ...) for anything else.
[[nodiscard]] MipsInstruction decode_r5900(std::uint32_t pc, std::uint32_t word);
// AOT decoder: only PS2N_R5900_AOT_OPCODES; everything else is rejected with
// "AOT unsupported R5900 instruction ... (no fallback/stub)".
[[nodiscard]] MipsInstruction decode_mips(std::uint32_t pc, std::uint32_t word);
[[nodiscard]] std::uint32_t mips_sign_extended_imm(std::uint32_t word) noexcept;
[[nodiscard]] std::uint32_t mips_branch_target(std::uint32_t pc, std::uint32_t word) noexcept;
[[nodiscard]] std::uint32_t mips_jump_target(std::uint32_t pc, std::uint32_t word) noexcept;
} // namespace ps2native
