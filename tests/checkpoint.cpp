#include "boot_checks.hpp"
#include "checkpoint_fixture.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/sha256.hpp"
#include "ps2native/io.hpp"
#include <iostream>
#include <memory>
using namespace ps2native;
using namespace boot_checks;
using checkpoint_fixture::legacy_envelope;
ElfImage program() {
    std::vector<std::uint8_t> bytes(0x10C);
    const auto put=[&](unsigned at,unsigned value,unsigned n=4){for(unsigned i=0;i<n;++i)bytes[at+i]=static_cast<std::uint8_t>(value>>(i*8u));};
    bytes[0]=0x7F;bytes[1]='E';bytes[2]='L';bytes[3]='F';bytes[4]=1;bytes[5]=1;bytes[6]=1;
    put(16,2,2);put(18,8,2);put(20,1);put(24,0x100000);put(28,52);put(36,0x20924001);
    put(40,52,2);put(42,32,2);put(44,1,2);
    put(52,1);put(56,0x100);put(60,0x100000);put(64,0x100000);put(68,12);put(72,12);put(76,5);put(80,16);
    put(0x100,0x24C60001u);put(0x104,0x1000FFFEu);return parse_ee_elf(bytes);
}
void finish_transfer(Memory& memory) {
    auto& gs=const_cast<GsRegisters&>(memory.gs());auto& gif=const_cast<Gif&>(memory.gif());
    gif.write_fifo(0x6666666655555555ull,0x8888888877777777ull,gs);
    require(gs.vram().read(0,64,0,7,0)==0x88888888u,"restored partial IMAGE transfer finishes at the correct pixel");
}
int main(int argc,char** argv)try {
    if(argc!=2)return 2;const auto directory=std::filesystem::path(argv[1]);ps2native::create_directories(directory);
    {
        const auto image=program();auto source=std::make_unique<Memory>();auto destination=std::make_unique<Memory>();
        EEState original,restored;prepare_direct_elf(image,nullptr,original,*source);prepare_direct_elf(image,nullptr,restored,*destination);
        const auto write_latch=[](Memory& memory,unsigned offset){
            const std::array<std::uint32_t,5> words{0x3C01BF80u,0x34021234u,0xAC220000u|offset,0x08000003u,0u};
            std::vector<std::uint8_t> bytes(words.size()*4u);
            for(unsigned n=0;n<words.size();++n)for(unsigned b=0;b<4;++b)bytes[n*4u+b]=static_cast<std::uint8_t>(words[n]>>(b*8u));
            memory.load_iop_program(bytes,0u,0u);auto& iop=const_cast<Iop&>(memory.iop());iop.run_until(3u);
            require(!iop.stopped(),"latch restore fixture writes one register");
        };
        write_latch(*source,0x1074u);write_latch(*destination,0x1000u);
        require(!destination->iop().diagnostic_latch(0x1F801074u),"destination warms a missing latch at equal vector size");
        require(destination->iop().diagnostic_latch(0x1F801000u)==0x1234u,"destination warms a present latch");
        BootReport boundary;boundary.outcome=BootOutcome::BudgetExhausted;boundary.pc=original.pc;
        const CheckpointContext context{sha256_hex(image.bytes),"latch lookup restore"};const auto path=directory/"latch-lookup.ps2ncp";
        save_checkpoint(path,original,*source,boundary,context);(void)load_checkpoint(path,restored,*destination,context);
        require(destination->iop().diagnostic_latch(0x1F801074u)==0x1234u,"restore invalidates a cached absent address");
        require(!destination->iop().diagnostic_latch(0x1F801000u),"restore invalidates a cached present address");
    }


    {
        const auto image=program();auto memoryOwner=std::make_unique<Memory>();auto& memory=*memoryOwner;
        auto stateOwner=std::make_unique<EEState>();auto& state=*stateOwner;prepare_direct_elf(image,nullptr,state,memory);
        memory.write32(0xB0002000u,127u);
        memory.write32(0xB0002000u,0x5800003Fu);
        for(unsigned q=0;q<4;++q) memory.write128(0xB0007010u,0xF0DEBC9A78563412ull,0xF0DEBC9A78563412ull);
        BootReport boundary;boundary.outcome=BootOutcome::BudgetExhausted;boundary.pc=state.pc;
        CheckpointContext context{sha256_hex(image.bytes),"pending unaligned IPU table"};
        const auto file=directory/"pending-ipu-table.ps2ncp";save_checkpoint(file,state,memory,boundary,context);
        auto resumedOwner=std::make_unique<EEState>();auto& resumed=*resumedOwner;
        auto restoredOwner=std::make_unique<Memory>();auto& restored=*restoredOwner;prepare_direct_elf(image,nullptr,resumed,restored);
        (void)load_checkpoint(file,resumed,restored,context);
        require(checkpoint_state_sha256(state,memory)==checkpoint_state_sha256(resumed,restored),"pending unaligned IPU table restores every field");
        for(auto* target:{&memory,&restored}) {
            target->write32(0xB000F000u,0x100u);
            target->write128(0xB0007010u,0xF0DEBC9A78563412ull,0xF0DEBC9A78563412ull);
            require(target->load32_observed(0xB0002010u).value&0x80000000u,"unaligned IPU table still needs the last qword after restore");
            target->write128(0xB0007010u,0xF0DEBC9A78563412ull,0xF0DEBC9A78563412ull);
            require(!(target->load32_observed(0xB0002010u).value&0x80000000u) && (target->load32_observed(0xB000F000u).value&0x100u),"restored table completes with its original bit offset and IRQ");
        }
        require(checkpoint_state_sha256(state,memory)==checkpoint_state_sha256(resumed,restored),"IPU continuation has identical full state and table bytes");
    }
    {
        const auto image=program();auto memoryOwner=std::make_unique<Memory>();auto& memory=*memoryOwner;
        auto stateOwner=std::make_unique<EEState>();auto& state=*stateOwner;prepare_direct_elf(image,nullptr,state,memory);
        memory.write32(0xB000F430u,mch_ricm(kMchSioPinInit,true,0,0));
        BootOptions options;options.max_instructions=1;
        const auto report=run_ee_boot(state,memory,nullptr,options);
        CheckpointContext context{sha256_hex(image.bytes),"pending-mch"};
        const auto path=directory/"pending-mch.ps2ncp";save_checkpoint(path,state,memory,report,context);
        const auto expected=checkpoint_state_sha256(state,memory);
        auto restoredOwner=std::make_unique<Memory>();auto& restored=*restoredOwner;
        auto resumedOwner=std::make_unique<EEState>();auto& resumed=*resumedOwner;prepare_direct_elf(image,nullptr,resumed,restored);
        (void)load_checkpoint(path,resumed,restored,context);
        require(checkpoint_state_sha256(resumed,restored)==expected,"pending MCH chain restores into a fresh destination");
        for(auto pair:{std::pair{&state,&memory},std::pair{&resumed,&restored}}) {
            require(pair.second->read32(0xB000F430u)&kMchRicmBusy,"restored MCH transaction remains busy");
            pair.first->retired_instructions+=mch_busy_cycles(kRdramSckShort);
            require(!(pair.second->read32(0xB000F430u)&kMchRicmBusy),"restored MCH transaction completes on its original deadline");
        }
        require(checkpoint_state_sha256(resumed,restored)==checkpoint_state_sha256(state,memory),"MCH completion preserves full continuation state");
    }
    {
        auto image=program();auto stateOwner=std::make_unique<EEState>();auto& state=*stateOwner;auto memoryOwner=std::make_unique<Memory>();auto& memory=*memoryOwner;prepare_direct_elf(image,nullptr,state,memory);
        state.set_reg128_observed(9,0x44E5800044E58000ull,0x44E5800044E58000ull,0x3FFF00003FFFull,0x3FFF00003FFFull);
        for(auto& bounds:state.gpr_candidates[9].bounds)bounds=FloatBounds{0x44E59100u,0x44E594FFu};
        BootReport boundary;boundary.outcome=BootOutcome::BudgetExhausted;boundary.pc=state.pc;
        CheckpointContext context{sha256_hex(image.bytes),"CPU lane certificates"};const auto file=directory/"gpr-lane-bounds.ps2ncp";
        save_checkpoint(file,state,memory,boundary,context);const auto expected=checkpoint_state_sha256(state,memory);
        auto restoredStateOwner=std::make_unique<EEState>();auto& restored=*restoredStateOwner;auto restoredOwner=std::make_unique<Memory>();auto& resumed=*restoredOwner;prepare_direct_elf(image,nullptr,restored,resumed);(void)load_checkpoint(file,restored,resumed,context);
        require(checkpoint_state_sha256(restored,resumed)==expected && restored.gpr_candidates[9].bounds==state.gpr_candidates[9].bounds,"checkpoint retains all CPU lane certificates");
        const auto info=inspect_checkpoint(file);const auto bytes=read_file(file);
        const auto payload=std::span<const std::uint8_t>(bytes).last(static_cast<std::size_t>(info.payload_bytes));
        auto reviewed=context;reviewed.allow_runtime_change=true;
        for(const auto* schema:{"50585f2224cd852e9ef36ef63b53e10fe41691975b7c38fe44dd5384280d08cc",
                               "11a100b3f0fda71733f6772b12b4d4748ef162dc10fd182c2b64cf6fe2dd1e0b",
                               "5a3fed18dfb483c07fddea413019e614a6ad69d786b6c191d632df37d6525bf5",
                               "db30806fe4f74e4781e737979c2ad78fd6bac2b17383aac1ff4fc3c9b286f9f1"}) {
            const auto alias=directory/"gpr-lane-helper-alias.ps2ncp";
            write_file_atomically(alias,legacy_envelope(info,payload,schema,true,true));
            (void)load_checkpoint(alias,restored,resumed,reviewed);
            require(checkpoint_state_sha256(restored,resumed)==expected && restored.gpr_candidates[9].bounds==state.gpr_candidates[9].bounds,
                "helper-only schema aliases preserve populated CPU lane certificates and shared domains");
        }
    }
    {
        auto image=program();EEState state;auto storage=std::make_unique<Memory>();auto& memory=*storage;prepare_direct_elf(image,nullptr,state,memory);
        memory.write128(0xA0000200u,0x07000011u,0);memory.write128(0xA0000210u,0x07000022u,0);
        memory.write128(0xA0000300u,0x8000u,0);memory.write128(0xA0000310u,0x8000u,0);
        memory.write32(0xB000E000u,1u);
        for(unsigned c=1;c<=2u;++c){const auto base=0xB0008000u+c*0x1000u;memory.write32(base+0x10u,0x100u*(c+1u));memory.write32(base+0x20u,2u);memory.write32(base,0x101u);}
        BootOptions step;step.max_instructions=2;
        auto boundary=run_ee_boot(state,memory,nullptr,step);(void)memory.load32_observed(0xB0009020u);
        require(boundary.outcome==BootOutcome::BudgetExhausted && memory.iop().ee_dma_read(0x10009020u)==1u && memory.iop().ee_dma_read(0x1000A020u)==2u,"checkpoint stops after VIF1 owns the first round-robin grant");
        const auto file=directory/"graphics-bus-turn.ps2ncp";CheckpointContext context{sha256_hex(image.bytes),"graphics bus cursor"};save_checkpoint(file,state,memory,boundary,context);
        EEState rs;auto restored_storage=std::make_unique<Memory>();auto& restored=*restored_storage;prepare_direct_elf(image,nullptr,rs,restored);(void)load_checkpoint(file,rs,restored,context);
        for(auto pair:{std::pair{&state,&memory},std::pair{&rs,&restored}}){require(run_ee_boot(*pair.first,*pair.second,nullptr,step).outcome==BootOutcome::BudgetExhausted,"both arbitration continuations retire");(void)pair.second->load32_observed(0xB0009020u);require(pair.second->iop().ee_dma_read(0x1000A020u)==1u && pair.second->iop().ee_dma_read(0x10009020u)==1u,"restored arbitration rotates to GIF rather than repeating VIF1");}
        require(checkpoint_state_sha256(state,memory)==checkpoint_state_sha256(rs,restored),"pending shared graphics DMA round-trips every field");
    }
    for(const bool partial:{false,true}) {
        auto image=program();EEState state;Memory memory;prepare_direct_elf(image,nullptr,state,memory);
        memory.write32(0xB0003C20u,2u);
        if(partial)memory.write128(0xB0005000u,0,0xA000000000000000ull);
        else memory.write128(0xB0005000u,0x80000000u,0);
        BootReport boundary;boundary.outcome=BootOutcome::BudgetExhausted;boundary.pc=state.pc;
        const auto file=directory/(partial ? "vif-irq-payload.ps2ncp" : "vif-irq-stall.ps2ncp");
        CheckpointContext context{sha256_hex(image.bytes),"VIF interrupt"};save_checkpoint(file,state,memory,boundary,context);
        EEState rs;Memory restored;prepare_direct_elf(image,nullptr,rs,restored);(void)load_checkpoint(file,rs,restored,context);
        require(checkpoint_state_sha256(state,memory)==checkpoint_state_sha256(rs,restored),"active I-bit payload and INT/VIS round-trip");
        for(auto* m:{&memory,&restored}) {
            if(partial)m->write128(0xB0005000u,0x1234u,0);
            require((m->load32_observed(0xB0003C00u).value&0xC00u)==0xC00u,"restored completion retains INT/VIS");
            m->write32(0xB0003C10u,8u);m->write128(0xB0005000u,0,0);
        }
        require(checkpoint_state_sha256(state,memory)==checkpoint_state_sha256(rs,restored),"restored VIF IRQ continuation remains exact");
    }
    for(unsigned prefix:{7u,11u}) {
        auto micro_image=program();Memory micro;EEState ms;prepare_direct_elf(micro_image,nullptr,ms,micro);
        constexpr std::uint32_t nop=0x8000033Cu,unop=0x2FFu;
        const std::uint32_t lows[]{0x01E10000u,0x01E20001u,nop,nop,0x03E02002u,nop,nop};
        const std::uint32_t highs[]{unop,unop,(15u<<21u)|(2u<<16u)|(1u<<11u)|(3u<<6u)|0x2Au,
            (15u<<21u)|(4u<<16u)|(3u<<11u)|0x17Du,unop,unop|0x40000000u,unop};
        for(unsigned n=0;n<7;++n)micro.write64(0xB1008000u+n*8u,lows[n]|(std::uint64_t{highs[n]}<<32u));
        micro.write128(0xB100C000u,0x3F8000013F800001ull,0x3F8000013F800001ull);
        micro.write128(0xB100C010u,0x3F8000033F800003ull,0x3F8000033F800003ull);
        micro.write128(0xB0005000u,0x14000000u,0);
        BootOptions options;options.max_instructions=prefix;
        const auto boundary=run_ee_boot(ms,micro,nullptr,options);
        require(boundary.outcome==BootOutcome::BudgetExhausted,"micro certificate fixture reaches a safe pipeline boundary");
        const auto path=directory/("micro-bounds-"+std::to_string(prefix)+".ps2ncp");
        CheckpointContext context{sha256_hex(micro_image.bytes),"micro numeric ranges"};
        save_checkpoint(path,ms,micro,boundary,context);const auto saved=checkpoint_state_sha256(ms,micro);
        EEState rs;Memory restored;prepare_direct_elf(micro_image,nullptr,rs,restored);
        (void)load_checkpoint(path,rs,restored,context);
        require(checkpoint_state_sha256(rs,restored)==saved,"pending and retired VU numeric ranges round-trip");
        options.max_instructions=40;
        require(run_ee_boot(ms,micro,nullptr,options).outcome==BootOutcome::BudgetExhausted &&
                run_ee_boot(rs,restored,nullptr,options).outcome==BootOutcome::BudgetExhausted,"both micro continuations finish");
        require(checkpoint_state_sha256(ms,micro)==checkpoint_state_sha256(rs,restored),"micro range continuation matches every guest field");
        const auto coordinate=micro.load32_observed(0xB100C020u);
        require(coordinate.value==16u && !coordinate.unknown,"real VU1 arithmetic, FTOI and SQ publish the independently expected coordinate");
    }
    for(const bool queued:{false,true}) {
        auto image=program();EEState state;Memory memory;prepare_direct_elf(image,nullptr,state,memory);
        memory.set_clock(&state.retired_instructions,&state.stall_cycles);
        if(queued){
            memory.write64(0xB1008000u,0x000002FF8000033Cull);
            memory.write64(0xB1008008u,0x400002FF8000033Cull);
            memory.write64(0xB1008010u,0x000002FF8000033Cull);
            memory.write128(0xB0005000u,0x0500000001000101ull,0x1000000014000000ull);
        }
        const std::array<std::uint32_t,8> words=queued ?
            std::array<std::uint32_t,8>{0,0,0,0x6C010003u,0x447FFE00u,2,3,4} :
            std::array<std::uint32_t,8>{0x01000101u,0x05000000u,0x6C020003u,0x447FFE00u,2,3,4,0x447FFE00u};
        for(unsigned n=0;n<8;++n)memory.write32(0x80002000u+n*4u,words[n]);
        memory.store32_observed(queued ? 0x80002010u : 0x8000200Cu,{0x447FFE00u,0x1FFu});
        memory.remember_fpu_relation(queued ? 0x80002010u : 0x8000200Cu,{0x447FFE00u,0x1FFu},
            {memory.allocate_fpu_origin(),0,136,false,0x447FFE00u,0x1FFu,0x447FFEFFu,0x447FFF00u});
        if(!queued)memory.store32_observed(0x8000201Cu,{0x447FFE00u,0x100u});
        if(!queued)memory.remember_fpu_relation(0x8000201Cu,{0x447FFE00u,0x100u},
            {memory.allocate_fpu_origin(),0,136,false,0x447FFE00u,0x100u,0x447FFE00u,0x447FFF00u});
        memory.write32(0xB000E000u,1);memory.write32(0xB0009010u,0x2000u);memory.write32(0xB0009020u,2);
        memory.write32(0xB0009000u,0x101u);
        BootOptions options;options.max_instructions=4;const auto prefix=run_ee_boot(state,memory,nullptr,options);
        require(prefix.outcome==BootOutcome::BudgetExhausted,"observed VIF fixture reaches a successful DMA boundary");
        const auto path=directory/(queued ? "vif-queued-unknown.ps2ncp" : "vif-partial-unknown.ps2ncp");
        CheckpointContext context{sha256_hex(image.bytes),"vif-observed"};
        save_checkpoint(path,state,memory,prefix,context);const auto saved=checkpoint_state_sha256(state,memory);
        const auto finish=[&](EEState& s,Memory& m){
            if(!queued)m.write128(0xB0005000u,0x0000000600000005ull,7);
            options.max_instructions=8;const auto report=run_ee_boot(s,m,nullptr,options);
            require(report.outcome==BootOutcome::BudgetExhausted,"restored VIF data drains through the real scheduler");
            const auto value=m.load32_observed(queued ? 0xB100C030u : 0xB100C040u);
            require(value.unknown==(queued ? 0x1FFu : 0x100u),"VIF retains queued and partial UNPACK uncertainty after continuation");
        };
        finish(state,memory);const auto expected=checkpoint_state_sha256(state,memory);
        EEState resumed;Memory restored;prepare_direct_elf(image,nullptr,resumed,restored);
        (void)load_checkpoint(path,resumed,restored,context);
        require(checkpoint_state_sha256(resumed,restored)==saved,"VIF input queue and partial data masks round-trip");
        finish(resumed,restored);
        require(checkpoint_state_sha256(resumed,restored)==expected,"continuous/restored VIF guest data, clocks and all devices match");
    }
    for(const unsigned loops:{0u,1u}) {
        auto gap_image=program();Memory gap;EEState gap_state;prepare_direct_elf(gap_image,nullptr,gap_state,gap);
        BootOptions gap_options;gap_options.max_instructions=1;
        const auto boundary=run_ee_boot(gap_state,gap,nullptr,gap_options);
        auto& gap_gif=const_cast<Gif&>(gap.gif());auto& gap_gs=const_cast<GsRegisters&>(gap.gs());
        gap_gif.write_fifo(loops|(1ull<<60u),15u,gap_gs);
        if(loops)gap_gif.write_fifo(0,0,gap_gs);
        const auto gap_file=directory/"between-giftags.ps2ncp";
        CheckpointContext gap_context{sha256_hex(gap_image.bytes)};
        const auto gap_hash=checkpoint_state_sha256(gap_state,gap);
        save_checkpoint(gap_file,gap_state,gap,boundary,gap_context);
        Memory loaded_gap;EEState loaded_state;prepare_direct_elf(gap_image,nullptr,loaded_state,loaded_gap);
        (void)load_checkpoint(gap_file,loaded_state,loaded_gap,gap_context);
        require(checkpoint_state_sha256(loaded_state,loaded_gap)==gap_hash,
            "a non-EOP GIFtag may await the next tag with zero payload loops, including NLOOP=0");
        gap_gif.write_fifo(1ull<<15u,0,gap_gs);
        const_cast<Gif&>(loaded_gap.gif()).write_fifo(1ull<<15u,0,const_cast<GsRegisters&>(loaded_gap.gs()));
        require(checkpoint_state_sha256(loaded_state,loaded_gap)==checkpoint_state_sha256(gap_state,gap),
            "a restored GIF tag gap continues the same packet through its final EOP tag");
    }
    for(const unsigned prefix:{3u,9u}) {
        auto mfifo_image=program();Memory mfifo;EEState ms;prepare_direct_elf(mfifo_image,nullptr,ms,mfifo);
        mfifo.write64(0x70003FF0u,0x70000002u);mfifo.write64(0x70003FF8u,0);
        mfifo.write64(0x70000000u,0x1000000000008001ull);mfifo.write64(0x70000008u,0xEu);
        mfifo.write64(0x70000010u,0xFFFFFFFF12345678ull);mfifo.write64(0x70000018u,0x62);
        for(const auto [at,value]:std::initializer_list<std::pair<unsigned,unsigned>>{
            {0xB000E040,0x70},{0xB000E050,0x200},{0xB000E000,0xD},
            {0xB000D010,0x260},{0xB000A020,0},{0xB000A030,0x260},{0xB000A000,0x105},
            {0xB000D020,3},{0xB000D080,0x3FF0},{0xB000D000,0x100}})mfifo.write32(at,value);
        BootOptions mo;mo.max_instructions=prefix;auto mb=run_ee_boot(ms,mfifo,nullptr,mo);
        require(mb.outcome==BootOutcome::BudgetExhausted,"MFIFO reaches a valid partial-transfer boundary");
        const auto mf=directory/("mfifo-"+std::to_string(prefix)+".ps2ncp");
        CheckpointContext mc{sha256_hex(mfifo_image.bytes),"mfifo"};save_checkpoint(mf,ms,mfifo,mb,mc);
        Memory restored_mfifo;EEState rs;prepare_direct_elf(mfifo_image,nullptr,rs,restored_mfifo);
        (void)load_checkpoint(mf,rs,restored_mfifo,mc);
        mo.max_instructions=20-prefix;
        require(run_ee_boot(ms,mfifo,nullptr,mo).outcome==BootOutcome::BudgetExhausted &&
            run_ee_boot(rs,restored_mfifo,nullptr,mo).outcome==BootOutcome::BudgetExhausted,
            "continuous and restored MFIFO finish through the production EE scheduler");
        require(checkpoint_state_sha256(ms,mfifo)==checkpoint_state_sha256(rs,restored_mfifo) &&
            mfifo.gs().read(kGsSiglblid+4,4)==0x12345678u,
            "mid-source and mid-drain checkpoint continuations preserve full state and actual GIF output");
    }
    auto image=program();Memory memory;EEState state;prepare_direct_elf(image,nullptr,state,memory);
    memory.diagnostic_data_cache(true);state.set_cp0(Cop0::Config,0x10003);memory.set_cache_config(0x10003);memory.write32(0x80000300u,0x12345678u);
    require(memory.diagnostic_ram(0x300,4).value==0,"checkpoint fixture has a dirty cached word absent from backing RAM");
    memory.store32_observed(0x80000310u,{0xA0000000u,15});
    state.set_reg64_candidates(5,{0x1122u,0x1133u});
    std::vector<std::uint8_t> iop(0x104);
    const std::uint32_t words[]={0x3C01A000u,0x8C220100u,0x1000FFFFu,0};
    for(unsigned n=0;n<4;++n)for(unsigned b=0;b<4;++b)iop[n*4+b]=static_cast<std::uint8_t>(words[n]>>(b*8u));
    iop[0x100]=0x5A;memory.load_iop_program(iop,0,0);
    BootOptions options;options.max_instructions=17;auto first=run_ee_boot(state,memory,nullptr,options);
    require(first.outcome==BootOutcome::BudgetExhausted && state.delay_pending,"checkpoint fixture stops inside a branch delay sequence");
    auto& gs=const_cast<GsRegisters&>(memory.gs());auto& gif=const_cast<Gif&>(memory.gif());
    gs.general_write(0x50,1ull<<48);gs.general_write(0x51,0);gs.general_write(0x52,8u|(1ull<<32));gs.general_write(0x53,0);
    gif.write_fifo(2u|(1ull<<15)|(2ull<<58),0,gs);
    gif.write_fifo(0x2222222211111111ull,0x4444444433333333ull,gs);
    memory.write_vu_vector(1,{{0x40800000u,0},{0,0}});memory.vu_issue_q(state.cycles(),0,1,0,0,true);
    // Use a kuseg address beyond Direct ELF's identity-mapped RAM so this
    // instruction fetch necessarily exercises a guest-programmed TLB entry.
    constexpr auto translated_pc=0x04000000u;
    memory.tlb_write(0,{0u,translated_pc,(0x100u<<6)|0x1Fu,(0x101u<<6)|0x1Fu});
    require(memory.fetch32(translated_pc)==image.instruction_at(image.entry),"checkpoint source has a warmed TLB instruction mapping");
    const auto file=directory/"pending.ps2ncp";CheckpointContext context{sha256_hex(image.bytes),"{\"pad\":65535,\"field\":12}"};
    const auto before=checkpoint_state_sha256(state,memory);save_checkpoint(file,state,memory,first,context);
    require(checkpoint_state_sha256(state,memory)==before,"saving does not advance or mutate the guest");
    const auto info=inspect_checkpoint(file);require(info.retired==17 && info.pc==state.pc && info.host_metadata==context.host_metadata,"checkpoint envelope retains cycle/PC and host script metadata");
    options.max_instructions=80;const auto continuous=run_ee_boot(state,memory,nullptr,options);
    finish_transfer(memory);memory.vu_advance_q(state.cycles());
    const auto expected=checkpoint_state_sha256(state,memory);
#ifdef PS2N_CHECKPOINT_SAVE_ONLY
    std::cout<<"LEGACY_SAVE before="<<before<<" continued="<<expected<<'\n';return 0;
#else
    Memory restored;EEState resumed;prepare_direct_elf(image,nullptr,resumed,restored);
    restored.write32(0x2000u,0x12345678u);
    restored.tlb_write(1,{0u,translated_pc,(2u<<6)|0x1Fu,(3u<<6)|0x1Fu});
    require(restored.fetch32(translated_pc)==0x12345678u,"restore destination warms the same virtual page at a different TLB index");
    const auto loaded=load_checkpoint(file,resumed,restored,context);
    require(restored.fetch32(translated_pc)==image.instruction_at(image.entry),"restore replaces a warmed destination TLB mapping before instruction fetch");
    require(checkpoint_state_sha256(resumed,restored)==before,"all modeled fields round-trip, including dirty cache and symbolic domains");
    require(restored.read32(0x80000300u)==0x12345678u && restored.load32_observed(0x80000310u).unknown==15,
        "restored CPU-visible cache data and unknown bits are retained");
    // The visibility checks above touch LRU state. Restore the unchanged saved
    // state again before the continuous-versus-resumed execution comparison.
    (void)load_checkpoint(file,resumed,restored,context);
    const auto replay=run_ee_boot(resumed,restored,nullptr,options);
    finish_transfer(restored);restored.vu_advance_q(resumed.cycles());
    require(replay.outcome==continuous.outcome && replay.retired==continuous.retired && checkpoint_state_sha256(resumed,restored)==expected,
        "continuous and restored execution have identical complete modeled state");
    auto wrong=context;wrong.scenario_sha256=sha256_hex({});const auto unchanged=checkpoint_state_sha256(resumed,restored);
    rejects([&]{(void)load_checkpoint(file,resumed,restored,wrong);},"scenario identity");
    require(checkpoint_state_sha256(resumed,restored)==unchanged,"identity refusal leaves the destination untouched");
    auto bytes=read_file(file);bytes.back()^=1;const auto damaged=directory/"damaged.ps2ncp";write_file_atomically(damaged,bytes);
    rejects([&]{(void)load_checkpoint(damaged,resumed,restored,context);},"checksum");
    BootReport refused;refused.outcome=BootOutcome::BlockPoint;refused.pc=resumed.pc;
    rejects([&]{save_checkpoint(file,resumed,restored,refused,context);},"partial instructions");
    require(inspect_checkpoint(file).payload_sha256==loaded.payload_sha256,"a refused save cannot overwrite the previous safe checkpoint");
    Memory failed_iop_memory;EEState failed_iop_state;prepare_direct_elf(image,nullptr,failed_iop_state,failed_iop_memory);
    failed_iop_memory.write32(image.entry,0x1000FFFFu);failed_iop_memory.write32(image.entry+4u,0u);
    const std::array<std::uint8_t,4> unsupported_iop{0xFFu,0xFFu,0xFFu,0xFFu};
    failed_iop_memory.load_iop_program(unsupported_iop,0x100u,0x100u);
    BootOptions failed_iop_options;failed_iop_options.max_instructions=32;failed_iop_options.tail_instructions=0;
    const auto ee_boundary=run_ee_boot(failed_iop_state,failed_iop_memory,nullptr,failed_iop_options);
    require(ee_boundary.outcome==BootOutcome::BudgetExhausted && ee_boundary.iop_stop,
        "fixture isolates a refused IOP instruction while the independent EE still reaches its budget");
    rejects([&]{save_checkpoint(file,failed_iop_state,failed_iop_memory,ee_boundary,context);},"IOP refusal");
    require(inspect_checkpoint(file).payload_sha256==loaded.payload_sha256,"an IOP refusal cannot replace the previous successful full-system checkpoint");
    // Derive a legacy-format fixture independently through the public API:
    // toggling the host experiment identifies its sole serialized byte, then
    // the two newly introduced booleans are omitted from an old envelope.
    const auto current_file=directory/"migration-current.ps2ncp",flag_file=directory/"migration-flag.ps2ncp";
    save_checkpoint(current_file,state,memory,continuous,context);const auto current_info=inspect_checkpoint(current_file);
    refine_checkpoint_scalar_carry(state,memory);
    auto flag_context=context;auto flag_identity=image.bytes;flag_identity.push_back(1);flag_context.scenario_sha256=sha256_hex(flag_identity);
    save_checkpoint(flag_file,state,memory,continuous,flag_context);
    const auto current_bytes=read_file(current_file),flag_bytes=read_file(flag_file);
    const auto current_payload=std::span<const std::uint8_t>(current_bytes).last(static_cast<std::size_t>(current_info.payload_bytes));
    const auto flag_payload=std::span<const std::uint8_t>(flag_bytes).last(static_cast<std::size_t>(inspect_checkpoint(flag_file).payload_bytes));
    std::size_t changed=0,flag_offset=0;
    require(current_payload.size()==flag_payload.size(),"scalar flag does not change serialized lengths");
    for(std::size_t i=0;i<current_payload.size();++i)if(current_payload[i]!=flag_payload[i]){++changed;flag_offset=i;}
    require(changed==1 && current_payload[flag_offset]==0 && flag_payload[flag_offset]==1 && current_payload[flag_offset+1]==0,
        "only the explicit scalar-experiment boolean changed");
    std::vector<std::uint8_t> legacy_payload(current_payload.begin(),current_payload.end());
    const std::array<std::uint8_t,8> st_tag{'G','S','S','T','U','N','K','1'};
    const auto st_at=std::search(legacy_payload.begin(),legacy_payload.end(),st_tag.begin(),st_tag.end());
    require(st_at!=legacy_payload.end(),"current checkpoint retains tagged per-vertex ST uncertainty");
    legacy_payload.erase(st_at,st_at+32);
    // Omit the tagged numerical SPU2 extension; these stopped voices have
    // no optional loop addresses. Locate the tag, never a host struct offset.
    const std::array<std::uint8_t,8> audio_tag{'S','P','U','2','P','C','M','1'};
    const auto audio_at=std::search(legacy_payload.begin(),legacy_payload.end(),audio_tag.begin(),audio_tag.end());
    require(audio_at!=legacy_payload.end(),"current checkpoint has a tagged SPU2 extension");
    constexpr auto audio_bytes=8u+48u*(3u+16u+12u+1u+64u+40u)+2u;
    legacy_payload.erase(audio_at,audio_at+audio_bytes);
    const auto pre_audio_payload=legacy_payload;
    legacy_payload.erase(legacy_payload.begin()+flag_offset,legacy_payload.begin()+flag_offset+2);
    const auto legacy_file=directory/"legacy-scalar.ps2ncp";write_file_atomically(legacy_file,legacy_envelope(current_info,legacy_payload));
    (void)load_checkpoint(current_file,state,memory,context);
    const auto exact=checkpoint_state_sha256(state,memory);
    auto reviewed=context;reviewed.allow_runtime_change=true;
    for(const auto* schema:{"50585f2224cd852e9ef36ef63b53e10fe41691975b7c38fe44dd5384280d08cc",
                               "11a100b3f0fda71733f6772b12b4d4748ef162dc10fd182c2b64cf6fe2dd1e0b",
                           "db30806fe4f74e4781e737979c2ad78fd6bac2b17383aac1ff4fc3c9b286f9f1"}) {
        const auto previous=directory/"before-ipu-bit-reader.ps2ncp";
        write_file_atomically(previous,legacy_envelope(current_info,current_payload,schema,true,true));
        rejects([&]{(void)load_checkpoint(previous,resumed,restored,context);},"schema");
        (void)load_checkpoint(previous,resumed,restored,reviewed);
        require(checkpoint_state_sha256(resumed,restored)==exact,"audited IPU helper transition preserves every checkpoint byte");
    }
    const auto unknown_schema=directory/"unreviewed-schema.ps2ncp";
    write_file_atomically(unknown_schema,legacy_envelope(current_info,current_payload,
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",true));
    const auto before_unknown=checkpoint_state_sha256(resumed,restored);
    rejects([&]{(void)load_checkpoint(unknown_schema,resumed,restored,reviewed);},"schema");
    require(checkpoint_state_sha256(resumed,restored)==before_unknown,"runtime opt-in cannot admit an unreviewed schema or mutate the destination");
    const auto same_bus_file=directory/"same-graphics-bus-fields.ps2ncp";
    constexpr auto bus_schema="267777539ec7b7df428a6c552962a9b5cffeb8a3b539cdd175bebdff921c79c8";
    write_file_atomically(same_bus_file,legacy_envelope(current_info,current_payload,bus_schema,true));
    if(current_info.schema_sha256!=bus_schema)
        rejects([&]{(void)load_checkpoint(same_bus_file,resumed,restored,context);},"schema");
    (void)load_checkpoint(same_bus_file,resumed,restored,reviewed);
    require(checkpoint_state_sha256(resumed,restored)==exact,"execution-only VU helper changes retain every serialized field");
    const auto bus_file=directory/"legacy-graphics-bus.ps2ncp";
    write_file_atomically(bus_file,legacy_envelope(current_info,current_payload,"25e615e5098e581ee91b9dd84ffe9782be7b73f47ebfa76e377caa06fa871ffd"));
    (void)load_checkpoint(bus_file,resumed,restored,reviewed);
    require(checkpoint_state_sha256(resumed,restored)==exact,"pre-arbitration checkpoints retain all interrupt and numeric state");
    const auto irq_file=directory/"legacy-vif-irq.ps2ncp";
    write_file_atomically(irq_file,legacy_envelope(current_info,current_payload,"fa9cf50ce0d0fade6cc76b98bd076cdc6fad4e6bd5db34d76b2dfbe586011827"));
    rejects([&]{(void)load_checkpoint(irq_file,resumed,restored,context);},"schema");
    (void)load_checkpoint(irq_file,resumed,restored,reviewed);
    require(checkpoint_state_sha256(resumed,restored)==exact,"pre-IRQ checkpoints preserve existing DMA numeric metadata and every guest field");
    const auto transfer_file=directory/"legacy-transfer-bounds.ps2ncp";
    write_file_atomically(transfer_file,legacy_envelope(current_info,current_payload,"b36ce4ca5c0bf510d302b10b002b27d736cb1fbc4e258e8a2b8961950e2d18e3"));
    rejects([&]{(void)load_checkpoint(transfer_file,resumed,restored,context);},"schema");
    (void)load_checkpoint(transfer_file,resumed,restored,reviewed);
    require(checkpoint_state_sha256(resumed,restored)==exact,"older byte-preserving DMA checkpoints migrate without adding numeric certainty");
    const auto vif_file=directory/"legacy-vif-payload.ps2ncp";
    write_file_atomically(vif_file,legacy_envelope(current_info,current_payload,"fe6e8c07cc00203aa30906e155d9511c731237aa3919857a36a7098ea4409051"));
    rejects([&]{(void)load_checkpoint(vif_file,resumed,restored,context);},"schema");
    (void)load_checkpoint(vif_file,resumed,restored,reviewed);
    require(checkpoint_state_sha256(resumed,restored)==exact,"older known VIF words migrate without inventing or discarding data");
    const auto voice_file=directory/"legacy-voice-readback.ps2ncp";
    write_file_atomically(voice_file,legacy_envelope(current_info,current_payload,"5662ca9c89ac26516224d7aa8f3e777e8d72ec24255364cfedd3481e13db1882"));
    rejects([&]{(void)load_checkpoint(voice_file,resumed,restored,context);},"schema");
    (void)load_checkpoint(voice_file,resumed,restored,reviewed);
    require(checkpoint_state_sha256(resumed,restored)==exact,"old fromSPR checkpoints retain state and keep voice readback experiment disabled");
    for(const auto* schema:{"72d8f630ea9a0befdba0e33bbc9d80394b6a2c29db31cf0a402bd6299876a61e",
            "cd1aa8bf7755b3926af94afc8385147b755b5de6b335faa25049d9f719960ecd",
            "3613da6d23479169c5d84e8eea6a68ed75b7acc10e86c5c324238a5cbccf495f",
            "06f3fe6fbdb88d6864fb5e3b18139b95e13d22312976051c0cbcd9406931d695"}) {
        const auto decoder_file=directory/"decoder-transition.ps2ncp";
        write_file_atomically(decoder_file,legacy_envelope(current_info,pre_audio_payload,schema));
        rejects([&]{(void)load_checkpoint(decoder_file,resumed,restored,context);},"schema");
        (void)load_checkpoint(decoder_file,resumed,restored,reviewed);
        require(checkpoint_state_sha256(resumed,restored)==exact,"audited decoder-only transitions preserve every serialized field");
    }
    require(!memory.begin_fpu_arithmetic(),"baseline scalar experiment remains disabled");
    const auto conservative=checkpoint_state_sha256(state,memory);
    rejects([&]{(void)load_checkpoint(legacy_file,resumed,restored,context);},"schema");
    (void)load_checkpoint(legacy_file,resumed,restored,reviewed);
    require(checkpoint_state_sha256(resumed,restored)==conservative,"legacy migration preserves every old field and conservatively closes the scalar configuration window");
    rejects([&]{restored.diagnostic_ee_early_digit_carry();},"must precede");
    options.max_instructions=32;(void)run_ee_boot(state,memory,nullptr,options);(void)run_ee_boot(resumed,restored,nullptr,options);
    require(checkpoint_state_sha256(state,memory)==checkpoint_state_sha256(resumed,restored),"migrated and current checkpoints continue with identical complete state");
    const auto strict=checkpoint_state_sha256(state,memory);
    rejects([&]{refine_checkpoint_texture_precision(state,memory,512);},"additive");
    rejects([&]{refine_checkpoint_texture_precision(state,memory,128);},"before guest execution");
    require(checkpoint_state_sha256(state,memory)==strict,"invalid texture refinement cannot mutate a checkpoint");
    refine_checkpoint_texture_precision(state,memory,4);
    refine_checkpoint_texture_precision(state,memory,5);
    rejects([&]{refine_checkpoint_texture_precision(state,memory,133);},"before guest execution");
    refine_checkpoint_texture_precision(state,memory,21);
    const auto refined=checkpoint_state_sha256(state,memory);
    rejects([&]{refine_checkpoint_texture_precision(state,memory,1);},"additive");
    require(refined!=strict && checkpoint_state_sha256(state,memory)==refined,"explicit refinement adds hypotheses without removing prior modes or touching rejected state");
    {
    options.max_instructions=1;const auto st_boundary=run_ee_boot(state,memory,nullptr,options);
    require(st_boundary.outcome==BootOutcome::BudgetExhausted,"ST checkpoint fixture reaches a fresh successful CPU boundary");
    auto& gs=const_cast<GsRegisters&>(memory.gs());
    gs.general_write(0,0x16);gs.general_write(1,0x4000000080808080ull);
    gs.general_write(0x18,0);gs.general_write(0x1A,1);gs.general_write(0x4C,1u<<16);
    gs.general_write(0x40,(7ull<<16)|(7ull<<48));gs.general_write(0x47,3u<<16);
    gs.general_write(0x4E,1ull<<32);gs.general_write(0x22,0);gs.general_write(0x4A,0);
    gs.general_write(6,32ull|(1ull<<14)|(3ull<<26)|(3ull<<30)|(1ull<<34)|(1ull<<35));
    gs.general_write(8,0);gs.general_write(0x14,0);
    gs.general_write_observed(2,{0,0x100});gs.general_write(5,0);
    gs.general_write(2,0x3EC000003EC00000ull);
    const auto st_file=directory/"st-vertex.ps2ncp";save_checkpoint(st_file,state,memory,st_boundary,context);
    const auto st_hash=checkpoint_state_sha256(state,memory);
    (void)load_checkpoint(st_file,resumed,restored,context);
    require(checkpoint_state_sha256(resumed,restored)==st_hash,"checkpoint retains old ST uncertainty after a known latch overwrite");
    rejects([&]{const_cast<GsRegisters&>(restored.gs()).general_write(5,16u|(16ull<<16));},"unknown coordinate bits");
    const auto st_info=inspect_checkpoint(st_file);const auto st_bytes=read_file(st_file);
    std::vector<std::uint8_t> pre_st(st_bytes.end()-st_info.payload_bytes,st_bytes.end());
    const auto extension=std::search(pre_st.begin(),pre_st.end(),st_tag.begin(),st_tag.end());
    require(extension!=pre_st.end(),"synthetic ST checkpoint has a tagged extension");pre_st.erase(extension,extension+32);
    const auto pre_st_file=directory/"legacy-st-vertex.ps2ncp";
    write_file_atomically(pre_st_file,legacy_envelope(st_info,pre_st,"4d32f9c6e62746ff39398d75d0efb1ae80a7a653a7abb8fb4f4c3a6933ede047"));
    rejects([&]{(void)load_checkpoint(pre_st_file,resumed,restored,context);},"schema");
    (void)load_checkpoint(pre_st_file,resumed,restored,reviewed);
    rejects([&]{const_cast<GsRegisters&>(restored.gs()).general_write(5,16u|(16ull<<16));},"unknown coordinate bits");
    }
    std::cout<<"Complete checkpoint, immutable identities, transactional refusals and continuous/resumed state equivalence passed\n";return 0;
#endif
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
