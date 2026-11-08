#pragma once
// Shared R3000A semantics: constant instruction operands are compiled offline.
#include "ps2native/iop.hpp"
#include <limits>
#include <iomanip>
#include <sstream>
namespace ps2native {
inline std::string iop_instruction_hex(std::uint32_t value) {
    std::ostringstream out;out<<"0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<value;return out.str();
}
template<std::uint32_t Word> void Iop::execute_native() {
    if(fetch(pc_)!=Word)throw IopExecutionStop{"IOP AOT instruction identity mismatch at "+iop_instruction_hex(pc_)};
    execute_instruction<Word>();
}
template<std::uint32_t Word> void Iop::execute_instruction() {
    const auto pc = pc_;
    constexpr std::uint32_t word = static_cast<std::uint32_t>(Word);
    const bool delay_slot = in_delay_slot_;
    const auto after = branch_target_;
    std::optional<std::uint32_t> target; // taken branch or jump of this instruction
    bool branch = false;
    std::optional<std::uint32_t> exception; // ExcCode raised by this instruction
    write_.reset();
    undefined_load_.reset();
    undefined_known_ = 0u;
    undefined_sio2_epoch_=0;
    const auto delayed = pending_load_; // becomes visible after this instruction reads its operands
    pending_load_.reset();

    const unsigned op = word >> 26, rs = (word >> 21) & 31u, rt = (word >> 16) & 31u, rd = (word >> 11) & 31u,
                   sa = (word >> 6) & 31u, fn = word & 63u;
    const auto imm = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(word & 0xFFFFu)));
    const auto uimm = word & 0xFFFFu;
    const auto unsupported = [&] { return IopExecutionStop{"instruction " + iop_instruction_hex(word) + " is not implemented"}; };
    const auto overflow = [&] { return IopExecutionStop{"integer overflow exception (not vectored)"}; };
    const auto branch_if = [&](bool taken) {
        branch = true;
        if (taken) target = pc + 4u + (imm << 2);
    };
    // LWL/LWR merge with the register as it will be after a load in flight.
    const auto merge_base = [&](unsigned r)->PendingLoad {
        if(delayed && delayed->reg==r)return *delayed;
        const bool defined=(gpr_defined_&(1u<<r))!=0u;
        return PendingLoad{r,gpr_[r],defined,defined ? std::string{} : gpr_origin_[r],defined ? ~0u : gpr_known_[r]};
    };
    const auto store_value = [&](std::uint32_t address,std::size_t bytes,unsigned r,const char* what) {
        const auto mask=bytes==4u ? ~0u : (1u<<(bytes*8u))-1u;
        const auto known=(gpr_defined_&(1u<<r)) ? ~0u : gpr_known_[r];
        if((known&mask)==mask)store(address,bytes,gpr_[r]&mask,what);
        else {
            const ObservedWord observed{gpr_[r],~known};
            store(address,bytes,std::nullopt,what,&observed,bytes==4u ? gpr_sio2_epoch_[r] : 0u);
        }
    };

    switch (op) {
        case 0x00:
            switch (fn) {
                case 0x00: shift_left(rd, rt, sa); break;                                      // SLL
                case 0x02: shift_right(rd, rt, sa); break;                                   // SRL
                case 0x03: set_reg(rd, static_cast<std::uint32_t>(static_cast<std::int32_t>(reg(rt)) >> sa)); break;
                case 0x04: shift_left(rd, rt, masked_reg(rs,31u)&31u); break;                  // SLLV
                case 0x06: shift_right(rd, rt, masked_reg(rs,31u)&31u); break;                 // SRLV
                case 0x07: set_reg(rd, static_cast<std::uint32_t>(static_cast<std::int32_t>(reg(rt)) >> (reg(rs) & 31u))); break;
                case 0x08: branch = true; target = reg(rs); break;                              // JR
                case 0x09: branch = true; target = reg(rs); set_reg(rd, pc + 8u); break;        // JALR
                case 0x0C: exception = 8u; break;                                              // SYSCALL
                case 0x0D: exception = 9u; break;                                              // BREAK
                case 0x10: if (!hi_) throw IopExecutionStop{"MFHI before any multiply/divide"}; set_reg(rd, *hi_); break;
                case 0x11: hi_ = reg(rs); break;                                                // MTHI
                case 0x12: if (!lo_) throw IopExecutionStop{"MFLO before any multiply/divide"}; set_reg(rd, *lo_); break;
                case 0x13: lo_ = reg(rs); break;                                                // MTLO
                case 0x18: {                                                                    // MULT
                    const auto p = static_cast<std::int64_t>(static_cast<std::int32_t>(reg(rs))) *
                                   static_cast<std::int32_t>(reg(rt));
                    lo_ = static_cast<std::uint32_t>(p);
                    hi_ = static_cast<std::uint32_t>(static_cast<std::uint64_t>(p) >> 32);
                    break;
                }
                case 0x19: {                                                                    // MULTU
                    const auto p = static_cast<std::uint64_t>(reg(rs)) * reg(rt);
                    lo_ = static_cast<std::uint32_t>(p);
                    hi_ = static_cast<std::uint32_t>(p >> 32);
                    break;
                }
                case 0x1A: {                                                                    // DIV
                    const auto n = static_cast<std::int32_t>(reg(rs)), d = static_cast<std::int32_t>(reg(rt));
                    // R3000A results for x/0 and INT_MIN/-1 (no exception).
                    if (d == 0) { lo_ = n >= 0 ? 0xFFFFFFFFu : 1u; hi_ = static_cast<std::uint32_t>(n); }
                    else if (n == std::numeric_limits<std::int32_t>::min() && d == -1) { lo_ = 0x80000000u; hi_ = 0u; }
                    else { lo_ = static_cast<std::uint32_t>(n / d); hi_ = static_cast<std::uint32_t>(n % d); }
                    break;
                }
                case 0x1B: {                                                                    // DIVU
                    const auto n = reg(rs), d = reg(rt);
                    if (d == 0u) { lo_ = 0xFFFFFFFFu; hi_ = n; }
                    else { lo_ = n / d; hi_ = n % d; }
                    break;
                }
                case 0x20: {                                                                    // ADD
                    const auto a = reg(rs), b = reg(rt), r = a + b;
                    if (((a ^ r) & (b ^ r)) >> 31) throw overflow();
                    set_reg(rd, r);
                    break;
                }
                case 0x21: // ADDU; either $zero operand makes this an exact move.
                    if (!rs) copy_reg(rd, rt);
                    else if (!rt) copy_reg(rd, rs);
                    else add_sub_unsigned(rd,rs,rt,false);
                    break;
                case 0x22: {                                                                    // SUB
                    const auto a = reg(rs), b = reg(rt), r = a - b;
                    if (((a ^ b) & (a ^ r)) >> 31) throw overflow();
                    set_reg(rd, r);
                    break;
                }
                case 0x23: add_sub_unsigned(rd,rs,rt,true); break;                              // SUBU
                case 0x24: and_regs(rd, rs, rt); break;                                          // AND
                case 0x25: or_value(rd,rs,gpr_[rt],(gpr_defined_&(1u<<rt)) ? ~0u : gpr_known_[rt],gpr_origin_[rt],gpr_sio2_epoch_[rt]); break;
                case 0x26: set_reg(rd, reg(rs) ^ reg(rt)); break;
                case 0x27: set_reg(rd, ~(reg(rs) | reg(rt))); break;
                case 0x2A: set_reg(rd, static_cast<std::int32_t>(reg(rs)) < static_cast<std::int32_t>(reg(rt)) ? 1u : 0u); break;
                case 0x2B: set_reg(rd, reg(rs) < reg(rt) ? 1u : 0u); break;
                default: throw unsupported();
            }
            break;
        case 0x01: {
            const auto s = static_cast<std::int32_t>(reg(rs));
            switch (rt) {
                case 0x00: branch_if(s < 0); break;                                             // BLTZ
                case 0x01: branch_if(s >= 0); break;                                            // BGEZ
                case 0x10: branch_if(s < 0); set_reg(31, pc + 8u); break;                       // BLTZAL
                case 0x11: branch_if(s >= 0); set_reg(31, pc + 8u); break;                      // BGEZAL
                default: throw unsupported();
            }
            break;
        }
        case 0x02: branch = true; target = ((pc + 4u) & 0xF0000000u) | ((word & 0x03FFFFFFu) << 2); break;  // J
        case 0x03: branch = true; target = ((pc + 4u) & 0xF0000000u) | ((word & 0x03FFFFFFu) << 2);         // JAL
                   set_reg(31, pc + 8u); break;
        case 0x04: branch_if(regs_equal(rs, rt)); break;                                    // BEQ
        case 0x05: branch_if(!regs_equal(rs, rt)); break;                                   // BNE
        case 0x06: branch_if(static_cast<std::int32_t>(reg(rs)) <= 0); break;
        case 0x07: branch_if(static_cast<std::int32_t>(reg(rs)) > 0); break;
        case 0x08: {                                                                            // ADDI
            const auto a = reg(rs), r = a + imm;
            if (((a ^ r) & (imm ^ r)) >> 31) throw overflow();
            set_reg(rt, r);
            break;
        }
        case 0x09: set_reg(rt, reg(rs) + imm); break;
        case 0x0A: set_reg(rt, static_cast<std::int32_t>(reg(rs)) < static_cast<std::int32_t>(imm) ? 1u : 0u); break;
        case 0x0B: set_reg(rt, reg(rs) < imm ? 1u : 0u); break;
        case 0x0C: and_immediate(rt, rs, uimm); break;
        case 0x0D: or_value(rt,rs,uimm,~0u,{}); break;
        case 0x0E:
            if(gpr_defined_&(1u<<rs))set_reg(rt,gpr_[rs]^uimm);
            else if(rt)write_=PendingLoad{rt,(gpr_[rs]^uimm)&gpr_known_[rs],false,gpr_origin_[rs],gpr_known_[rs]};
            break;
        case 0x0F: set_reg(rt, uimm << 16); break;
        case 0x10:                                                                              // COP0
            if (rs == 0x00) load_reg(rt, cop0_read(rd));                                        // MFC0 (load delay)
            else if (rs == 0x04) cop0_write(rd, reg(rt));                                       // MTC0
            else if (rs == 0x10 && fn == 0x10) {                                                // RFE
                const auto sr = cop0_read(12);
                cop0_write(12, (sr & ~0xFu) | ((sr >> 2) & 0xFu));
            } else throw unsupported();
            break;
        case 0x20: load_reg(rt, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(load(reg(rs) + imm, 1u, "LB"))))); break;
        case 0x21: load_reg(rt, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(load(reg(rs) + imm, 2u, "LH"))))); break;
        case 0x23: load_reg(rt, load(reg(rs) + imm, 4u, "LW")); break;
        case 0x24: load_reg(rt, load(reg(rs) + imm, 1u, "LBU")); break;
        case 0x25: load_reg(rt, load(reg(rs) + imm, 2u, "LHU")); break;
        case 0x22: case 0x26: { // LWL/LWR: byte lanes merge with a pending load, including its uncertainty.
            const auto address=reg(rs)+imm,shift=address&3u,aligned=address&~3u;
            const auto prior=merge_base(rt);
            auto value=prior.value,known=prior.defined ? ~0u : prior.known;
            auto origin=prior.origin;
            const auto first=op==0x22u ? 0u : shift,last=op==0x22u ? shift+1u : 4u;
            for(unsigned k=first;k<last;++k) {
                const auto bit=8u*(op==0x22u ? k+3u-shift : k-shift);
                undefined_load_.reset();undefined_known_=0;
                const auto byte=load(aligned+k,1u,op==0x22u ? "LWL" : "LWR");
                const auto byte_known=undefined_load_ ? undefined_known_&0xFFu : 0xFFu;
                if(undefined_load_ && byte_known!=0xFFu)origin=*undefined_load_;
                value=(value&~(0xFFu<<bit))|(byte<<bit);
                known=(known&~(0xFFu<<bit))|(byte_known<<bit);
            }
            undefined_load_.reset();undefined_sio2_epoch_=0;
            if(known!=~0u)undefined_load_=origin.empty() ? "IOP unaligned merge retains unknown source/register bytes" : origin;
            undefined_known_=known;load_reg(rt,value);break;
        }
        case 0x28: store_value(reg(rs)+imm,1u,rt,"SB");break;
        case 0x29: store_value(reg(rs)+imm,2u,rt,"SH");break;
        case 0x2B: store_value(reg(rs)+imm,4u,rt,"SW");break;
        case 0x2A: case 0x2E: { // SWL/SWR transport each selected byte; they never inspect the payload's value.
            const auto address=reg(rs)+imm,shift=address&3u,aligned=address&~3u;
            const auto value=gpr_[rt],known=(gpr_defined_&(1u<<rt)) ? ~0u : gpr_known_[rt];
            const auto first=op==0x2Au ? 0u : shift,last=op==0x2Au ? shift+1u : 4u;
            for(unsigned k=first;k<last;++k) {
                const auto bit=8u*(op==0x2Au ? k+3u-shift : k-shift);
                const ObservedWord byte{(value>>bit)&0xFFu,((~known)>>bit)&0xFFu};
                store(aligned+k,1u,byte.unknown ? std::nullopt : std::optional<std::uint32_t>{byte.value},op==0x2Au ? "SWL" : "SWR",&byte);
            }
            break;
        }
        default:
            throw unsupported();
    }
    if (branch && delay_slot) throw IopExecutionStop{"branch or jump in a delay slot (undefined)"};

    // Commit: the load in flight lands first, then this instruction's result
    // (which wins when both write the same register).
    if (delayed) commit(*delayed);
    if (exception) return enter_exception(*exception, pc, delay_slot);
    if (write_) commit(*write_);
    if (delay_slot) {
        pc_ = *after;
        in_delay_slot_ = false;
        branch_target_.reset();
    } else if (branch) {
        pc_ = pc + 4u;
        in_delay_slot_ = true;
        branch_target_ = target.value_or(pc + 8u);
    } else {
        pc_ = pc + 4u;
    }
}
} // namespace ps2native
