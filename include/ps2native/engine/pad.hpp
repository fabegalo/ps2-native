// DS2 wire protocol adapted from ps2-native patch 0062 (PCSX2 behavior, GPL-3.0-or-later).
#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <span>
namespace ps2native::engine {
struct Pad {
    bool connected{},config{};
    std::uint8_t mode=0x41;
    std::uint16_t buttons=0xffff,analog_pressure_mask{};
    std::array<std::uint8_t,4> sticks{128,128,128,128};
    std::array<std::uint8_t,12> pressure{};
    std::array<std::uint8_t,6> motors{255,255,255,255,255,255};
    std::array<std::uint8_t,2> rumble{};
    void disconnect(){*this={};}
    void exchange(std::span<const std::uint8_t> input,std::vector<std::uint8_t>& reply);
};
}
