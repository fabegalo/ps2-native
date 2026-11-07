// End-to-end generated native code, checked against the shared interpreter.
#include "boot_checks.hpp"
#include "vu_aot_fixture.hpp"
#include "ps2native/vu_aot.hpp"
#include "ps2native/aot_runtime.hpp"
#include "ps2native/boot.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/sha256.hpp"
#include <filesystem>
#include <iostream>
#include <memory>

using namespace ps2native;
using namespace boot_checks;
namespace {
void analysis_contract(const std::filesystem::path& directory) {
    const auto raw=vu_aot_fixture::micro_image();const auto image=parse_vu1_aot_image(raw);
    const auto id=vu1_aot_identity(image);const auto capture=encode_vu1_aot_image(image);
    require(vu1_aot_identity(parse_vu1_aot_image(capture))==id,"lossless capture/raw identity is deterministic");
    auto hole=image;hole.words[4000]={0,~0u};
    require(vu1_aot_identity(hole)!=id && vu1_aot_identity(parse_vu1_aot_image(encode_vu1_aot_image(hole)))==vu1_aot_identity(hole),
        "unwritten bits are part of the exact identity and survive capture");
    require(vu1_aot_identity(image,true)!=id,"explicit numeric mode is part of translation identity");
    auto malformed=capture;malformed[12]=1;malformed[8]|=1;
    rejects([&]{(void)parse_vu1_aot_image(malformed);},"unknown bits");
    rejects([&]{(void)parse_vu1_aot_image(std::span<const std::uint8_t>(raw).first(16));},"16 KiB");
    VuAotOptions options;options.entries={0,512,1024,2048,16376};const auto graph=analyze_vu1_aot(image,options);
    require(graph.instructions.at(0).writes_acc && graph.instructions.at(8).reads_acc && graph.instructions.at(24).writes_q &&
        graph.instructions.at(32).reads_q,"CFG exposes shared vector/ACC/Q dependencies");
    require(graph.instructions.contains(128u*8u) && !graph.instructions.contains(129u*8u) && !graph.instructions.contains(140u*8u) &&
        std::any_of(graph.boundaries.begin(),graph.boundaries.end(),[](const auto& entry){return entry.reason.find("indirect")!=std::string::npos;}),
        "indirect control flow remains an explicit proof boundary");
    auto unsupported=image;unsupported.words[1]={0x800002FFu,0};
    require(analyze_vu1_aot(unsupported).instructions.empty(),"unmodeled I control cannot become a fake native instruction");
    options.entries={4};rejects([&]{(void)analyze_vu1_aot(image,options);},"aligned byte PC");
    options.entries={0,512,1024,2048,16376};const auto split=generate_vu1_aot_sources(image,options,"vu_test",1024);
    const auto again=generate_vu1_aot_sources(image,options,"vu_test",1024);
    require(split.dispatcher==again.dispatcher && split.partitions.size()==again.partitions.size() && split.partitions.size()>=3,
        "stable partitions use the shared AOT source/cache infrastructure");
    for(std::size_t index=0;index<split.partitions.size();++index)require(split.partitions[index].name==again.partitions[index].name &&
        split.partitions[index].text==again.partitions[index].text,"partition content names are deterministic");
    const auto source=directory/"vu-split.cpp";
    (void)write_aot_sources(source,split);
    const auto manifest=aot_sources_sha256(source);
    require(!write_aot_sources(source,split) && aot_sources_sha256(source)==manifest,"unchanged source cache is reused without touching files");
    (void)write_text_if_changed(source,"// corrupted dispatcher\n");
    rejects([&]{(void)aot_sources_sha256(source);},"identity mismatch");
    (void)write_aot_sources(source,split);
}
void same_word(ObservedWord a,ObservedWord b,const char* field) {
    require(a.value==b.value && a.unknown==b.unknown,std::string("native/interpreter ")+field);
}
void compare(const VuControl& a,const VuControl& b,const Gif& ga,const Gif& gb,const GsRegisters& sa,const GsRegisters& sb) {
    require(a.micro_running(1)==b.micro_running(1) && a.diagnostic_micro_pc(1)==b.diagnostic_micro_pc(1) &&
        a.diagnostic_micro_instructions(1)==b.diagnostic_micro_instructions(1) && a.next_micro_cycle()==b.next_micro_cycle(),
        "VU issue count, PC, cycles, branch/end delay and pipeline drain");
    for(unsigned at=0x4000u;at<0x4300u;at+=4u)same_word(a.observe_data(at),b.observe_data(at),"VF/VI");
    for(unsigned index:{0u,1u,2u,4u,5u,6u,7u,10u})same_word(a.observe_data(0x4300u+index*16u),b.observe_data(0x4300u+index*16u),"flags/I/Q/P");
    same_word(a.observe(29),b.observe(29),"VPU status");
    for(unsigned at=0;at<16384u;at+=8u) {
        const auto left=a.read_memory(0x1100C000u+at,8),right=b.read_memory(0x1100C000u+at,8);
        require(left.value==right.value && left.unknown==right.unknown,"VU data and unknown masks");
    }
    require(ga.read_stat()==gb.read_stat() && ga.packets()==gb.packets() &&
        a.path1_transfer_pending()==b.path1_transfer_pending() && sa.vram().pixel_writes()==sb.vram().pixel_writes(),
        "XGKICK, independent PATH1 bus beats and GIF/GS effects");
}
struct Device {
    VuControl vu;Vif vif;Gif gif;GsRegisters gs;
    explicit Device(bool native) {
        gs.reset(0);vu.enable_vu1_aot(native);vu_aot_fixture::upload(vu,vif,gif,gs);vu_aot_fixture::seed(vu);
        vu_aot_fixture::seed_graphics(vu,gs);
    }
};
void equivalence(unsigned entry,bool dynamic=false) {
    Device interpreted(false),native(true);
    interpreted.vu.start_micro(1,entry,0,{31,0},{17,0});native.vu.start_micro(1,entry,0,{31,0},{17,0});
    for(unsigned cycle=0;cycle<=80u;++cycle) {
        interpreted.vu.advance_micro(cycle,&interpreted.gif,&interpreted.gs);native.vu.advance_micro(cycle,&native.gif,&native.gs);
        compare(interpreted.vu,native.vu,interpreted.gif,native.gif,interpreted.gs,native.gs);
    }
    require(!native.vu.micro_running(1),"fixture reaches real E completion");
    const auto stats=native.vu.vu1_aot_stats();
    require(stats.native_instructions>0,"the generated compiled translation actually retired native LIWs");
    require(dynamic ? stats.interpreted_instructions>0 && stats.untranslated_pc>0 : stats.interpreted_instructions==0,
        "proven PCs are native and the dynamic target falls back explicitly; entry="+std::to_string(entry)+
        " native="+std::to_string(stats.native_instructions)+" interpreted="+std::to_string(stats.interpreted_instructions)+
        " missing="+std::to_string(stats.missing_program)+" untranslated="+std::to_string(stats.untranslated_pc));
    if(entry==vu_aot_fixture::math_entry) {
        require(native.vu.observe_data(0x4320u).value==0xF37BC0u && !native.vu.observe_data(0x4320u).unknown,"compiled FCSET and CLIP publish the independent 24-bit expected history");
        require(native.vu.read_memory(0x1100C03Cu,4).value==1 && !native.vu.read_memory(0x1100C03Cu,4).unknown,"native ISW stores a real zero-extended integer through its writeback");
        require(native.vu.observe_data(0x4210u).value==1 && !native.vu.observe_data(0x4210u).unknown,"compiled FCAND derives the real clipping predicate");
        require(native.vu.observe_data(0x4280).value==7,"taken branch executes the real delay pair and skips untaken storage");
        require(!native.vu.observe_data(0x4360).unknown && native.vu.observe_data(0x4360).value==0x3F000000u,
            "DIV, WAITQ, latency and paired upper consume the actual Q");
        require(native.vu.read_memory(0x1100C020,4).value==128,"FTOI and interlocked SQ write the real computed word");
    }
    if(entry==vu_aot_fixture::wrap_entry)require(native.vu.observe_data(0x42F0).value==1,"BAL wrapped link and branch delay");
    if(entry==vu_aot_fixture::draw_entry || entry==vu_aot_fixture::wrap_entry) {
        const auto left=interpreted.gs.scanout(80),right=native.gs.scanout(80);
        require(left.rgba==right.rgba && right.width==8 && right.height==8 && right.rgba.size()==256,
            "VU-produced sprite yields identical complete RGBA scanout");
        for(unsigned pixel=0;pixel<64;++pixel)require(right.rgba[pixel*4u]==0x10 && right.rgba[pixel*4u+1u]==0x20 &&
            right.rgba[pixel*4u+2u]==0x40 && right.rgba[pixel*4u+3u]==255,"actual covered sprite pixel");
    }
}
void invalidation() {
    Device interpreted(false),native(true);
    interpreted.vu.start_micro(1,0,0);native.vu.start_micro(1,0,0);
    interpreted.vu.advance_micro(1);native.vu.advance_micro(1);
    require(native.vu.vu1_aot_stats().native_instructions==1,"native pair executed before executable mutation");
    const auto patch=vu_aot_fixture::addiu(11,0,123);
    for(auto* device:{&interpreted,&native})device->vu.write_memory(0x11008008u,4,{patch,0});
    for(unsigned cycle=2;cycle<=80;++cycle) {
        interpreted.vu.advance_micro(cycle,&interpreted.gif,&interpreted.gs);native.vu.advance_micro(cycle,&native.gif,&native.gs);
        compare(interpreted.vu,native.vu,interpreted.gif,native.gif,interpreted.gs,native.gs);
    }
    require(native.vu.observe_data(0x42B0).value==123 && native.vu.vu1_aot_stats().missing_program>0 &&
        native.vu.vu1_aot_stats().interpreted_instructions>0,"changed microcode cannot execute a stale translation");
    // MPG follows the same invalidation path as an aperture store.
    const auto before=native.vu.vu1_aot_stats().native_instructions;
    const auto replacement=std::uint64_t{vu_aot_fixture::addiu(12,0,55)}|(std::uint64_t{vu_aot_fixture::upper_nop|0x40000000u}<<32u);
    native.vif.write_fifo(0x4A01000000000000ull,replacement,native.gif,native.gs,&native.vu,90);
    native.vu.start_micro(1,0,90);native.vu.advance_micro(100,&native.gif,&native.gs);
    require(native.vu.observe_data(0x42C0).value==55 && native.vu.vu1_aot_stats().native_instructions==before,
        "MPG replacement selects explicit missing-program interpretation");
    native.vu.write_memory(0x11008000u,1,{0,1});native.vu.start_micro(1,0,101);
    rejects([&]{native.vu.advance_micro(102,&native.gif,&native.gs);},"unwritten micro memory");
}
void refusals() {
    Device interpreted(false),native(true);
    for(auto* device:{&interpreted,&native}) {
        device->vu.write_vector(31,{{0,~0ull},{0,~0ull}});device->vu.write(2,0x401u);
        device->vu.vector_store_increment(31,2,15);device->vu.start_micro(1,0,0);
    }
    rejects([&]{interpreted.vu.advance_micro(1);},"FMAC multiply consumes unknown");
    rejects([&]{native.vu.advance_micro(1);},"FMAC multiply consumes unknown");
    compare(interpreted.vu,native.vu,interpreted.gif,native.gif,interpreted.gs,native.gs);
    require(native.vu.diagnostic_micro_instructions(1)==0 && native.vu.vu1_aot_stats().native_instructions==0 &&
        native.vu.observe_data(0x4210).value==2000,"a refused upper cannot commit its paired integer lower or claim native retirement");
}
void complete_state(const std::filesystem::path& directory) {
    std::vector<std::string> reference;
    for(unsigned engine=0;engine<3;++engine) {
        auto memory=std::make_unique<Memory>();EEState state;vu_aot_fixture::initialize(*memory,state,engine==2,0);
        state.set_cp0(Cop0::Status,kStatusIE|kStatusEIE|kStatusIM7);state.set_cp0(Cop0::Compare,7);state.compare_written=true;
        BootOptions options;options.tail_instructions=0;options.max_instructions=1;
        unsigned sample=0;
        for(unsigned cycle=1;cycle<=65;++cycle) {
            const auto report=run_ee_boot(state,*memory,engine ? &aot_module() : nullptr,options);
            require(report.outcome==BootOutcome::BudgetExhausted,"integrated EE/VU execution: "+report.reason);
            if(cycle<=8 || cycle==11 || cycle==17 || cycle==25 || cycle==40 || cycle==65) {
                const auto hash=checkpoint_state_sha256(state,*memory);
                if(!engine)reference.push_back(hash);else require(hash==reference.at(sample),"complete checkpoint state including ACC, pending writes, Q/P, IRQs, VIF, GIF, GS and clocks");
                ++sample;
            }
        }
        require(engine!=2 || memory->vu1_aot_stats().native_instructions>0,"EE AOT integrates actual native VU dispatch");
        require(state.pc>=0x80000200u && state.pc<0x80000400u,"real Count/Compare interruption occurs while VU pipelines continue");
    }
    auto memory=std::make_unique<Memory>();EEState state;vu_aot_fixture::initialize(*memory,state,true,0);
    BootOptions options;options.max_instructions=8;options.tail_instructions=0;
    auto report=run_ee_boot(state,*memory,&aot_module(),options);
    const auto file=directory/"vu-pending.ps2ncp";CheckpointContext context{sha256_hex(vu_aot_fixture::ee_image()),"vu-aot"};
    const auto saved=checkpoint_state_sha256(state,*memory);save_checkpoint(file,state,*memory,report,context);
    options.max_instructions=57;report=run_ee_boot(state,*memory,&aot_module(),options);const auto final=checkpoint_state_sha256(state,*memory);
    // Mutate the old object's bound microcode before restoration. Host caches
    // must be revalidated against restored serialized bytes, never persisted.
    memory->write32(0xB1008000,vu_aot_fixture::addiu(9,0,19));
    (void)load_checkpoint(file,state,*memory,context);
    require(checkpoint_state_sha256(state,*memory)==saved,"pending VU checkpoints restore exact complete state");
    report=run_ee_boot(state,*memory,&aot_module(),options);
    require(report.outcome==BootOutcome::BudgetExhausted && checkpoint_state_sha256(state,*memory)==final,"native resume after microcode invalidation preserves all serialized state");
}
}
template<class Character> int vu_aot_main(int argc,Character** argv) try {
    if(argc!=2)return 2;const auto directory=std::filesystem::path(argv[1]);ps2native::create_directories(directory);
    analysis_contract(directory);equivalence(vu_aot_fixture::math_entry);equivalence(vu_aot_fixture::draw_entry);equivalence(vu_aot_fixture::indirect_entry,true);equivalence(vu_aot_fixture::wrap_entry);
    invalidation();refusals();complete_state(directory);
    std::cout<<"VU1 generated native equivalence: VF/VI/ACC, Q/P/flags, cycle boundaries, memory, VIF MPG, branches/delays, PATH1/GS RGBA, complete checkpoints, missing/modified/unknown fallback passed\n";return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return vu_aot_main(argc,argv);}
#else
int main(int argc,char** argv){return vu_aot_main(argc,argv);}
#endif
