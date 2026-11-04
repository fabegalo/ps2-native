#pragma once
#include <array>
#include <cstdint>
namespace ps2native {
// Full 128-bit GPR storage. Implemented instructions operate on the lower
// 64 bits and preserve the upper 64 bits, except LQ/SQ which move all 128.
struct Register128 {
    std::uint64_t low{};
    std::uint64_t high{};
};
// COP0 register numbers used by the boot path (EE Core User's Manual).
enum class Cop0 : unsigned {
    Index = 0, Random = 1, EntryLo0 = 2, EntryLo1 = 3, Context = 4, PageMask = 5,
    Wired = 6, BadVAddr = 8, Count = 9, EntryHi = 10, Compare = 11, Status = 12,
    Cause = 13, Epc = 14, PRid = 15, Config = 16, BadPAddr = 23, Debug = 24,
    Perf = 25, TagLo = 28, TagHi = 29, ErrorEpc = 30
};
// Status fields (R5900 layout).
inline constexpr std::uint32_t kStatusIE = 1u << 0;
inline constexpr std::uint32_t kStatusEXL = 1u << 1;
inline constexpr std::uint32_t kStatusERL = 1u << 2;
inline constexpr std::uint32_t kStatusKSU = 3u << 3;
inline constexpr std::uint32_t kStatusIM7 = 1u << 15;
inline constexpr std::uint32_t kStatusIM2 = 1u << 10; // INTC INT0
inline constexpr std::uint32_t kStatusIM3 = 1u << 11; // DMAC INT1
inline constexpr std::uint32_t kStatusEIE = 1u << 16;
inline constexpr std::uint32_t kStatusEDI = 1u << 17;
inline constexpr std::uint32_t kStatusBEV = 1u << 22;
inline constexpr std::uint32_t kStatusCU1 = 1u << 29;
// Bits that exist in the R5900 Status register; the rest read as zero.
inline constexpr std::uint32_t kStatusWritable = 0xF0C79C1Fu;

struct EEState {
    // Unknown bits from partially specified MMIO are symbolic. Ordinary GPR
    // consumers refuse them; bitwise semantics may prove a result independent.
    std::array<std::uint64_t, 32> unknown_gpr_bits{};
    std::array<std::uint64_t, 32> unknown_gpr_high_bits{};
    std::uint32_t unknown_gprs{};
    std::array<Register128, 32> gpr{};
    // LO/HI are 128 bits: lo/hi are the lower halves (pipeline 0), lo1/hi1 the
    // upper halves written by the MMI pipeline-1 instructions (MULT1, DIV1, ...).
    std::uint64_t lo{};
    std::uint64_t hi{};
    std::uint64_t lo1{};
    std::uint64_t hi1{};
    std::uint64_t shift_amount{}; // SA saved/restored by MFSA/MTSA
    // COP1 (FPU) registers as raw 32-bit words. Cleared at reset like every
    // register (hypothesis: their power-on contents are not documented).
    std::array<std::uint32_t, 32> fpr{};
    // Numeric uncertainty from certified arithmetic bounds and opaque raw
    // transfers. A numeric consumer must prove its result or refuse.
    std::array<std::uint32_t, 32> unknown_fpr_bits{};
    std::uint32_t fcr31{0x01000001u}; // fixed bits + reset flags (EE Core manual)
    std::uint32_t fpu_acc{}; // same explicit reset hypothesis as raw FPRs
    std::uint32_t pc{};
    bool delay_pending{};
    std::uint32_t delay_target{};
    // The pending transfer came from JR/JALR: its target must be a verified entry.
    bool delay_indirect{};
    // pc was reached through JR/JALR and has not yet been checked by the dispatcher.
    bool indirect_entry_pending{};
    // Address of the instruction being executed (diagnostics: faults, block points).
    std::uint32_t instruction_pc{};
    // The instruction at instruction_pc executes in a branch delay slot.
    bool instruction_in_delay_slot{};
    // Base timing remains one cycle per instruction, plus modeled stalls.
    std::uint64_t retired_instructions{};
    std::uint64_t stall_cycles{};
    [[nodiscard]] std::uint64_t cycles() const noexcept { return retired_instructions+stall_cycles; }
    std::array<std::uint32_t, 32> cop0{};
    // EE Core User's Manual pp.80/84: enables reset clear, configuration
    // fields are undefined. Context save/restore must retain that distinction.
    std::uint32_t bpc_unknown{0x17BF8007u}, pccr_unknown{0x000FFBFEu};
    std::uint32_t taglo_unknown{}; // populated by explicit D-cache diagnostic reads
    // COP0 Count advances once per EE cycle: it reads as cop0[Count] plus the
    // elapsed cycles since the last MTC0 Count (count_anchor).
    std::uint64_t count_anchor{};
    bool compare_written{}; // Compare is undefined until programmed.
    [[nodiscard]] std::uint32_t count() const noexcept {
        return cop0[9] + static_cast<std::uint32_t>(cycles() - count_anchor);
    }
    void stall(std::uint64_t elapsed) noexcept {
        const auto distance=static_cast<std::uint32_t>(cop0[11]-count());
        if(compare_written && (distance==0 || elapsed>=distance))cop0[13]|=kStatusIM7;
        stall_cycles+=elapsed;
    }
    [[nodiscard]] std::uint32_t reg32(unsigned index) const;
    [[nodiscard]] std::uint64_t reg64(unsigned index) const;
    [[nodiscard]] Register128 reg128(unsigned index) const;
    void set_reg32(unsigned index, std::uint32_t value);
    void set_reg32_sign_extended(unsigned index, std::uint32_t value);
    void set_reg64(unsigned index, std::uint64_t value);
    void set_reg64_observed(unsigned index, std::uint64_t value, std::uint64_t unknown);
    void set_reg128(unsigned index, std::uint64_t low, std::uint64_t high);
    void set_reg128_observed(unsigned index, std::uint64_t low, std::uint64_t high,
                             std::uint64_t unknown_low, std::uint64_t unknown_high);
    [[nodiscard]] std::uint32_t cp0(Cop0 reg) const noexcept { return cop0[static_cast<unsigned>(reg)]; }
    void set_cp0(Cop0 reg, std::uint32_t value) noexcept {
        cop0[static_cast<unsigned>(reg)] = value;
        if(reg==Cop0::Debug)bpc_unknown=0;
        if(reg==Cop0::Perf)pccr_unknown=0;
        if(reg==Cop0::TagLo)taglo_unknown=0;
    }
};

// EE core state after a hardware reset: every register cleared, PC at the
// reset vector 0xBFC00000, Status = BEV | ERL (bootstrap vectors, error level),
// PRid = 0x2E20 (R5900 implementation 0x2E, revision 0x20), Config = 0x440
// (16 KiB instruction cache, 8 KiB data cache, caches disabled), Random = 47.
[[nodiscard]] EEState ee_reset_state();
// Samples Count/Compare before every instruction in both engines. Returns true
// when interrupt entry changed PC. Timing retains the shared 1 cycle/instruction
// model; this does not claim cycle-accurate superscalar execution.
bool poll_ee_timer_interrupt(EEState& state);
class Memory;
bool poll_ee_interrupts(EEState& state, Memory& memory);
// Level-1 synchronous exception entry. Does not implement a BIOS service:
// execution continues at the guest's V_COMMON handler (or bootstrap vector).
// Nested EXL preserves EPC/BD; the instruction's pending branch is cancelled.
void enter_ee_exception(EEState& state, std::uint32_t code, std::uint32_t pc, bool in_delay_slot);
} // namespace ps2native
