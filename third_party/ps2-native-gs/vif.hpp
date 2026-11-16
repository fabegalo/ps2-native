#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <stdexcept>

namespace ps2::state {
class Stream;
}

namespace ps2::vif {
struct Unit {
    std::uint32_t stat = 0, err = 0, mark = 0, cycle = 0, mode = 0, num = 0, mask = 0, code = 0;
    std::uint32_t itops = 0, base = 0, ofst = 0, tops = 0, itop = 0, top = 0, vu_addr = 0;
    std::uint32_t row[4] = {}, col[4] = {};
    bool dbf = false, stalled = false, vu_pending = false;
    std::vector<std::uint32_t> pending;
};
std::array<Unit,2>& unit_storage();
std::uint8_t* vu_memory(int);
std::uint8_t* vu_code(int);

void state(ps2::state::Stream &s);

void reset();

// The ITOP VU0 sees (XITOP): the one VIF0 latched at its last MSCAL. VU0 has its own, apart from VU1's TOP and ITOP.
std::uint32_t itop(int unit);
void stats();
// VIF1 as VU1's thread sees it, for the report of a packet that does not end (vu_micro.cpp).
void describe_vu1();
void transfer(int unit, const std::uint8_t *data, std::uint32_t qwc);
void transfer_words(int unit, const std::uint32_t *words, std::uint32_t count);
bool stalled(int unit);
std::uint32_t read(int unit, std::uint32_t reg);
void write(int unit, std::uint32_t reg, std::uint32_t value);
void flush(int unit);

}
