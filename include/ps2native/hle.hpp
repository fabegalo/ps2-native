#pragma once
#include <memory>
#include <cstdint>

namespace ps2native {
class Memory;
struct EEState;
struct ExecutionContext;

// PS2Recomp-derived EE services, explicitly attached to one existing console.
// Scheduling is syscall-driven; device IRQ/alarm wakeups are not implemented.
class HleKernel {
public:
    HleKernel(EEState& state, Memory& memory);
    ~HleKernel();
    HleKernel(const HleKernel&) = delete;
    HleKernel& operator=(const HleKernel&) = delete;
    void dispatch_syscall(ExecutionContext& context);
    void finish_syscall();
    void check_running() const;
    [[nodiscard]] std::uint64_t syscall_count() const noexcept;
    [[nodiscard]] int current_thread_id() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
} // namespace ps2native
