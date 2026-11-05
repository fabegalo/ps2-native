#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace ps2native::profiling {
// Host-only, opt-in wall-time attribution. No guest clocks or serialized fields.
enum class Phase : unsigned { Ee, Iop, Vu, Gs, Memory, Devices, CodeChecks, Bounds, Sync, Presentation, Dispatch, Count };
inline constexpr std::array names{"ee", "iop", "vu", "gs", "memory", "devices", "code_checks", "numeric_bounds", "sync", "presentation", "dispatch"};
struct Profile {
    std::array<std::uint64_t,static_cast<unsigned>(Phase::Count)> exclusive_ns{},inclusive_ns{},calls{};
};
#if defined(PS2N_ENABLE_PROFILING)
inline constexpr bool available=true;
namespace detail {
using Clock=std::chrono::steady_clock;
inline thread_local Profile* active{};
inline thread_local Phase phase=Phase::Ee;
inline thread_local Clock::time_point since{};
inline std::uint64_t elapsed(Clock::time_point start,Clock::time_point end) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count());
}
inline void flush(Clock::time_point now) {
    active->exclusive_ns[static_cast<unsigned>(phase)]+=elapsed(since,now);since=now;
}
}
class Scope {
public:
    explicit Scope(Phase next) : profile_(detail::active),phase_(next) {
        if(!profile_)return;
        start_=detail::Clock::now();previous_=detail::phase;
        ++profile_->calls[static_cast<unsigned>(next)];
        changed_=next!=previous_;
        if(changed_){detail::flush(start_);detail::phase=next;}
    }
    ~Scope() {
        if(!profile_)return;
        const auto now=detail::Clock::now();
        profile_->inclusive_ns[static_cast<unsigned>(phase_)]+=detail::elapsed(start_,now);
        if(changed_){detail::flush(now);detail::phase=previous_;}
    }
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
private:
    Profile* profile_{};
    Phase phase_{},previous_{};
    detail::Clock::time_point start_{};
    bool changed_{};
};
class Session {
public:
    explicit Session(Profile* profile) : profile_(profile) {
        if(!profile_)return;
        start_=detail::Clock::now();previous_=detail::active;previous_phase_=detail::phase;
        if(previous_)detail::flush(start_);
        detail::active=profile_;detail::phase=Phase::Ee;detail::since=start_;
        ++profile_->calls[0];
    }
    ~Session() {
        if(!profile_)return;
        const auto now=detail::Clock::now();detail::flush(now);
        profile_->inclusive_ns[0]+=detail::elapsed(start_,now);
        detail::active=previous_;detail::phase=previous_phase_;detail::since=now;
    }
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
private:
    Profile* profile_{},*previous_{};
    Phase previous_phase_{};
    detail::Clock::time_point start_{};
};
#else
inline constexpr bool available=false;
class Scope {
public:
    explicit Scope(Phase) noexcept {}
};
class Session {
public:
    explicit Session(Profile* profile) {
        if(profile)throw std::runtime_error("profiling requires -DPS2N_ENABLE_PROFILING=ON; normal builds have no per-access timing overhead");
    }
};
#endif
}
