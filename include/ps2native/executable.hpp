#pragma once
#include "ps2native/aot_runtime.hpp"
#include "ps2native/memory.hpp"
#include <string_view>

namespace ps2native {
// Offline regeneration boundary for independently verified dynamic EE code.
// Exports exactly one explicit region to a deterministic ELF; never guesses
// executable bytes from data. Only Direct ELF mode (no firmware profile mixing).
[[nodiscard]] ElfImage snapshot_ee_code(const Memory& memory, std::uint32_t address,
                                       std::uint32_t bytes, std::string_view expected_code_sha256);

class VerifiedAotOverlay {
public:
    // The AOT must match this exact standalone ELF, and every byte must already
    // be present in this runtime's RAM. Per-instruction AOT guards still detect
    // later writes; no runtime code generation or JIT is involved.
    static VerifiedAotOverlay verify(const ElfImage& image, const AotModule& module, const Memory& memory);
    [[nodiscard]] const AotModule& module_for(const Memory& memory) const;
private:
    VerifiedAotOverlay(const AotModule& module, const Memory& memory) : module_(&module), memory_(&memory) {}
    const AotModule* module_;
    const Memory* memory_;
};
} // namespace ps2native
