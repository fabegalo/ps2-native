#pragma once
#include "ps2native/elf.hpp"
#include <string>
namespace ps2native {
// Emits host-compilable C++ only for reachable, supported M0 instructions.
// Throws for unsupported opcodes / unresolved direct control flow.
[[nodiscard]] std::string generate_aot_cpp(const ElfImage& image);
} // namespace ps2native
