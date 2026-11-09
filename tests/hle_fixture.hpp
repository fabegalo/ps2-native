#pragma once
#include "direct_program.hpp"

namespace hle_test {
inline constexpr auto text=direct_test::text,data=direct_test::data;
inline direct_test::Program program() {
    m20::Assembler a(text);std::vector<std::uint32_t> roots;
    const auto call=[&](unsigned number){a.addiu(3,0,number);a.raw(12);roots.push_back(a.here());};
    a.la(4,data);call(0x40);a.addu(16,2,0);
    a.la(4,data+0x20);call(0x50);a.addu(17,2,0);
    a.la(4,data+0x40);call(0x20);a.addu(18,2,0);
    a.addiu(4,0,0);a.addiu(5,0,64);call(0x29);
    a.addu(4,18,0);a.addu(5,16,0);call(0x22);
    a.addu(4,16,0);a.la(5,data+0x80);call(0x47);
    a.addu(4,16,0);call(0x42);
    a.addu(4,17,0);a.addiu(5,0,4);call(0x52);
    a.addu(4,16,0);call(0x45);a.la(8,data);a.sw(2,0x10C,8);
    a.addu(4,17,0);a.addiu(5,0,4);a.addiu(6,0,0x11);a.la(7,data+0x110);call(0x57);
    a.la(8,data);a.sw(2,0x118,8);
    a.addu(4,18,0);a.la(5,data+0x140);call(0x30);
    call(0x7F);a.la(8,data);a.sw(2,0x114,8);
    const auto stop=a.here();a.brk();
    while(a.here()<text+0x400)a.nop();roots.push_back(a.here());
    call(0x44);a.la(8,data);a.sw(2,0x100,8);
    a.addiu(4,0,1);a.addiu(5,0,4);a.addiu(6,0,0x11);a.la(7,data+0x104);call(0x56);
    a.la(8,data);a.sw(2,0x108,8);call(0x23);a.nop();
    while(a.here()<text+0x600)a.nop();roots.push_back(a.here());
    a.addiu(16,0,0x1234);call(0x7F);a.addiu(2,0,0x1234);a.jr(31);a.nop();
    while(a.here()<text+0x700)a.nop();roots.push_back(a.here());
    a.addu(4,5,0);call(0x42);a.jr(31);a.nop();
    while(a.here()<text+0x800)a.nop();roots.push_back(a.here());
    a.label("alarm_idle");a.nop();a.beq(0,0,"alarm_idle");a.nop();
    while(a.here()<text+0x900)a.nop();roots.push_back(a.here());
    a.addu(4,6,0);call(0x42);a.jr(31);a.nop();
    std::vector<std::uint8_t> values(0x3000);
    direct_test::put(values,4,1); // semaphore maximum, initial count zero
    direct_test::put(values,0x20,2); // multi-wait event flag
    direct_test::put(values,0x44,text+0x400);
    direct_test::put(values,0x48,data+0x1000);direct_test::put(values,0x4C,0x1000);
    direct_test::put(values,0x54,32);direct_test::put(values,0x58,32);
    direct_test::put(values,0x110,0xDEADBEEF);
    return {{{text,5,m20::le_words(a.finish())},{data,6,std::move(values)}},stop,{},std::move(roots)};
}
}
