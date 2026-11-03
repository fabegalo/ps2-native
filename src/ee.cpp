#include "ps2native/ee.hpp"

#include <stdexcept>

namespace ps2native {
std::uint32_t EEState::reg32(unsigned index) const {
    if (index >= gpr.size()) throw std::out_of_range("Invalid R5900 GPR index");
    return index == 0 ? 0u : static_cast<std::uint32_t>(gpr[index].low);
}
void EEState::set_reg32(unsigned index, std::uint32_t value) {
    if (index >= gpr.size()) throw std::out_of_range("Invalid R5900 GPR index");
    if (index != 0) gpr[index].low = value;
}
void EEState::set_reg32_sign_extended(unsigned index, std::uint32_t value) {
    if (index >= gpr.size()) throw std::out_of_range("Invalid R5900 GPR index");
    if (index != 0) {
        const auto signed_value = value < 0x80000000u
            ? static_cast<std::int64_t>(value)
            : static_cast<std::int64_t>(value) - 0x100000000ll;
        gpr[index].low = static_cast<std::uint64_t>(signed_value);
    }
}
}  // namespace ps2native
