#include "ps2native/ee_semantics.hpp"
#include "ps2native/memory.hpp"

#include <cstddef>
#include <algorithm>
#include <stdexcept>

namespace ps2native {
void ee::unknown_gpr_operand(unsigned index, unsigned width) {
    throw std::runtime_error("EE GPR " + std::to_string(index) + " consumes unknown MMIO bits (" +
                             std::to_string(width) + "-bit operand)");
}
void enter_ee_exception(EEState& state, std::uint32_t code, std::uint32_t pc, bool in_delay_slot) {
    if (code > 31u) throw std::invalid_argument("invalid EE exception code");
    const auto status = state.cp0(Cop0::Status);
    auto cause = (state.cp0(Cop0::Cause) & ~0x7Cu) | (code << 2u);
    if (!(status & kStatusEXL)) {
        state.set_cp0(Cop0::Epc, in_delay_slot ? pc - 4u : pc);
        cause = (cause & ~0x80000000u) | (in_delay_slot ? 0x80000000u : 0u);
    }
    state.set_cp0(Cop0::Cause, cause);
    state.set_cp0(Cop0::Status, status | kStatusEXL);
    state.pc = (status & kStatusBEV) ? 0xBFC00380u : 0x80000180u;
    state.delay_pending = false;
    state.delay_indirect = false;
    state.indirect_entry_pending = false;
}

namespace {
bool poll_interrupts(EEState& state, Memory* memory) {
    profiling::Scope profile(profiling::Phase::Interrupts);
    auto cause = state.cp0(Cop0::Cause);
    const auto status = state.cp0(Cop0::Status);
    if (memory != nullptr && (status & kStatusIM2)) {
        cause &= ~kStatusIM2;
        if (memory->intc_interrupt_pending()) cause |= kStatusIM2;
    }
    if(memory != nullptr && (status&kStatusIM3)) {
        cause&=~kStatusIM3;
        if(memory->dmac_interrupt_pending())cause|=kStatusIM3;
    }
    if (state.compare_written && state.count() == state.cp0(Cop0::Compare)) cause |= kStatusIM7;
    state.set_cp0(Cop0::Cause, cause);
    if ((status & (kStatusEXL | kStatusERL)) || !(status & kStatusIE) || !(status & kStatusEIE) ||
        !(status & cause & (kStatusIM7|kStatusIM2|kStatusIM3))) return false;
    state.set_cp0(Cop0::Epc, state.delay_pending ? state.pc - 4u : state.pc);
    state.set_cp0(Cop0::Cause, (cause & ~0x8000007Cu) | (state.delay_pending ? 0x80000000u : 0u));
    state.set_cp0(Cop0::Status, status | kStatusEXL);
    state.pc = (status & kStatusBEV) ? 0xBFC00400u : 0x80000200u;
    state.delay_pending = false;
    state.delay_indirect = false;
    state.indirect_entry_pending = false;
    return true;
}
} // namespace
bool poll_ee_timer_interrupt(EEState& state) { return poll_interrupts(state, nullptr); }
bool poll_ee_interrupts(EEState& state, Memory& memory) {
    return poll_interrupts(state, &memory);
}
namespace {
void check_index(std::size_t index, std::size_t count) {
    if (index >= count) throw std::out_of_range("Invalid R5900 GPR index");
}
} // namespace
std::uint32_t EEState::reg32(unsigned index) const {
    return ee::read_gpr32(*this,index);
}
std::uint64_t EEState::reg64(unsigned index) const {
    return ee::read_gpr64(*this,index);
}
Register128 EEState::reg128(unsigned index) const {
    return ee::read_gpr128(*this,index);
}
void EEState::set_reg32(unsigned index, std::uint32_t value) {
    ee::write_gpr32(*this,index,value);
}
void EEState::set_reg32_sign_extended(unsigned index, std::uint32_t value) {
    ee::write_gpr32_signed(*this,index,value);
}
void EEState::set_reg64(unsigned index, std::uint64_t value) {
    ee::write_gpr64(*this,index,value);
}
void EEState::set_reg64_observed(unsigned index, std::uint64_t value, std::uint64_t unknown) {
    ee::write_gpr64_observed(*this,index,value,unknown);
}
void EEState::set_reg128(unsigned index, std::uint64_t low, std::uint64_t high) {
    ee::write_gpr128(*this,index,low,high);
}
void EEState::forget_reg_candidates(unsigned index) noexcept {
    if (candidate_gprs & (1u << index)) {
        gpr_candidates[index].value=gpr_candidates[index].unknown=0;
        gpr_candidates[index].values.reset();
        candidate_gprs &= ~(1u << index);
    }
}
std::shared_ptr<const std::vector<std::uint64_t>> EEState::reg64_candidates(unsigned index) const {
    check_index(index,gpr.size());
    if(!index || !(candidate_gprs&(1u<<index)))return {};
    const auto& domain=gpr_candidates[index];
    if(domain.value!=gpr[index].low || domain.unknown!=unknown_gpr_bits[index])return {};
    return domain.values;
}
void EEState::set_reg64_candidates(unsigned index,std::vector<std::uint64_t> values) {
    check_index(index,gpr.size());
    if(values.empty() || values.size()>512u)throw std::invalid_argument("EE finite integer domain exceeds its work bound");
    std::sort(values.begin(),values.end());values.erase(std::unique(values.begin(),values.end()),values.end());
    std::uint64_t all=~0ull,any=0;
    for(const auto value:values){all&=value;any|=value;}
    set_reg64_observed(index,all,all^any);
    if(index && values.size()>1u) {
        auto& domain=gpr_candidates[index];domain.value=all;domain.unknown=all^any;
        domain.values=std::make_shared<const std::vector<std::uint64_t>>(std::move(values));
        candidate_gprs|=1u<<index;
    }
}
void EEState::set_reg128_observed(unsigned index, std::uint64_t low, std::uint64_t high,
                                  std::uint64_t unknown_low, std::uint64_t unknown_high) {
    ee::write_gpr128_observed(*this,index,low,high,unknown_low,unknown_high);
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
