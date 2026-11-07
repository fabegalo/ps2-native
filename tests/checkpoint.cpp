#include "boot_checks.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/sha256.hpp"
#include "ps2native/io.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
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
std::vector<std::uint8_t> legacy_envelope(CheckpointInfo info,std::span<const std::uint8_t> payload,
    const char* schema="771b628039a2b4c73a7c836b022c56d167cf8cd22310d8fe75803cb7934d3f88") {
    std::vector<std::uint8_t> bytes{'P','S','2','N','C','P','0','1'};
    const auto integer=[&](std::uint64_t value,unsigned size){for(unsigned i=0;i<size;++i)bytes.push_back(static_cast<std::uint8_t>(value>>(8u*i)));};
    info.schema_sha256=schema;
    info.payload_sha256=sha256_hex(payload);info.payload_bytes=payload.size();
    for(const auto* value:{&info.schema_sha256,&info.runtime_sha256,&info.bindings_sha256,&info.scenario_sha256,&info.host_metadata,&info.payload_sha256}) {
        integer(value->size(),8);bytes.insert(bytes.end(),value->begin(),value->end());
    }
    integer(info.retired,8);integer(info.cycles,8);integer(info.payload_bytes,8);integer(info.pc,4);
    bytes.insert(bytes.end(),payload.begin(),payload.end());return bytes;
}
int main(int argc,char** argv)try {
    if(argc!=2)return 2;const auto directory=std::filesystem::path(argv[1]);ps2native::create_directories(directory);
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
    rejects([&]{refine_checkpoint_texture_precision(state,memory,64);},"additive");
    require(checkpoint_state_sha256(state,memory)==strict,"invalid texture refinement cannot mutate a checkpoint");
    refine_checkpoint_texture_precision(state,memory,4);
    refine_checkpoint_texture_precision(state,memory,5);
    rejects([&]{refine_checkpoint_texture_precision(state,memory,69);},"additive");
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
