#include "ps2native/vif.hpp"
#include <string>

namespace ps2native {
void Vif1::write_fifo(std::uint64_t low, std::uint64_t high, Gif& gif, GsRegisters& gs) {
    if (direct_left_) {
        gif.write_path2(low, high, gs);
        --direct_left_;
        return;
    }
    const std::array<std::uint32_t,4> words{static_cast<std::uint32_t>(low), static_cast<std::uint32_t>(low >> 32),
        static_cast<std::uint32_t>(high), static_cast<std::uint32_t>(high >> 32)};
    for (unsigned i = 0; i < 4u; ++i) {
        const auto command = words[i];
        if (!command) continue;
        if ((command & 0xFFFF0000u) != 0x50000000u)
            throw DeviceRefusal("VIF1 command unsupported: " + std::to_string(command >> 24));
        // Bits after DIRECT up to alignment are padding, not VIFcodes.
        direct_left_ = command & 0xFFFFu;
        if (!direct_left_) direct_left_ = 65536u;
        break;
    }
}
} // namespace ps2native
