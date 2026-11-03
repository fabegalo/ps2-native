#include "ps2native/ee.hpp"

#include <cstddef>
#include <stdexcept>

namespace ps2native {
namespace {
void check_index(std::size_t index, std::size_t count) {
    if (index >= count) throw std::out_of_range("Invalid R5900 GPR index");
}
} // namespace
std::uint32_t EEState::reg32(unsigned index) const {
    check_index(index, gpr.size());
    return index == 0 ? 0u : static_cast<std::uint32_t>(gpr[index].low);
}
std::uint64_t EEState::reg64(unsigned index) const {
    check_index(index, gpr.size());
    return index == 0 ? 0u : gpr[index].low;
}
Register128 EEState::reg128(unsigned index) const {
    check_index(index, gpr.size());
    return index == 0 ? Register128{} : gpr[index];
}
void EEState::set_reg32(unsigned index, std::uint32_t value) {
    check_index(index, gpr.size());
    if (index != 0) gpr[index].low = value;
}
void EEState::set_reg32_sign_extended(unsigned index, std::uint32_t value) {
    check_index(index, gpr.size());
    if (index != 0) {
        const auto signed_value = value < 0x80000000u
            ? static_cast<std::int64_t>(value)
            : static_cast<std::int64_t>(value) - 0x100000000ll;
        gpr[index].low = static_cast<std::uint64_t>(signed_value);
    }
}
void EEState::set_reg64(unsigned index, std::uint64_t value) {
    check_index(index, gpr.size());
    if (index != 0) gpr[index].low = value;
}
void EEState::set_reg128(unsigned index, std::uint64_t low, std::uint64_t high) {
    check_index(index, gpr.size());
    if (index != 0) gpr[index] = {low, high};
}
EEState ee_reset_state() {
    EEState state;
    state.pc = 0xBFC00000u;
    state.set_cp0(Cop0::Status, kStatusBEV | kStatusERL);
    state.set_cp0(Cop0::PRid, 0x00002E20u);
    state.set_cp0(Cop0::Config, 0x00000440u);
    state.set_cp0(Cop0::Random, 47u);
    return state;
}
}  // namespace ps2native
