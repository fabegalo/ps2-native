// Test inputs adapted from ps2autotests tests/cpu/ee/{alu.cpp,shared.h}.
// Copyright (c) 2014, ps2autotests project contributors. ISC: ps2autotests/LICENSE.md.
#pragma once
#include "direct_program.hpp"
#include "boot_checks.hpp"
#include <sstream>

namespace autotest {
using namespace ps2native;
struct Case { std::string label; std::uint32_t function; Register128 left,right,expected; };
inline std::vector<Case> cases(const std::filesystem::path& expected) {
    const std::map<std::string,std::uint32_t> ops{{"addu",0x21},{"and",0x24},{"daddu",0x2d},
        {"dsubu",0x2f},{"nor",0x27},{"or",0x25},{"slt",0x2a},{"sltu",0x2b},{"subu",0x23},{"xor",0x26}};
    const std::map<std::string,Register128> values{
        {"C_ZERO",{0,0}},{"C_ONE",{1,0}},{"C_NEGONE",{~0ull,~0ull}},
        {"C_S16_MAX",{0x7fff,0}},{"C_S16_MIN",{0xffffffffffff8000ull,~0ull}},
        {"C_S32_MAX",{0x7fffffff,0}},{"C_S32_MIN",{0xffffffff80000000ull,~0ull}},
        {"C_S64_MAX",{0x7fffffffffffffffull,0}},{"C_S64_MIN",{0x8000000000000000ull,~0ull}},
        {"C_GARBAGE1",{0x0000133800001337ull,0x0000133a00001339ull}},
        {"C_GARBAGE2",{0xdeadbeeedeadbeefull,0xdeadbeecdeadbeedull}}};
    const auto bytes=read_file(expected);
    std::istringstream input(std::string(bytes.begin(),bytes.end()));std::string line;
    std::vector<Case> result;
    while(std::getline(input,line)) {
        if(!line.starts_with("  "))continue;
        std::istringstream row(line);std::string op,left,right;row>>op>>left>>right;
        if(!ops.contains(op) || !left.starts_with("C_"))continue;
        boot_checks::require(left.ends_with(',') && right.ends_with(':'),"upstream ALU operands");
        left.pop_back();right.pop_back();
        std::uint32_t a,b,c,d;row>>std::hex>>a>>b>>c>>d;
        boot_checks::require(bool(row),"upstream ALU expected result");
        result.push_back({line.substr(2,line.find(':')-2),ops.at(op),values.at(left),values.at(right),
            {std::uint64_t{c}<<32|d,std::uint64_t{a}<<32|b}});
    }
    boot_checks::require(result.size()==120,"all 120 selected upstream ALU rows are present");
    return result;
}
inline constexpr std::uint32_t outputs=direct_test::data+0x2000;
inline direct_test::Program program(const std::vector<Case>& cases) {
    using namespace m20;
    Assembler a(direct_test::text);a.la(8,direct_test::data);a.la(9,outputs);
    std::vector<std::uint8_t> data(cases.size()*48);
    for(std::size_t i=0;i<cases.size();++i) {
        boot_checks::require(cases[i].function==cases[i-i%12].function,"twelve upstream rows per operation");
        const Register128 values[]{cases[i].left,cases[i].right,{0x0000133800001337ull,0x0000133a00001339ull}};
        for(unsigned j=0;j<3;++j){direct_test::put(data,i*48+j*16,values[j].low,8);direct_test::put(data,i*48+j*16+8,values[j].high,8);}
        if(i%12==0) {
            const auto loop="op"+std::to_string(i);
            a.addiu(10,zero,12);a.label(loop);
            a.lq(2,0,8);a.lq(3,16,8);a.lq(4,32,8);
            a.raw(2u<<21|3u<<16|4u<<11|cases[i].function);a.sq(4,0,9);
            a.addiu(8,8,48);a.addiu(9,9,16);a.addiu(10,10,-1);a.bne(10,zero,loop);a.nop();
        }
    }
    const auto stop=a.here();a.brk();
    return {{{direct_test::text,5,le_words(a.finish())},{direct_test::data,6,std::move(data),0x3000}},stop,{},{}};
}
} // namespace autotest
