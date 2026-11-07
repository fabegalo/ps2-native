#pragma once
#include <cstdint>
#include <vector>
namespace iop_aot_fixture {
inline std::vector<std::uint8_t> image() {
    std::vector<std::uint8_t> bytes(0x1100);
    const auto put=[&](unsigned at,std::uint32_t word){for(unsigned b=0;b<4;++b)bytes[at+b]=static_cast<std::uint8_t>(word>>(b*8u));};
    const std::uint32_t code[]{0x34011080,0x24020005,0xAC220000,0x8C230000,0x24640001,
        0x00622821,0x10A50002,0x24060007,0x24060063,0x0000000C,0x24480003,0x1000FFFF,0};
    for(unsigned n=0;n<std::size(code);++n)put(0x1000+4*n,code[n]);
    const std::uint32_t handler[]{0x40077000,0,0x24E70004,0x00E00008,0x42000010};
    for(unsigned n=0;n<std::size(handler);++n)put(0x80+4*n,handler[n]);
    put(0x200,0x25290001u);put(0x204,0x03E00008u);put(0x208,0);
    return bytes;
}
}
