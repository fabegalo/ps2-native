#include "ps2native/ee.hpp"

#include <cstddef>
#include <stdexcept>

namespace ps2native {
bool poll_ee_timer_interrupt(EEState& state) {
    auto cause = state.cp0(Cop0::Cause);
    if (state.compare_written && state.count() == state.cp0(Cop0::Compare)) cause |= kStatusIM7;
    state.set_cp0(Cop0::Cause, cause);
    const auto status = state.cp0(Cop0::Status);
    if ((status & (kStatusEXL | kStatusERL)) || !(status & kStatusIE) || !(status & kStatusEIE) ||
        !(status & cause & kStatusIM7)) return false;
    state.set_cp0(Cop0::Epc, state.delay_pending ? state.pc - 4u : state.pc);
    state.set_cp0(Cop0::Cause, (cause & ~0x8000007Cu) | (state.delay_pending ? 0x80000000u : 0u));
    state.set_cp0(Cop0::Status, status | kStatusEXL);
    state.pc = (status & kStatusBEV) ? 0xBFC00400u : 0x80000200u;
    state.delay_pending = false;
    state.delay_indirect = false;
    state.indirect_entry_pending = false;
    return true;
}
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
