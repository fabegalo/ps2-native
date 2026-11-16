#pragma once
#include <cstdint>
namespace ps2::hw {inline constexpr int kEventGsFinish=1;std::uint32_t crt_mode();void intc_raise(unsigned cause);}
