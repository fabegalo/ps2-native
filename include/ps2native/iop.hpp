#pragma once
// The IOP (I/O processor): an R3000A-compatible core that starts at the same
// reset vector as the EE (ps2tek: "Upon reset, both the EE and IOP begin
// executing at BFC00000h") and takes the PRId < 0x59 branch of the shared ROM0
// reset code. Modeled for the boot path only: the MIPS I integer instruction
// set with branch and load delay slots, the COP0 registers the reset code
// writes, cache isolation (Status.IsC), 2 MiB of RAM, ROM0, and the bus
// configuration registers of the reset code as latches. Everything else stops
// the IOP with a named reason; the EE is refused when it depends on IOP state
// that the IOP did not produce.
//
// Time base: the IOP clock is 36.864 MHz, exactly one eighth of the EE clock
// (fact); one IOP cycle per instruction is a hypothesis, like the EE's. The
// IOP never touches EE state here, so it runs lazily: whenever the EE reads
// state the IOP owns, the IOP first runs up to the EE's current time. Both EE
// engines (interpreter and AOT) see the same IOP state at the same EE cycle.
#include "ps2native/console.hpp"
#include "ps2native/device_timing.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ps2native {
inline constexpr std::uint64_t kEeCyclesPerIopCycle = 8u; // 294.912 MHz / 36.864 MHz
inline constexpr std::uint32_t kIopRamBytes = 2u * 1024u * 1024u; // PS2Ident: 2097152 on every model
// EE physical window onto IOP RAM.
inline constexpr std::uint32_t kIopRamEePhysical = 0x1C000000u;

class Iop {
public:
    struct Event {
        std::uint64_t cycle; // IOP cycle
        std::string text;
    };
    Iop();
    // Power-on with `rom0` (empty: no ROM, the IOP stays stopped) on console
    // `hardware` (nullptr: none, so its PRId read stops the IOP).
    void reset(std::span<const std::uint8_t> rom0, const ConsoleHardware* hardware);
    // Runs until `cycle` IOP cycles since reset, or until the IOP stops.
    void run_until(std::uint64_t cycle);
    // EE read of IOP RAM at `offset` (the IOP must have been run up to now).
    // Throws DeviceRefusal when the bytes were never written by the IOP, or
    // when the IOP stopped before this time (its later state is unknown).
    [[nodiscard]] std::uint64_t ee_read(std::uint32_t offset, std::size_t bytes) const;

    [[nodiscard]] std::uint64_t cycle() const noexcept { return cycle_; }
    [[nodiscard]] std::uint32_t pc() const noexcept { return pc_; }
    [[nodiscard]] bool stopped() const noexcept { return stop_.has_value(); }
    [[nodiscard]] const std::optional<std::string>& stop_reason() const noexcept { return stop_; }
    [[nodiscard]] const std::vector<Event>& events() const noexcept { return events_; }
    [[nodiscard]] std::uint32_t gpr(unsigned index) const noexcept { return gpr_[index & 31u]; }

private:
    struct PendingLoad {
        unsigned reg;
        std::uint32_t value;
        bool defined{true};
        std::string origin; // why the value is unknown (when !defined)
    };
    struct Access {
        std::uint32_t physical;
        bool cached;
        bool kseg2;
    };
    void step();
    [[nodiscard]] std::uint32_t reg(unsigned index) const;
    // Value to store: may be unknown (copied as unknown into RAM or a latch).
    [[nodiscard]] std::optional<std::uint32_t> stored_reg(unsigned index) const;
    void set_reg(unsigned index, std::uint32_t value);
    void load_reg(unsigned index, std::uint32_t value);
    [[nodiscard]] Access translate(std::uint32_t address, std::size_t bytes, bool write, const char* what) const;
    [[nodiscard]] std::uint32_t fetch(std::uint32_t address) const;
    [[nodiscard]] std::uint32_t load(std::uint32_t address, std::size_t bytes, const char* what);
    void store(std::uint32_t address, std::size_t bytes, std::optional<std::uint32_t> value, const char* what);
    [[nodiscard]] std::uint32_t io_read(std::uint32_t physical, std::size_t bytes, const char* what);
    void io_write(std::uint32_t physical, std::size_t bytes, std::optional<std::uint32_t> value, const char* what);
    void mechacon_command(std::uint8_t command, const char* what);
    void cop0_write(unsigned index, std::uint32_t value);
    [[nodiscard]] std::uint32_t cop0_read(unsigned index) const;

    std::vector<std::uint8_t> rom_;
    std::vector<std::uint8_t> ram_;
    std::vector<std::uint8_t> ram_written_; // 1 per byte written since power-on
    std::array<std::uint32_t, 32> gpr_{};
    std::uint32_t gpr_defined_{1u};         // bit n: GPR n holds a known value ($zero always)
    std::array<std::string, 32> gpr_origin_; // why an unknown GPR is unknown
    std::optional<std::uint32_t> hi_, lo_;
    std::uint32_t pc_{};
    std::optional<std::uint32_t> branch_target_; // set by a jump/branch: taken after the delay slot
    bool in_delay_slot_{};
    std::optional<PendingLoad> pending_load_;    // load result, visible one instruction later
    std::optional<PendingLoad> write_;           // this instruction's ALU result
    std::optional<std::string> undefined_load_;  // this instruction loaded an unknown value: why
    std::array<std::optional<std::uint32_t>, 16> cop0_{};
    const ConsoleHardware* hardware_{};
    // Bus configuration as written; empty value: unknown (reset value never
    // established, or an unknown value written back).
    std::vector<std::pair<std::uint32_t, std::optional<std::uint32_t>>> latches_;
    // MECHACON S command interface: parameters written so far, the last
    // command and its pending result bytes.
    std::vector<std::uint8_t> s_params_;
    std::optional<std::uint8_t> s_command_;
    std::deque<std::uint8_t> s_results_;
    std::uint64_t cycle_{};
    std::optional<std::string> stop_;
    std::uint64_t stop_cycle_{};
    std::vector<Event> events_;
};
} // namespace ps2native
