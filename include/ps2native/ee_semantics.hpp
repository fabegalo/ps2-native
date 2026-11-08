#pragma once
// Single source of R5900 integer semantics, shared by host-compiled AOT
// blocks (which call ee::exec<Op> with constant pc/word, folded by the host
// compiler) and by the interpreter (which calls the same templates with the
// decoded word). Neither path has its own copy of an instruction's behavior.
#include "ps2native/ee.hpp"
#include "ps2native/memory.hpp"
#include "ps2native/mips.hpp"
#include "ps2native/ee_fpu.hpp"
#include "ps2native/vu_fixed.hpp"

#include <cstdint>
#include <algorithm>
#include <bit>
#include <tuple>
#include <span>

// Real translations can exceed a compiler's global heuristic inline budget.
// These small execution boundaries must still expose constants to codegen.
#if defined(_MSC_VER)
#define PS2N_EE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define PS2N_EE_INLINE inline __attribute__((always_inline))
#else
#define PS2N_EE_INLINE inline
#endif

namespace ps2native {
enum class ExecutionEngine : std::uint8_t { Aot, Interpreter };

class InstructionObserver {
public:
    virtual ~InstructionObserver() = default;
    // Called once per instruction about to execute, after its checks passed.
    virtual void on_instruction(std::uint32_t pc, std::uint32_t word, ExecutionEngine engine) = 0;
    virtual std::uint64_t idle_deadline() const {return 0;}
    virtual void on_idle_instructions(std::uint32_t, std::span<const std::uint32_t>, std::uint64_t) {}
};

struct InterruptSampleCache {
    std::uint64_t deadline{}, sample_cycle{}, retry_after{}, count_anchor{};
    std::uint32_t status{}, cause{}, count{}, compare{};
    bool compare_written{}, pending{};
};
struct ExecutionContext {
    EEState& state;
    Memory& memory;
    std::uint64_t& executed;
    std::uint64_t budget;
    std::uint32_t stop_pc;
    InstructionObserver* observer{};
    // The dispatcher already sampled interrupts at this instruction boundary.
    // Consumed once by the selected engine; later instructions still sample.
    bool interrupts_sampled{};
    InterruptSampleCache interrupt_cache{};
};
void flush_interrupt_cache(ExecutionContext& ctx);
void prime_interrupt_cache(ExecutionContext& ctx);
bool poll_ee_interrupts_cached_slow(ExecutionContext& ctx);
PS2N_EE_INLINE bool poll_ee_interrupts_cached(ExecutionContext& ctx) {
    auto& cache=ctx.interrupt_cache;const auto& state=ctx.state;
    if(cache.deadline && state.cycles()<cache.deadline && cache.status==state.cp0(Cop0::Status) &&
       cache.cause==state.cp0(Cop0::Cause) && cache.count==state.cp0(Cop0::Count) &&
       cache.count_anchor==state.count_anchor && cache.compare==state.cp0(Cop0::Compare) &&
       cache.compare_written==state.compare_written) {
        cache.sample_cycle=state.cycles();cache.pending=true;return false;
    }
    return poll_ee_interrupts_cached_slow(ctx);
}
PS2N_EE_INLINE bool interrupt_inert_instruction(ExecutionContext& ctx,std::uint32_t word) {
    const auto op=word>>26u;
    // Index writeback only publishes RAM; the enclosing proof excludes DMA.
    if(op==0x2Fu && ((word>>16u)&31u)==0x14u)return true;
    if(op==0x10u || op==0x12u || op==0x2Fu || (!op && ((word&63u)==12u || (word&63u)==13u)))return false;
    unsigned bytes=0;bool write=false,align=false;
    switch(op) {
        case 0x20:case 0x24:bytes=1;break;
        case 0x28:bytes=1;write=true;break;
        case 0x21:case 0x25:bytes=2;break;
        case 0x29:bytes=2;write=true;break;
        case 0x23:case 0x27:case 0x31:bytes=4;break;
        case 0x2B:case 0x39:bytes=4;write=true;break;
        case 0x37:bytes=8;break;
        case 0x3F:bytes=8;write=true;break;
        case 0x22:case 0x26:bytes=4;align=true;break;
        case 0x2A:case 0x2E:bytes=4;align=true;write=true;break;
        case 0x1A:case 0x1B:bytes=8;align=true;break;
        case 0x2C:case 0x2D:bytes=8;align=true;write=true;break;
        case 0x1E:bytes=16;align=true;break;
        case 0x1F:bytes=16;align=true;write=true;break;
        default:return op<0x20u;
    }
    const auto base=(word>>21u)&31u;
    if(base && (ctx.state.unknown_gpr_bits[base]&0xFFFFFFFFull))return false;
    auto address=static_cast<std::uint32_t>(base ? ctx.state.gpr[base].low : 0u)+
        static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(word)));
    if(align)address&=~(bytes-1u);
    return ctx.memory.interrupt_inert_address(address,bytes,write);
}

struct AotBlock;
struct AotModule;
// Per-dispatch-run cache of immutable module metadata, never executable bytes.
// Module identity is part of every key, including cached interpreter misses.
class AotDispatchCache {
public:
    [[nodiscard]] const AotBlock* find(const AotModule& module, std::uint32_t pc) noexcept;
private:
    struct Entry { const AotModule* module{}; std::uint32_t pc{}; const AotBlock* block{}; };
    std::array<Entry,256> entries_{};
};

[[noreturn]] void aot_code_modified(std::uint32_t pc);
[[noreturn]] void aot_delay_transfer_refused();
void check_cache_privilege(ExecutionContext& ctx);
bool aot_polling_loop(ExecutionContext& ctx,std::uint32_t start,std::span<const std::uint32_t> words);

// Visible to the host compiler so constant PCs, words and delay-slot flags
// specialize the guard together with the instruction. Fetch and interrupt
// sampling stay at every instruction boundary, including after DMA/observers.
inline bool aot_enter_checked(ExecutionContext& ctx, std::uint32_t pc, std::uint32_t word, bool has_delay_slot) {
    auto& state = ctx.state;
    if (state.pc != pc) return false;
    if (ctx.executed >= ctx.budget || (!state.delay_pending && state.pc == ctx.stop_pc)) return false;
    state.instruction_pc=pc;
    state.instruction_in_delay_slot=state.delay_pending;
    const auto sampled=ctx.interrupts_sampled;ctx.interrupts_sampled=false;
    if (!sampled && poll_ee_interrupts_cached(ctx)) return false;
    if(ctx.interrupt_cache.deadline || state.cycles()>=ctx.interrupt_cache.retry_after) {
        if(!interrupt_inert_instruction(ctx,word))flush_interrupt_cache(ctx);
        else if(!ctx.interrupt_cache.deadline)prime_interrupt_cache(ctx);
    }
    if (has_delay_slot && state.delay_pending)
        aot_delay_transfer_refused();
    {
        profiling::Scope profile(profiling::Phase::CodeChecks);
        if (!ctx.memory.code_matches(pc,word)) aot_code_modified(pc);
    }
    if ((word >> 26u) == 0x2Fu) check_cache_privilege(ctx);
    if (ctx.observer != nullptr) ctx.observer->on_instruction(pc, word, ExecutionEngine::Aot);
    return true;
}

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
// Shared by EEState's public accessors and decoded/AOT semantics. Keeping the
// definitions visible here lets constant register indices fold without LTO,
// while retaining the same bounds, symbolic-bit and candidate-domain rules.
inline void check_gpr_index(unsigned index) {
    if (index >= 32u) throw std::out_of_range("Invalid R5900 GPR index");
}
[[noreturn]] void unknown_gpr_operand(unsigned index, unsigned width);
PS2N_EE_INLINE std::uint32_t read_gpr32(const EEState& state, unsigned index) {
    check_gpr_index(index);
    if ((state.unknown_gprs & (1u << index)) && (state.unknown_gpr_bits[index] & 0xFFFFFFFFull))
        unknown_gpr_operand(index,32);
    return index == 0 ? 0u : static_cast<std::uint32_t>(state.gpr[index].low);
}
PS2N_EE_INLINE std::uint64_t read_gpr64(const EEState& state, unsigned index) {
    check_gpr_index(index);
    if (state.unknown_gprs & (1u << index))
        unknown_gpr_operand(index,64);
    return index == 0 ? 0u : state.gpr[index].low;
}
PS2N_EE_INLINE Register128 read_gpr128(const EEState& state, unsigned index) {
    check_gpr_index(index);
    if ((state.unknown_gprs & (1u << index)) || state.unknown_gpr_high_bits[index])
        unknown_gpr_operand(index,128);
    return index == 0 ? Register128{} : state.gpr[index];
}
PS2N_EE_INLINE void write_gpr64(EEState& state, unsigned index, std::uint64_t value) {
    check_gpr_index(index);
    if (state.candidate_gprs & (1u << index)) state.forget_reg_candidates(index);
    state.gpr_candidates[index].bounds[0].reset();state.gpr_candidates[index].bounds[1].reset();
    if (state.unknown_gprs) {
        state.unknown_gpr_bits[index] = 0;
        state.unknown_gprs &= ~(1u << index);
    }
    if (index != 0) state.gpr[index].low = value;
}
PS2N_EE_INLINE void write_gpr32(EEState& state, unsigned index, std::uint32_t value) {
    write_gpr64(state,index,value);
}
PS2N_EE_INLINE void write_gpr32_signed(EEState& state, unsigned index, std::uint32_t value) {
    write_gpr64(state,index,sx32(value));
}
PS2N_EE_INLINE void write_gpr64_observed(EEState& state, unsigned index, std::uint64_t value, std::uint64_t unknown) {
    write_gpr64(state,index,value & ~unknown);
    if (index != 0 && unknown) {
        state.unknown_gpr_bits[index] = unknown;
        state.unknown_gprs |= 1u << index;
    }
}
PS2N_EE_INLINE void write_gpr128(EEState& state, unsigned index, std::uint64_t low, std::uint64_t high) {
    write_gpr64(state,index,low);
    state.unknown_gpr_high_bits[index] = 0;
    state.gpr_candidates[index].bounds[2].reset();state.gpr_candidates[index].bounds[3].reset();
    if (index != 0) state.gpr[index].high = high;
}
PS2N_EE_INLINE void write_gpr128_observed(EEState& state, unsigned index, std::uint64_t low, std::uint64_t high,
                                 std::uint64_t unknown_low, std::uint64_t unknown_high) {
    write_gpr128(state,index,low & ~unknown_low,high & ~unknown_high);
    if (index != 0) {
        state.unknown_gpr_bits[index] = unknown_low;
        state.unknown_gpr_high_bits[index] = unknown_high;
        if (unknown_low) state.unknown_gprs |= 1u << index;
    }
}
inline std::uint64_t raw_gpr64(const EEState& state, unsigned index) {
    return index == 0 ? 0u : state.gpr[index].low;
}
inline bool less_observed(std::uint64_t a,std::uint64_t ua,std::uint64_t b,std::uint64_t ub,bool signed_order,
    const std::vector<std::uint64_t>* av=nullptr,const std::vector<std::uint64_t>* bv=nullptr) {
    const std::uint64_t sign=signed_order ? 1ull<<63u : 0ull;
    a=(a^sign)&~ua;b=(b^sign)&~ub;
    // Every concrete word lies within these unsigned ordered intervals.
    // Their disjointness can prove a comparison without guessing low bits.
    const auto bounds=[sign](std::uint64_t value,std::uint64_t unknown,const auto* candidates) {
        std::uint64_t low=value,high=value|unknown;
        if(candidates) {
            low=~0ull;high=0;
            for(const auto candidate:*candidates){low=std::min(low,candidate^sign);high=std::max(high,candidate^sign);}
        }
        return std::pair{low,high};
    };
    const auto [amin,amax]=bounds(a,ua,av);const auto [bmin,bmax]=bounds(b,ub,bv);
    if(amax<bmin)return true;
    if(amin>=bmax)return false;
    throw std::runtime_error("EE comparison depends on unknown operand bits");
}
template<class Function>
inline bool finite_unary(EEState& state,unsigned dest,unsigned source,Function function) {
    if(!(state.candidate_gprs&(1u<<source)))return false;
    const auto candidates=state.reg64_candidates(source);if(!candidates)return false;
    std::vector<std::uint64_t> values;values.reserve(candidates->size());
    for(const auto value:*candidates)values.push_back(function(value));
    state.set_reg64_candidates(dest,std::move(values));return true;
}
template<class Function>
inline bool finite_binary(EEState& state,unsigned dest,unsigned a,unsigned b,Function function) {
    if(!(state.candidate_gprs&((1u<<a)|(1u<<b))))return false;
    const auto av=state.reg64_candidates(a),bv=state.reg64_candidates(b);
    if((state.unknown_gpr_bits[a]&&!av) || (state.unknown_gpr_bits[b]&&!bv))return false;
    const auto ac=av ? av->size() : 1u,bc=a==b ? 1u : bv ? bv->size() : 1u;
    if(ac*bc>256u)return false;
    std::vector<std::uint64_t> values;values.reserve(ac*bc);
    for(std::size_t i=0;i<ac;++i)for(std::size_t j=0;j<bc;++j) {
        const auto x=av ? (*av)[i] : raw_gpr64(state,a);
        const auto y=a==b ? x : bv ? (*bv)[j] : raw_gpr64(state,b);
        values.push_back(function(x,y));
    }
    state.set_reg64_candidates(dest,std::move(values));return true;
}
inline FpuAffine finite_word_relation(Memory& memory,ObservedWord word,const std::vector<std::uint64_t>& candidates) {
    std::vector<std::uint32_t> values;values.reserve(candidates.size());
    for(const auto value:candidates)values.push_back(static_cast<std::uint32_t>(value));
    std::sort(values.begin(),values.end());values.erase(std::unique(values.begin(),values.end()),values.end());
    // Raw word domains use the same generation-checked RAM sidecar as COP1.
    // No affine scaling is inferred from an arbitrary integer computation.
    return {memory.allocate_fpu_origin(),0,0,false,word.value,word.unknown,values.front(),values.back(),
        std::make_shared<const std::vector<std::uint32_t>>(std::move(values))};
}
inline ObservedWord gpr_word(const EEState& state,unsigned reg,unsigned lane) {
    if(!reg)return {};
    const auto value=lane<2u ? state.gpr[reg].low : state.gpr[reg].high;
    const auto unknown=lane<2u ? state.unknown_gpr_bits[reg] : state.unknown_gpr_high_bits[reg];
    const auto shift=(lane&1u)*32u;
    return {static_cast<std::uint32_t>(value>>shift),static_cast<std::uint32_t>(unknown>>shift)};
}
inline FpuAffine bounded_word_relation(Memory& memory,ObservedWord word,FloatBounds range) {
    if(!word_bounds_match(word,range))throw DeviceRefusal("CPU transfer bounds differ from the copied word");
    FpuAffine relation{memory.allocate_fpu_origin(),0,(word.unknown&0xFF800000u) ? 0u : (word.value>>23u)&255u,
        false,word.value,word.unknown,range[0],range[1],{}};
    if(word.unknown&0xFF800000u)
        relation.ranges=std::make_shared<const std::vector<FloatBounds>>(std::initializer_list<FloatBounds>{range});
    return relation;
}
inline void load_gpr_bounds(EEState& state,Memory& memory,unsigned reg,std::uint32_t address,unsigned lanes) {
    if(!reg || (!state.unknown_gpr_bits[reg] && (lanes<3u || !state.unknown_gpr_high_bits[reg])))return;
    for(unsigned lane=0;lane<lanes;++lane) {
        const auto word=gpr_word(state,reg,lane);if(!word.unknown)continue;
        const auto relation=memory.fpu_relation(address+lane*4u,word);
        if(relation.origin) {
            const FloatBounds range{relation.minimum,relation.maximum};
            if(!word_bounds_match(word,range))throw DeviceRefusal("CPU load bounds differ from the loaded word");
            state.gpr_candidates[reg].bounds[lane]=range;
        }
    }
}
inline void store_gpr_bounds(const EEState& state,Memory& memory,unsigned reg,std::uint32_t address,unsigned lanes) {
    if(!reg)return;
    for(unsigned lane=0;lane<lanes;++lane)if(const auto bounds=state.gpr_candidates[reg].bounds[lane]) {
        const auto word=gpr_word(state,reg,lane);
        if(word.unknown)memory.remember_fpu_relation(address+lane*4u,word,bounded_word_relation(memory,word,*bounds));
    }
}
inline std::optional<std::vector<std::uint32_t>> stored_word_candidates(Memory& memory,std::uint32_t address,ObservedWord word) {
    if(!word.unknown)return std::vector<std::uint32_t>{word.value};
    const auto relation=memory.fpu_relation(address,word);
    if(!relation.origin)return {};
    if(relation.candidates) {
        if(relation.candidates->empty() || relation.candidates->size()>512u ||
           std::any_of(relation.candidates->begin(),relation.candidates->end(),[&](auto value){return ((value^word.value)&~word.unknown)!=0;}))
            throw DeviceRefusal("stored word candidates differ from their observed bits");
        return *relation.candidates;
    }
    if(relation.minimum>relation.maximum || std::uint64_t{relation.maximum}-relation.minimum>=512u)return {};
    std::vector<std::uint32_t> values;
    for(auto value=relation.minimum;;++value) {
        if(!((value^word.value)&~word.unknown))values.push_back(value);
        if(value==relation.maximum)break;
    }
    if(values.empty())return {};
    return values;
}
inline ObservedDoubleword add_observed(std::uint64_t a,std::uint64_t ua,std::uint64_t b,std::uint64_t ub,
                                      unsigned width,bool subtract) {
    a&=~ua;b=(subtract ? ~b : b)&~ub;
    unsigned carry_min=subtract ? 1u : 0u,carry_max=carry_min;
    ObservedDoubleword result{};
    for(unsigned bit=0;bit<width;++bit) {
        const auto av=static_cast<unsigned>((a>>bit)&1u),bv=static_cast<unsigned>((b>>bit)&1u);
        const auto amin=av+bv+carry_min;
        const auto amax=(av|static_cast<unsigned>((ua>>bit)&1u))+
                        (bv|static_cast<unsigned>((ub>>bit)&1u))+carry_max;
        if(amin==amax)result.value|=std::uint64_t{amin&1u}<<bit;
        else result.unknown|=1ull<<bit;
        carry_min=amin>>1u;carry_max=amax>>1u;
    }
    return result;
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
    return read_gpr32(state,rs(word)) + simm32(word);
}

// Branch/jump bookkeeping of the instruction in flight.
struct InstructionFrame {
    bool in_delay_slot{};
    bool pending_indirect{};
    std::uint32_t pending_target{};
};
// Starts executing the instruction at pc: the default next PC is pc + 4.
PS2N_EE_INLINE InstructionFrame begin(EEState& state, std::uint32_t pc) {
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
PS2N_EE_INLINE bool retire(ExecutionContext& ctx, const InstructionFrame& frame) {
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
    write_gpr64(s,rd, lo);
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

template <MipsOpcode Op, std::uint64_t StaticWord = 0x100000000ull>
PS2N_EE_INLINE void exec(EEState& s, Memory& m, std::uint32_t pc, std::uint32_t word) {
    // The interpreter keeps a decoded word; generated AOT fixes it in the
    // template even when the host compiler chooses an out-of-line body.
    const auto w = StaticWord <= 0xFFFFFFFFull ? static_cast<std::uint32_t>(StaticWord) : word;
    using O = MipsOpcode;
    [[maybe_unused]] const unsigned s_ = rs(w);
    [[maybe_unused]] const unsigned t_ = rt(w);
    [[maybe_unused]] const unsigned d_ = rd(w);
    [[maybe_unused]] const unsigned a_ = sa(w);
    (void)m;
    (void)pc;
    // Host execution is sequential: guest memory accesses are already ordered (SYNC).
    if constexpr (Op == O::Cfc2 || Op == O::Ctc2 || Op == O::Qmfc2 || Op == O::Qmtc2 || Op == O::Viswr || Op == O::Vsub || Op == O::Vsqi || Op == O::Viadd || Op == O::Vadd || Op == O::Vmr32 || Op == O::Lqc2 || Op == O::Sqc2 || Op == O::Vmul || Op == O::VmulBc || Op == O::Vmadd || Op == O::VmaddBc || Op == O::Vmula || Op == O::VmulaBc || Op == O::Vmadda || Op == O::VmaddaBc || Op == O::VaddBc || Op == O::VsubBc || Op == O::VaddaBc || Op == O::Vopmula || Op == O::Vopmsub || Op == O::Vsqrt || Op == O::Vrsqrt || Op == O::Vdiv || Op == O::Vwaitq || Op == O::VaddQ || Op == O::VsubQ || Op == O::VmulQ || Op == O::Vmove || Op == O::Vnop || Op == O::Vftoi || Op == O::Vrinit || Op == O::Vrnext || Op == O::Vrget || Op == O::Vrxor || Op == O::Vcallms || Op == O::Vmsub || Op == O::VmsubBc || Op == O::Vmsuba || Op == O::VmsubaBc) {
        if (!(s.cp0(Cop0::Status) & (1u<<30)))
            throw std::runtime_error("COP2 transfer with Status.CU2 = 0; coprocessor unusable vectoring is not modeled");
        constexpr bool transfer=Op==O::Cfc2 || Op==O::Ctc2 || Op==O::Qmfc2 || Op==O::Qmtc2;
        constexpr bool memory_transfer=Op==O::Lqc2 || Op==O::Sqc2;
        if constexpr(Op==O::Vcallms)m.vu_sync_micro(s.cycles());
        if(m.vu0_micro_running()) {
            m.vu_sync_micro(s.cycles());
            if(!memory_transfer && (!transfer || (w&1u))) {
                // shortcut: M-bit early transfer release remains refused by the micro engine.
                for(unsigned events=0;m.vu0_micro_running();++events) {
                    if(events==1000000u)throw std::runtime_error("VU0 micro interlock exceeded the bounded event limit");
                    const auto next=m.vu_next_micro_cycle();
                    if(next<=s.cycles())throw std::runtime_error("VU0 micro interlock has no future issue event");
                    s.stall(next-s.cycles());m.vu_sync_micro(s.cycles());
                }
            }
        }
        m.vu_advance_q(s.cycles());
        if constexpr (Op==O::Vcallms) {
            m.vu0_call_micro((w>>6u)&0x7FFFu,s.cycles());
        } else if constexpr (Op==O::Vnop) {
            // VU manual v6 p.304: no operation and no status-flag changes.
            // VNOP consumes its ordinary instruction slot without waiting
            // for the independent FDIV unit or touching unknown VF/VI data.
        } else if constexpr (Op==O::Vsqrt || Op==O::Vrsqrt || Op==O::Vdiv || Op==O::Vwaitq) {
            const auto ready=m.vu_q_ready_cycle();
            if(ready>s.cycles())s.stall(ready-s.cycles());
            m.vu_advance_q(s.cycles());
            if constexpr(Op!=O::Vwaitq)
                m.vu_issue_q(s.cycles(),d_,t_,(w>>21u)&3u,(w>>23u)&3u,Op==O::Vsqrt ? 1u : (Op==O::Vrsqrt ? 2u : 0u));
        } else if constexpr (Op==O::Vftoi) {
            constexpr unsigned fractions[]{0,4,12,15};
            const auto source=m.observe_vu_vector(d_);auto result=m.observe_vu_vector(t_);
            for(unsigned lane=0;lane<4u;++lane)if((w>>21u)&(8u>>lane)) {
                const auto& input=lane<2u ? source.low : source.high;
                auto& output=lane<2u ? result.low : result.high;
                const auto shift=(lane&1u)*32u;const auto mask=0xFFFFFFFFull<<shift;
                const auto value=vu_float_to_fixed({static_cast<std::uint32_t>(input.value>>shift),
                    static_cast<std::uint32_t>(input.unknown>>shift)},fractions[w&3u]);
                output.value=(output.value&~mask)|(std::uint64_t{value.value}<<shift);
                output.unknown=(output.unknown&~mask)|(std::uint64_t{value.unknown}<<shift);
            }
            m.write_vu_vector(t_,result);
        } else if constexpr(Op==O::VaddQ) m.vu_add(a_,d_,0,(w>>21u)&15u,5);
        else if constexpr(Op==O::VsubQ) m.vu_add(a_,d_,0,(w>>21u)&15u,5,true);
        else if constexpr(Op==O::VmulQ) m.vu_product(a_,d_,0,(w>>21u)&15u,5,false,false);
        else if constexpr (Op == O::Lqc2 || Op == O::Sqc2) {
            const auto address=effective_address(s,w);
            if(address&15u)throw BusError(BusFault::Unaligned,Op==O::Sqc2,16,address,{},BusRegion::Unmapped,
                                         "COP2 quadword transfer requires 16-byte alignment");
            if constexpr (Op==O::Lqc2) {
                const auto low=m.load64_observed(address),high=m.load64_observed(address+8u);
                m.write_vu_vector(t_,{low,high});
                for(unsigned lane=0;lane<4u;++lane) {
                    const auto half=lane<2u ? low : high;
                    const auto shift=(lane&1u)*32u;
                    const ObservedWord word{static_cast<std::uint32_t>(half.value>>shift),static_cast<std::uint32_t>(half.unknown>>shift)};
                    if(!word.unknown)continue;
                    const auto relation=m.fpu_relation(address+4u*lane,word);
                    if(relation.origin)m.restore_vu_vector_bounds(t_,lane,{relation.minimum,relation.maximum});
                }
            } else {
                const auto value=m.observe_vu_vector(t_);
                std::array<std::optional<FloatBounds>,4> bounds{};
                if(value.low.unknown || value.high.unknown)
                    for(unsigned lane=0;lane<4u;++lane)bounds[lane]=m.observe_vu_vector_bounds(t_,lane);
                if(value.low.unknown || value.high.unknown)m.store128_observed(address,value);
                else m.write128(address,value.low.value,value.high.value);
                // SQC2 transports the same values, not every encoding in the
                // coarser bit hull. Reuse the cache/RAM provenance sidecar so
                // LWC1 and later LQC2 see the numeric bounds already proved by VU.
                for(unsigned lane=0;lane<4u;++lane)if(bounds[lane]) {
                    const auto half=lane<2u ? value.low : value.high;
                    const auto shift=(lane&1u)*32u;
                    const ObservedWord word{static_cast<std::uint32_t>(half.value>>shift),static_cast<std::uint32_t>(half.unknown>>shift)};
                    if(!word.unknown)continue;
                    m.remember_fpu_relation(address+4u*lane,word,bounded_word_relation(m,word,*bounds[lane]));
                }
            }
        } else if constexpr (Op == O::Cfc2) {
            const auto word=m.observe_vu_control(rd(w));
            const auto unknown=std::uint64_t{word.unknown} | ((word.unknown&0x80000000u) ? 0xFFFFFFFF00000000ull : 0u);
            write_gpr64_observed(s,t_,sx32(word.value),unknown);
        } else if constexpr (Op == O::Ctc2)
            m.write_vu_control_observed(rd(w),{static_cast<std::uint32_t>(raw_gpr64(s,t_)),
                                              static_cast<std::uint32_t>(s.unknown_gpr_bits[t_])});
        else if constexpr (Op == O::Qmfc2) {
            const auto word=m.observe_vu_vector(d_);
            write_gpr128_observed(s,t_,word.low.value,word.high.value,word.low.unknown,word.high.unknown);
            if(t_ && (word.low.unknown || word.high.unknown))for(unsigned lane=0;lane<4u;++lane)s.gpr_candidates[t_].bounds[lane]=m.observe_vu_vector_bounds(d_,lane);
        } else if constexpr (Op == O::Qmtc2) {
            const auto value=t_ ? s.gpr[t_] : Register128{};
            m.write_vu_vector(d_,{{value.low,s.unknown_gpr_bits[t_]},{value.high,s.unknown_gpr_high_bits[t_]}});
            if(t_)for(unsigned lane=0;lane<4u;++lane)if(const auto bounds=s.gpr_candidates[t_].bounds[lane])m.restore_vu_vector_bounds(d_,lane,*bounds);
            // MFC1/integer operations can carry a finite low-64-bit domain.
            // Q MTC2 copies those same raw lanes; it does not widen their
            // numeric range to every combination of the unknown bit mask.
            if(const auto candidates=s.reg64_candidates(t_))for(unsigned lane=0;lane<2u;++lane) {
                const auto shift=lane*32u;
                if(!static_cast<std::uint32_t>(s.unknown_gpr_bits[t_]>>shift))continue;
                FloatBounds bounds{~0u,0};
                for(const auto candidate:*candidates) {
                    const auto word=static_cast<std::uint32_t>(candidate>>shift);
                    bounds[0]=std::min(bounds[0],word);bounds[1]=std::max(bounds[1],word);
                }
                m.restore_vu_vector_bounds(d_,lane,bounds);
            }
        } else if constexpr (Op == O::Viswr) m.vu_integer_store(t_,d_,(w>>21u)&15u);
        else if constexpr (Op == O::Vsub) m.vu_subtract(a_,d_,t_,(w>>21u)&15u);
        else if constexpr (Op == O::Vadd) m.vu_add(a_,d_,t_,(w>>21u)&15u);
        else if constexpr (Op == O::VaddBc || Op == O::VsubBc || Op == O::VaddaBc)
            m.vu_add(a_,d_,t_,(w>>21u)&15u,w&3u,Op==O::VsubBc,Op==O::VaddaBc);
        else if constexpr (Op == O::Vopmula || Op == O::Vopmsub)
            m.vu_product(a_,d_,t_,14,4,Op==O::Vopmsub,Op==O::Vopmula,true);
        else if constexpr (Op == O::Vmove) {
            const auto source=m.observe_vu_vector(d_);
            auto destination=m.observe_vu_vector(t_);
            const auto mask=(w>>21u)&15u;
            const auto low_mask=((mask&8u) ? 0xFFFFFFFFull : 0ull)|((mask&4u) ? 0xFFFFFFFF00000000ull : 0ull);
            const auto high_mask=((mask&2u) ? 0xFFFFFFFFull : 0ull)|((mask&1u) ? 0xFFFFFFFF00000000ull : 0ull);
            destination.low.value=(destination.low.value&~low_mask)|(source.low.value&low_mask);
            destination.low.unknown=(destination.low.unknown&~low_mask)|(source.low.unknown&low_mask);
            destination.high.value=(destination.high.value&~high_mask)|(source.high.value&high_mask);
            destination.high.unknown=(destination.high.unknown&~high_mask)|(source.high.unknown&high_mask);
            std::array<std::optional<FloatBounds>,4> bounds{};
            for(unsigned lane=0;lane<4u;++lane)bounds[lane]=m.observe_vu_vector_bounds((mask&(8u>>lane)) ? d_ : t_,lane);
            m.write_vu_vector(t_,destination);
            for(unsigned lane=0;lane<4u;++lane)if(bounds[lane])m.restore_vu_vector_bounds(t_,lane,*bounds[lane]);
        }
        else if constexpr (Op == O::Vmr32) m.vu_rotate(t_,d_,(w>>21u)&15u);
        else if constexpr (Op == O::Vmul || Op == O::VmulBc || Op == O::Vmadd || Op == O::VmaddBc ||
                           Op == O::Vmula || Op == O::VmulaBc || Op == O::Vmadda || Op == O::VmaddaBc || Op == O::Vmsub || Op == O::VmsubBc || Op == O::Vmsuba || Op == O::VmsubaBc) {
            constexpr bool broadcast=Op==O::VmulBc || Op==O::VmaddBc || Op==O::VmulaBc || Op==O::VmaddaBc || Op==O::VmsubBc || Op==O::VmsubaBc;
            constexpr bool accumulate=Op==O::Vmadd || Op==O::VmaddBc || Op==O::Vmadda || Op==O::VmaddaBc || Op==O::Vmsub || Op==O::VmsubBc || Op==O::Vmsuba || Op==O::VmsubaBc;
            constexpr bool to_accumulator=Op==O::Vmula || Op==O::VmulaBc || Op==O::Vmadda || Op==O::VmaddaBc || Op==O::Vmsuba || Op==O::VmsubaBc;
            constexpr bool subtract=Op==O::Vmsub || Op==O::VmsubBc || Op==O::Vmsuba || Op==O::VmsubaBc;
            m.vu_product(a_,d_,t_,(w>>21u)&15u,broadcast ? w&3u : 4u,accumulate,to_accumulator,false,subtract);
        }
        else if constexpr (Op == O::Vsqi) m.vu_vector_store_increment(d_,t_,(w>>21u)&15u);
        else if constexpr (Op == O::Vrinit) m.vu_rinit(d_, (w >> 21u) & 3u);
        else if constexpr (Op == O::Vrxor) m.vu_rxor(d_, (w >> 21u) & 3u);
        else if constexpr (Op == O::Vrget) m.vu_rget(t_, (w >> 21u) & 15u);
        else if constexpr (Op == O::Vrnext) m.vu_rnext(t_, (w >> 21u) & 15u);
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
        if(finite_unary(s,d_,t_,[count](std::uint64_t input) {
            auto value=input&mask;
            if constexpr(left)value=(value<<count)&mask;
            else {
                const auto fill=count ? mask^(mask>>count) : 0ull;
                value=(value>>count)|((arithmetic && (value&(1ull<<(width-1u)))) ? fill : 0ull);
            }
            return wide ? value : sx32(static_cast<std::uint32_t>(value));
        }))return;
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
        write_gpr64_observed(s,d_,value,unknown);
    } else if constexpr (Op==O::Addu || Op==O::Subu || Op==O::Daddu || Op==O::Dsubu) {
        constexpr bool wide=Op==O::Daddu || Op==O::Dsubu,subtract=Op==O::Subu || Op==O::Dsubu;
        constexpr auto mask=wide ? ~0ull : 0xFFFFFFFFull;
        if(finite_binary(s,d_,s_,t_,[](std::uint64_t a,std::uint64_t b) {
            const auto value=subtract ? a-b : a+b;
            return wide ? value : sx32(static_cast<std::uint32_t>(value));
        }))return;
        const auto a=raw_gpr64(s,s_)&mask,b=raw_gpr64(s,t_)&mask;
        const auto ua=s.unknown_gpr_bits[s_]&mask,ub=s.unknown_gpr_bits[t_]&mask;
        std::uint64_t value=0,unknown=0;
        // A known zero is an exact arithmetic identity, including copies
        // encoded as DADDU/ADDU. Do not numerically consume opaque payloads.
        if(!b && !ub){value=a;unknown=ua;}
        else if constexpr(!subtract) {
            if(!a && !ua){value=b;unknown=ub;}
            else if(!ua && !ub)value=a+b;
            else {const auto sum=add_observed(a,ua,b,ub,wide ? 64u : 32u,false);value=sum.value;unknown=sum.unknown;}
        } else {
            if(!ua && !ub)value=a-b;
            else if(s_==t_){value=0;unknown=0;}
            else {const auto sum=add_observed(a,ua,b,ub,wide ? 64u : 32u,true);value=sum.value;unknown=sum.unknown;}
        }
        if constexpr(!wide) {
            value=sx32(static_cast<std::uint32_t>(value));
            if(unknown&0x80000000u)unknown|=0xFFFFFFFF00000000ull;
        }
        write_gpr64_observed(s,d_,value,unknown);
    } else if constexpr (Op == O::And) {
        if(finite_binary(s,d_,s_,t_,[](auto a,auto b){return a&b;}))return;
        const auto a = raw_gpr64(s, s_), b = raw_gpr64(s, t_);
        const auto ua = s.unknown_gpr_bits[s_], ub = s.unknown_gpr_bits[t_];
        const auto unknown = (ua | ub) & ~((~a & ~ua) | (~b & ~ub));
        write_gpr64_observed(s,d_, a & b, unknown);
    } else if constexpr (Op == O::Or) {
        if(finite_binary(s,d_,s_,t_,[](auto a,auto b){return a|b;}))return;
        const auto a = raw_gpr64(s, s_), b = raw_gpr64(s, t_);
        const auto ua = s.unknown_gpr_bits[s_], ub = s.unknown_gpr_bits[t_];
        write_gpr64_observed(s,d_, a | b, (ua | ub) & ~((a & ~ua) | (b & ~ub)));
    } else if constexpr (Op == O::Xor) {
        if(finite_binary(s,d_,s_,t_,[](auto a,auto b){return a^b;}))return;
        write_gpr64_observed(s,d_, raw_gpr64(s, s_) ^ raw_gpr64(s, t_),
                            s_ == t_ ? 0u : s.unknown_gpr_bits[s_] | s.unknown_gpr_bits[t_]);
    } else if constexpr (Op == O::Nor) {
        if(finite_binary(s,d_,s_,t_,[](auto a,auto b){return ~(a|b);} ))return;
        const auto a = raw_gpr64(s, s_), b = raw_gpr64(s, t_);
        const auto ua = s.unknown_gpr_bits[s_], ub = s.unknown_gpr_bits[t_];
        write_gpr64_observed(s,d_, ~(a | b), (ua | ub) & ~((a & ~ua) | (b & ~ub)));
    } else if constexpr (Op == O::Slt) {
        write_gpr64(s,d_,s_!=t_ && less_observed(raw_gpr64(s,s_),s.unknown_gpr_bits[s_],raw_gpr64(s,t_),s.unknown_gpr_bits[t_],true,s.reg64_candidates(s_).get(),s.reg64_candidates(t_).get()));
    } else if constexpr (Op == O::Sltu) {
        write_gpr64(s,d_,s_!=t_ && less_observed(raw_gpr64(s,s_),s.unknown_gpr_bits[s_],raw_gpr64(s,t_),s.unknown_gpr_bits[t_],false,s.reg64_candidates(s_).get(),s.reg64_candidates(t_).get()));
    } else if constexpr (Op == O::Movz) {
        if (read_gpr64(s,t_) == 0u) write_gpr64(s,d_, read_gpr64(s,s_));
    } else if constexpr (Op == O::Movn) {
        if (read_gpr64(s,t_) != 0u) write_gpr64(s,d_, read_gpr64(s,s_));
    // MULT/DIV families: pipeline 0 uses the lower halves of LO/HI, the MMI
    // pipeline-1 forms (MULT1, DIV1, ...) the upper halves, with identical rules.
    } else if constexpr (Op == O::Madd || Op == O::Maddu || Op == O::Madd1 || Op == O::Maddu1) {
        // EE instruction manual, MADD/MADDU/MADD1/MADDU1: concatenate
        // only each accumulator's low word, add modulo 2^64, then independently
        // sign-extend both resulting words. The other pipeline is untouched.
        const auto a=read_gpr64(s,s_),b=read_gpr64(s,t_);
        if(a!=sx32(static_cast<std::uint32_t>(a)) || b!=sx32(static_cast<std::uint32_t>(b)))
            throw std::runtime_error("EE MADD requires sign-extended word operands; other inputs are undefined");
        constexpr bool second=Op==O::Madd1 || Op==O::Maddu1;
        constexpr bool unsigned_product=Op==O::Maddu || Op==O::Maddu1;
        auto [lo,hi]=second ? std::tie(s.lo1,s.hi1) : std::tie(s.lo,s.hi);
        const auto product=unsigned_product ? std::uint64_t{static_cast<std::uint32_t>(a)}*static_cast<std::uint32_t>(b) :
            static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(a))*
                static_cast<std::int64_t>(static_cast<std::int32_t>(b)));
        const auto accumulator=(std::uint64_t{static_cast<std::uint32_t>(hi)}<<32u)|static_cast<std::uint32_t>(lo);
        multiply(s,lo,hi,d_,accumulator+product);
    } else if constexpr (Op == O::Mult || Op == O::Mult1 || Op == O::Multu || Op == O::Multu1) {
        constexpr bool second=Op==O::Mult1 || Op==O::Multu1;
        auto [lo,hi]=second ? std::tie(s.lo1,s.hi1) : std::tie(s.lo,s.hi);
        const auto zero=[&](unsigned r){return !(s.unknown_gpr_bits[r]&0xFFFFFFFFu) && static_cast<std::uint32_t>(raw_gpr64(s,r))==0;};
        std::uint64_t product=0;
        if(!zero(s_) && !zero(t_)) {
            const auto a=read_gpr32(s,s_),b=read_gpr32(s,t_);
            if constexpr(Op==O::Mult || Op==O::Mult1)
                product=static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(a))*static_cast<std::int64_t>(static_cast<std::int32_t>(b)));
            else product=std::uint64_t{a}*b;
        }
        multiply(s,lo,hi,d_,product);
    } else if constexpr (Op == O::Div || Op == O::Div1) {
        auto [lo, hi] = Op == O::Div ? std::tie(s.lo, s.hi) : std::tie(s.lo1, s.hi1);
        divide_signed(lo, hi, read_gpr32(s,s_), read_gpr32(s,t_));
    } else if constexpr (Op == O::Divu || Op == O::Divu1) {
        auto [lo, hi] = Op == O::Divu ? std::tie(s.lo, s.hi) : std::tie(s.lo1, s.hi1);
        divide_unsigned(lo, hi, read_gpr32(s,s_), read_gpr32(s,t_));
    } else if constexpr (Op == O::Mfhi) {
        write_gpr64(s,d_, s.hi);
    } else if constexpr (Op == O::Mflo) {
        write_gpr64(s,d_, s.lo);
    } else if constexpr (Op == O::Mthi) {
        s.hi = read_gpr64(s,s_);
    } else if constexpr (Op == O::Mtlo) {
        s.lo = read_gpr64(s,s_);
    } else if constexpr (Op == O::Mfhi1) {
        write_gpr64(s,d_, s.hi1);
    } else if constexpr (Op == O::Mflo1) {
        write_gpr64(s,d_, s.lo1);
    } else if constexpr (Op == O::Mthi1) {
        s.hi1 = read_gpr64(s,s_);
    } else if constexpr (Op == O::Mtlo1) {
        s.lo1 = read_gpr64(s,s_);
    } else if constexpr (Op == O::Mfsa) {
        write_gpr64(s,d_,s.shift_amount);
    } else if constexpr (Op == O::Mtsa) {
        s.shift_amount=read_gpr64(s,s_);
    } else if constexpr (Op == O::Mtsab || Op == O::Mtsah) {
        constexpr unsigned mask=Op==O::Mtsab ? 15u : 7u;
        if(s.unknown_gpr_bits[s_]&mask)throw std::runtime_error("EE SA setup consumes unknown shift count");
        s.shift_amount=((raw_gpr64(s,s_)^w)&mask)*(Op==O::Mtsab ? 8u : 16u);
    } else if constexpr (Op == O::Qfsrv) {
        if(s.shift_amount>127u || (s.shift_amount&7u))
            throw std::runtime_error("EE QFSRV requires an established byte/halfword shift amount");
        std::optional<std::vector<std::uint64_t>> domain;
        if(!s.shift_amount)if(const auto candidates=s.reg64_candidates(t_))domain=*candidates;
        const auto a=s_ ? s.gpr[s_] : Register128{},b=t_ ? s.gpr[t_] : Register128{};
        const std::array<std::uint64_t,4> values{b.low,b.high,a.low,a.high};
        const std::array<std::uint64_t,4> unknown{
            s.unknown_gpr_bits[t_],s.unknown_gpr_high_bits[t_],s.unknown_gpr_bits[s_],s.unknown_gpr_high_bits[s_]};
        const auto word=static_cast<unsigned>(s.shift_amount/64u),bits=static_cast<unsigned>(s.shift_amount%64u);
        const auto extract=[word,bits](const auto& limbs,unsigned half) {
            const auto at=word+half;
            return (limbs[at]>>bits)|(bits ? limbs[at+1u]<<(64u-bits) : 0u);
        };
        write_gpr128_observed(s,d_,extract(values,0),extract(values,1),extract(unknown,0),extract(unknown,1));
        if(domain)s.set_reg64_candidates(d_,std::move(*domain));
    } else if constexpr (Op == O::Pmfhi) {
        write_gpr128(s,d_,s.hi,s.hi1);
    } else if constexpr (Op == O::Pmflo) {
        write_gpr128(s,d_,s.lo,s.lo1);
    } else if constexpr (Op == O::Pmthi) {
        const auto value=read_gpr128(s,s_);s.hi=value.low;s.hi1=value.high;
    } else if constexpr (Op == O::Pmtlo) {
        const auto value=read_gpr128(s,s_);s.lo=value.low;s.lo1=value.high;
    // PADDUW: four 32-bit lanes, unsigned saturation at 0xFFFFFFFF; all 128 bits written.
    } else if constexpr (Op == O::Padduw) {
        const auto a = read_gpr128(s,s_);
        const auto b = read_gpr128(s,t_);
        auto lanes = [](std::uint64_t x, std::uint64_t y) {
            std::uint64_t out = 0;
            for (unsigned shift = 0; shift < 64u; shift += 32u) {
                const auto sum = ((x >> shift) & 0xFFFFFFFFull) + ((y >> shift) & 0xFFFFFFFFull);
                out |= (sum > 0xFFFFFFFFull ? 0xFFFFFFFFull : sum) << shift;
            }
            return out;
        };
        write_gpr128(s,d_, lanes(a.low, b.low), lanes(a.high, b.high));
    // PCPYH: broadcast the low halfword independently in each 64-bit half.
    } else if constexpr (Op == O::Pcpyh) {
        const auto source = t_ ? s.gpr[t_] : Register128{};
        const auto ulow = t_ ? (s.unknown_gpr_bits[t_] & 0xFFFFu) : 0u;
        const auto uhigh = t_ ? (s.unknown_gpr_high_bits[t_] & 0xFFFFu) : 0u;
        constexpr auto repeat = 0x0001000100010001ull;
        write_gpr128_observed(s, d_, (source.low & 0xFFFFu) * repeat, (source.high & 0xFFFFu) * repeat,
                              ulow * repeat, uhigh * repeat);
    } else if constexpr (Op == O::Pcpyld) {
        write_gpr128_observed(s,d_,raw_gpr64(s,t_),raw_gpr64(s,s_),s.unknown_gpr_bits[t_],s.unknown_gpr_bits[s_]);
    } else if constexpr (Op == O::Plzcw) {
        // EE Instruction Set v6 p.215: count the leading sign bits minus
        // one independently in the low two words; preserve upper 64 bits.
        const auto source=read_gpr64(s,s_);
        std::uint64_t result=0;
        for(unsigned shift:{0u,32u}) {
            const auto word=static_cast<std::uint32_t>(source>>shift);
            const auto normalized=(word&0x80000000u) ? ~word : word;
            result|=std::uint64_t(std::countl_zero(normalized)-1)<<shift;
        }
        write_gpr64(s,d_,result);
    } else if constexpr (Op == O::Pcpyud) {
        write_gpr128_observed(s,d_,s_ ? s.gpr[s_].high : 0u,t_ ? s.gpr[t_].high : 0u,
                             s.unknown_gpr_high_bits[s_],s.unknown_gpr_high_bits[t_]);
    } else if constexpr (Op == O::Pextlw || Op == O::Pextuw) {
        const auto a=s_ ? s.gpr[s_] : Register128{},b=t_ ? s.gpr[t_] : Register128{};
        const auto x=Op==O::Pextlw?a.low:a.high, y=Op==O::Pextlw?b.low:b.high;
        const auto ux=Op==O::Pextlw?s.unknown_gpr_bits[s_]:s.unknown_gpr_high_bits[s_];
        const auto uy=Op==O::Pextlw?s.unknown_gpr_bits[t_]:s.unknown_gpr_high_bits[t_];
        write_gpr128_observed(s,d_,(y&0xFFFFFFFFu)|(x<<32u),(y>>32u)|(x&0xFFFFFFFF00000000ull),
                             (uy&0xFFFFFFFFu)|(ux<<32u),(uy>>32u)|(ux&0xFFFFFFFF00000000ull));
    } else if constexpr (Op == O::Psubb || Op == O::Psubw) {
        const auto x=read_gpr128(s,s_),y=read_gpr128(s,t_);
        const auto subtract=[](std::uint64_t a,std::uint64_t b) {
            std::uint64_t result=0;
            constexpr unsigned width=Op==O::Psubw ? 32u : 8u;
            constexpr std::uint64_t mask=Op==O::Psubw ? 0xFFFFFFFFu : 0xFFu;
            for(unsigned bit=0;bit<64;bit+=width)
                result|=(((a>>bit)-(b>>bit))&mask)<<bit;
            return result;
        };
        write_gpr128(s,d_,subtract(x.low,y.low),subtract(x.high,y.high));
    } else if constexpr (Op == O::Ppacw) {
        const auto a=s_ ? s.gpr[s_] : Register128{},b=t_ ? s.gpr[t_] : Register128{};
        write_gpr128_observed(s,d_,(b.low&0xFFFFFFFFu)|(b.high<<32u),(a.low&0xFFFFFFFFu)|(a.high<<32u),
            (s.unknown_gpr_bits[t_]&0xFFFFFFFFu)|(s.unknown_gpr_high_bits[t_]<<32u),
            (s.unknown_gpr_bits[s_]&0xFFFFFFFFu)|(s.unknown_gpr_high_bits[s_]<<32u));
    // PAND/PXOR/POR/PNOR: bitwise over all 128 bits.
    } else if constexpr (Op == O::Pand || Op == O::Pxor || Op == O::Por || Op == O::Pnor) {
        const auto a = read_gpr128(s,s_);
        const auto b = read_gpr128(s,t_);
        auto op = [](std::uint64_t x, std::uint64_t y) -> std::uint64_t {
            if constexpr (Op == O::Pand) return x & y;
            else if constexpr (Op == O::Pxor) return x ^ y;
            else if constexpr (Op == O::Por) return x | y;
            else return ~(x | y);
        };
        write_gpr128(s,d_, op(a.low, b.low), op(a.high, b.high));
    } else if constexpr (Op == O::Addiu) {
        if(finite_unary(s,t_,s_,[w](auto value){return sx32(static_cast<std::uint32_t>(value)+simm32(w));}))return;
        write_gpr32_signed(s,t_, read_gpr32(s,s_) + simm32(w));
    } else if constexpr (Op == O::Daddiu) {
        if(finite_unary(s,t_,s_,[w](auto value){return value+simm64(w);}))return;
        if(s.unknown_gpr_bits[s_]) {
            const auto sum=add_observed(raw_gpr64(s,s_),s.unknown_gpr_bits[s_],simm64(w),0,64,false);
            write_gpr64_observed(s,t_,sum.value,sum.unknown);return;
        }
        write_gpr64(s,t_, read_gpr64(s,s_) + simm64(w));
    } else if constexpr (Op == O::Slti) {
        write_gpr64(s,t_,less_observed(raw_gpr64(s,s_),s.unknown_gpr_bits[s_],simm64(w),0,true,s.reg64_candidates(s_).get()));
    } else if constexpr (Op == O::Sltiu) {
        write_gpr64(s,t_,less_observed(raw_gpr64(s,s_),s.unknown_gpr_bits[s_],simm64(w),0,false,s.reg64_candidates(s_).get()));
    // Logical immediates are zero-extended and act on all 64 bits.
    } else if constexpr (Op == O::Andi) {
        if(finite_unary(s,t_,s_,[w](auto value){return value&zimm(w);}))return;
        write_gpr64_observed(s,t_, raw_gpr64(s, s_) & zimm(w), s.unknown_gpr_bits[s_] & zimm(w));
    } else if constexpr (Op == O::Ori) {
        if(finite_unary(s,t_,s_,[w](auto value){return value|zimm(w);}))return;
        write_gpr64_observed(s,t_, raw_gpr64(s, s_) | zimm(w), s.unknown_gpr_bits[s_] & ~zimm(w));
    } else if constexpr (Op == O::Xori) {
        if(finite_unary(s,t_,s_,[w](auto value){return value^zimm(w);}))return;
        write_gpr64_observed(s,t_, raw_gpr64(s, s_) ^ zimm(w), s.unknown_gpr_bits[s_]);
    } else if constexpr (Op == O::Lui) {
        write_gpr32_signed(s,t_, (w & 0xffffu) << 16);
    } else if constexpr (Op == O::Lb || Op == O::Lbu || Op == O::Lh || Op == O::Lhu) {
        constexpr bool half=Op==O::Lh || Op==O::Lhu;
        constexpr bool signed_load=Op==O::Lb || Op==O::Lh;
        const auto word=half ? m.load16_observed(effective_address(s,w)) : m.load8_observed(effective_address(s,w));
        std::uint64_t value=word.value,unknown=word.unknown;
        if constexpr(signed_load) {
            constexpr auto sign=half ? 0x8000u : 0x80u;
            constexpr auto extension=half ? 0xFFFFFFFFFFFF0000ull : 0xFFFFFFFFFFFFFF00ull;
            if(value&sign)value|=extension;
            if(unknown&sign)unknown|=extension;
        }
        write_gpr64_observed(s,t_,value,unknown);
    } else if constexpr (Op == O::Lwl || Op == O::Lwr || Op == O::Ldl || Op == O::Ldr) {
        constexpr bool wide=Op==O::Ldl || Op==O::Ldr;
        constexpr bool left=Op==O::Lwl || Op==O::Ldl;
        constexpr unsigned bytes=wide ? 8u : 4u;
        const auto address=effective_address(s,w);
        const auto offset=address&(bytes-1u);
        const auto aligned=address&~(bytes-1u);
        std::uint64_t value=0,source_unknown=0;
        if constexpr(wide){const auto observed=m.load64_observed(aligned);value=observed.value;source_unknown=observed.unknown;}
        else {const auto observed=m.load32_observed(aligned);value=observed.value;source_unknown=observed.unknown;}
        auto result=raw_gpr64(s,t_);
        auto unknown=s.unknown_gpr_bits[t_];
        std::array<std::optional<FloatBounds>,2> copied_bounds{};
        if(t_ && (unknown || source_unknown)) {
            const auto first=left ? bytes-1u-offset : 0u,count=left ? offset+1u : bytes-offset;
            for(unsigned lane=0;lane<bytes/4u;++lane) {
                const auto at=lane*4u;
                if(at+4u<=first || at>=first+count)copied_bounds[lane]=s.gpr_candidates[t_].bounds[lane];
                else if(at>=first && at+4u<=first+count) {
                    const auto from=left ? at-first : at+offset;
                    if(from&3u)continue;
                    const ObservedWord word{static_cast<std::uint32_t>(value>>(from*8u)),static_cast<std::uint32_t>(source_unknown>>(from*8u))};
                    const auto relation=m.fpu_relation(aligned+from,word);
                    if(relation.origin) {
                        const FloatBounds range{relation.minimum,relation.maximum};
                        if(!word_bounds_match(word,range))throw DeviceRefusal("partial CPU load certificate differs from the copied word");
                        copied_bounds[lane]=range;
                    }
                }
            }
        }
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
        write_gpr64_observed(s,t_,wide ? result : sx32(static_cast<std::uint32_t>(result)),unknown);
        if(t_)for(unsigned lane=0;lane<bytes/4u;++lane)s.gpr_candidates[t_].bounds[lane]=copied_bounds[lane];
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
        if(used&s.unknown_gpr_bits[t_]) {
            const auto first=left ? 0u : offset;
            const auto count=left ? offset+1u : bytes-offset;
            const auto shift=left ? (bytes-1u-offset)*8u : 0u;
            const auto mask=count==8u ? ~0ull : (1ull<<(count*8u))-1u;
            m.store_bytes_observed(aligned+first,count,{(value>>shift)&mask,(s.unknown_gpr_bits[t_]>>shift)&mask});
            for(unsigned lane=0;lane<bytes/4u;++lane) {
                const auto at=lane*4u;if(at<first || at+4u>first+count)continue;
                const auto from=left ? bytes-1u-offset+at : at-offset;
                if(from&3u)continue;
                if(const auto bounds=s.gpr_candidates[t_].bounds[from/4u]) {
                    const auto word=gpr_word(s,t_,from/4u);
                    if(word.unknown)m.remember_fpu_relation(aligned+at,word,bounded_word_relation(m,word,*bounds));
                }
            }
            return;
        }
        // Only enabled bytes are written; never read-modify-write an MMIO
        // register or touch the adjacent page to complete a partial store.
        for(unsigned i=0;i<bytes;++i) {
            if((left && i<=offset) || (!left && i>=offset)) {
                const auto source=left ? bytes-1u-offset+i : i-offset;
                m.write8(aligned+i,static_cast<std::uint8_t>(value>>(source*8u)));
            }
        }
    } else if constexpr (Op == O::Lw || Op == O::Lwu) {
        const auto address=effective_address(s,w);const auto word=m.load32_observed(address);
        if(word.unknown) {
            const auto relation=m.fpu_relation(address,word);
            if(relation.candidates) {
                std::vector<std::uint64_t> values;values.reserve(relation.candidates->size());
                for(const auto value:*relation.candidates)values.push_back(Op==O::Lw ? sx32(value) : value);
                s.set_reg64_candidates(t_,std::move(values));return;
            }
        }
        const auto unknown = std::uint64_t{word.unknown} | ((word.unknown & 0x80000000u) ? 0xFFFFFFFF00000000ull : 0u);
        write_gpr64_observed(s,t_,Op==O::Lw ? sx32(word.value) : word.value,Op==O::Lw ? unknown : word.unknown);
        load_gpr_bounds(s,m,t_,address,1);
    } else if constexpr (Op == O::Ld) {
        const auto address=effective_address(s,w);const auto word=m.load64_observed(address);
        write_gpr64_observed(s,t_,word.value,word.unknown);load_gpr_bounds(s,m,t_,address,2);
    // LQ/SQ silently clear the low four address bits (no address error).
    } else if constexpr (Op == O::Lq) {
        const auto quad = effective_address(s, w) & ~15u;
        const auto low = m.load64_observed(quad),high=m.load64_observed(quad+8u);
        write_gpr128_observed(s,t_,low.value,high.value,low.unknown,high.unknown);
        // GPR finite domains cover the low 64 bits; upper lanes retain their raw masks.
        if(t_ && low.unknown) {
            const auto a=stored_word_candidates(m,quad,{static_cast<std::uint32_t>(low.value),static_cast<std::uint32_t>(low.unknown)});
            const auto b=stored_word_candidates(m,quad+4u,{static_cast<std::uint32_t>(low.value>>32u),static_cast<std::uint32_t>(low.unknown>>32u)});
            if(a && b && a->size()*b->size()<=512u) {
                std::vector<std::uint64_t> values;
                for(auto x:*a)for(auto y:*b)values.push_back(x|(std::uint64_t{y}<<32u));
                s.set_reg64_candidates(t_,std::move(values));
            }
        }
        load_gpr_bounds(s,m,t_,quad,4);
    } else if constexpr (Op == O::Sb) {
        m.store8_observed(effective_address(s,w),{static_cast<std::uint32_t>(raw_gpr64(s,t_)),static_cast<std::uint32_t>(s.unknown_gpr_bits[t_])});
    } else if constexpr (Op == O::Sh) {
        m.store16_observed(effective_address(s,w),{static_cast<std::uint32_t>(raw_gpr64(s,t_)),static_cast<std::uint32_t>(s.unknown_gpr_bits[t_])});
    } else if constexpr (Op == O::Sw) {
        const auto unknown = static_cast<std::uint32_t>(s.unknown_gpr_bits[t_]);
        if (unknown) {
            const auto address=effective_address(s,w);const ObservedWord word{static_cast<std::uint32_t>(raw_gpr64(s,t_)),unknown};
            const auto candidates=s.reg64_candidates(t_);
            m.store32_observed(address,word);
            if(candidates)m.remember_fpu_relation(address,word,finite_word_relation(m,word,*candidates));
            store_gpr_bounds(s,m,t_,address,1);
        }
        else m.write32(effective_address(s, w), read_gpr32(s,t_));
    } else if constexpr (Op == O::Sd) {
        const auto unknown=s.unknown_gpr_bits[t_];
        if (unknown) {
            const auto address=effective_address(s,w);m.store64_observed(address,{raw_gpr64(s,t_),unknown});
            store_gpr_bounds(s,m,t_,address,2);
        }
        else m.write64(effective_address(s,w),read_gpr64(s,t_));
    } else if constexpr (Op == O::Sq) {
        const auto quad = effective_address(s, w) & ~15u;
        const auto unknown_low=s.unknown_gpr_bits[t_],unknown_high=s.unknown_gpr_high_bits[t_];
        if (unknown_low|unknown_high) {
            const auto value=t_ ? s.gpr[t_] : Register128{};
            m.store128_observed(quad,{{value.low,unknown_low},{value.high,unknown_high}});
            if(const auto candidates=s.reg64_candidates(t_))for(unsigned lane=0;lane<2;++lane) {
                const auto shift=lane*32u;const ObservedWord word{static_cast<std::uint32_t>(value.low>>shift),static_cast<std::uint32_t>(unknown_low>>shift)};
                if(!word.unknown)continue;
                std::vector<std::uint64_t> projected;projected.reserve(candidates->size());
                for(const auto item:*candidates)projected.push_back(item>>shift);
                m.remember_fpu_relation(quad+lane*4u,word,finite_word_relation(m,word,projected));
            }
            store_gpr_bounds(s,m,t_,quad,4);
        } else {
            const auto value=read_gpr128(s,t_);
            m.write128(quad,value.low,value.high);
        }
    // R5900 branches compare the full lower 64 bits. The condition reads rs
    // before BxxZAL writes the link register.
    } else if constexpr (Op==O::CFalseS || Op==O::CEqualS || Op==O::CLessS || Op==O::CLessEqualS) {
        if(!(s.cp0(Cop0::Status)&kStatusCU1))
            throw std::runtime_error("COP1 comparison with Status.CU1 = 0; coprocessor unusable vectoring is not modeled");
        bool result=ee_fpu_compare(s.fpr[d_],s.fpr[t_],w&7u);
        if constexpr(Op!=O::CFalseS)if(s.unknown_fpr_bits[d_] || s.unknown_fpr_bits[t_]) {
            if(d_==t_)result=Op!=O::CLessS; // EE has no NaNs; copies retain identity.
            else {
                const auto classes=[](std::uint32_t value,std::uint32_t unknown) {
                    std::vector<std::array<std::uint32_t,2>> out;
                    value&=~unknown;
                    for(unsigned sign=0;sign<2;++sign) {
                        if(!(unknown&0x80000000u) && sign!=(value>>31u))continue;
                        const auto low=(value&0x7FFFFFFFu)|(sign<<31u);
                        out.push_back({low,low|(unknown&0x7FFFFFFFu)});
                    }
                    return out;
                };
                std::optional<bool> common;
                for(const auto& a:classes(s.fpr[d_],s.unknown_fpr_bits[d_]))
                for(const auto& b:classes(s.fpr[t_],s.unknown_fpr_bits[t_])) {
                    bool all=true,none=true,less=true,greater=true;
                    for(const auto x:a)for(const auto y:b) {
                        const auto comparison=ee_fpu_compare(x,y,w&7u);
                        all&=comparison;none&=!comparison;
                        less&=ee_fpu_compare(x,y,4);greater&=ee_fpu_compare(y,x,4);
                    }
                    // Ordered comparisons are monotone within a sign class,
                    // including denormals flushed to zero. Equality also
                    // requires disjoint intervals to certify false; unequal
                    // endpoints alone cannot exclude an interior equality.
                    if((!all && !none) || (Op==O::CEqualS && none && !less && !greater) ||
                       (common && *common!=all))throw std::runtime_error("COP1 comparison consumes unknown FPR bits with an unproven condition");
                    common=all;
                }
                result=*common;
            }
        }
        s.fcr31=(s.fcr31&~(1u<<23))|(result ? 1u<<23 : 0u);
    } else if constexpr (Op==O::Bc0f || Op==O::Bc0t || Op==O::Bc0fl || Op==O::Bc0tl) {
        const auto status=s.cp0(Cop0::Status);
        if(!(status&(kStatusEXL|kStatusERL|(1u<<28u))) && (status&kStatusKSU))
            throw std::runtime_error("COP0 branch unusable in supervisor/user mode with Status.CU0 = 0");
        const auto condition=m.dma_cop0_condition();
        constexpr bool positive=Op==O::Bc0t || Op==O::Bc0tl;
        branch(s,condition==positive,pc,w,Op==O::Bc0fl || Op==O::Bc0tl);
    } else if constexpr (Op==O::Bc1f || Op==O::Bc1t || Op==O::Bc1fl || Op==O::Bc1tl) {
        if(!(s.cp0(Cop0::Status)&kStatusCU1))
            throw std::runtime_error("COP1 branch with Status.CU1 = 0; coprocessor unusable vectoring is not modeled");
        const bool condition=(s.fcr31&(1u<<23))!=0;
        constexpr bool positive=Op==O::Bc1t || Op==O::Bc1tl;
        branch(s,condition==positive,pc,w,Op==O::Bc1fl || Op==O::Bc1tl);
    } else if constexpr (Op == O::Beq || Op == O::Beql) {
        const auto equal = [&]() {
            if (s_ == t_) return true;
            const auto va = raw_gpr64(s, s_), vb = raw_gpr64(s, t_);
            const auto ua = s.unknown_gpr_bits[s_], ub = s.unknown_gpr_bits[t_];
            if (!ua && !ub) return va == vb;
            if ((va ^ vb) & ~(ua | ub)) return false;
            return read_gpr64(s, s_) == read_gpr64(s, t_);
        }();
        branch(s, equal, pc, w, Op == O::Beql);
    } else if constexpr (Op == O::Bne || Op == O::Bnel) {
        const auto not_equal = [&]() {
            if (s_ == t_) return false;
            const auto va = raw_gpr64(s, s_), vb = raw_gpr64(s, t_);
            const auto ua = s.unknown_gpr_bits[s_], ub = s.unknown_gpr_bits[t_];
            if (!ua && !ub) return va != vb;
            if ((va ^ vb) & ~(ua | ub)) return true;
            return read_gpr64(s, s_) != read_gpr64(s, t_);
        }();
        branch(s, not_equal, pc, w, Op == O::Bnel);
    } else if constexpr (Op == O::Blez || Op == O::Blezl) {
        const bool positive = s.unknown_gpr_bits[s_]
            ? less_observed(0, 0, raw_gpr64(s, s_), s.unknown_gpr_bits[s_], true, nullptr, s.reg64_candidates(s_).get())
            : (s64(read_gpr64(s, s_)) > 0);
        branch(s, !positive, pc, w, Op == O::Blezl);
    } else if constexpr (Op == O::Bgtz || Op == O::Bgtzl) {
        const bool positive = s.unknown_gpr_bits[s_]
            ? less_observed(0, 0, raw_gpr64(s, s_), s.unknown_gpr_bits[s_], true, nullptr, s.reg64_candidates(s_).get())
            : (s64(read_gpr64(s, s_)) > 0);
        branch(s, positive, pc, w, Op == O::Bgtzl);
    } else if constexpr (Op == O::Bltz || Op == O::Bltzl || Op == O::Bltzal || Op == O::Bltzall) {
        const bool taken = negative_gpr64(s,s_);
        if constexpr (Op == O::Bltzal || Op == O::Bltzall) write_gpr32_signed(s,31u, pc + 8u);
        branch(s, taken, pc, w, Op == O::Bltzl || Op == O::Bltzall);
    } else if constexpr (Op == O::Bgez || Op == O::Bgezl || Op == O::Bgezal || Op == O::Bgezall) {
        const bool taken = !negative_gpr64(s,s_);
        if constexpr (Op == O::Bgezal || Op == O::Bgezall) write_gpr32_signed(s,31u, pc + 8u);
        branch(s, taken, pc, w, Op == O::Bgezl || Op == O::Bgezall);
    } else if constexpr (Op == O::J) {
        s.delay_pending = true;
        s.delay_target = mips_jump_target(pc, w);
    } else if constexpr (Op == O::Jal) {
        write_gpr32_signed(s,31u, pc + 8u);
        s.delay_pending = true;
        s.delay_target = mips_jump_target(pc, w);
    } else if constexpr (Op == O::Jr) {
        s.delay_pending = true;
        s.delay_indirect = true;
        s.delay_target = read_gpr32(s,s_);
    } else if constexpr (Op == O::Jalr) {
        // Deliberate order: JALR rs==rd must read its target BEFORE writing link.
        s.delay_target = read_gpr32(s,s_);
        write_gpr32_signed(s,d_, pc + 8u);
        s.delay_pending = true;
        s.delay_indirect = true;
    } else {
        static_assert(kSystemInstruction<Op>, "system instructions are interpreter-only");
    }
}
} // namespace ee
} // namespace ps2native
#undef PS2N_EE_INLINE
