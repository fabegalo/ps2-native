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

// Unsupported guest behavior stops execution before it can be guessed.
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

} // namespace ps2native
