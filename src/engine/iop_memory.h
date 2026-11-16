#pragma once
#include <cstdint>
namespace ps2native::engine { class Console; }
namespace ps2x::iop::detail {
class IopMemory {
public:
    explicit IopMemory(ps2native::engine::Console& console) : console_(console) {}
    std::uint8_t read8(std::uint32_t address);
    std::uint16_t read16(std::uint32_t address);
    std::uint32_t read32(std::uint32_t address);
    void write8(std::uint32_t address,std::uint8_t value);
    void write16(std::uint32_t address,std::uint16_t value);
    void write32(std::uint32_t address,std::uint32_t value);
private:
    ps2native::engine::Console& console_;
};
}
