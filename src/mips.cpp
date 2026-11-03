#include "ps2native/mips.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ps2native {
namespace {
std::string hex32(std::uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(8)
           << std::setfill('0') << value;
    return stream.str();
}
} // namespace

bool MipsInstruction::has_delay_slot() const noexcept {
    switch (opcode) {
        case MipsOpcode::Beq:
        case MipsOpcode::Bne:
        case MipsOpcode::J:
        case MipsOpcode::Jal:
        case MipsOpcode::Jr:
        case MipsOpcode::Jalr:
            return true;
        default:
            return false;
    }
}

std::uint32_t mips_sign_extended_imm(std::uint32_t word) noexcept {
    const auto imm = word & 0xffffu;
    return imm >= 0x8000u ? imm | 0xffff0000u : imm;
}
std::uint32_t mips_branch_target(std::uint32_t pc, std::uint32_t word) noexcept {
    return pc + 4u + 4u * mips_sign_extended_imm(word);
}
std::uint32_t mips_jump_target(std::uint32_t pc, std::uint32_t word) noexcept {
    return ((pc + 4u) & 0xf0000000u) | ((word & 0x03ffffffu) << 2);
}

MipsInstruction decode_mips(std::uint32_t pc, std::uint32_t word) {
    const auto op = word >> 26;
    const auto rs = static_cast<std::uint8_t>((word >> 21) & 31u);
    const auto rt = static_cast<std::uint8_t>((word >> 16) & 31u);
    const auto rd = static_cast<std::uint8_t>((word >> 11) & 31u);
    const auto shamt = (word >> 6) & 31u;
    const auto funct = word & 63u;
    auto make = [&](MipsOpcode kind) { return MipsInstruction{pc, word, kind, rs, rt, rd}; };
    if (word == 0) return make(MipsOpcode::Nop);
    if (op == 0 && shamt == 0) {
        if (funct == 0x21) return make(MipsOpcode::Addu);
        if (funct == 0x08 && rt == 0 && rd == 0) return make(MipsOpcode::Jr);
        if (funct == 0x09 && rt == 0) return make(MipsOpcode::Jalr);
    }
    switch (op) {
        case 0x09: return make(MipsOpcode::Addiu);
        case 0x0d: return make(MipsOpcode::Ori);
        case 0x0f: if (rs == 0) return make(MipsOpcode::Lui); break;
        case 0x23: return make(MipsOpcode::Lw);
        case 0x2b: return make(MipsOpcode::Sw);
        case 0x04: return make(MipsOpcode::Beq);
        case 0x05: return make(MipsOpcode::Bne);
        case 0x02: return make(MipsOpcode::J);
        case 0x03: return make(MipsOpcode::Jal);
        default: break;
    }
    throw std::runtime_error("AOT unsupported R5900 instruction " + hex32(word) +
                             " at " + hex32(pc) + " (no fallback/stub)");
}
} // namespace ps2native
