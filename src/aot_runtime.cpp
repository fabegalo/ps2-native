#include "ps2native/aot_runtime.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ps2native {
namespace {
std::string hex32(std::uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return stream.str();
}
} // namespace

const AotBlock* find_aot_block(const AotModule& module, std::uint32_t pc) noexcept {
    const auto* end = module.blocks + module.block_count;
    const auto* it = std::lower_bound(module.blocks, end, pc,
        [](const AotBlock& block, std::uint32_t address) { return block.address < address; });
    return it != end && it->address == pc ? it : nullptr;
}

bool is_verified_indirect_entry(const AotModule& module, std::uint32_t pc) noexcept {
    return std::binary_search(module.indirect_entries,
                              module.indirect_entries + module.indirect_entry_count, pc);
}

bool aot_enter(ExecutionContext& ctx, std::uint32_t pc, std::uint32_t word, bool has_delay_slot) {
    auto& state = ctx.state;
    if (state.pc != pc) return false;
    if (ctx.executed >= ctx.budget || (!state.delay_pending && state.pc == ctx.stop_pc)) return false;
    state.instruction_pc=pc;
    state.instruction_in_delay_slot=state.delay_pending;
    const auto sampled=ctx.interrupts_sampled;ctx.interrupts_sampled=false;
    if (!sampled && poll_ee_interrupts(state,ctx.memory)) return false;
    if (has_delay_slot && state.delay_pending)
        throw std::runtime_error("Control transfer in an active delay slot is unsupported");
    {
        profiling::Scope profile(profiling::Phase::CodeChecks);
        if (ctx.memory.fetch32(pc) != word)
            throw std::runtime_error("AOT executable code modified at " + hex32(pc));
    }
    if (ctx.observer != nullptr) ctx.observer->on_instruction(pc, word, ExecutionEngine::Aot);
    return true;
}

std::uint64_t run_aot_module(const AotModule& module, EEState& state, Memory& memory,
                             std::uint32_t stop_pc, std::uint64_t max_instructions) {
    std::uint64_t executed = 0;
    ExecutionContext ctx{state, memory, executed, max_instructions, stop_pc};
    while (true) {
        if (!state.delay_pending && state.pc == stop_pc) {
            state.indirect_entry_pending = false; // Host return address.
            return executed;
        }
        if (executed >= max_instructions)
            throw std::runtime_error("Guest instruction budget exhausted");
        (void)poll_ee_interrupts(state,memory);
        ctx.interrupts_sampled=true;
        memory.set_cpu_context(state.cp0(Cop0::Status), state.cp0(Cop0::EntryHi)); memory.set_cache_config(state.cp0(Cop0::Config));
        const auto* block = find_aot_block(module, state.pc);
        if (block == nullptr)
            throw std::runtime_error("AOT target not compiled (indirect jump/dynamic code) at " +
                                     hex32(state.pc));
        if (state.indirect_entry_pending) {
            if (!is_verified_indirect_entry(module, state.pc))
                throw std::runtime_error("AOT indirect transfer to " + hex32(state.pc) +
                                         " is not a verified entry (function root, call continuation"
                                         " or proven jump-table target)");
            state.indirect_entry_pending = false;
        }
        const auto before = executed;
        block->execute(ctx); // Host-compiled basic-block function, no interpreter.
        if (before == executed)
            throw std::runtime_error("AOT dispatcher did not make progress");
    }
}
} // namespace ps2native
