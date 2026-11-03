#pragma once
// GS privileged registers (physical 0x12000000..0x12001FFF) as seen by the EE,
// independent of any graphics backend: control state only, nothing is drawn.
//
// Modeled: SMODE1, SMODE2, SRFSH, SYNCH1, SYNCH2, SYNCV (write-only, 64-bit
// stores) and CSR writes. Field layouts follow ps2sdk gs_privileged.h; the
// SMODE1 PRST/SINT start-up sequence follows ps2sdk graph_mode.c. Everything
// else (reads, other registers, other widths, reserved bits) is refused.
#include "ps2native/device_timing.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace ps2native {
inline constexpr std::uint32_t kGsPrivilegedBase = 0x12000000u;
inline constexpr std::uint32_t kGsPrivilegedEnd = 0x12002000u;
inline constexpr std::uint32_t kGsSmode1 = 0x12000010u;
inline constexpr std::uint32_t kGsSmode2 = 0x12000020u;
inline constexpr std::uint32_t kGsSrfsh = 0x12000030u;
inline constexpr std::uint32_t kGsSynch1 = 0x12000040u;
inline constexpr std::uint32_t kGsSynch2 = 0x12000050u;
inline constexpr std::uint32_t kGsSyncv = 0x12000060u;
inline constexpr std::uint32_t kGsCsr = 0x12001000u;

// SMODE1 fields used by the line clock.
inline constexpr std::uint64_t kSmode1Prst = 1ull << 16;
inline constexpr std::uint64_t kSmode1Sint = 1ull << 17;
inline constexpr unsigned kSmode1CmodShift = 13u;
// CSR write bits (ps2sdk GS_SET_CSR).
inline constexpr std::uint64_t kCsrSignal = 1ull << 0;
inline constexpr std::uint64_t kCsrFinish = 1ull << 1;
inline constexpr std::uint64_t kCsrHsint = 1ull << 2;
inline constexpr std::uint64_t kCsrVsint = 1ull << 3;
inline constexpr std::uint64_t kCsrEdwint = 1ull << 4;
inline constexpr std::uint64_t kCsrFlush = 1ull << 8;
inline constexpr std::uint64_t kCsrReset = 1ull << 9;

class GsRegisters {
public:
    // HBLANK source for the EE timers: lines start at `epoch` and repeat
    // every `period` EE cycles; the first edge is one period after `epoch`.
    struct LineClock {
        std::uint64_t epoch{};
        std::uint64_t period{};
        bool operator==(const LineClock&) const = default;
    };
    // Called with the reason before a write changes the line clock; it may
    // throw DeviceRefusal to reject the write (nothing is modified then).
    using LineClockGuard = std::function<void()>;

    // Power-on state at EE cycle `now`: nothing written; the line clock is
    // NTSC from `now` (hypothesis, as before the GS was modeled).
    void reset(std::uint64_t now);
    [[nodiscard]] std::uint64_t read(std::uint32_t physical, std::size_t bytes) const;
    void write(std::uint32_t physical, std::size_t bytes, std::uint64_t value, std::uint64_t now,
               const LineClockGuard& guard);
    // Throws DeviceRefusal when no HBLANK is generated or its timing is not modeled.
    [[nodiscard]] LineClock line_clock() const;
    // Last value written to a modeled register since reset (for traces and tests).
    [[nodiscard]] std::optional<std::uint64_t> latched(std::uint32_t physical) const;

private:
    enum class Sync : std::uint8_t { Running, Stopped, UnsupportedMode };
    struct LineState {
        Sync sync{Sync::Running};
        unsigned cmod{2u};
        LineClock clock{};
        bool operator==(const LineState&) const = default;
    };
    static constexpr std::size_t kModeRegisters = 6u; // SMODE1..SYNCV
    void change_line_state(const LineState& next, const LineClockGuard& guard);

    std::array<std::optional<std::uint64_t>, kModeRegisters> mode_{};
    std::optional<std::uint64_t> csr_written_;
    LineState line_{};
};
} // namespace ps2native
