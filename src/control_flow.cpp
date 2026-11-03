#include "ps2native/control_flow.hpp"

#include <queue>
#include <sstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ps2native {
AotControlFlow analyze_control_flow(
    const ElfImage& image, std::span<const std::uint32_t> additional_function_entries,
    std::span<const AotJumpTable> additional_jump_tables) {
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
    // Only ELF R_MIPS_32 relocations to executable symbols are inferred.
    // The parser validates source section, target section, and actual linked word.
    for (const auto& rejected : image.rejected_code_pointers) {
        graph.rejected_indirect_targets.push_back(
            {rejected.source, rejected.value, rejected.reason});
    }
    for (const auto& target : image.relocated_code_pointers) {
        seed(target.target, true);
        graph.resolved_indirect_targets.push_back(
            {target.source, target.target, "ELF R_MIPS_32"});
    }
    // For stripped ELFs, a caller may identify the exact table address/count.
    // Never scan unrelated data; an invalid table is rejected in full.
    if (additional_jump_tables.size() > 256u)
        throw std::runtime_error("Too many explicitly declared jump tables");
    std::size_t pointer_count = 0;
    for (const auto& table : additional_jump_tables) {
        if ((table.address & 3u) != 0u || table.entries == 0u ||
            table.entries > 4096u || pointer_count + table.entries > 4096u)
            throw std::runtime_error("Invalid jump table alignment/count/limit");
        pointer_count += table.entries;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> verified;
        for (std::uint32_t index = 0; index < table.entries; ++index) {
            const std::uint64_t position = static_cast<std::uint64_t>(table.address) +
                                           static_cast<std::uint64_t>(index) * 4u;
            if (position > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("Jump table address overflow");
            const auto source = static_cast<std::uint32_t>(position);
            std::uint32_t target{};
            try {
                target = image.data_word_at(source);
                (void)image.instruction_at(target);
            } catch (const std::exception& error) {
                std::ostringstream message;
                message << "Rejected explicit jump table entry " << index
                        << " at 0x" << std::hex << std::uppercase << source
                        << ": " << error.what();
                throw std::runtime_error(message.str());
            }
            verified.emplace_back(source, target);
        }
        // Commit only when every entry is backed by valid executable data.
        for (const auto [source, target] : verified) {
            seed(target, true);
            graph.resolved_indirect_targets.push_back(
                {source, target, "explicit jump table"});
        }
    }
    if (graph.function_entries.size() > 4096)
        throw std::runtime_error("Too many AOT entry hints/symbols/tables");


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
