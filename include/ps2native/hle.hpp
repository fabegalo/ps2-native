#pragma once
#include <memory>
#include <cstdint>
class PS2Runtime;
class EeScheduler;

namespace ps2native {
class Memory;
struct EEState;
struct ExecutionContext;
struct BiosImage;

// Host attachment for directly compiled PS2Recomp kernel services and scheduler.
// Only context, checked memory and canonical device interfaces live here.
class HleKernel {
public:
    explicit HleKernel(PS2Runtime& runtime);
    ~HleKernel();
    HleKernel(const HleKernel&) = delete;
    HleKernel& operator=(const HleKernel&) = delete;
    void dispatch_syscall(ExecutionContext& context);
    void finish_syscall();
    bool complete_syscall_return();
    bool dispatch_interrupt(std::uint32_t lines);
    void configure_bios(std::shared_ptr<const BiosImage> bios);
    void check_running() const;
    bool poll_execution(ExecutionContext& context);
    [[nodiscard]] std::uint64_t next_deadline() const;
    [[nodiscard]] std::uint64_t syscall_count() const noexcept;
    [[nodiscard]] int current_thread_id() const noexcept;
private:
    friend class ::PS2Runtime;
    struct Impl;
    std::unique_ptr<Impl> p_;
};
} // namespace ps2native
