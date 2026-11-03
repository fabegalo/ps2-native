#include "ps2native/control_flow.hpp"

#include <queue>
#include <stdexcept>
#include <utility>

namespace ps2native {
AotControlFlow analyze_control_flow(
    const ElfImage& image, std::span<const std::uint32_t> additional_function_entries) {
    AotControlFlow graph;
    std::set<std::uint32_t> expanded_as_normal;
    std::queue<std::pair<std::uint32_t, bool>> pending;

    auto seed = [&](std::uint32_t address, bool function) {
        if ((address & 3u) != 0)
            throw std::runtime_error("Unaligned AOT function entry address");
        (void)image.instruction_at(address); // Validate executable file-backed code.
        graph.leaders.insert(address);
        if (function) graph.function_entries.insert(address);
        pending.emplace(address, false);
    };

    seed(image.entry, true);
    for (const auto address : image.function_entries) seed(address, true);
    for (const auto address : additional_function_entries) seed(address, true);
    if (graph.function_entries.size() > 4096)
        throw std::runtime_error("Too many AOT entry hints/symbols");

    while (!pending.empty()) {
        const auto [pc, is_delay_slot] = pending.front();
        pending.pop();
        if (!is_delay_slot && !expanded_as_normal.insert(pc).second) continue;
        if (graph.instructions.size() >= 131072)
            throw std::runtime_error("M1 AOT instruction discovery limit reached");
        const auto decoded = decode_mips(pc, image.instruction_at(pc));
        if (is_delay_slot && decoded.has_delay_slot())
            throw std::runtime_error("Control transfer in branch delay slot is unsupported by M1");
        graph.instructions.emplace(pc, decoded);
        if (is_delay_slot) continue;
        if (!decoded.has_delay_slot()) {
            pending.emplace(pc + 4u, false);
            continue;
        }
        // Delay slot is a dispatchable entry to preserve exact budget, fault
        // positions and resume behavior, even after indirect transfers.
        graph.leaders.insert(pc + 4u);
        pending.emplace(pc + 4u, true);
        switch (decoded.opcode) {
            case MipsOpcode::Beq:
            case MipsOpcode::Bne:
                seed(mips_branch_target(pc, decoded.word), false);
                seed(pc + 8u, false);
                break;
            case MipsOpcode::Jal:
                seed(pc + 8u, false); // Caller continuation after return.
                [[fallthrough]];
            case MipsOpcode::J:
                seed(mips_jump_target(pc, decoded.word), false);
                break;
            case MipsOpcode::Jalr:
                seed(pc + 8u, false); // Target resolved only at runtime.
                break;
            case MipsOpcode::Jr:
                break; // May return to a compiled continuation or exit to host.
            default:
                break;
        }
    }
    return graph;
}
} // namespace ps2native
