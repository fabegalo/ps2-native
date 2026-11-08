#pragma once
#include "ps2native/control_flow.hpp"
#include <string>
#include <cstdint>
#include <vector>
    // RUNTIME_HELPERS_BEGIN
#include <iosfwd>
    // RUNTIME_HELPERS_END
namespace ps2native {
// Optional known code entry points for stripped ELF or verified function pointers.
// Never guess arbitrary data words as executable code.
struct AotOptions {
    std::vector<std::uint32_t> additional_function_entries;
    std::vector<AotJumpTable> jump_tables;
    // Hybrid translation (firmware or Direct ELF): discovery stops at instructions the
    // AOT does not translate and at targets outside the static image; those
    // addresses are listed as unsupported boundaries instead of failing.
    bool unsupported_boundaries{};
    // Provenance exported by the generated module ("elf", "bios-rom0") and the
    // SHA-256 of the input; computed from the ELF bytes when left empty.
    std::string source_kind{"elf"};
    std::string source_sha256;
};

// Emits compiled native basic-block functions and a dispatch table.
// The runtime fails closed for unknown dynamic targets and modified code.
    // RUNTIME_HELPERS_BEGIN
// Shared offline emitter for the SDK and existing native preparation backend.
void emit_aot_instruction(std::ostream&, const MipsInstruction&);
    // RUNTIME_HELPERS_END
[[nodiscard]] std::string generate_aot_cpp(const ElfImage& image,
                                           const AotOptions& options = {});
} // namespace ps2native
