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
struct EEState {
    std::array<Register128, 32> gpr{};
    // Lower 64 bits of LO/HI (pipeline 0). LO1/HI1 (MMI pipeline 1) are not modeled.
    std::uint64_t lo{};
    std::uint64_t hi{};
    std::uint32_t pc{};
    bool delay_pending{};
    std::uint32_t delay_target{};
    // The pending transfer came from JR/JALR: its target must be a verified entry.
    bool delay_indirect{};
    // pc was reached through JR/JALR and has not yet been checked by the dispatcher.
    bool indirect_entry_pending{};
    std::uint64_t retired_instructions{};
    [[nodiscard]] std::uint32_t reg32(unsigned index) const;
    [[nodiscard]] std::uint64_t reg64(unsigned index) const;
    [[nodiscard]] Register128 reg128(unsigned index) const;
    void set_reg32(unsigned index, std::uint32_t value);
    void set_reg32_sign_extended(unsigned index, std::uint32_t value);
    void set_reg64(unsigned index, std::uint64_t value);
    void set_reg128(unsigned index, std::uint64_t low, std::uint64_t high);
};
} // namespace ps2native
