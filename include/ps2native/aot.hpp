#pragma once
#include "ps2native/control_flow.hpp"
#include <string>
#include <cstdint>
#include <vector>
namespace ps2native {
// Optional known code entry points for stripped ELF or verified function pointers.
// Never guess arbitrary data words as executable code.
struct AotOptions {
    std::vector<std::uint32_t> additional_function_entries;
    std::vector<AotJumpTable> jump_tables;
};

// Emits compiled native basic-block functions and a dispatch table.
// The runtime fails closed for unknown dynamic targets and modified code.
[[nodiscard]] std::string generate_aot_cpp(const ElfImage& image,
                                           const AotOptions& options = {});
} // namespace ps2native
