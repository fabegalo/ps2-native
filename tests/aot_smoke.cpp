#include "ps2native/aot_runtime.hpp"
#include "ps2native/core.hpp"
#include "ps2native/boot.hpp"
#include "ps2native/checkpoint.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
template<std::uint32_t Pc> void irq_nop(ps2native::ExecutionContext& ctx) {
    using namespace ps2native;
    if (!aot_enter(ctx,Pc,0,false)) return;
    const auto frame=ee::begin(ctx.state,Pc);
    ee::exec<MipsOpcode::Sll,0>(ctx.state,ctx.memory,Pc,0);
    (void)ee::retire(ctx,frame);
}
void check_boundaries(const ps2native::ElfImage& elf) {
    using namespace ps2native;
    constexpr std::uint32_t host_return=0xfffffff0u;
    // Include straight-line interiors, loop branches, delay slots and return.
    for (const auto boundary : {1u,2u,5u,6u,7u,17u,36u,37u}) {
        std::string reference;
        for (unsigned engine=0;engine<2;++engine) {
            Memory memory;memory.load(elf);EEState state;state.pc=elf.entry;
            state.set_reg32_sign_extended(31,host_return);
            BootOptions options;options.max_instructions=boundary;options.stop_at=host_return;
            const auto* module=&aot_module();
            const auto report=run_ee_boot(state,memory,module,options);
            if (report.retired!=boundary || report.outcome==BootOutcome::BlockPoint)
                throw std::runtime_error("AOT boundary execution failed: "+report.reason);
            const auto hash=checkpoint_state_sha256(state,memory);
            if (!engine) reference=hash;
            else if (hash!=reference) throw std::runtime_error("AOT boundary state differs between native slices");
            options.max_instructions=37u-boundary;
            const auto resumed=run_ee_boot(state,memory,module,options);
            if (resumed.outcome!=BootOutcome::StopAddress || state.reg32(2)!=37u || state.retired_instructions!=37u)
                throw std::runtime_error("AOT continuation across budget/delay slot failed");
            if (engine && (report.interpreted_instructions || resumed.interpreted_instructions))
                throw std::runtime_error("Proven instruction interior fell back to interpretation on resume");
        }
    }
    // A store after dispatch must remain visible at the next instruction,
    // even when both instructions are in the same compiled basic block.
    Memory memory;memory.load(elf);EEState state;state.pc=elf.entry;
    state.set_reg32_sign_extended(31,host_return);
    struct ModifyNext final : InstructionObserver {
        Memory& memory;std::uint32_t first;
        ModifyNext(Memory& m,std::uint32_t pc):memory(m),first(pc){}
        void on_instruction(std::uint32_t pc,std::uint32_t,ExecutionEngine) override {
            if (pc==first) memory.write32(first+4u,0xFFFFFFFFu);
        }
    } observer(memory,elf.entry);
    std::uint64_t executed=0;ExecutionContext ctx{state,memory,executed,37,host_return,&observer};
    bool rejected=false;
    try { find_aot_block(aot_module(),state.pc)->execute(ctx); }
    catch (const std::runtime_error& error) {
        rejected=std::string(error.what()).find("AOT executable code modified")!=std::string::npos;
    }
    if (!rejected || executed!=1 || state.pc!=elf.entry+4u)
        throw std::runtime_error("AOT did not reject a same-block executable write at its exact boundary");

    bool checked_delay_irq=false;
    // The interrupt test executes two zero words in reset RAM; translate them explicitly.
    auto irq_module=aot_module();
    std::vector<AotBlock> irq_blocks(irq_module.blocks,irq_module.blocks+irq_module.block_count);
    irq_blocks.push_back({0x80000200u,irq_nop<0x80000200u>});
    irq_blocks.push_back({0x80000204u,irq_nop<0x80000204u>});
    irq_module.blocks=irq_blocks.data();irq_module.block_count=irq_blocks.size();
    for (const auto compare : {1u,5u,6u,7u}) {
        std::string reference;
        for (unsigned engine=0;engine<2;++engine) {
            Memory irq_memory;irq_memory.load(elf);EEState irq_state;irq_state.pc=elf.entry;
            irq_state.set_reg32_sign_extended(31,host_return);
            irq_state.set_cp0(Cop0::Status,kStatusIE|kStatusEIE|kStatusIM7);
            irq_state.set_cp0(Cop0::Compare,compare);irq_state.compare_written=true;
            BootOptions options;options.max_instructions=compare+2u;
            const auto report=run_ee_boot(irq_state,irq_memory,&irq_module,options);
            if (report.outcome!=BootOutcome::BudgetExhausted || irq_state.pc!=0x80000208u)
                throw std::runtime_error("AOT timer interruption boundary failed: "+report.reason);
            checked_delay_irq|=(irq_state.cp0(Cop0::Cause)&0x80000000u)!=0;
            const auto hash=checkpoint_state_sha256(irq_state,irq_memory);
            if (!engine) reference=hash;
            else if (hash!=reference) throw std::runtime_error("AOT interrupt state differs between native slices");
        }
    }
    if (!checked_delay_irq) throw std::runtime_error("AOT timer test did not exercise an interrupted delay slot");
}
}

template <typename Character>
int aot_smoke_main(int argc, Character** argv) {
    if (argc != 2) return 2;
    try {
        const auto data = ps2native::read_file(std::filesystem::path(argv[1]));
        const auto elf = ps2native::parse_ee_elf(data);
        ps2native::Memory memory;
        memory.load(elf);
        ps2native::EEState state;
        state.pc = elf.entry;
        constexpr std::uint32_t host_return = 0xfffffff0u;
        state.set_reg32_sign_extended(31, host_return);

        // Exact instruction budget must accept termination on the final instruction.
        const auto executed = ps2native::run_recompiled(state, memory, host_return, 37);
        if (state.reg32(2) != 37u || memory.read32(0x40u) != 37u ||
            state.pc != host_return || executed != 37u ||
            state.retired_instructions != 37u)
            throw std::runtime_error("AOT result mismatch (branches, delay slots, JAL/JR, LW/SW)");
        // Budget exhaustion is explicit, never a silent partial success.
        ps2native::EEState short_state;
        short_state.pc = elf.entry;
        short_state.set_reg32_sign_extended(31, host_return);
        bool budget_rejected = false;
        try { (void)ps2native::run_recompiled(short_state, memory, host_return, 5); }
        catch (const std::runtime_error&) { budget_rejected = true; }
        if (!budget_rejected) throw std::runtime_error("Instruction budget not enforced");
        if (memory.read32(elf.entry) != elf.instruction_at(elf.entry))
            throw std::runtime_error("Guest ELF modified unexpectedly");
        check_boundaries(elf);
        std::cout << "AOT TEST PASS: ELF -> generated C++ -> host native code -> 37 executed MIPS instructions;"
                  << " $v0=37; RAM[0x40]=37; returned through JR delay slot.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "aot_smoke: " << e.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return aot_smoke_main(argc, argv); }
#else
int main(int argc, char** argv) { return aot_smoke_main(argc, argv); }
#endif
