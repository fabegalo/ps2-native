#pragma once
// Single source of R5900 integer semantics, shared by host-compiled AOT
// blocks (which call ee::exec<Op> with constant pc/word, folded by the host
// compiler) and by the interpreter (which calls the same templates with the
// decoded word). Neither path has its own copy of an instruction's behavior.
#include "ps2native/ee.hpp"
#include "ps2native/memory.hpp"
#include "ps2native/mips.hpp"

#include <cstdint>
#include <bit>
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
inline bool negative_gpr64(const EEState& state,unsigned index) {
    // EE core BGEZ/BLTZ test bit 63; unrelated payload bits need not be known.
    constexpr auto sign=1ull<<63;
    if(index && (state.unknown_gpr_bits[index]&sign))
        throw std::runtime_error("EE branch consumes unknown sign bit");
    return (raw_gpr64(state,index)&sign)!=0;
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
    if constexpr (Op == O::Cfc2 || Op == O::Ctc2 || Op == O::Qmfc2 || Op == O::Qmtc2 || Op == O::Viswr || Op == O::Vsub || Op == O::Vsqi || Op == O::Viadd || Op == O::Vadd || Op == O::Vmr32 || Op == O::Lqc2 || Op == O::Sqc2 || Op == O::Vmul || Op == O::VmulBc || Op == O::Vmadd || Op == O::VmaddBc || Op == O::Vmula || Op == O::VmulaBc || Op == O::Vmadda || Op == O::VmaddaBc || Op == O::VaddBc || Op == O::VsubBc || Op == O::Vopmula || Op == O::Vopmsub || Op == O::Vsqrt || Op == O::Vdiv || Op == O::Vwaitq || Op == O::VaddQ || Op == O::VmulQ) {
        if (!(s.cp0(Cop0::Status) & (1u<<30)))
            throw std::runtime_error("COP2 transfer with Status.CU2 = 0; coprocessor unusable vectoring is not modeled");
        // Implemented ALU macros complete synchronously in the shared instruction
        // timing model. Microprogram starts remain refused, so no pending
        // microprogram can distinguish interlocked and NI transfers here.
        m.vu_advance_q(s.cycles());
        if constexpr (Op==O::Vsqrt || Op==O::Vdiv || Op==O::Vwaitq) {
            const auto ready=m.vu_q_ready_cycle();
            if(ready>s.cycles())s.stall(ready-s.cycles());
            m.vu_advance_q(s.cycles());
            if constexpr(Op!=O::Vwaitq)
                m.vu_issue_q(s.cycles(),d_,t_,(w>>21u)&3u,(w>>23u)&3u,Op==O::Vsqrt);
        } else if constexpr(Op==O::VaddQ) m.vu_add(a_,d_,0,(w>>21u)&15u,5);
        else if constexpr(Op==O::VmulQ) m.vu_product(a_,d_,0,(w>>21u)&15u,5,false,false);
        else if constexpr (Op == O::Lqc2 || Op == O::Sqc2) {
            const auto address=effective_address(s,w);
            if(address&15u)throw BusError(BusFault::Unaligned,Op==O::Sqc2,16,address,{},BusRegion::Unmapped,
                                         "COP2 quadword transfer requires 16-byte alignment");
            if constexpr (Op==O::Lqc2) {
                const auto low=m.load64_observed(address),high=m.load64_observed(address+8u);
                m.write_vu_vector(t_,{low,high});
            } else {
                const auto value=m.observe_vu_vector(t_);
                if(value.low.unknown || value.high.unknown)m.store128_observed(address,value);
                else m.write128(address,value.low.value,value.high.value);
            }
        } else if constexpr (Op == O::Cfc2) {
            const auto word=m.observe_vu_control(rd(w));
            const auto unknown=std::uint64_t{word.unknown} | ((word.unknown&0x80000000u) ? 0xFFFFFFFF00000000ull : 0u);
            s.set_reg64_observed(t_,sx32(word.value),unknown);
        } else if constexpr (Op == O::Ctc2)
            m.write_vu_control_observed(rd(w),{static_cast<std::uint32_t>(raw_gpr64(s,t_)),
                                              static_cast<std::uint32_t>(s.unknown_gpr_bits[t_])});
        else if constexpr (Op == O::Qmfc2) {
            const auto word=m.observe_vu_vector(d_);
            s.set_reg128_observed(t_,word.low.value,word.high.value,word.low.unknown,word.high.unknown);
        } else if constexpr (Op == O::Qmtc2) {
            const auto value=t_ ? s.gpr[t_] : Register128{};
            m.write_vu_vector(d_,{{value.low,s.unknown_gpr_bits[t_]},{value.high,s.unknown_gpr_high_bits[t_]}});
        } else if constexpr (Op == O::Viswr) m.vu_integer_store(t_,d_,(w>>21u)&15u);
        else if constexpr (Op == O::Vsub) m.vu_subtract(a_,d_,t_,(w>>21u)&15u);
        else if constexpr (Op == O::Vadd) m.vu_add(a_,d_,t_,(w>>21u)&15u);
        else if constexpr (Op == O::VaddBc || Op == O::VsubBc)
            m.vu_add(a_,d_,t_,(w>>21u)&15u,w&3u,Op==O::VsubBc);
        else if constexpr (Op == O::Vopmula || Op == O::Vopmsub)
            m.vu_product(a_,d_,t_,14,4,Op==O::Vopmsub,Op==O::Vopmula,true);
        else if constexpr (Op == O::Vmr32) m.vu_rotate(t_,d_,(w>>21u)&15u);
        else if constexpr (Op == O::Vmul || Op == O::VmulBc || Op == O::Vmadd || Op == O::VmaddBc ||
                           Op == O::Vmula || Op == O::VmulaBc || Op == O::Vmadda || Op == O::VmaddaBc) {
            constexpr bool broadcast=Op==O::VmulBc || Op==O::VmaddBc || Op==O::VmulaBc || Op==O::VmaddaBc;
            constexpr bool accumulate=Op==O::Vmadd || Op==O::VmaddBc || Op==O::Vmadda || Op==O::VmaddaBc;
            constexpr bool to_accumulator=Op==O::Vmula || Op==O::VmulaBc || Op==O::Vmadda || Op==O::VmaddaBc;
            m.vu_product(a_,d_,t_,(w>>21u)&15u,broadcast ? w&3u : 4u,accumulate,to_accumulator);
        }
        else if constexpr (Op == O::Vsqi) m.vu_vector_store_increment(d_,t_,(w>>21u)&15u);
        else m.vu_add_integer(a_,d_,t_);
    } else if constexpr (Op == O::Nop || Op == O::Sync) {
    // 32-bit shifts produce sign-extended 64-bit results.
    } else if constexpr (Op == O::Sll || Op == O::Srl || Op == O::Sra ||
                         Op == O::Sllv || Op == O::Srlv || Op == O::Srav ||
                         Op == O::Dsll || Op == O::Dsrl || Op == O::Dsra ||
                         Op == O::Dsll32 || Op == O::Dsrl32 || Op == O::Dsra32 ||
                         Op == O::Dsllv || Op == O::Dsrlv || Op == O::Dsrav) {
        constexpr bool wide=Op==O::Dsll || Op==O::Dsrl || Op==O::Dsra ||
            Op==O::Dsll32 || Op==O::Dsrl32 || Op==O::Dsra32 ||
            Op==O::Dsllv || Op==O::Dsrlv || Op==O::Dsrav;
        constexpr bool variable=Op==O::Sllv || Op==O::Srlv || Op==O::Srav ||
            Op==O::Dsllv || Op==O::Dsrlv || Op==O::Dsrav;
        constexpr bool left=Op==O::Sll || Op==O::Sllv || Op==O::Dsll || Op==O::Dsll32 || Op==O::Dsllv;
        constexpr bool arithmetic=Op==O::Sra || Op==O::Srav || Op==O::Dsra || Op==O::Dsra32 || Op==O::Dsrav;
        constexpr unsigned width=wide ? 64u : 32u;
        unsigned count=a_;
        if constexpr(variable) {
            if(s.unknown_gpr_bits[s_]&(width-1u))
                throw std::runtime_error("EE shift consumes unknown shift count");
            count=static_cast<unsigned>(raw_gpr64(s,s_))&(width-1u);
        } else if constexpr(Op==O::Dsll32 || Op==O::Dsrl32 || Op==O::Dsra32) count+=32u;
        constexpr auto mask=wide ? ~0ull : 0xFFFFFFFFull;
        auto value=raw_gpr64(s,t_)&mask, unknown=s.unknown_gpr_bits[t_]&mask;
        if constexpr(left) {value=(value<<count)&mask;unknown=(unknown<<count)&mask;}
        else {
            const auto sign=1ull<<(width-1u);
            const auto fill=count ? mask^(mask>>count) : 0ull;
            value=(value>>count)|((arithmetic && (value&sign)) ? fill : 0ull);
            unknown=(unknown>>count)|((arithmetic && (unknown&sign)) ? fill : 0ull);
        }
        if constexpr(!wide) {
            value=sx32(static_cast<std::uint32_t>(value));
            if(unknown&0x80000000u)unknown|=0xFFFFFFFF00000000ull;
        }
        s.set_reg64_observed(d_,value,unknown);
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
    } else if constexpr (Op == O::Mfsa) {
        s.set_reg64(d_,s.shift_amount);
    } else if constexpr (Op == O::Mtsa) {
        s.shift_amount=s.reg64(s_);
    } else if constexpr (Op == O::Pmfhi) {
        s.set_reg128(d_,s.hi,s.hi1);
    } else if constexpr (Op == O::Pmflo) {
        s.set_reg128(d_,s.lo,s.lo1);
    } else if constexpr (Op == O::Pmthi) {
        const auto value=s.reg128(s_);s.hi=value.low;s.hi1=value.high;
    } else if constexpr (Op == O::Pmtlo) {
        const auto value=s.reg128(s_);s.lo=value.low;s.lo1=value.high;
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
        s.set_reg128_observed(d_,raw_gpr64(s,t_),raw_gpr64(s,s_),s.unknown_gpr_bits[t_],s.unknown_gpr_bits[s_]);
    } else if constexpr (Op == O::Plzcw) {
        // EE Instruction Set v6 p.215: count the leading sign bits minus
        // one independently in the low two words; preserve upper 64 bits.
        const auto source=s.reg64(s_);
        std::uint64_t result=0;
        for(unsigned shift:{0u,32u}) {
            const auto word=static_cast<std::uint32_t>(source>>shift);
            const auto normalized=(word&0x80000000u) ? ~word : word;
            result|=std::uint64_t(std::countl_zero(normalized)-1)<<shift;
        }
        s.set_reg64(d_,result);
    } else if constexpr (Op == O::Pcpyud) {
        s.set_reg128_observed(d_,s_ ? s.gpr[s_].high : 0u,t_ ? s.gpr[t_].high : 0u,
                             s.unknown_gpr_high_bits[s_],s.unknown_gpr_high_bits[t_]);
    } else if constexpr (Op == O::Pextlw || Op == O::Pextuw) {
        const auto a=s_ ? s.gpr[s_] : Register128{},b=t_ ? s.gpr[t_] : Register128{};
        const auto x=Op==O::Pextlw?a.low:a.high, y=Op==O::Pextlw?b.low:b.high;
        const auto ux=Op==O::Pextlw?s.unknown_gpr_bits[s_]:s.unknown_gpr_high_bits[s_];
        const auto uy=Op==O::Pextlw?s.unknown_gpr_bits[t_]:s.unknown_gpr_high_bits[t_];
        s.set_reg128_observed(d_,(y&0xFFFFFFFFu)|(x<<32u),(y>>32u)|(x&0xFFFFFFFF00000000ull),
                             (uy&0xFFFFFFFFu)|(ux<<32u),(uy>>32u)|(ux&0xFFFFFFFF00000000ull));
    } else if constexpr (Op == O::Psubb) {
        const auto x=s.reg128(s_),y=s.reg128(t_);
        const auto subtract=[](std::uint64_t a,std::uint64_t b) {
            std::uint64_t result=0;
            for(unsigned bit=0;bit<64;bit+=8)
                result|=(((a>>bit)-(b>>bit))&0xFFu)<<bit;
            return result;
        };
        s.set_reg128(d_,subtract(x.low,y.low),subtract(x.high,y.high));
    } else if constexpr (Op == O::Ppacw) {
        const auto a=s_ ? s.gpr[s_] : Register128{},b=t_ ? s.gpr[t_] : Register128{};
        s.set_reg128_observed(d_,(b.low&0xFFFFFFFFu)|(b.high<<32u),(a.low&0xFFFFFFFFu)|(a.high<<32u),
            (s.unknown_gpr_bits[t_]&0xFFFFFFFFu)|(s.unknown_gpr_high_bits[t_]<<32u),
            (s.unknown_gpr_bits[s_]&0xFFFFFFFFu)|(s.unknown_gpr_high_bits[s_]<<32u));
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
    } else if constexpr (Op == O::Lwl || Op == O::Lwr || Op == O::Ldl || Op == O::Ldr) {
        constexpr bool wide=Op==O::Ldl || Op==O::Ldr;
        constexpr bool left=Op==O::Lwl || Op==O::Ldl;
        constexpr unsigned bytes=wide ? 8u : 4u;
        const auto address=effective_address(s,w);
        const auto offset=address&(bytes-1u);
        const auto aligned=address&~(bytes-1u);
        std::uint64_t value=0,source_unknown=0;
        if constexpr(wide)value=m.read64(aligned);
        else {const auto observed=m.load32_observed(aligned);value=observed.value;source_unknown=observed.unknown;}
        auto result=raw_gpr64(s,t_);
        auto unknown=s.unknown_gpr_bits[t_];
        // Merge selected bytes with the old destination, little endian.
        // A byte loop avoids host shifts by the width at the full-load edges.
        for(unsigned i=0;i<bytes;++i) {
            if((left && i<=offset) || (!left && i>=offset)) {
                const auto target=left ? bytes-1u-offset+i : i-offset;
                const auto mask=0xFFull<<(target*8u);
                result=(result&~mask)|(((value>>(i*8u))&0xFFull)<<(target*8u));
                unknown=(unknown&~mask)|(((source_unknown>>(i*8u))&0xFFull)<<(target*8u));
            }
        }
        if constexpr(!wide)unknown=(unknown&0xFFFFFFFFu)|((unknown&0x80000000u) ? 0xFFFFFFFF00000000ull : 0u);
        s.set_reg64_observed(t_,wide ? result : sx32(static_cast<std::uint32_t>(result)),unknown);
    } else if constexpr (Op == O::Swl || Op == O::Swr || Op == O::Sdl || Op == O::Sdr) {
        constexpr unsigned bytes=(Op==O::Sdl || Op==O::Sdr) ? 8u : 4u;
        constexpr bool left=Op==O::Swl || Op==O::Sdl;
        const auto address=effective_address(s,w);
        const auto offset=address&(bytes-1u);
        const auto aligned=address&~(bytes-1u);
        const auto value=raw_gpr64(s,t_);
        std::uint64_t used=0;
        for(unsigned i=0;i<bytes;++i)if((left && i<=offset) || (!left && i>=offset)) {
            const auto source=left ? bytes-1u-offset+i : i-offset;
            used|=0xFFull<<(source*8u);
        }
        if(used&s.unknown_gpr_bits[t_])(void)s.reg64(t_); // refuse before any write
        // Only enabled bytes are written; never read-modify-write an MMIO
        // register or touch the adjacent page to complete a partial store.
        for(unsigned i=0;i<bytes;++i) {
            if((left && i<=offset) || (!left && i>=offset)) {
                const auto source=left ? bytes-1u-offset+i : i-offset;
                m.write8(aligned+i,static_cast<std::uint8_t>(value>>(source*8u)));
            }
        }
    } else if constexpr (Op == O::Lw) {
        const auto word = m.load32_observed(effective_address(s, w));
        const auto unknown = std::uint64_t{word.unknown} | ((word.unknown & 0x80000000u) ? 0xFFFFFFFF00000000ull : 0u);
        s.set_reg64_observed(t_, sx32(word.value), unknown);
    } else if constexpr (Op == O::Lwu) {
        const auto word = m.load32_observed(effective_address(s, w));
        s.set_reg64_observed(t_, word.value, word.unknown);
    } else if constexpr (Op == O::Ld) {
        const auto word=m.load64_observed(effective_address(s,w));
        s.set_reg64_observed(t_,word.value,word.unknown);
    // LQ/SQ silently clear the low four address bits (no address error).
    } else if constexpr (Op == O::Lq) {
        const auto quad = effective_address(s, w) & ~15u;
        const auto low = m.load64_observed(quad),high=m.load64_observed(quad+8u);
        s.set_reg128_observed(t_,low.value,high.value,low.unknown,high.unknown);
    } else if constexpr (Op == O::Sb) {
        m.write8(effective_address(s, w), static_cast<std::uint8_t>(s.reg32(t_)));
    } else if constexpr (Op == O::Sh) {
        m.write16(effective_address(s, w), static_cast<std::uint16_t>(s.reg32(t_)));
    } else if constexpr (Op == O::Sw) {
        const auto unknown = static_cast<std::uint32_t>(s.unknown_gpr_bits[t_]);
        if (unknown) m.store32_observed(effective_address(s, w), {static_cast<std::uint32_t>(raw_gpr64(s,t_)),unknown});
        else m.write32(effective_address(s, w), s.reg32(t_));
    } else if constexpr (Op == O::Sd) {
        const auto unknown=s.unknown_gpr_bits[t_];
        if (unknown) m.store64_observed(effective_address(s,w),{raw_gpr64(s,t_),unknown});
        else m.write64(effective_address(s,w),s.reg64(t_));
    } else if constexpr (Op == O::Sq) {
        const auto quad = effective_address(s, w) & ~15u;
        const auto unknown_low=s.unknown_gpr_bits[t_],unknown_high=s.unknown_gpr_high_bits[t_];
        if (unknown_low|unknown_high) {
            const auto value=t_ ? s.gpr[t_] : Register128{};
            m.store128_observed(quad,{{value.low,unknown_low},{value.high,unknown_high}});
        } else {
            const auto value=s.reg128(t_);
            m.write128(quad,value.low,value.high);
        }
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
        const bool taken = negative_gpr64(s,s_);
        if constexpr (Op == O::Bltzal || Op == O::Bltzall) s.set_reg32_sign_extended(31u, pc + 8u);
        branch(s, taken, pc, w, Op == O::Bltzl || Op == O::Bltzall);
    } else if constexpr (Op == O::Bgez || Op == O::Bgezl || Op == O::Bgezal || Op == O::Bgezall) {
        const bool taken = !negative_gpr64(s,s_);
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
