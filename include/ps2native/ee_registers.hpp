#pragma once
// EE hardware registers at physical 0x10000000..0x1000FFFF that the boot path
// models. Each register documents what is known and what is a hypothesis;
// every other address, access size or mode still fails with a reason.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace ps2native {
// EE time base: one EE core cycle per retired instruction. This is a
// deterministic hypothesis (the real R5900 CPI varies with stalls and cache
// misses); COP0 Count and every EE timer derive from this one clock, so they
// stay mutually consistent.
inline constexpr std::uint64_t kEeClockHz = 294'912'000u;
// BUSCLK is half the EE clock (147.456 MHz).
inline constexpr std::uint64_t kEeCyclesPerBusCycle = 2u;
// HBLANK before the GS CRTC is programmed: NTSC line length of 9370 BUSCLK
// cycles (ps2tek). Hypothesis for the reset state; the BIOS itself converts
// its HBLANK measurement with 15734 Hz, an NTSC line rate.
inline constexpr std::uint64_t kBusCyclesPerNtscLine = 9370u;

// A modeled register refused this access: the reason names the register and
// what is missing. Memory turns it into a BusError with the full address.
class DeviceRefusal : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class EeRegisters {
public:
    // State after a hardware reset; the clock is attached separately.
    void reset();
    // EE cycle counter (EEState::retired_instructions during boot).
    void set_clock(const std::uint64_t* ee_cycles) noexcept { clock_ = ee_cycles; }
    [[nodiscard]] std::uint64_t read(std::uint32_t physical, std::size_t bytes);
    void write(std::uint32_t physical, std::size_t bytes, std::uint64_t value);

private:
    // EE timers T0..T3 (Tn_COUNT/MODE/COMP/HOLD at 0x10000000 + n * 0x800).
    struct Timer {
        std::uint32_t mode{};
        std::uint16_t count_at_anchor{};
        std::uint64_t anchor_cycle{};
        std::optional<std::uint16_t> compare; // unset until written
    };
    [[nodiscard]] std::uint64_t now() const;
    [[nodiscard]] std::uint16_t timer_count(const Timer& timer) const;
    void write_timer(unsigned index, unsigned reg, std::uint32_t value);
    [[nodiscard]] std::uint32_t read_timer(unsigned index, unsigned reg);

    std::array<Timer, 4> timers_{};
    // 0x1000F500: unidentified register written by the BIOS reset code.
    std::optional<std::uint32_t> f500_;
    const std::uint64_t* clock_{};
};
} // namespace ps2native
