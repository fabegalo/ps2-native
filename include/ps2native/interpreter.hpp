#pragma once
#include "ps2native/ee_semantics.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace ps2native {
enum class StopKind : std::uint8_t {
    GuestException, // architectural exception the guest raised (SYSCALL, BREAK): vectoring not modeled
    NotModeled,     // defined hardware behavior this build does not model yet
    Undefined       // architecturally undefined/unpredictable use
};
[[nodiscard]] const char* stop_kind_name(StopKind kind) noexcept;

// The interpreter refuses to guess: anything it cannot reproduce faithfully stops execution.
class GuestStop : public std::runtime_error {
public:
    GuestStop(StopKind stop_kind, const std::string& message) : std::runtime_error(message), kind(stop_kind) {}
    StopKind kind;
};

class SystemEventObserver {
public:
    virtual ~SystemEventObserver() = default;
    // RUNTIME_HELPERS_BEGIN
    // A bounded observer may decline formatting after its trace is full.
    virtual bool wants_system_events() const noexcept { return true; }
    // RUNTIME_HELPERS_END
    // COP0 writes, TLB maintenance, CACHE operations, ERET, EI/DI.
    virtual void on_system_event(std::string_view event) = 0;
};

// Executes exactly the instruction at state.pc: integer instructions through the
// shared semantics (identical to AOT blocks); COP0, TLB, ERET, EI/DI, CACHE and
// PREF here. Throws BusError, GuestStop or std::runtime_error (unknown encoding).
void interpret_instruction(ExecutionContext& ctx, SystemEventObserver* events = nullptr);
    // RUNTIME_HELPERS_BEGIN
// Per-run decode metadata only; fetch still checks the current guest bytes.
class InterpreterDecodeCache {
public:
    [[nodiscard]] MipsInstruction decode(std::uint32_t pc,std::uint32_t word) {
        auto& entry=entries_[((pc>>2u)^(pc>>12u))&(entries_.size()-1u)];
        if(!entry.valid || entry.instruction.pc!=pc || entry.instruction.word!=word) {
            entry.instruction=decode_r5900(pc,word);entry.valid=true;
        }
        return entry.instruction;
    }
private:
    struct Entry {MipsInstruction instruction{};bool valid{};};
    std::array<Entry,256> entries_{};
};
void interpret_instruction(ExecutionContext& ctx,SystemEventObserver* events,InterpreterDecodeCache& cache);
    // RUNTIME_HELPERS_END
} // namespace ps2native
