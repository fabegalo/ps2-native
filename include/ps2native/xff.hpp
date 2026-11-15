#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ps2native {
// A relocatable "xff2" module that a game's own loader linked into EE RAM (Shadow of the
// Colossus loads its code this way). Read from RAM after the loader finished, so every
// address is final; nothing here relocates.
struct LoadedXffModule {
    struct Section { std::uint32_t address = 0, size = 0; };
    // Instruction words the game's linker rewrites later: imports, bound to whichever module
    // defines the symbol at that time. Type is the MIPS relocation (4 = 26, 5 = HI16, 6 = LO16).
    struct LiveSite { std::uint32_t address = 0; std::uint8_t type = 0; };
    std::uint32_t header = 0, entry = 0;
    std::vector<Section> code;              // allocated executable PROGBITS sections
    std::vector<std::uint32_t> functions;   // defined STT_FUNC symbols inside code
    std::vector<std::uint32_t> imports;     // absolute STT_FUNC symbols: functions of the program it links against
    std::vector<LiveSite> live;             // inside code, sorted by address
};

// The module whose loader-written entry address is `entry`, or nullopt when no xff2 header in
// `ram` names it. Throws when such a header exists but its tables are inconsistent.
[[nodiscard]] std::optional<LoadedXffModule> find_loaded_xff(std::span<const std::uint8_t> ram, std::uint32_t entry);
} // namespace ps2native
