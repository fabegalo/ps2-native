#pragma once
#include <array>
#include <cstdint>
namespace ps2native {
// Full 128-bit GPR *storage*; only a subset of lower-word semantics is implemented in M0.
struct Register128 {
    std::uint64_t low{};
    std::uint64_t high{};
};
struct EEState {
    std::array<Register128, 32> gpr{};
    std::uint64_t lo{};
    std::uint64_t hi{};
    std::uint32_t pc{};
    bool delay_pending{};
    std::uint32_t delay_target{};
    std::uint64_t retired_instructions{};
    [[nodiscard]] std::uint32_t reg32(unsigned index) const;
    void set_reg32(unsigned index, std::uint32_t value);
    void set_reg32_sign_extended(unsigned index, std::uint32_t value);
};
} // namespace ps2native
