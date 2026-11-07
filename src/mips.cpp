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
        case MipsOpcode::Bc1f: case MipsOpcode::Bc1t:
        case MipsOpcode::Bc0f: case MipsOpcode::Bc0t:
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
        case MipsOpcode::Bc1fl: case MipsOpcode::Bc1tl:
        case MipsOpcode::Bc0fl: case MipsOpcode::Bc0tl:
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
        case MipsOpcode::Add: case MipsOpcode::Sub: case MipsOpcode::Dadd: case MipsOpcode::Dsub:
        case MipsOpcode::Addu: case MipsOpcode::Subu: case MipsOpcode::Daddu:
        case MipsOpcode::Dsubu: case MipsOpcode::And: case MipsOpcode::Or:
        case MipsOpcode::Xor: case MipsOpcode::Nor: case MipsOpcode::Slt:
        case MipsOpcode::Sltu: case MipsOpcode::Movz: case MipsOpcode::Movn:
        case MipsOpcode::Mult: case MipsOpcode::Multu:
        case MipsOpcode::Madd: case MipsOpcode::Maddu: case MipsOpcode::Madd1: case MipsOpcode::Maddu1:
        case MipsOpcode::Mfhi: case MipsOpcode::Mflo: case MipsOpcode::Mfsa: case MipsOpcode::Pmfhi: case MipsOpcode::Pmflo: case MipsOpcode::Jalr:
        case MipsOpcode::Mult1: case MipsOpcode::Multu1: case MipsOpcode::Mfhi1:
        case MipsOpcode::Mflo1: case MipsOpcode::Padduw: case MipsOpcode::Pand:
        case MipsOpcode::Pxor: case MipsOpcode::Por: case MipsOpcode::Pnor: case MipsOpcode::Pcpyh: case MipsOpcode::Pcpyld: case MipsOpcode::Pcpyud:
        case MipsOpcode::Pextlw: case MipsOpcode::Pextuw: case MipsOpcode::Ppacw: case MipsOpcode::Qfsrv:
        case MipsOpcode::Psubb: case MipsOpcode::Psubw: case MipsOpcode::Plzcw:
            target = rd;
            break;
        case MipsOpcode::Addi: case MipsOpcode::Daddi: case MipsOpcode::Addiu: case MipsOpcode::Daddiu: case MipsOpcode::Slti:
        case MipsOpcode::Sltiu: case MipsOpcode::Andi: case MipsOpcode::Ori:
        case MipsOpcode::Xori: case MipsOpcode::Lui:
        case MipsOpcode::Lb: case MipsOpcode::Lbu: case MipsOpcode::Lh:
        case MipsOpcode::Lhu: case MipsOpcode::Lw: case MipsOpcode::Lwu:
        case MipsOpcode::Ld: case MipsOpcode::Lq: case MipsOpcode::Mfc0: case MipsOpcode::Mfc1: case MipsOpcode::Cfc1: case MipsOpcode::Cfc2: case MipsOpcode::Qmfc2:
        case MipsOpcode::Lwl: case MipsOpcode::Lwr: case MipsOpcode::Ldl: case MipsOpcode::Ldr:
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

const char* mips_opcode_name(MipsOpcode opcode) noexcept {
    switch (opcode) {
#define PS2N_OPCODE_NAME(name) case MipsOpcode::name: return #name;
        PS2N_R5900_AOT_OPCODES(PS2N_OPCODE_NAME)
        PS2N_R5900_SYSTEM_OPCODES(PS2N_OPCODE_NAME)
#undef PS2N_OPCODE_NAME
    }
    return "?";
}

bool mips_aot_supported(MipsOpcode opcode) noexcept {
    if (opcode == MipsOpcode::Tlbwr) return false;
    switch (opcode) {
#define PS2N_OPCODE_AOT(name) case MipsOpcode::name: return true;
        PS2N_R5900_AOT_OPCODES(PS2N_OPCODE_AOT)
        PS2N_R5900_SYSTEM_OPCODES(PS2N_OPCODE_AOT)
#undef PS2N_OPCODE_AOT
        default: return false;
    }
}

std::optional<MipsInstruction> try_decode_r5900(std::uint32_t pc, std::uint32_t word) noexcept {
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
            // The 20-bit code field of SYSCALL/BREAK is free for software use.
            case 0x0c: return make(MipsOpcode::Syscall);
            case 0x0d: return make(MipsOpcode::Break);
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
            case 0x20: if (sa == 0) return make(MipsOpcode::Add); break;
            case 0x21: if (sa == 0) return make(MipsOpcode::Addu); break;
            case 0x22: if (sa == 0) return make(MipsOpcode::Sub); break;
            case 0x23: if (sa == 0) return make(MipsOpcode::Subu); break;
            case 0x24: if (sa == 0) return make(MipsOpcode::And); break;
            case 0x25: if (sa == 0) return make(MipsOpcode::Or); break;
            case 0x26: if (sa == 0) return make(MipsOpcode::Xor); break;
            case 0x27: if (sa == 0) return make(MipsOpcode::Nor); break;
            case 0x28: if (rs == 0 && rt == 0 && sa == 0) return make(MipsOpcode::Mfsa); break;
            case 0x29: if (rt == 0 && rd == 0 && sa == 0) return make(MipsOpcode::Mtsa); break;
            case 0x2a: if (sa == 0) return make(MipsOpcode::Slt); break;
            case 0x2b: if (sa == 0) return make(MipsOpcode::Sltu); break;
            case 0x2c: if (sa == 0) return make(MipsOpcode::Dadd); break;
            case 0x2d: if (sa == 0) return make(MipsOpcode::Daddu); break;
            case 0x2e: if (sa == 0) return make(MipsOpcode::Dsub); break;
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
            case 0x18: return make(MipsOpcode::Mtsab);
            case 0x19: return make(MipsOpcode::Mtsah);
            default: break;
        }
    } else if (op == 0x1c) {
        // MMI: pipeline-1 HI/LO moves, multiply and divide (same field rules
        // as their SPECIAL counterparts) and the MMI1/MMI2/MMI3 sub-tables
        // (sa field).
        switch (funct) {
            case 0x00: if(sa==0)return make(MipsOpcode::Madd); break;
            case 0x01: if(sa==0)return make(MipsOpcode::Maddu); break;
            case 0x20: if(sa==0)return make(MipsOpcode::Madd1); break;
            case 0x21: if(sa==0)return make(MipsOpcode::Maddu1); break;
            case 0x04: if(rt==0 && sa==0)return make(MipsOpcode::Plzcw); break;
            case 0x10: if (rs == 0 && rt == 0 && sa == 0) return make(MipsOpcode::Mfhi1); break;
            case 0x11: if (rt == 0 && rd == 0 && sa == 0) return make(MipsOpcode::Mthi1); break;
            case 0x12: if (rs == 0 && rt == 0 && sa == 0) return make(MipsOpcode::Mflo1); break;
            case 0x13: if (rt == 0 && rd == 0 && sa == 0) return make(MipsOpcode::Mtlo1); break;
            case 0x18: if (sa == 0) return make(MipsOpcode::Mult1); break;
            case 0x19: if (sa == 0) return make(MipsOpcode::Multu1); break;
            case 0x1a: if (rd == 0 && sa == 0) return make(MipsOpcode::Div1); break;
            case 0x1b: if (rd == 0 && sa == 0) return make(MipsOpcode::Divu1); break;
            case 0x08:
                if(sa==1)return make(MipsOpcode::Psubw);
                if(sa==9)return make(MipsOpcode::Psubb);
                if(sa==0x12)return make(MipsOpcode::Pextlw);
                if(sa==0x13)return make(MipsOpcode::Ppacw);
                break;
            case 0x28:
                if (sa == 0x10) return make(MipsOpcode::Padduw);
                if (sa == 0x12) return make(MipsOpcode::Pextuw);
                if (sa == 0x1B) return make(MipsOpcode::Qfsrv);
                break;
            case 0x09:
                if(rs==0 && rt==0 && sa==8)return make(MipsOpcode::Pmfhi);
                if(rs==0 && rt==0 && sa==9)return make(MipsOpcode::Pmflo);
                if (sa == 0x12) return make(MipsOpcode::Pand);
                if (sa == 0x13) return make(MipsOpcode::Pxor);
                if (sa == 0x0E) return make(MipsOpcode::Pcpyld);
                break;
            case 0x29:
                if(sa==0x0E)return make(MipsOpcode::Pcpyud);
                if(rt==0 && rd==0 && sa==8)return make(MipsOpcode::Pmthi);
                if(rt==0 && rd==0 && sa==9)return make(MipsOpcode::Pmtlo);
                if (sa == 0x12) return make(MipsOpcode::Por);
                if (sa == 0x13) return make(MipsOpcode::Pnor);
                if (sa == 0x1B && rs == 0) return make(MipsOpcode::Pcpyh);
                break;
            default: break;
        }
    } else if (op == 0x10) {
        // COP0. MFC0/MTC0 with a non-zero low field are the debug/performance
        // sub-encodings (MFBPC, MFPC, ...), which are not decoded.
        if ((word & 0x7ffu) == 0u) {
            if (rs == 0x00) return make(MipsOpcode::Mfc0);
            if (rs == 0x04) return make(MipsOpcode::Mtc0);
        }
        if(rs==8u && rt<4u) {
            constexpr MipsOpcode branches[]{MipsOpcode::Bc0f,MipsOpcode::Bc0t,MipsOpcode::Bc0fl,MipsOpcode::Bc0tl};
            return make(branches[rt]);
        }
        if (rs == 0x10 && rt == 0 && rd == 0 && sa == 0) {
            switch (funct) {
                case 0x01: return make(MipsOpcode::Tlbr);
                case 0x02: return make(MipsOpcode::Tlbwi);
                case 0x06: return make(MipsOpcode::Tlbwr);
                case 0x08: return make(MipsOpcode::Tlbp);
                case 0x18: return make(MipsOpcode::Eret);
                case 0x38: return make(MipsOpcode::Ei);
                case 0x39: return make(MipsOpcode::Di);
                default: break;
            }
        }
    } else if (op == 0x12) {
        if (rs==16u && funct==0x38u)return make(MipsOpcode::Vcallms);
        if (word==0x4A0002FFu) return make(MipsOpcode::Vnop);
        if (rs>=16u && (word&0x7FCu)==0x17Cu) return make(MipsOpcode::Vftoi);
        if (word==0x4A0003BFu) return make(MipsOpcode::Vwaitq);
        if (rs>=16u && (word&0x7FFu)==0x33Cu) return make(MipsOpcode::Vmove);
        if (rs>=16u && (word&0x7FFu)==0x3BCu) return make(MipsOpcode::Vdiv);
        // The VU manual's VSQRT diagram clears fsf, whereas the EE assembler
        // encoding and original ROM code set bit 21. Both select only ft.ftf;
        // retain the reserved bit-22/fs-register checks rather than accepting
        // arbitrary unused fields. This does not change the SQRT operand lane.
        if (rs>=16u && !(rs&2u) && rd==0u && (word&0x7FFu)==0x3BDu) return make(MipsOpcode::Vsqrt);
        if (rs>=16u && (word&0x7FFu)==0x3BEu) return make(MipsOpcode::Vrsqrt);
        if (rs>=16u && rt==0u && funct==0x20u) return make(MipsOpcode::VaddQ);
        if (rs>=16u && rt==0u && funct==0x24u) return make(MipsOpcode::VsubQ);
        if (rs>=16u && rt==0u && funct==0x1Cu) return make(MipsOpcode::VmulQ);
        if (rs==16u && funct==0x30u && rt<16u && rd<16u && sa<16u) return make(MipsOpcode::Viadd);
        if (rs>=16u && funct==0x2Cu) return make(MipsOpcode::Vsub);
        if (rs>=16u && funct==0x28u) return make(MipsOpcode::Vadd);
        if (rs>=16u && (funct&0x3Cu)==0u) return make(MipsOpcode::VaddBc);
        if (rs>=16u && (funct&0x3Cu)==4u) return make(MipsOpcode::VsubBc);
        if (rs>=16u && (word&0x7FCu)==0x03Cu) return make(MipsOpcode::VaddaBc);
        if (rs==30u && (word&0x7FFu)==0x2FEu) return make(MipsOpcode::Vopmula);
        if (rs==30u && funct==0x2Eu) return make(MipsOpcode::Vopmsub);
        if (rs>=16u && funct==0x2Au) return make(MipsOpcode::Vmul);
        if (rs>=16u && funct==0x29u) return make(MipsOpcode::Vmadd);
        if (rs>=16u && (funct&0x3Cu)==0x18u) return make(MipsOpcode::VmulBc);
        if (rs>=16u && (funct&0x3Cu)==0x08u) return make(MipsOpcode::VmaddBc);
        if (rs>=16u && (word&0x7FCu)==0x1BCu) return make(MipsOpcode::VmulaBc);
        if (rs>=16u && (word&0x7FCu)==0x0BCu) return make(MipsOpcode::VmaddaBc);
        if (rs>=16u && (word&0x7FFu)==0x2BEu) return make(MipsOpcode::Vmula);
        if (rs>=16u && (word&0x7FFu)==0x2BDu) return make(MipsOpcode::Vmadda);
        if (rs>=16u && (word&0x7FFu)==0x33Du) return make(MipsOpcode::Vmr32);
        if (rs>=16u && (word&0x7FFu)==0x37Du && rt<16u) return make(MipsOpcode::Vsqi);
        if (rs>=16u && (word&0x7FFu)==0x3FFu && rt<16u && rd<16u) return make(MipsOpcode::Viswr);
        if (word & 0x7FEu) return std::nullopt;
        if (rs == 1u) return make(MipsOpcode::Qmfc2);
        if (rs == 2u) return make(MipsOpcode::Cfc2);
        if (rs == 5u) return make(MipsOpcode::Qmtc2);
        if (rs == 6u) return make(MipsOpcode::Ctc2);
    } else if (op == 0x11) {
        if(rs==8 && rt<4u) {
            constexpr MipsOpcode branches[]={MipsOpcode::Bc1f,MipsOpcode::Bc1t,MipsOpcode::Bc1fl,MipsOpcode::Bc1tl};
            return make(branches[rt]);
        }
        if(rs==16 && sa==0) {
            if(funct==0x30)return make(MipsOpcode::CFalseS);
            if(funct==0x32)return make(MipsOpcode::CEqualS);
            if(funct==0x34)return make(MipsOpcode::CLessS);
            if(funct==0x36)return make(MipsOpcode::CLessEqualS);
        }
        if(rs==16 && rt==0 && funct==0x24)return make(MipsOpcode::CvtWS);
        if(rs==20 && rt==0 && funct==0x20)return make(MipsOpcode::CvtSW);
        if(rs==16 && funct==0)return make(MipsOpcode::AddS);
        if(rs==16 && funct==1)return make(MipsOpcode::SubS);
        if(rs==16 && funct==2)return make(MipsOpcode::MulS);
        if(rs==16 && funct==3)return make(MipsOpcode::DivS);
        if(rs==16 && rd==0 && funct==4)return make(MipsOpcode::SqrtS);
        if(rs==16 && rt==0 && funct==5)return make(MipsOpcode::AbsS);
        if(rs==16 && rt==0 && funct==6)return make(MipsOpcode::MovS);
        if(rs==16 && rt==0 && funct==7)return make(MipsOpcode::NegS);
        if(rs==16 && funct==0x1Cu)return make(MipsOpcode::MaddS);
        if(rs==16 && funct==0x18u && sa==0)return make(MipsOpcode::AddaS);
        if(rs==16 && funct==0x1Au && sa==0)return make(MipsOpcode::MulaS);
        if(rs==16 && funct==0x1Fu && sa==0)return make(MipsOpcode::MsubaS);
        if(rs==16 && funct==0x1Eu && sa==0)return make(MipsOpcode::MaddaS);
        if(rs==16 && funct==0x1Du)return make(MipsOpcode::MsubS);
        if(word&0x7FFu)return std::nullopt;
        switch(rs) {
            case 0: return make(MipsOpcode::Mfc1);
            case 2: return make(MipsOpcode::Cfc1);
            case 4: return make(MipsOpcode::Mtc1);
            case 6: return make(MipsOpcode::Ctc1);
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
            case 0x08: return make(MipsOpcode::Addi);
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
            case 0x18: return make(MipsOpcode::Daddi);
            case 0x19: return make(MipsOpcode::Daddiu);
            case 0x1a: return make(MipsOpcode::Ldl);
            case 0x1b: return make(MipsOpcode::Ldr);
            case 0x1e: return make(MipsOpcode::Lq);
            case 0x1f: return make(MipsOpcode::Sq);
            case 0x20: return make(MipsOpcode::Lb);
            case 0x21: return make(MipsOpcode::Lh);
            case 0x22: return make(MipsOpcode::Lwl);
            case 0x23: return make(MipsOpcode::Lw);
            case 0x24: return make(MipsOpcode::Lbu);
            case 0x25: return make(MipsOpcode::Lhu);
            case 0x26: return make(MipsOpcode::Lwr);
            case 0x27: return make(MipsOpcode::Lwu);
            case 0x28: return make(MipsOpcode::Sb);
            case 0x29: return make(MipsOpcode::Sh);
            case 0x2a: return make(MipsOpcode::Swl);
            case 0x2b: return make(MipsOpcode::Sw);
            case 0x2c: return make(MipsOpcode::Sdl);
            case 0x2d: return make(MipsOpcode::Sdr);
            case 0x2e: return make(MipsOpcode::Swr);
            case 0x2f: return make(MipsOpcode::Cache);
            case 0x31: return make(MipsOpcode::Lwc1);
            case 0x33: return make(MipsOpcode::Pref);
            case 0x37: return make(MipsOpcode::Ld);
            case 0x39: return make(MipsOpcode::Swc1);
            case 0x36: return make(MipsOpcode::Lqc2);
            case 0x3e: return make(MipsOpcode::Sqc2);
            case 0x3f: return make(MipsOpcode::Sd);
            default: break;
        }
    }
    return std::nullopt;
}

namespace {
// Encoding class of an instruction the interpreter does not implement.
const char* encoding_class(std::uint32_t word) {
    switch (word >> 26) {
        case 0x00: return "SPECIAL";
        case 0x01: return "REGIMM";
        case 0x10: return "COP0";
        case 0x11: case 0x31: case 0x39: return "COP1/FPU";
        case 0x12: case 0x36: case 0x3e: return "COP2/VU0 macro";
        case 0x1c: return "MMI";
        case 0x1a: case 0x1b: case 0x22: case 0x26: case 0x2a: case 0x2c: case 0x2d: case 0x2e:
            return "unaligned load/store";
        default: return "other";
    }
}
} // namespace

MipsInstruction decode_r5900(std::uint32_t pc, std::uint32_t word) {
    if (const auto decoded = try_decode_r5900(pc, word)) return *decoded;
    throw std::runtime_error(std::string("R5900 instruction ") + hex32(word) + " at " + hex32(pc) +
                             " (" + encoding_class(word) + ") not implemented by the interpreter");
}

MipsInstruction decode_mips(std::uint32_t pc, std::uint32_t word) {
    const auto decoded = try_decode_r5900(pc, word);
    if (!decoded || !mips_aot_supported(decoded->opcode))
        throw std::runtime_error("AOT unsupported R5900 instruction " + hex32(word) +
                                 " at " + hex32(pc) + " (no fallback/stub)");
    return *decoded;
}
} // namespace ps2native
