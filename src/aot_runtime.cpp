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

const AotBlock* AotDispatchCache::find(const AotModule& module, std::uint32_t pc) noexcept {
    auto& entry=entries_[((pc>>2u)^(pc>>12u)) & (entries_.size()-1u)];
    if (entry.module!=&module || entry.pc!=pc) {
        entry={&module,pc,find_aot_block(module,pc)};
    }
    return entry.block;
}

bool is_verified_indirect_entry(const AotModule& module, std::uint32_t pc) noexcept {
    return std::binary_search(module.indirect_entries,
                              module.indirect_entries + module.indirect_entry_count, pc);
}

bool aot_enter(ExecutionContext& ctx, std::uint32_t pc, std::uint32_t word, bool has_delay_slot) {
    return aot_enter_checked(ctx,pc,word,has_delay_slot);
}

void aot_code_modified(std::uint32_t pc) {
    throw std::runtime_error("AOT executable code modified at " + hex32(pc));
}

void aot_delay_transfer_refused() {
    throw std::runtime_error("Control transfer in an active delay slot is unsupported");
}

std::uint64_t run_aot_module(const AotModule& module, EEState& state, Memory& memory,
                             std::uint32_t stop_pc, std::uint64_t max_instructions) {
    std::uint64_t executed = 0;
    ExecutionContext ctx{state, memory, executed, max_instructions, stop_pc};
    AotDispatchCache dispatch;
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
        const AotBlock* block;
        {
            profiling::Scope lookup_profile(profiling::Phase::Dispatch);
            block=dispatch.find(module,state.pc);
        }
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
