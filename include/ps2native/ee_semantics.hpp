#pragma once
// Single source of R5900 integer semantics, shared by host-compiled AOT
// blocks (which call ee::exec<Op> with constant pc/word, folded by the host
// compiler) and by the interpreter (which calls the same templates with the
// decoded word). Neither path has its own copy of an instruction's behavior.
#include "ps2native/ee.hpp"
#include "ps2native/memory.hpp"
#include "ps2native/mips.hpp"

#include <cstdint>

namespace ps2native {
enum class ExecutionEngine : std::uint8_t { Aot, Interpreter };

class InstructionObserver {
public:
    virtual ~InstructionObserver() = default;
    // Called once per instruction about to execute, after its checks passed.
    virtual void on_instruction(std::uint32_t pc, std::uint32_t word, ExecutionEngine engine) = 0;
};

struct ExecutionContext {
    EEState& state;
    Memory& memory;
    std::uint64_t& executed;
    std::uint64_t budget;
    std::uint32_t stop_pc;
    InstructionObserver* observer{};
};

namespace ee {
constexpr std::uint64_t sx32(std::uint32_t value) {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(value)));
}
constexpr std::int64_t s64(std::uint64_t value) { return static_cast<std::int64_t>(value); }
constexpr unsigned rs(std::uint32_t word) { return (word >> 21) & 31u; }
constexpr unsigned rt(std::uint32_t word) { return (word >> 16) & 31u; }
constexpr unsigned rd(std::uint32_t word) { return (word >> 11) & 31u; }
constexpr unsigned sa(std::uint32_t word) { return (word >> 6) & 31u; }
constexpr std::uint32_t simm32(std::uint32_t word) {
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(word & 0xffffu)));
}
constexpr std::uint64_t simm64(std::uint32_t word) { return sx32(simm32(word)); }
constexpr std::uint64_t zimm(std::uint32_t word) { return word & 0xffffu; }
// Effective address: lower 32 bits of rs plus the sign-extended offset.
inline std::uint32_t effective_address(const EEState& state, std::uint32_t word) {
    return state.reg32(rs(word)) + simm32(word);
}

// Branch/jump bookkeeping of the instruction in flight.
struct InstructionFrame {
    bool in_delay_slot{};
    bool pending_indirect{};
    std::uint32_t pending_target{};
};
// Starts executing the instruction at pc: the default next PC is pc + 4.
inline InstructionFrame begin(EEState& state, std::uint32_t pc) {
    const InstructionFrame frame{state.delay_pending, state.delay_indirect, state.delay_target};
    state.instruction_pc = pc;
    state.instruction_in_delay_slot = frame.in_delay_slot;
    state.delay_pending = false;
    state.delay_indirect = false;
    state.pc = pc + 4u;
    return frame;
}
// Retires the instruction. Returns true when it completed a delay slot, i.e.
// control left the straight-line sequence (the caller returns to dispatch).
inline bool retire(ExecutionContext& ctx, const InstructionFrame& frame) {
    ++ctx.executed;
    ++ctx.state.retired_instructions;
    if (!frame.in_delay_slot) return false;
    ctx.state.pc = frame.pending_target;
    // JR/JALR targets are always checked by the dispatcher.
    if (frame.pending_indirect) ctx.state.indirect_entry_pending = true;
    return true;
}

inline void branch(EEState& state, bool taken, std::uint32_t pc, std::uint32_t word, bool likely) {
    if (likely && !taken) {
        state.pc = pc + 8u; // likely: delay slot nullified
        return;
    }
    state.delay_pending = true;
    state.delay_target = taken ? mips_branch_target(pc, word) : pc + 8u;
}

template <MipsOpcode>
inline constexpr bool kSystemInstruction = false;

template <MipsOpcode Op>
inline void exec(EEState& s, Memory& m, std::uint32_t pc, std::uint32_t w) {
    using O = MipsOpcode;
    [[maybe_unused]] const unsigned s_ = rs(w);
    [[maybe_unused]] const unsigned t_ = rt(w);
    [[maybe_unused]] const unsigned d_ = rd(w);
    [[maybe_unused]] const unsigned a_ = sa(w);
    (void)m;
    (void)pc;
    // Host execution is sequential: guest memory accesses are already ordered (SYNC).
    if constexpr (Op == O::Nop || Op == O::Sync) {
    // 32-bit shifts produce sign-extended 64-bit results.
    } else if constexpr (Op == O::Sll) {
        s.set_reg64(d_, sx32(s.reg32(t_) << a_));
    } else if constexpr (Op == O::Srl) {
        s.set_reg64(d_, sx32(s.reg32(t_) >> a_));
    } else if constexpr (Op == O::Sra) {
        s.set_reg64(d_, sx32(static_cast<std::uint32_t>(static_cast<std::int32_t>(s.reg32(t_)) >> a_)));
    } else if constexpr (Op == O::Sllv) {
        s.set_reg64(d_, sx32(s.reg32(t_) << (s.reg32(s_) & 31u)));
    } else if constexpr (Op == O::Srlv) {
        s.set_reg64(d_, sx32(s.reg32(t_) >> (s.reg32(s_) & 31u)));
    } else if constexpr (Op == O::Srav) {
        s.set_reg64(d_, sx32(static_cast<std::uint32_t>(
                            static_cast<std::int32_t>(s.reg32(t_)) >> (s.reg32(s_) & 31u))));
    } else if constexpr (Op == O::Dsll) {
        s.set_reg64(d_, s.reg64(t_) << a_);
    } else if constexpr (Op == O::Dsrl) {
        s.set_reg64(d_, s.reg64(t_) >> a_);
    } else if constexpr (Op == O::Dsra) {
        s.set_reg64(d_, static_cast<std::uint64_t>(s64(s.reg64(t_)) >> a_));
    } else if constexpr (Op == O::Dsll32) {
        s.set_reg64(d_, s.reg64(t_) << (a_ + 32u));
    } else if constexpr (Op == O::Dsrl32) {
        s.set_reg64(d_, s.reg64(t_) >> (a_ + 32u));
    } else if constexpr (Op == O::Dsra32) {
        s.set_reg64(d_, static_cast<std::uint64_t>(s64(s.reg64(t_)) >> (a_ + 32u)));
    } else if constexpr (Op == O::Dsllv) {
        s.set_reg64(d_, s.reg64(t_) << (s.reg32(s_) & 63u));
    } else if constexpr (Op == O::Dsrlv) {
        s.set_reg64(d_, s.reg64(t_) >> (s.reg32(s_) & 63u));
    } else if constexpr (Op == O::Dsrav) {
        s.set_reg64(d_, static_cast<std::uint64_t>(s64(s.reg64(t_)) >> (s.reg32(s_) & 63u)));
    } else if constexpr (Op == O::Addu) {
        s.set_reg32_sign_extended(d_, s.reg32(s_) + s.reg32(t_));
    } else if constexpr (Op == O::Subu) {
        s.set_reg32_sign_extended(d_, s.reg32(s_) - s.reg32(t_));
    } else if constexpr (Op == O::Daddu) {
        s.set_reg64(d_, s.reg64(s_) + s.reg64(t_));
    } else if constexpr (Op == O::Dsubu) {
        s.set_reg64(d_, s.reg64(s_) - s.reg64(t_));
    } else if constexpr (Op == O::And) {
        s.set_reg64(d_, s.reg64(s_) & s.reg64(t_));
    } else if constexpr (Op == O::Or) {
        s.set_reg64(d_, s.reg64(s_) | s.reg64(t_));
    } else if constexpr (Op == O::Xor) {
        s.set_reg64(d_, s.reg64(s_) ^ s.reg64(t_));
    } else if constexpr (Op == O::Nor) {
        s.set_reg64(d_, ~(s.reg64(s_) | s.reg64(t_)));
    } else if constexpr (Op == O::Slt) {
        s.set_reg64(d_, s64(s.reg64(s_)) < s64(s.reg64(t_)) ? 1u : 0u);
    } else if constexpr (Op == O::Sltu) {
        s.set_reg64(d_, s.reg64(s_) < s.reg64(t_) ? 1u : 0u);
    } else if constexpr (Op == O::Movz) {
        if (s.reg64(t_) == 0u) s.set_reg64(d_, s.reg64(s_));
    } else if constexpr (Op == O::Movn) {
        if (s.reg64(t_) != 0u) s.set_reg64(d_, s.reg64(s_));
    // R5900 MULT/MULTU: sign-extended 32-bit halves in LO/HI; LO also goes to rd.
    } else if constexpr (Op == O::Mult) {
        const auto product = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(static_cast<std::int32_t>(s.reg32(s_))) *
            static_cast<std::int64_t>(static_cast<std::int32_t>(s.reg32(t_))));
        s.lo = sx32(static_cast<std::uint32_t>(product));
        s.hi = sx32(static_cast<std::uint32_t>(product >> 32));
        s.set_reg64(d_, s.lo);
    } else if constexpr (Op == O::Multu) {
        const auto product = static_cast<std::uint64_t>(s.reg32(s_)) * s.reg32(t_);
        s.lo = sx32(static_cast<std::uint32_t>(product));
        s.hi = sx32(static_cast<std::uint32_t>(product >> 32));
        s.set_reg64(d_, s.lo);
    // R5900 DIV/DIVU never trap: overflow and division by zero have defined results.
    } else if constexpr (Op == O::Div) {
        const auto n = static_cast<std::int32_t>(s.reg32(s_));
        const auto d = static_cast<std::int32_t>(s.reg32(t_));
        if (n == INT32_MIN && d == -1) {
            s.lo = sx32(0x80000000u);
            s.hi = 0u;
        } else if (d != 0) {
            s.lo = sx32(static_cast<std::uint32_t>(n / d));
            s.hi = sx32(static_cast<std::uint32_t>(n % d));
        } else {
            s.lo = n < 0 ? 1u : ~0ull;
            s.hi = sx32(static_cast<std::uint32_t>(n));
        }
    } else if constexpr (Op == O::Divu) {
        const auto n = s.reg32(s_);
        const auto d = s.reg32(t_);
        if (d != 0u) {
            s.lo = sx32(n / d);
            s.hi = sx32(n % d);
        } else {
            s.lo = ~0ull;
            s.hi = sx32(n);
        }
    } else if constexpr (Op == O::Mfhi) {
        s.set_reg64(d_, s.hi);
    } else if constexpr (Op == O::Mflo) {
        s.set_reg64(d_, s.lo);
    } else if constexpr (Op == O::Mthi) {
        s.hi = s.reg64(s_);
    } else if constexpr (Op == O::Mtlo) {
        s.lo = s.reg64(s_);
    } else if constexpr (Op == O::Addiu) {
        s.set_reg32_sign_extended(t_, s.reg32(s_) + simm32(w));
    } else if constexpr (Op == O::Daddiu) {
        s.set_reg64(t_, s.reg64(s_) + simm64(w));
    } else if constexpr (Op == O::Slti) {
        s.set_reg64(t_, s64(s.reg64(s_)) < s64(simm64(w)) ? 1u : 0u);
    } else if constexpr (Op == O::Sltiu) {
        s.set_reg64(t_, s.reg64(s_) < simm64(w) ? 1u : 0u);
    // Logical immediates are zero-extended and act on all 64 bits.
    } else if constexpr (Op == O::Andi) {
        s.set_reg64(t_, s.reg64(s_) & zimm(w));
    } else if constexpr (Op == O::Ori) {
        s.set_reg64(t_, s.reg64(s_) | zimm(w));
    } else if constexpr (Op == O::Xori) {
        s.set_reg64(t_, s.reg64(s_) ^ zimm(w));
    } else if constexpr (Op == O::Lui) {
        s.set_reg32_sign_extended(t_, (w & 0xffffu) << 16);
    } else if constexpr (Op == O::Lb) {
        s.set_reg64(t_, static_cast<std::uint64_t>(static_cast<std::int64_t>(
                            static_cast<std::int8_t>(m.read8(effective_address(s, w))))));
    } else if constexpr (Op == O::Lbu) {
        s.set_reg64(t_, m.read8(effective_address(s, w)));
    } else if constexpr (Op == O::Lh) {
        s.set_reg64(t_, static_cast<std::uint64_t>(static_cast<std::int64_t>(
                            static_cast<std::int16_t>(m.read16(effective_address(s, w))))));
    } else if constexpr (Op == O::Lhu) {
        s.set_reg64(t_, m.read16(effective_address(s, w)));
    } else if constexpr (Op == O::Lw) {
        s.set_reg32_sign_extended(t_, m.read32(effective_address(s, w)));
    } else if constexpr (Op == O::Lwu) {
        s.set_reg64(t_, m.read32(effective_address(s, w)));
    } else if constexpr (Op == O::Ld) {
        s.set_reg64(t_, m.read64(effective_address(s, w)));
    // LQ/SQ silently clear the low four address bits (no address error).
    } else if constexpr (Op == O::Lq) {
        const auto quad = effective_address(s, w) & ~15u;
        const auto low = m.read64(quad);
        s.set_reg128(t_, low, m.read64(quad + 8u));
    } else if constexpr (Op == O::Sb) {
        m.write8(effective_address(s, w), static_cast<std::uint8_t>(s.reg32(t_)));
    } else if constexpr (Op == O::Sh) {
        m.write16(effective_address(s, w), static_cast<std::uint16_t>(s.reg32(t_)));
    } else if constexpr (Op == O::Sw) {
        m.write32(effective_address(s, w), s.reg32(t_));
    } else if constexpr (Op == O::Sd) {
        m.write64(effective_address(s, w), s.reg64(t_));
    } else if constexpr (Op == O::Sq) {
        const auto quad = effective_address(s, w) & ~15u;
        const auto value = s.reg128(t_);
        m.write64(quad, value.low);
        m.write64(quad + 8u, value.high);
    // R5900 branches compare the full lower 64 bits. The condition reads rs
    // before BxxZAL writes the link register.
    } else if constexpr (Op == O::Beq || Op == O::Beql) {
        branch(s, s.reg64(s_) == s.reg64(t_), pc, w, Op == O::Beql);
    } else if constexpr (Op == O::Bne || Op == O::Bnel) {
        branch(s, s.reg64(s_) != s.reg64(t_), pc, w, Op == O::Bnel);
    } else if constexpr (Op == O::Blez || Op == O::Blezl) {
        branch(s, s64(s.reg64(s_)) <= 0, pc, w, Op == O::Blezl);
    } else if constexpr (Op == O::Bgtz || Op == O::Bgtzl) {
        branch(s, s64(s.reg64(s_)) > 0, pc, w, Op == O::Bgtzl);
    } else if constexpr (Op == O::Bltz || Op == O::Bltzl || Op == O::Bltzal || Op == O::Bltzall) {
        const bool taken = s64(s.reg64(s_)) < 0;
        if constexpr (Op == O::Bltzal || Op == O::Bltzall) s.set_reg32_sign_extended(31u, pc + 8u);
        branch(s, taken, pc, w, Op == O::Bltzl || Op == O::Bltzall);
    } else if constexpr (Op == O::Bgez || Op == O::Bgezl || Op == O::Bgezal || Op == O::Bgezall) {
        const bool taken = s64(s.reg64(s_)) >= 0;
        if constexpr (Op == O::Bgezal || Op == O::Bgezall) s.set_reg32_sign_extended(31u, pc + 8u);
        branch(s, taken, pc, w, Op == O::Bgezl || Op == O::Bgezall);
    } else if constexpr (Op == O::J) {
        s.delay_pending = true;
        s.delay_target = mips_jump_target(pc, w);
    } else if constexpr (Op == O::Jal) {
        s.set_reg32_sign_extended(31u, pc + 8u);
        s.delay_pending = true;
        s.delay_target = mips_jump_target(pc, w);
    } else if constexpr (Op == O::Jr) {
        s.delay_pending = true;
        s.delay_indirect = true;
        s.delay_target = s.reg32(s_);
    } else if constexpr (Op == O::Jalr) {
        // Deliberate order: JALR rs==rd must read its target BEFORE writing link.
        s.delay_target = s.reg32(s_);
        s.set_reg32_sign_extended(d_, pc + 8u);
        s.delay_pending = true;
        s.delay_indirect = true;
    } else {
        static_assert(kSystemInstruction<Op>, "system instructions are interpreter-only");
    }
}
} // namespace ee
} // namespace ps2native
