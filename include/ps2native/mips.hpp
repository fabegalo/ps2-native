#pragma once

#include <cstdint>
#include <optional>

namespace ps2native {
// Explicitly supported R5900 integer subset (lower 64-bit GPR semantics).
// This decoder does NOT claim general R5900, MMI, COP0, COP1 or COP2 coverage;
// every other encoding is rejected with an explicit error.
enum class MipsOpcode : std::uint8_t {
    Nop, Sync,
    // SPECIAL shifts and register arithmetic.
    Sll, Srl, Sra, Sllv, Srlv, Srav,
    Dsll, Dsrl, Dsra, Dsll32, Dsrl32, Dsra32, Dsllv, Dsrlv, Dsrav,
    Addu, Subu, Daddu, Dsubu, And, Or, Xor, Nor, Slt, Sltu, Movz, Movn,
    Mult, Multu, Div, Divu, Mfhi, Mflo, Mthi, Mtlo,
    // Immediate arithmetic.
    Addiu, Daddiu, Slti, Sltiu, Andi, Ori, Xori, Lui,
    // Loads and stores.
    Lb, Lbu, Lh, Lhu, Lw, Lwu, Ld, Lq, Sb, Sh, Sw, Sd, Sq,
    // Branches (normal, linking and branch-likely) and jumps.
    Beq, Bne, Blez, Bgtz, Bltz, Bgez, Bltzal, Bgezal,
    Beql, Bnel, Blezl, Bgtzl, Bltzl, Bgezl, Bltzall, Bgezall,
    J, Jal, Jr, Jalr
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

[[nodiscard]] MipsInstruction decode_mips(std::uint32_t pc, std::uint32_t word);
[[nodiscard]] std::uint32_t mips_sign_extended_imm(std::uint32_t word) noexcept;
[[nodiscard]] std::uint32_t mips_branch_target(std::uint32_t pc, std::uint32_t word) noexcept;
[[nodiscard]] std::uint32_t mips_jump_target(std::uint32_t pc, std::uint32_t word) noexcept;
} // namespace ps2native
