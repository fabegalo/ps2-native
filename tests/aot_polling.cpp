#include "boot_checks.hpp"
#include "direct_program.hpp"
#include "ps2native/checkpoint.hpp"
#include <iostream>

using namespace ps2native;
using namespace boot_checks;
namespace {
constexpr auto start=direct_test::text;
constexpr std::array<std::uint32_t,7> words{0x8E020000u,0u,0u,0u,0u,0x1440FFFAu,0u};
constexpr std::array<std::uint32_t,7> idle_words{0u,0u,0u,0u,0u,0x1000FFFAu,0u};
constexpr std::array<std::uint32_t,7> cache_words{0xBE140000u,0u,0u,0u,0u,0x1000FFFAu,0u};
constexpr std::array<std::uint32_t,7> arithmetic_words{0x24420001u,0x24630003u,0x34440008u,0x00042840u,0u,0x1000FFFAu,0u};
bool saw_interrupt_cache{};

void native_arithmetic(ExecutionContext& ctx) {
    for(auto i=(ctx.state.pc-start)/4u;i<arithmetic_words.size();++i) {
        const auto pc=start+i*4u;
        if(!aot_enter_checked(ctx,pc,arithmetic_words[i],i==5u))return;
        const auto frame=ee::begin(ctx.state,pc);
        switch(i) {
            case 0:case 1:ee::exec<MipsOpcode::Addiu>(ctx.state,ctx.memory,pc,arithmetic_words[i]);break;
            case 2:ee::exec<MipsOpcode::Ori>(ctx.state,ctx.memory,pc,arithmetic_words[i]);break;
            case 3:ee::exec<MipsOpcode::Sll>(ctx.state,ctx.memory,pc,arithmetic_words[i]);break;
            case 5:ee::exec<MipsOpcode::Beq>(ctx.state,ctx.memory,pc,arithmetic_words[i]);break;
            default:ee::exec<MipsOpcode::Nop>(ctx.state,ctx.memory,pc,arithmetic_words[i]);break;
        }
        if(ee::retire(ctx,frame))return;
    }
}
template<bool Polling=true,bool Batch=true,bool Cache=false> void native_loop(ExecutionContext& ctx) {
    const auto& sequence=Cache ? cache_words : Polling ? words : idle_words;
    if constexpr(Batch)if(aot_polling_loop(ctx,start,sequence))return;
    const auto first=(ctx.state.pc-start)/4u;
    for(auto i=first;i<words.size();++i) {
        const auto pc=start+i*4u;
        if(!aot_enter_checked(ctx,pc,sequence[i],i==5u))return;
        if constexpr(!Batch)saw_interrupt_cache=saw_interrupt_cache || ctx.interrupt_cache.deadline!=0u;
        if constexpr(Cache)if(!i)check_cache_privilege(ctx);
        const auto frame=ee::begin(ctx.state,pc);
        if(!i && Cache)execute_cache_instruction(ctx,sequence[i]);
        else if(!i && Polling)ee::exec<MipsOpcode::Lw>(ctx.state,ctx.memory,pc,sequence[i]);
        else if(i==5u) {
            if constexpr(Polling)ee::exec<MipsOpcode::Bne>(ctx.state,ctx.memory,pc,sequence[i]);
            else ee::exec<MipsOpcode::Beq>(ctx.state,ctx.memory,pc,sequence[i]);
        } else ee::exec<MipsOpcode::Nop>(ctx.state,ctx.memory,pc,sequence[i]);
        if(ee::retire(ctx,frame))return;
    }
}
struct Result {std::string state;BootReport report;std::uint32_t epc{};bool cache_seen{};};
ElfImage image(bool pure=false,bool cache=false,bool arithmetic=false) {
    m20::Assembler handler(0x80000200u);
    handler.la(26,0xFFFFFFFFu);handler.mtc0(26,11);handler.eret();
    const auto& sequence=arithmetic ? arithmetic_words : cache ? cache_words : pure ? idle_words : words;
    const direct_test::Program program{{{start,5,m20::le_words(std::vector<std::uint32_t>(sequence.begin(),sequence.end()))},
        {direct_test::data,6,{1u,0u,0u,0u}},{0x200u,5,m20::le_words(handler.finish())}},0,{}, {}};
    return parse_ee_elf(direct_test::elf(program));
}
Result run(std::uint64_t budget,bool native,bool sliced=false,bool full=false,bool changed=false,
           std::optional<std::uint32_t> stop={},bool irq=false,bool pure=false,bool batch=true,bool dmac=true,bool cache=false,bool arithmetic=false) {
    saw_interrupt_cache=false;
    const auto program=image(pure,cache,arithmetic);
    const auto hash=sha256_hex(program.bytes);
    std::array<AotBlock,7> blocks{};
    for(unsigned i=0;i<blocks.size();++i)blocks[i]={start+i*4u,arithmetic ? native_arithmetic : cache ? native_loop<false,false,true> : pure ? native_loop<false> :
        batch ? native_loop<true> : native_loop<true,false>};
    const std::array<std::uint32_t,1> entries{start};
    const AotModule module{blocks.data(),blocks.size(),entries.data(),entries.size(),"elf",hash.c_str()};
    EEState state;Memory memory;prepare_direct_elf(program,native?&module:nullptr,state,memory);
    if(arithmetic)for(unsigned reg=2;reg<=5;++reg)state.set_reg64(reg,reg);
    if(cache) {
        memory.diagnostic_data_cache();state.set_cp0(Cop0::Config,0x10443u);memory.set_cache_config(0x10443u);
        memory.store32_observed(0x80002000u,{0x12345600u,0xFFu});
    }
    state.set_reg32(16,direct_test::data);
    memory.write64(kGsSmode1|0xA0000000u,kSmode1Sint);
    m20::Assembler iop(0x1000u);
    direct_test::store(iop,0xBF801578u,1u);
    direct_test::store(iop,0xBF801570u,0x8000u);
    direct_test::store(iop,0xBF801538u,0x01000600u); // armed SIF1 receiver, empty FIFO
    direct_test::store(iop,0xBF801128u,19u);
    direct_test::store(iop,0xBF801124u,0x2D8u); // /8, zero return, repeat/toggle compare IRQ
    iop.label("idle");iop.beq(0,0,"idle");iop.nop();
    memory.load_iop_program(m20::le_words(iop.finish()),0x1000u,0x1000u);
    memory.write32(0xB000E000u,1u);
    memory.write32(0xB000C020u,0u);
    memory.write32(0xB000C000u,0x184u); // armed SIF0 receiver, no IOP source
    memory.write32(kIntcMask|0xA0000000u,1u<<9u);
    memory.write32(0xB0000020u,3u);memory.write32(0xB0000000u,0xFFFEu);
    memory.write32(0xB0000010u,irq ? 0x80u : 0x380u);
    state.set_cp0(Cop0::Status,kStatusIM2|(dmac ? kStatusIM3 : 0u)|(irq ? kStatusIM7|kStatusIE|kStatusEIE : 0u));
    state.retired_instructions=1024u;memory.sync_iop();
    state.count_anchor=state.cycles();state.set_cp0(Cop0::Count,0u);
    state.set_cp0(Cop0::Compare,512u);state.compare_written=true;
    if(changed)memory.write32(start+8u,1u);
    BootOptions options;options.max_instructions=budget;options.copy_device_histories=false;
    options.max_trace_events=500;options.trace=full ? TraceLevel::Full : TraceLevel::Events;
    options.stop_at=stop;
    BootReport report;
    if(sliced) {
        options.max_instructions=1u;options.tail_instructions=0;
        for(std::uint64_t i=0;i<budget;++i) {
            report=run_ee_boot(state,memory,nullptr,options);
            if(report.outcome!=BootOutcome::BudgetExhausted)break;
        }
    } else report=run_ee_boot(state,memory,native?&module:nullptr,options);
    return {checkpoint_state_sha256(state,memory),report,state.cp0(Cop0::Epc),saw_interrupt_cache};
}
}
int main() try {
    for(const auto budget:{1u,4u,5u,6u,7u,8u,32u,512u,4097u}) {
        const auto native=run(budget,true,false,false,false,{},false,false,false,true,false,true);
        const auto reference=run(budget,false,false,false,false,{},false,false,false,true,false,true);
        require(native.state==reference.state,"arithmetic with cached interrupts preserves full guest state at budget "+std::to_string(budget));
    }
    for(unsigned offset=0;offset<5u;++offset) {
        const auto stop=start+offset*4u;
        const auto native=run(512u,true,false,false,false,stop,false,false,false,true,false,true);
        const auto reference=run(512u,false,false,false,false,stop,false,false,false,true,false,true);
        require(native.state==reference.state,"arithmetic with cached interrupts respects interior stop addresses");
    }
    for(const bool full:{false,true}) {
        const auto native=run(4097u,true,false,full,false,{},true,false,false,true,false,true);
        const auto reference=run(4097u,false,false,full,false,{},true,false,false,true,false,true);
        require(native.state==reference.state,"arithmetic with cached interrupts preserves interrupt entry and continuation");
    }
    {
        EEState state;Memory memory;prepare_direct_elf(image(),nullptr,state,memory);
        require(memory.code_matches(start,words[0]) && memory.code_matches(start,words[0]),"warm executable identity");
        memory.write8(start+1u,0xFFu);
        require(!memory.code_matches(start,words[0]),"partial CPU store invalidates checked code");
        memory.write32(start,words[0]);
        require(memory.code_matches(start,words[0]),"restored known code is revalidated");
        memory.store32_observed(start,{words[0],1u});
        rejects([&]{(void)memory.code_matches(start,words[0]);},"unknown RAM bits");
        memory.write32(start,words[0]);
        require(memory.code_matches(start,words[0]),"knowledge restoration invalidates stale checks");
        memory.write32(0x70000000u,words[0]);
        require(memory.code_matches(0x70000000u,words[0]),"scratchpad executable identity");
        memory.write32(0x70000000u,0u);
        require(!memory.code_matches(0x70000000u,words[0]),"scratchpad writes invalidate checked code");
    }
    AotOptions options;options.unsupported_boundaries=true;options.additional_function_entries={start+4u,start+12u};
    {
        Memory memory;std::uint64_t clock=0;
        memory.reset_for_boot(std::vector<std::uint8_t>(Memory::kBiosRomBytes));memory.set_clock(&clock);
        memory.tlb_write(0,{0,0x70000000u,0x80000007u,7u});memory.set_translation_context(0,false);
        memory.write32(0x80002000u,words[0]);memory.write32(0x70000100u,0xFFFFFFFFu);
        require(memory.code_matches(0x80002000u,words[0]),"warm code before scheduled fromSPR DMA");
        memory.write32(0xB000E000u,1u);memory.write32(0xB000E020u,0u);
        memory.write32(0xB000D010u,0x2000u);memory.write32(0xB000D020u,1u);
        memory.write32(0xB000D080u,0x100u);memory.write32(0xB000D000u,0x100u);
        clock=2u;
        require(!memory.code_matches(0x80002000u,words[0]) && memory.fetch32(0x80002000u)==0xFFFFFFFFu,
                "scheduled DMA invalidates checked code before its next native instruction");
    }
    require(generate_aot_cpp(image(),options).find("aot_polling_loop(ctx,")!=std::string::npos,
            "interior dispatch leaders do not hide a proved polling loop");
    require(generate_aot_cpp(image(true),options).find("aot_polling_loop(ctx,")!=std::string::npos,
            "pure idle branches are recognized without game-specific addresses");
    for(const auto budget:{1u,6u,7u,8u,15u,511u,4097u}) {
        const auto reference=run(budget,false),fast=run(budget,true),sliced=run(budget,false,true);
        require(reference.state==fast.state && fast.state==sliced.state,"EE/IOP timers and state match at arbitrary budgets");
        require(normalized(reference.report.tail)==normalized(fast.report.tail),"batched instruction tails retain exact sequence and PCs");
        require(fast.report.retired==budget && fast.report.aot_instructions==budget,"native logical retirement and coverage agree");
        if(budget==4097u)require(fast.report.batched_instructions>3000u,"proved RAM polling uses native loop batching");
    }
    const auto full=run(4097u,true,false,true),reference=run(4097u,false);
    require(!full.report.batched_instructions && full.state==reference.state,"full tracing retains per-instruction observation");
    const auto stopped=run(1000u,true,false,false,false,start+12u);
    require(stopped.report.outcome==BootOutcome::StopAddress && stopped.report.retired==3u,"interior stop addresses retain the instruction boundary");
    const auto changed=run(1000u,true,false,false,true);
    require(changed.report.retired==2u && changed.report.reason.find("modified")!=std::string::npos,
            "lookahead falls back so modified code fails only at its original boundary");
    const auto irq=run(4097u,true,false,false,false,{},true);
    const auto irq_reference=run(4097u,false,false,false,false,{},true);
    require(irq.state==irq_reference.state && irq.epc==start+4u && irq.report.batched_instructions>0u,
            "Count/Compare interrupts a batched loop at the exact original instruction");
    for(const auto budget:{1u,6u,7u,8u,511u,4097u}) {
        const auto idle=run(budget,true,false,false,false,{},true,true);
        const auto slow=run(budget,false,false,false,false,{},true,true);
        require(idle.state==slow.state && normalized(idle.report.tail)==normalized(slow.report.tail),
                "pure idle loops preserve interrupts, timers and exact continuation");
        if(budget==4097u)require(idle.report.batched_instructions>3000u,"pure idle uses exact native batching");
    }
    for(const auto budget:{1u,6u,7u,8u,15u,511u,4097u}) {
        const auto cached=run(budget,true,false,false,false,{},true,false,false);
        const auto uncached=run(budget,true,false,true,false,{},true,false,false);
        const auto interpreted=run(budget,false,false,false,false,{},true,false,false);
        require(!cached.report.batched_instructions && cached.state==uncached.state && cached.state==interpreted.state,
                "event-deadline interrupt sampling preserves full hardware state without loop aggregation");
        if(budget==4097u)require(cached.cache_seen && !uncached.cache_seen,"native deadlines are used and full tracing remains uncached");
    }
    const auto no_dmac=run(4097u,true,false,false,false,{},true,false,false,false);
    const auto no_dmac_reference=run(4097u,false,false,false,false,{},true,false,false,false);
    require(!no_dmac.cache_seen && no_dmac.state==no_dmac_reference.state,
            "disabled DMAC sampling preserves every original RAM/DMA ordering point");
    for(const auto budget:{1u,7u,8u,511u,4097u}) {
        const auto cached=run(budget,true,false,false,false,{},true,false,false,true,true);
        const auto traced=run(budget,true,false,true,false,{},true,false,false,true,true);
        const auto interpreted=run(budget,false,false,false,false,{},true,false,false,true,true);
        require(cached.state==traced.state && cached.state==interpreted.state,
                "index writeback preserves masked data, interrupt state and exact event boundaries");
        if(budget==4097u)require(cached.cache_seen && !traced.cache_seen,
                "index writeback uses only the proved quiescent native window");
    }
    std::cout<<"Native polling: state, IOP events, timers, tails, tracing, stop and code identity passed\n";
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
