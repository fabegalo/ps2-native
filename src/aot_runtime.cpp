#include "ps2native/aot_runtime.hpp"
#include "ps2native/hle.hpp"

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

bool poll_sdk_kernel(ExecutionContext& ctx) {
    const auto dispatched=ctx.memory.hle_kernel()->poll_execution(ctx);
    if(dispatched)++ctx.kernel_dispatches;
    return dispatched;
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
void flush_interrupt_cache(ExecutionContext& ctx) {
    auto& cache=ctx.interrupt_cache;
    const auto pending=cache.pending;
    cache.deadline=0;cache.pending=false;
    ctx.memory.set_idle_dma_window(nullptr,nullptr,0u);
    if(!pending)return;
    const auto lines=ctx.memory.settle_interrupt_sample(cache.sample_cycle,cache.status);
    if(lines!=(cache.cause&cache.status&(kStatusIM2|kStatusIM3)))
        throw std::logic_error("interrupt sampling proof crossed a device edge");
}
bool poll_ee_interrupts_cached_slow(ExecutionContext& ctx) {
    flush_interrupt_cache(ctx);
    const auto dispatched=poll_ee_interrupts(ctx.state,ctx.memory);
    if(dispatched && ctx.memory.hle_kernel())++ctx.kernel_dispatches;
    return dispatched;
}
void prime_interrupt_cache(ExecutionContext& ctx) {
    auto& cache=ctx.interrupt_cache;const auto& state=ctx.state;
    const auto now=state.cycles();
    cache.retry_after=now+16u;
    // The final DMAC sample materializes every skipped IOP ordering point.
    if(!ctx.observer || !(state.cp0(Cop0::Status)&kStatusIM3))return;
    const auto observed_until=ctx.observer->idle_deadline();
    if(observed_until<=now)return;
    std::optional<std::uint64_t> horizon;
    try {horizon=ctx.memory.idle_execution_horizon();}
    catch(const DeviceRefusal&){return;}
    if(!horizon)return;
    auto end=std::min(*horizon,observed_until);
    if(state.compare_written)end=std::min(end,now+static_cast<std::uint32_t>(state.cp0(Cop0::Compare)-state.count()));
    if(end<=now+1u)return;
    cache.deadline=end;cache.sample_cycle=now;cache.status=state.cp0(Cop0::Status);
    cache.cause=state.cp0(Cop0::Cause);cache.count=state.cp0(Cop0::Count);
    cache.count_anchor=state.count_anchor;cache.compare=state.cp0(Cop0::Compare);
    cache.compare_written=state.compare_written;cache.pending=false;
    ctx.memory.set_idle_dma_window(&state.retired_instructions,&state.stall_cycles,end);
}

bool aot_polling_loop(ExecutionContext& ctx,std::uint32_t start,std::span<const std::uint32_t> words) {
    flush_interrupt_cache(ctx);
    auto& state=ctx.state;
    if(!(state.cp0(Cop0::Status)&kStatusIM3))return false;
    if(state.pc!=start || state.delay_pending || ctx.executed>=ctx.budget || words.size()<3u || words.size()>16u ||
       ctx.budget-ctx.executed<words.size()*2u || ctx.stop_pc-start<words.size()*4u)return false;
    const auto hardwareDeadline=ctx.observer ? ctx.observer->idle_deadline() : UINT64_MAX;
    const auto deadline=ctx.memory.hle_kernel()?std::min(hardwareDeadline,ctx.memory.hle_kernel()->next_deadline()):hardwareDeadline;
    if(!deadline)return false;
    const auto load=words.front(),branch=words[words.size()-2u];
    const bool polling=load!=0u;
    const auto source=ee::rs(load),destination=ee::rt(load);
    if((polling && ((load>>26u)!=0x23u || !destination || source==destination)) ||
       ((branch>>26u)!=4u && (branch>>26u)!=5u) || ee::rs(branch)!=destination || ee::rt(branch)!=0u ||
       (!polling && (branch>>26u)!=4u) ||
       mips_branch_target(start+static_cast<std::uint32_t>(words.size()-2u)*4u,branch)!=start || words.back())return false;
    for(std::size_t i=1;i+2u<words.size();++i)if(words[i])return false;
    if(polling && (state.unknown_gprs&(1u<<source)) && (state.unknown_gpr_bits[source]&0xFFFFFFFFull))return false;
    const auto address=polling ? state.reg32(source)+ee::simm32(load) : 0u;
    const auto next_event=[&]{return polling ? ctx.memory.idle_ram_horizon(address) : ctx.memory.idle_execution_horizon();};
    std::optional<std::uint64_t> horizon;
    try {
        horizon=next_event();
        if(!horizon || *horizon<=state.cycles() || *horizon-state.cycles()<words.size()*2u)return false;
        for(std::size_t i=0;i<words.size();++i)
            if(ctx.memory.fetch32(start+static_cast<std::uint32_t>(i)*4u)!=words[i])return false;
    } catch(const BusError&) {return false;}
      catch(const DeviceRefusal&) {return false;}
    // Execute one real iteration first, establishing load/cache/provenance state.
    for(std::size_t i=0;i<words.size();++i) {
        const auto pc=start+static_cast<std::uint32_t>(i)*4u;
        const bool transfer=i+2u==words.size();
        if(!aot_enter_checked(ctx,pc,words[i],transfer))return true;
        const auto frame=ee::begin(state,pc);
        if(!i && polling)ee::exec<MipsOpcode::Lw>(state,ctx.memory,pc,words[i]);
        else if(transfer) {
            if((branch>>26u)==4u)ee::exec<MipsOpcode::Beq>(state,ctx.memory,pc,branch);
            else ee::exec<MipsOpcode::Bne>(state,ctx.memory,pc,branch);
        } else ee::exec<MipsOpcode::Nop>(state,ctx.memory,pc,words[i]);
        (void)ee::retire(ctx,frame);
    }
    if(state.pc!=start || state.delay_pending)return true;
    flush_interrupt_cache(ctx);
    horizon=next_event();
    if(!horizon)return true;
    auto end=std::min(*horizon,deadline);
    const auto now=state.cycles();
    if(state.compare_written)end=std::min(end,now+static_cast<std::uint32_t>(state.cp0(Cop0::Compare)-state.count()));
    if(end<=now)return true;
    const auto count=std::min<std::uint64_t>(ctx.budget-ctx.executed,end-now)/words.size()*words.size();
    if(!count)return true;
    state.retired_instructions+=count-1u;
    if(state.cp0(Cop0::Status)&kStatusIM2)ctx.memory.settle_idle_timers();
    if(poll_ee_interrupts(state,ctx.memory))throw std::logic_error("idle-loop event horizon crossed an interrupt");
    ++state.retired_instructions;ctx.executed+=count;
    if(ctx.observer)ctx.observer->on_idle_instructions(start,words,count);
    return true;
}

std::uint64_t run_aot_module(const AotModule& module, EEState& state, Memory& memory,
                             std::uint32_t stop_pc, std::uint64_t max_instructions) {
    std::uint64_t executed = 0;
    ExecutionContext ctx{state, memory, executed, max_instructions, stop_pc};
    AotDispatchCache dispatch;
    unsigned kernel_yields=0;
    while (true) {
        if (!state.delay_pending && state.pc == stop_pc) {
            state.indirect_entry_pending = false; // Host return address.
            return executed;
        }
        if (executed >= max_instructions)
            throw std::runtime_error("Guest instruction budget exhausted");
        if(state.pc==0 && memory.hle_kernel() && memory.hle_kernel()->complete_syscall_return())continue;
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
        const auto before_dispatch=ctx.kernel_dispatches;
        block->execute(ctx); // Host-compiled basic-block function, no interpreter.
        if (before == executed) {
            if(before_dispatch==ctx.kernel_dispatches || ++kernel_yields>64u)
                throw std::runtime_error("AOT dispatcher did not make progress");
        } else kernel_yields=0;
    }
}
} // namespace ps2native
