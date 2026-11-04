#pragma once
// Single source of R5900 integer semantics, shared by host-compiled AOT
// blocks (which call ee::exec<Op> with constant pc/word, folded by the host
// compiler) and by the interpreter (which calls the same templates with the
// decoded word). Neither path has its own copy of an instruction's behavior.
#include "ps2native/ee.hpp"
#include "ps2native/memory.hpp"
#include "ps2native/mips.hpp"

#include <cstdint>
#include <tuple>

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
inline std::uint64_t raw_gpr64(const EEState& state, unsigned index) {
    return index == 0 ? 0u : state.gpr[index].low;
}
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

// R5900 MULT/MULTU: sign-extended 32-bit halves in LO/HI; LO also goes to rd.
inline void multiply(EEState& s, std::uint64_t& lo, std::uint64_t& hi, unsigned rd, std::uint64_t product) {
    lo = sx32(static_cast<std::uint32_t>(product));
    hi = sx32(static_cast<std::uint32_t>(product >> 32));
    s.set_reg64(rd, lo);
}
// R5900 DIV/DIVU never trap: overflow and division by zero have defined results.
inline void divide_signed(std::uint64_t& lo, std::uint64_t& hi, std::uint32_t dividend, std::uint32_t divisor) {
    const auto n = static_cast<std::int32_t>(dividend);
    const auto d = static_cast<std::int32_t>(divisor);
    if (n == INT32_MIN && d == -1) {
        lo = sx32(0x80000000u);
        hi = 0u;
    } else if (d != 0) {
        lo = sx32(static_cast<std::uint32_t>(n / d));
        hi = sx32(static_cast<std::uint32_t>(n % d));
    } else {
        lo = n < 0 ? 1u : ~0ull;
        hi = sx32(static_cast<std::uint32_t>(n));
    }
}
inline void divide_unsigned(std::uint64_t& lo, std::uint64_t& hi, std::uint32_t n, std::uint32_t d) {
    if (d != 0u) {
        lo = sx32(n / d);
        hi = sx32(n % d);
    } else {
        lo = ~0ull;
        hi = sx32(n);
    }
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
        const auto a = raw_gpr64(s, s_), b = raw_gpr64(s, t_);
        const auto ua = s.unknown_gpr_bits[s_], ub = s.unknown_gpr_bits[t_];
        const auto unknown = (ua | ub) & ~((~a & ~ua) | (~b & ~ub));
        s.set_reg64_observed(d_, a & b, unknown);
    } else if constexpr (Op == O::Or) {
        const auto a = raw_gpr64(s, s_), b = raw_gpr64(s, t_);
        const auto ua = s.unknown_gpr_bits[s_], ub = s.unknown_gpr_bits[t_];
        s.set_reg64_observed(d_, a | b, (ua | ub) & ~((a & ~ua) | (b & ~ub)));
    } else if constexpr (Op == O::Xor) {
        s.set_reg64_observed(d_, raw_gpr64(s, s_) ^ raw_gpr64(s, t_),
                            s_ == t_ ? 0u : s.unknown_gpr_bits[s_] | s.unknown_gpr_bits[t_]);
    } else if constexpr (Op == O::Nor) {
        const auto a = raw_gpr64(s, s_), b = raw_gpr64(s, t_);
        const auto ua = s.unknown_gpr_bits[s_], ub = s.unknown_gpr_bits[t_];
        s.set_reg64_observed(d_, ~(a | b), (ua | ub) & ~((a & ~ua) | (b & ~ub)));
    } else if constexpr (Op == O::Slt) {
        s.set_reg64(d_, s64(s.reg64(s_)) < s64(s.reg64(t_)) ? 1u : 0u);
    } else if constexpr (Op == O::Sltu) {
        s.set_reg64(d_, s.reg64(s_) < s.reg64(t_) ? 1u : 0u);
    } else if constexpr (Op == O::Movz) {
        if (s.reg64(t_) == 0u) s.set_reg64(d_, s.reg64(s_));
    } else if constexpr (Op == O::Movn) {
        if (s.reg64(t_) != 0u) s.set_reg64(d_, s.reg64(s_));
    // MULT/DIV families: pipeline 0 uses the lower halves of LO/HI, the MMI
    // pipeline-1 forms (MULT1, DIV1, ...) the upper halves, with identical rules.
    } else if constexpr (Op == O::Mult || Op == O::Mult1) {
        auto [lo, hi] = Op == O::Mult ? std::tie(s.lo, s.hi) : std::tie(s.lo1, s.hi1);
        multiply(s, lo, hi, d_, static_cast<std::uint64_t>(
            static_cast<std::int64_t>(static_cast<std::int32_t>(s.reg32(s_))) *
            static_cast<std::int64_t>(static_cast<std::int32_t>(s.reg32(t_)))));
    } else if constexpr (Op == O::Multu || Op == O::Multu1) {
        auto [lo, hi] = Op == O::Multu ? std::tie(s.lo, s.hi) : std::tie(s.lo1, s.hi1);
        multiply(s, lo, hi, d_, static_cast<std::uint64_t>(s.reg32(s_)) * s.reg32(t_));
    } else if constexpr (Op == O::Div || Op == O::Div1) {
        auto [lo, hi] = Op == O::Div ? std::tie(s.lo, s.hi) : std::tie(s.lo1, s.hi1);
        divide_signed(lo, hi, s.reg32(s_), s.reg32(t_));
    } else if constexpr (Op == O::Divu || Op == O::Divu1) {
        auto [lo, hi] = Op == O::Divu ? std::tie(s.lo, s.hi) : std::tie(s.lo1, s.hi1);
        divide_unsigned(lo, hi, s.reg32(s_), s.reg32(t_));
    } else if constexpr (Op == O::Mfhi) {
        s.set_reg64(d_, s.hi);
    } else if constexpr (Op == O::Mflo) {
        s.set_reg64(d_, s.lo);
    } else if constexpr (Op == O::Mthi) {
        s.hi = s.reg64(s_);
    } else if constexpr (Op == O::Mtlo) {
        s.lo = s.reg64(s_);
    } else if constexpr (Op == O::Mfhi1) {
        s.set_reg64(d_, s.hi1);
    } else if constexpr (Op == O::Mflo1) {
        s.set_reg64(d_, s.lo1);
    } else if constexpr (Op == O::Mthi1) {
        s.hi1 = s.reg64(s_);
    } else if constexpr (Op == O::Mtlo1) {
        s.lo1 = s.reg64(s_);
    // PADDUW: four 32-bit lanes, unsigned saturation at 0xFFFFFFFF; all 128 bits written.
    } else if constexpr (Op == O::Padduw) {
        const auto a = s.reg128(s_);
        const auto b = s.reg128(t_);
        auto lanes = [](std::uint64_t x, std::uint64_t y) {
            std::uint64_t out = 0;
            for (unsigned shift = 0; shift < 64u; shift += 32u) {
                const auto sum = ((x >> shift) & 0xFFFFFFFFull) + ((y >> shift) & 0xFFFFFFFFull);
                out |= (sum > 0xFFFFFFFFull ? 0xFFFFFFFFull : sum) << shift;
            }
            return out;
        };
        s.set_reg128(d_, lanes(a.low, b.low), lanes(a.high, b.high));
    // PCPYH: broadcast the low halfword independently in each 64-bit half.
    } else if constexpr (Op == O::Pcpyh) {
        const auto source = s.reg128(t_);
        constexpr auto repeat = 0x0001000100010001ull;
        s.set_reg128(d_, (source.low & 0xFFFFu) * repeat, (source.high & 0xFFFFu) * repeat);
    } else if constexpr (Op == O::Pcpyld) {
        const auto low = s.reg64(t_);
        const auto high = s.reg64(s_);
        s.set_reg128(d_, low, high);
    // PAND/PXOR/POR/PNOR: bitwise over all 128 bits.
    } else if constexpr (Op == O::Pand || Op == O::Pxor || Op == O::Por || Op == O::Pnor) {
        const auto a = s.reg128(s_);
        const auto b = s.reg128(t_);
        auto op = [](std::uint64_t x, std::uint64_t y) -> std::uint64_t {
            if constexpr (Op == O::Pand) return x & y;
            else if constexpr (Op == O::Pxor) return x ^ y;
            else if constexpr (Op == O::Por) return x | y;
            else return ~(x | y);
        };
        s.set_reg128(d_, op(a.low, b.low), op(a.high, b.high));
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
        s.set_reg64_observed(t_, raw_gpr64(s, s_) & zimm(w), s.unknown_gpr_bits[s_] & zimm(w));
    } else if constexpr (Op == O::Ori) {
        s.set_reg64_observed(t_, raw_gpr64(s, s_) | zimm(w), s.unknown_gpr_bits[s_] & ~zimm(w));
    } else if constexpr (Op == O::Xori) {
        s.set_reg64_observed(t_, raw_gpr64(s, s_) ^ zimm(w), s.unknown_gpr_bits[s_]);
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
        const auto word = m.load32_observed(effective_address(s, w));
        const auto unknown = std::uint64_t{word.unknown} | ((word.unknown & 0x80000000u) ? 0xFFFFFFFF00000000ull : 0u);
        s.set_reg64_observed(t_, sx32(word.value), unknown);
    } else if constexpr (Op == O::Lwu) {
        const auto word = m.load32_observed(effective_address(s, w));
        s.set_reg64_observed(t_, word.value, word.unknown);
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
        m.write128(quad, value.low, value.high);
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
