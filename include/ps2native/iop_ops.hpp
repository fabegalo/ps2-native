#pragma once
// IOP instructions are emitted offline; the SDK owns their native semantics.
#include "ps2native/iop.hpp"
#include <native/iop_cpu.h>
#include <iomanip>
#include <sstream>
namespace ps2native {
inline std::string iop_instruction_hex(std::uint32_t value) {
    std::ostringstream out;out<<"0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<value;return out.str();
}
template<std::uint32_t Word> void Iop::execute_native() {
    if(fetch(pc_)!=Word)throw IopExecutionStop{"IOP AOT instruction identity mismatch at "+iop_instruction_hex(pc_)};
    ps2x::iop::native::Cpu::execute<Word>(*this);
}
} // namespace ps2native
