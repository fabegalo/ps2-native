#include "ps2native/interpreter.hpp"
#include "ps2native/ee_system.hpp"
namespace ps2native {
static void interpret_instruction_impl(ExecutionContext& ctx,SystemEventObserver* events,InterpreterDecodeCache* cache) {
    auto& state=ctx.state;auto& memory=ctx.memory;
    state.instruction_pc=state.pc;state.instruction_in_delay_slot=state.delay_pending;
    if(!ctx.interrupts_sampled)(void)poll_ee_interrupts(state,memory);
    ctx.interrupts_sampled=false;
    memory.set_cpu_context(state.cp0(Cop0::Status),state.cp0(Cop0::EntryHi));memory.set_cache_config(state.cp0(Cop0::Config));
    const auto pc=state.pc,word=memory.fetch32(pc);
    const auto instruction=cache ? cache->decode(pc,word) : decode_r5900(pc,word);
    if(instruction.has_delay_slot() && state.delay_pending)throw std::runtime_error("Control transfer in an active delay slot is unsupported");
    switch(instruction.opcode) {
#define PS2N_EXECUTE(name) case MipsOpcode::name: { \
    if(ctx.observer)ctx.observer->on_instruction(pc,word,ExecutionEngine::Interpreter); \
    const auto frame=ee::begin(state,pc);ee::exec<MipsOpcode::name>(state,memory,pc,word);(void)ee::retire(ctx,frame);break; }
        PS2N_R5900_AOT_OPCODES(PS2N_EXECUTE)
#undef PS2N_EXECUTE
#define PS2N_EXECUTE_SYSTEM(name) case MipsOpcode::name:execute_ee_system<MipsOpcode::name,-1,true>(ctx,word,events);break;
        PS2N_R5900_SYSTEM_OPCODES(PS2N_EXECUTE_SYSTEM)
#undef PS2N_EXECUTE_SYSTEM
    }
}
void interpret_instruction(ExecutionContext& ctx,SystemEventObserver* events) {interpret_instruction_impl(ctx,events,nullptr);}
void interpret_instruction(ExecutionContext& ctx,SystemEventObserver* events,InterpreterDecodeCache& cache) {interpret_instruction_impl(ctx,events,&cache);}
} // namespace ps2native
