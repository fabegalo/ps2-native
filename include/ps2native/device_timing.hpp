#pragma once
// Shared by the device register models: the refusal type and the EE time base.
#include <cstdint>
#include <stdexcept>

namespace ps2native {
// Unknown bits are symbolic, never a default hardware response.
struct ObservedWord { std::uint32_t value{}, unknown{}; };
struct ObservedDoubleword { std::uint64_t value{}, unknown{}; };
// A modeled register refused this access: the reason names the register and
// what is missing. Memory turns it into a BusError with the full address.
class DeviceRefusal : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// EE time base: one EE core cycle per retired instruction. This is a
// deterministic hypothesis (the real R5900 CPI varies with stalls and cache
// misses); COP0 Count, the EE timers and the GS line clock all derive from
// this one clock, so they stay mutually consistent.
inline constexpr std::uint64_t kEeClockHz = 294'912'000u;
// BUSCLK is half the EE clock (147.456 MHz).
inline constexpr std::uint64_t kEeCyclesPerBusCycle = 2u;
// Line lengths in BUSCLK cycles (ps2tek, EE timers): NTSC 9370, PAL 9436.
inline constexpr std::uint64_t kBusCyclesPerNtscLine = 9370u;
inline constexpr std::uint64_t kBusCyclesPerPalLine = 9436u;
} // namespace ps2native
