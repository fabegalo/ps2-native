#pragma once
#include <cstdint>
namespace ps2::vu {void run(int unit,std::uint32_t address);void wait(int unit);std::uint32_t tpc(int);void set_tops(std::uint32_t,std::uint32_t);void code_changed(int);}
