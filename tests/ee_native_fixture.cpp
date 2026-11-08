#include "ee_native_fixture.hpp"
#include "ps2native/ee_system.hpp"
#include "ps2native/iop.hpp"
#include <algorithm>
#include <sstream>

using namespace ps2native;
namespace {
constexpr bool system_operation(MipsOpcode op) {
    switch(op) {
#define SYSTEM_CASE(name) case MipsOpcode::name:return true;
        PS2N_R5900_SYSTEM_OPCODES(SYSTEM_CASE)
#undef SYSTEM_CASE
        default:return false;
    }
}
template<std::uint32_t Word,MipsOpcode Op>
void execute(ExecutionContext& ctx,SystemEventObserver* events) {
    if(ctx.observer)ctx.observer->on_instruction(ctx.state.pc,Word,ExecutionEngine::Aot);
    if constexpr(system_operation(Op))execute_ee_system<Op,Word>(ctx,Word,events);
    else {
        const auto pc=ctx.state.pc;
        const auto frame=ee::begin(ctx.state,pc);
        ee::exec<Op>(ctx.state,ctx.memory,pc,Word);
        (void)ee::retire(ctx,frame);
    }
}
struct Entry {std::uint32_t word;void (*execute)(ExecutionContext&,SystemEventObserver*);};
const Entry entries[]{
#define EE_FIXTURE(word,op) {word,execute<word,MipsOpcode::op>},
#include "ee_native_words.inc"
#undef EE_FIXTURE
};
}
void compiled_fixture_step(ExecutionContext& ctx,SystemEventObserver* events) {
    auto& state=ctx.state;auto& memory=ctx.memory;
    state.instruction_pc=state.pc;state.instruction_in_delay_slot=state.delay_pending;
    if(!ctx.interrupts_sampled)(void)poll_ee_interrupts(state,memory);
    ctx.interrupts_sampled=false;
    memory.set_cpu_context(state.cp0(Cop0::Status),state.cp0(Cop0::EntryHi));
    memory.set_cache_config(state.cp0(Cop0::Config));
    const auto word=memory.fetch32(state.pc);
    const auto* found=std::lower_bound(std::begin(entries),std::end(entries),word,
        [](const auto& entry,std::uint32_t value){return entry.word<value;});
    if(found==std::end(entries) || found->word!=word) {
        std::ostringstream message;message<<"missing synthetic EE AOT word 0x"<<std::hex<<word;
        throw std::runtime_error(message.str());
    }
    found->execute(ctx,events);
}
void reset_native_ee_fixture(Memory& memory,std::span<const std::uint8_t> rom) {
    memory.reset_for_boot(rom);
    seed_native_test_iop(memory);
}
void seed_native_test_iop(Memory& memory) {
    // EE unit tests use a genuine compiled IOP idle loop, not an unconfigured
    // BIOS bootstrap whose PRId read requires a separate console profile.
    constexpr std::array<std::uint8_t,8> idle{0xFF,0xFF,0x00,0x10,0,0,0,0};
    const_cast<Iop&>(memory.iop()).load_program(idle,0,0);
}
