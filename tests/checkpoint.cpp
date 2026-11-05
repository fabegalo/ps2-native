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
int main(int argc,char** argv)try {
    if(argc!=2 && argc!=4)return 2;const auto directory=std::filesystem::path(argv[1]);ps2native::create_directories(directory);
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
    const auto loaded=load_checkpoint(file,resumed,restored,context);
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
    if(argc==4 && std::string_view(argv[2])=="--legacy") {
        auto reviewed=context;reviewed.allow_runtime_change=true;
        (void)load_checkpoint(argv[3],resumed,restored,reviewed);
        require(checkpoint_state_sha256(resumed,restored)==before,"verified legacy writer and current reader encode identical modeled fields");
        (void)run_ee_boot(resumed,restored,nullptr,options);
        finish_transfer(restored);restored.vu_advance_q(resumed.cycles());
        require(checkpoint_state_sha256(resumed,restored)==expected,"legacy save/current restore continues with identical complete state");
        std::cout<<"Verified cross-layout save/restore equivalence passed\n";
    }
    std::cout<<"Complete checkpoint, immutable identities, transactional refusals and continuous/resumed state equivalence passed\n";return 0;
#endif
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
