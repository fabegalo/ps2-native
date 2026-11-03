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

bool MipsInstruction::is_conditional_branch() const noexcept {
    switch (opcode) {
        case MipsOpcode::Beq: case MipsOpcode::Bne:
        case MipsOpcode::Blez: case MipsOpcode::Bgtz:
        case MipsOpcode::Bltz: case MipsOpcode::Bgez:
        case MipsOpcode::Bltzal: case MipsOpcode::Bgezal:
            return true;
        default:
            return is_likely_branch();
    }
}

bool MipsInstruction::is_likely_branch() const noexcept {
    switch (opcode) {
        case MipsOpcode::Beql: case MipsOpcode::Bnel:
        case MipsOpcode::Blezl: case MipsOpcode::Bgtzl:
        case MipsOpcode::Bltzl: case MipsOpcode::Bgezl:
        case MipsOpcode::Bltzall: case MipsOpcode::Bgezall:
            return true;
        default:
            return false;
    }
}

bool MipsInstruction::links() const noexcept {
    switch (opcode) {
        case MipsOpcode::Jal: case MipsOpcode::Jalr:
        case MipsOpcode::Bltzal: case MipsOpcode::Bgezal:
        case MipsOpcode::Bltzall: case MipsOpcode::Bgezall:
            return true;
        default:
            return false;
    }
}

bool MipsInstruction::has_delay_slot() const noexcept {
    switch (opcode) {
        case MipsOpcode::J: case MipsOpcode::Jal:
        case MipsOpcode::Jr: case MipsOpcode::Jalr:
            return true;
        default:
            return is_conditional_branch();
    }
}

std::optional<unsigned> MipsInstruction::written_gpr() const noexcept {
    unsigned target = 0;
    switch (opcode) {
        case MipsOpcode::Sll: case MipsOpcode::Srl: case MipsOpcode::Sra:
        case MipsOpcode::Sllv: case MipsOpcode::Srlv: case MipsOpcode::Srav:
        case MipsOpcode::Dsll: case MipsOpcode::Dsrl: case MipsOpcode::Dsra:
        case MipsOpcode::Dsll32: case MipsOpcode::Dsrl32: case MipsOpcode::Dsra32:
        case MipsOpcode::Dsllv: case MipsOpcode::Dsrlv: case MipsOpcode::Dsrav:
        case MipsOpcode::Addu: case MipsOpcode::Subu: case MipsOpcode::Daddu:
        case MipsOpcode::Dsubu: case MipsOpcode::And: case MipsOpcode::Or:
        case MipsOpcode::Xor: case MipsOpcode::Nor: case MipsOpcode::Slt:
        case MipsOpcode::Sltu: case MipsOpcode::Movz: case MipsOpcode::Movn:
        case MipsOpcode::Mult: case MipsOpcode::Multu:
        case MipsOpcode::Mfhi: case MipsOpcode::Mflo: case MipsOpcode::Jalr:
            target = rd;
            break;
        case MipsOpcode::Addiu: case MipsOpcode::Daddiu: case MipsOpcode::Slti:
        case MipsOpcode::Sltiu: case MipsOpcode::Andi: case MipsOpcode::Ori:
        case MipsOpcode::Xori: case MipsOpcode::Lui:
        case MipsOpcode::Lb: case MipsOpcode::Lbu: case MipsOpcode::Lh:
        case MipsOpcode::Lhu: case MipsOpcode::Lw: case MipsOpcode::Lwu:
        case MipsOpcode::Ld: case MipsOpcode::Lq:
            target = rt;
            break;
        case MipsOpcode::Jal: case MipsOpcode::Bltzal: case MipsOpcode::Bgezal:
        case MipsOpcode::Bltzall: case MipsOpcode::Bgezall:
            target = 31;
            break;
        default:
            return std::nullopt;
    }
    if (target == 0) return std::nullopt;
    return target;
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
    const auto sa = static_cast<std::uint8_t>((word >> 6) & 31u);
    const auto funct = word & 63u;
    auto make = [&](MipsOpcode kind) { return MipsInstruction{pc, word, kind, rs, rt, rd, sa}; };
    if (word == 0) return make(MipsOpcode::Nop);
    if (op == 0x00) {
        // Fields that the architecture defines as zero must be zero; anything
        // else is a different (unsupported) encoding, never "close enough".
        switch (funct) {
            case 0x00: if (rs == 0) return make(MipsOpcode::Sll); break;
            case 0x02: if (rs == 0) return make(MipsOpcode::Srl); break;
            case 0x03: if (rs == 0) return make(MipsOpcode::Sra); break;
            case 0x04: if (sa == 0) return make(MipsOpcode::Sllv); break;
            case 0x06: if (sa == 0) return make(MipsOpcode::Srlv); break;
            case 0x07: if (sa == 0) return make(MipsOpcode::Srav); break;
            case 0x08: if (rt == 0 && rd == 0 && sa == 0) return make(MipsOpcode::Jr); break;
            case 0x09: if (rt == 0 && sa == 0) return make(MipsOpcode::Jalr); break;
            case 0x0a: if (sa == 0) return make(MipsOpcode::Movz); break;
            case 0x0b: if (sa == 0) return make(MipsOpcode::Movn); break;
            // SYNC/SYNC.L/SYNC.P: stype lives in the sa field.
            case 0x0f: if (rs == 0 && rt == 0 && rd == 0) return make(MipsOpcode::Sync); break;
            case 0x10: if (rs == 0 && rt == 0 && sa == 0) return make(MipsOpcode::Mfhi); break;
            case 0x11: if (rt == 0 && rd == 0 && sa == 0) return make(MipsOpcode::Mthi); break;
            case 0x12: if (rs == 0 && rt == 0 && sa == 0) return make(MipsOpcode::Mflo); break;
            case 0x13: if (rt == 0 && rd == 0 && sa == 0) return make(MipsOpcode::Mtlo); break;
            case 0x14: if (sa == 0) return make(MipsOpcode::Dsllv); break;
            case 0x16: if (sa == 0) return make(MipsOpcode::Dsrlv); break;
            case 0x17: if (sa == 0) return make(MipsOpcode::Dsrav); break;
            // R5900 MULT/MULTU are three-operand: LO is also written to rd.
            case 0x18: if (sa == 0) return make(MipsOpcode::Mult); break;
            case 0x19: if (sa == 0) return make(MipsOpcode::Multu); break;
            case 0x1a: if (rd == 0 && sa == 0) return make(MipsOpcode::Div); break;
            case 0x1b: if (rd == 0 && sa == 0) return make(MipsOpcode::Divu); break;
            case 0x21: if (sa == 0) return make(MipsOpcode::Addu); break;
            case 0x23: if (sa == 0) return make(MipsOpcode::Subu); break;
            case 0x24: if (sa == 0) return make(MipsOpcode::And); break;
            case 0x25: if (sa == 0) return make(MipsOpcode::Or); break;
            case 0x26: if (sa == 0) return make(MipsOpcode::Xor); break;
            case 0x27: if (sa == 0) return make(MipsOpcode::Nor); break;
            case 0x2a: if (sa == 0) return make(MipsOpcode::Slt); break;
            case 0x2b: if (sa == 0) return make(MipsOpcode::Sltu); break;
            case 0x2d: if (sa == 0) return make(MipsOpcode::Daddu); break;
            case 0x2f: if (sa == 0) return make(MipsOpcode::Dsubu); break;
            case 0x38: if (rs == 0) return make(MipsOpcode::Dsll); break;
            case 0x3a: if (rs == 0) return make(MipsOpcode::Dsrl); break;
            case 0x3b: if (rs == 0) return make(MipsOpcode::Dsra); break;
            case 0x3c: if (rs == 0) return make(MipsOpcode::Dsll32); break;
            case 0x3e: if (rs == 0) return make(MipsOpcode::Dsrl32); break;
            case 0x3f: if (rs == 0) return make(MipsOpcode::Dsra32); break;
            default: break;
        }
    } else if (op == 0x01) {
        // BxxZAL(L) with rs == $ra is architecturally unpredictable: reject it.
        switch (rt) {
            case 0x00: return make(MipsOpcode::Bltz);
            case 0x01: return make(MipsOpcode::Bgez);
            case 0x02: return make(MipsOpcode::Bltzl);
            case 0x03: return make(MipsOpcode::Bgezl);
            case 0x10: if (rs != 31) return make(MipsOpcode::Bltzal); break;
            case 0x11: if (rs != 31) return make(MipsOpcode::Bgezal); break;
            case 0x12: if (rs != 31) return make(MipsOpcode::Bltzall); break;
            case 0x13: if (rs != 31) return make(MipsOpcode::Bgezall); break;
            default: break;
        }
    } else {
        switch (op) {
            case 0x02: return make(MipsOpcode::J);
            case 0x03: return make(MipsOpcode::Jal);
            case 0x04: return make(MipsOpcode::Beq);
            case 0x05: return make(MipsOpcode::Bne);
            case 0x06: if (rt == 0) return make(MipsOpcode::Blez); break;
            case 0x07: if (rt == 0) return make(MipsOpcode::Bgtz); break;
            case 0x09: return make(MipsOpcode::Addiu);
            case 0x0a: return make(MipsOpcode::Slti);
            case 0x0b: return make(MipsOpcode::Sltiu);
            case 0x0c: return make(MipsOpcode::Andi);
            case 0x0d: return make(MipsOpcode::Ori);
            case 0x0e: return make(MipsOpcode::Xori);
            case 0x0f: if (rs == 0) return make(MipsOpcode::Lui); break;
            case 0x14: return make(MipsOpcode::Beql);
            case 0x15: return make(MipsOpcode::Bnel);
            case 0x16: if (rt == 0) return make(MipsOpcode::Blezl); break;
            case 0x17: if (rt == 0) return make(MipsOpcode::Bgtzl); break;
            case 0x19: return make(MipsOpcode::Daddiu);
            case 0x1e: return make(MipsOpcode::Lq);
            case 0x1f: return make(MipsOpcode::Sq);
            case 0x20: return make(MipsOpcode::Lb);
            case 0x21: return make(MipsOpcode::Lh);
            case 0x23: return make(MipsOpcode::Lw);
            case 0x24: return make(MipsOpcode::Lbu);
            case 0x25: return make(MipsOpcode::Lhu);
            case 0x27: return make(MipsOpcode::Lwu);
            case 0x28: return make(MipsOpcode::Sb);
            case 0x29: return make(MipsOpcode::Sh);
            case 0x2b: return make(MipsOpcode::Sw);
            case 0x37: return make(MipsOpcode::Ld);
            case 0x3f: return make(MipsOpcode::Sd);
            default: break;
        }
    }
    throw std::runtime_error("AOT unsupported R5900 instruction " + hex32(word) +
                             " at " + hex32(pc) + " (no fallback/stub)");
}
} // namespace ps2native
