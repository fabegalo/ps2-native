// Original deterministic SPU2 fixtures; all samples reach RAM via real PIO.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include "ps2native/audio.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
std::vector<std::uint8_t> firmware(unsigned mode,unsigned core=0) {
    m20::Assembler a(kEeResetVector);
    const auto sh=[&](unsigned address,unsigned value){a.la(8,address);a.la(9,value);a.sh(9,0,8);};
    for(unsigned c=0;c<2;++c)sh(0xBF90019Au+c*0x400u,0);
    const auto base=0xBF900000u+core*0x400u;
    sh(base+0x1A8u,0);sh(base+0x1AAu,0x2800u);
    // Two ADPCM blocks: positive samples followed by negative samples,
    // auto loop start and repeat, or an actual end+mute for mode 1.
    const auto predictor=mode>=8u && mode<=12u ? (mode-7u)<<4u : 0u;
    sh(base+0x1ACu,0x0404u|predictor);
    for(unsigned i=0;i<7;++i)sh(base+0x1ACu,0x7777u);
    sh(base+0x1ACu,(mode==1 ? 0x0104u : 0x0304u)|predictor);
    for(unsigned i=0;i<7;++i)sh(base+0x1ACu,0x9999u);
    sh(base+0x19Au,0x8010u);
    for(unsigned i=0;i<40;++i)a.nop(); // drain the actual FIFO
    sh(base+0x19Au,0x8000u);
    for(unsigned reg=0x180;reg<0x198;reg+=2)sh(base+reg,(reg==0x188 || reg==0x190)?1:0);
    sh(base+0x198u,0xC00u);
    sh(0xBF900760u+core*0x28u,0x3FFFu);sh(0xBF900762u+core*0x28u,0x3FFFu);
    sh(base,0x3FFFu);sh(base+2u,mode==2?0x4000u:0x3FFFu);
    sh(base+4u,mode==3?0u:mode==4?0x0800u:0x1000u);
    sh(base+6u,mode==13u ? 0x800Fu : 0x000Fu);sh(base+8u,0x1FC0u); // fast attack, infinite sustain, fast release
    sh(base+0x1C0u,0);sh(base+0x1C2u,mode==5?0x3800u:0x2800u);
    if(mode==6 || mode==14) { // comparator of the other core sees this core's header
        const auto other=0xBF900000u+(core^1u)*0x400u;
        sh(other+0x19Cu,0);sh(other+0x19Eu,mode==14?0x3800u:0x2800u);sh(other+0x19Au,0x40u);
    }
    sh(base+0x1A0u,1);
    a.label("wait");for(unsigned i=0;i<3200;++i)a.nop();
    if(mode==6){a.la(8,0xBF801070u);a.lw(9,0,8);a.nop();a.andi(9,9,0x200u);a.la(10,0xA0000100u);a.sw(9,0,10);}
    if(mode==7)sh(base+0x1A4u,1);
    if(mode==0 || mode==1) {
        for(unsigned i=0;i<768u*60u;++i)a.nop();
        a.la(10,0xA0000100u);a.la(8,base+0x340u);a.lhu(9,0,8);a.nop();a.andi(9,9,1);a.sw(9,0,10);
        a.la(8,base+0xAu);a.lhu(9,0,8);a.nop();a.sw(9,4,10);
        a.la(8,base+0x1C6u);a.lhu(9,0,8);a.nop();a.sw(9,8,10);
        a.la(8,base+0x1CAu);a.lhu(9,0,8);a.nop();a.sw(9,12,10);
    }
    if(mode==15){a.la(8,base+0x1C8u);a.lhu(9,0,8);a.nop();a.beq(9,0,"idle");a.nop();a.raw(0x0000000Du);}
    a.label("idle");a.beq(0,0,"idle");a.nop();
    auto rom=m20::build_firmware().rom;const auto code=m20::le_words(a.finish());
    std::copy(code.begin(),code.end(),rom.begin());return rom;
}
void reset(Iop& i,const std::vector<std::uint8_t>& rom) {
    i.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);
    i.diagnostic_spu2_pio_word_cycles(1);
}
}
int main(int argc,char** argv)try {
    require(argc==2,"test output directory required");
    const auto directory=std::filesystem::path(argv[1]);ps2native::create_directories(directory);
    for(unsigned core=0;core<2;++core)for(unsigned mode=0;mode<15;++mode) {
        Iop i;const auto rom=firmware(mode,core);reset(i,rom);
        std::vector<Iop::Spu2Sample> samples;
        i.set_spu2_sample_sink([&](unsigned c,const auto& s){if(c==core)samples.push_back(s);});
        i.run_until(767);require(!i.stopped() && !i.spu2_voice(core,0).modeled,"KON waits for the synthesis edge");
        i.run_until(768);
        if(mode==5){require(i.stopped() && i.stop_reason()->find("unknown sound RAM")!=std::string::npos,"unwritten samples refuse instead of returning silence");continue;}
        if(mode==12){require(i.stopped() && i.stop_reason()->find("reserved predictor")!=std::string::npos,"reserved ADPCM predictors fail with their cause");continue;}
        if(mode==14){require(i.stopped() && i.stop_reason()->find("unmodeled stopped decoder")!=std::string::npos,"unknown stopped decoder traffic cannot fabricate an absent IRQ");continue;}
        require(!i.stopped() && i.spu2_voice(core,0).key_ons==1 && i.spu2_voice(core,0).decoded_samples==4 &&
                i.spu2_voice(core,0).next==0x2802u,"KON decodes actual first four nibbles and advances NAX at 768");
        require(i.spu2_voice(core,0).decoded[0]==1792 && i.spu2_voice(core,0).envelope==14336,
                "ADPCM residual and linear ADSR attack match independent integer vectors");
        if(mode>=8 && mode<=11) {
            constexpr int second[]={3472,5012,4536,5208};
            require(i.spu2_voice(core,0).decoded[1]==second[mode-8],"all four predictive ADPCM filters match independent two-sample vectors");
        }
        i.run_until(768u*80u);
        require(!i.stopped() && samples.size()==80,"one stereo sample per exact IOP edge, core="+std::to_string(core)+
            " mode="+std::to_string(mode)+" frames="+std::to_string(samples.size())+" stop="+i.stop_reason().value_or("none"));
        if(mode==6)require(i.ee_read(0x100,4)&0x200u,"other core's IRQA causes real IOP IRQ9");
        if(mode==0 || mode==1)require(i.ee_read(0x100,4)==1 && i.ee_read(0x104,4)==(mode==1?0:16383) &&
            i.ee_read(0x108,4)==0x2800u && i.ee_read(0x10C,4)>=0x2801u,"guest ENDX/ENVX/LSAX/NAX read actual decoder and envelope state");
        require(samples[0].left!=0 && (mode==2?samples[0].right<0:samples[0].right==samples[0].left),"signed stereo gains apply to synthesized PCM");
        const auto& v=i.spu2_voice(core,0);
        if(mode==1 || mode==7)require(v.phase==0 && v.envelope==0 && samples.back().left==0,"end+mute and KOFF release stop the actual envelope");
        else require(v.phase==3 && v.envelope==16383,"attack/decay reach sustain deterministically (one fast decay step)");
        if(mode==3)require(v.next==0x2805u && v.read==0,"zero pitch retains samples but fills the bounded decoder FIFO");
        else require(v.loop==0x2800u && v.decoded_samples>28,"ADPCM loop addresses and repeated decoding evolve");
        if(mode==4)require(v.read==40,"half pitch consumes exactly forty source samples in eighty edges");
        if(!mode && !core) {
            const auto wav=directory/"synthesis.wav";if(file_exists(wav))remove_file(wav);
            save_spu2_wav(wav,samples);const auto bytes=read_file(wav);
            require(bytes.size()==44u+80u*4u && std::string(bytes.begin(),bytes.begin()+4)=="RIFF","export contains real stereo PCM with the exact frame count");
            rejects([&]{save_spu2_wav(wav,samples);},"already exists");
            auto gap=samples;gap[2].cycle+=1;rejects([&]{save_spu2_wav(directory/"gap.wav",gap);},"contiguous");
        }
    }
    for(unsigned core=0;core<2;++core){
        const auto rom=firmware(15,core);Iop strict;reset(strict,rom);strict.run_until(10000);
        require(strict.stopped() && strict.stop_reason()->find("voice address reserved bits")!=std::string::npos,"strict high-address readback retains reserved-bit uncertainty");
        Iop explicit_zero;reset(explicit_zero,rom);explicit_zero.diagnostic_spu2_voice_address_reserved_zero();explicit_zero.run_until(10000);
        require(!explicit_zero.stopped() && explicit_zero.spu2_voice(core,0).decoded_samples>4,"explicit readback policy preserves actual voice decoding and permits NAX high comparison");
    }
    {
        const auto rom=firmware(0);EEState state;Memory memory;
        const auto prepare=[&](EEState& s,Memory& m){
            s=ee_reset_state();DiagnosticAssumptions assumptions;assumptions.spu2_quiescent_reset=true;assumptions.spu2_pio_word_cycles=1;
            m.reset_for_boot(rom,nullptr,nullptr,&assumptions);m.set_clock(&s.retired_instructions,&s.stall_cycles);
            m.write32(0x80001000u,0x1000FFFFu);m.write32(0x80001004u,0);s.pc=0x80001000u;
        };
        prepare(state,memory);
        BootOptions options;options.max_instructions=13000;
        const auto first=run_ee_boot(state,memory,nullptr,options);memory.sync_iop();
        require(!memory.iop().stopped() && memory.iop().spu2_voice(0,0).modeled,"full-system checkpoint fixture has an actual pending decoder/envelope");
        const auto file=directory/"active.ps2ncp";CheckpointContext context{sha256_hex(rom),"active-spu2"};
        if(file_exists(file))remove_file(file); // this test's own synthetic artifact
        memory.diagnostic_spu2_voice_address_reserved_zero();
        save_checkpoint(file,state,memory,first,context);const auto before=checkpoint_state_sha256(state,memory);
        std::vector<Iop::Spu2Sample> continuous;
        const_cast<Iop&>(memory.iop()).set_spu2_sample_sink([&](unsigned core,const auto& sample){if(!core)continuous.push_back(sample);});
        options.max_instructions=100000;const auto report=run_ee_boot(state,memory,nullptr,options);memory.sync_iop();
        const auto expected=checkpoint_state_sha256(state,memory);
        EEState resumed;Memory restored;prepare(resumed,restored);
        (void)load_checkpoint(file,resumed,restored,context);
        require(checkpoint_state_sha256(resumed,restored)==before,"active sound RAM, FIFO, pitch, predictor, NAX and envelope round-trip");
        std::vector<Iop::Spu2Sample> replay;
        const_cast<Iop&>(restored.iop()).set_spu2_sample_sink([&](unsigned core,const auto& sample){if(!core)replay.push_back(sample);});
        const auto again=run_ee_boot(resumed,restored,nullptr,options);restored.sync_iop();
        require(report.outcome==again.outcome && checkpoint_state_sha256(resumed,restored)==expected && replay.size()==continuous.size(),"continuous/restored full-system audio state is identical");
        require(!replay.empty(),"checkpoint replay produces real clocked samples");
        for(std::size_t n=0;n<replay.size();++n)require(replay[n].cycle==continuous[n].cycle && replay[n].left==continuous[n].left && replay[n].right==continuous[n].right,"PCM after restore is bit-identical");
    }
    std::cout<<"SPU2 real PIO/ADPCM, signed mix, pitch, ADSR, loop, release, shared IRQ and WAV passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
