#pragma once

#include <cstdint>

namespace ps2native {
// Small, explicitly supported R5900 lower-word instruction subset.
// This decoder does NOT claim general R5900, MMI, COP0, COP1 or COP2 coverage.
enum class MipsOpcode : std::uint8_t {
    Nop, Addiu, Addu, Ori, Lui, Lw, Sw, Beq, Bne, J, Jal, Jr, Jalr
};

struct MipsInstruction {
    std::uint32_t pc{};
    std::uint32_t word{};
    MipsOpcode opcode{};
    std::uint8_t rs{};
    std::uint8_t rt{};
    std::uint8_t rd{};

    [[nodiscard]] bool has_delay_slot() const noexcept;
};

[[nodiscard]] MipsInstruction decode_mips(std::uint32_t pc, std::uint32_t word);
[[nodiscard]] std::uint32_t mips_sign_extended_imm(std::uint32_t word) noexcept;
[[nodiscard]] std::uint32_t mips_branch_target(std::uint32_t pc, std::uint32_t word) noexcept;
[[nodiscard]] std::uint32_t mips_jump_target(std::uint32_t pc, std::uint32_t word) noexcept;
} // namespace ps2native
