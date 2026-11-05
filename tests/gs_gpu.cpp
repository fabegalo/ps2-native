// Original synthetic GS/GIF streams. Pixel, knowledge and guest-state proofs;
// no firmware, game packets or reference-project implementation is included.
#include "boot_checks.hpp"
#include "gs_program.hpp"
#include "ps2native/boot.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/gif.hpp"
#include "ps2native/gs_gpu.hpp"
#include "ps2native/sha256.hpp"
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>

using namespace ps2native;
using boot_checks::require;
using boot_checks::rejects;
namespace {
constexpr unsigned width=64, height=32;
struct Stream {
    GsRegisters gs;
    Gif gif;
    Stream(std::shared_ptr<GsGpu> gpu={},unsigned precision=0) {
        gs.reset(0);gif.reset();
        if(precision)gs.diagnostic_texture_precision(precision);
        gs.set_gpu_backend(std::move(gpu));
    }
    void ad(unsigned reg,std::uint64_t value) {
        gif.write_fifo(1u|(1ull<<15u)|(1ull<<60u),0xEu,gs);
        gif.write_fifo(value,reg,gs);
    }
    void vertex(int x,int y,unsigned z=0,bool kick=true) {
        ad(kick?5u:0xDu,(static_cast<unsigned>(x)&0xFFFFu) |
           ((std::uint64_t{static_cast<unsigned>(y)}&0xFFFFu)<<16u)|(std::uint64_t{z}<<32u));
    }
    void sprite(unsigned x0,unsigned y0,unsigned x1,unsigned y1,unsigned color,unsigned z=0,unsigned mode=6) {
        ad(0,mode);ad(1,color);vertex(static_cast<int>(x0*16u),static_cast<int>(y0*16u),z);
        vertex(static_cast<int>(x1*16u),static_cast<int>(y1*16u),z);
    }
    void setup(unsigned psm=0,bool scan_known=true,bool fba_known=true) {
        ad(0x1A,1);ad(0x18,0);ad(0x4C,(1u<<16u)|(psm<<24u));
        ad(0x40,((width-1ull)<<16u)|((height-1ull)<<48u));
        ad(0x47,3u<<16u);ad(0x4E,1ull<<32u);
        ad(0x45,0);ad(0x46,1);ad(0x49,0);
        if(scan_known)ad(0x22,0);
        if(fba_known)ad(0x4A,0);
        gs.write(kGsPmode,8,0xFF25u,0,{});gs.write(kGsSmode2,8,0,0,{});
        gs.write(kGsDispfb1,8,(1u<<9u)|(std::uint64_t{psm}<<15u),0,{});
        gs.write(kGsDisplay1,8,((width-1ull)<<32u)|((height-1ull)<<44u),0,{});
    }
    void upload(unsigned base,unsigned psm,unsigned w,unsigned h,std::uint64_t pixels) {
        ad(0x50,(std::uint64_t{base/256u}<<32u)|(1ull<<48u)|(std::uint64_t{psm}<<56u));
        ad(0x51,0);ad(0x52,w|(std::uint64_t{h}<<32u));ad(0x53,0);
        const unsigned bytes=psm==20u ? (w*h+1u)/2u : psm==19u ? w*h : psm==1u ? w*h*3u : w*h*4u;
        gif.write_fifo((bytes+15u)/16u|(1ull<<15u)|(2ull<<58u),0,gs);
        for(unsigned n=0;n<bytes;n+=16u)gif.write_fifo(pixels,pixels,gs);
    }
};
void same(const Stream& cpu,const Stream& gpu,const std::string& label,bool scan=true) {
    const auto a=cpu.gs.vram().raw_words(),b=gpu.gs.vram().raw_words();
    require(std::equal(a.begin(),a.end(),b.begin(),b.end()),label+": full VRAM values");
    const auto ak=cpu.gs.vram().known_bits(),bk=gpu.gs.vram().known_bits();
    require(std::equal(ak.begin(),ak.end(),bk.begin(),bk.end()),label+": full known-bit masks");
    require(cpu.gs.vram().revision()==gpu.gs.vram().revision(),label+": VRAM revision");
    require(cpu.gs.vram().pixel_writes()==gpu.gs.vram().pixel_writes(),label+": proven color writes");
    require(cpu.gif.packets()==gpu.gif.packets(),label+": original GIF packet order");
    require(cpu.gs.observe_csr().value==gpu.gs.observe_csr().value &&
        cpu.gs.observe_csr().unknown==gpu.gs.observe_csr().unknown,label+": CSR flags and uncertainty");
    if(scan) {
        const auto af=cpu.gs.scanout(1234),bf=gpu.gs.scanout(1234);
        require(af.rgba==bf.rgba && af.revision==bf.revision && af.ee_cycle==bf.ee_cycle,
            label+": framebuffer bytes, revision and guest cycle");
    }
}
template<class F>void paired(std::shared_ptr<GsGpu> gpu,const std::string& label,F action,
                            unsigned psm=0,unsigned precision=0,bool scan_known=true,bool fba_known=true) {
    Stream cpu({},precision),accelerated(std::move(gpu),precision);
    for(auto* s:{&cpu,&accelerated}){s->setup(psm,scan_known,fba_known);action(*s);}
    same(cpu,accelerated,label,scan_known);
}
void check_raster(std::shared_ptr<GsGpu> gpu) {
    for(const unsigned psm:{0u,1u})for(const unsigned type:{3u,4u,5u})for(const bool reverse:{false,true}) {
        paired(gpu,"coverage/winding/assembly CT"+std::to_string(psm),[&](Stream& s) {
            s.sprite(0,0,width,height,0x80402010u);
            s.ad(0,type);s.ad(1,0xC0506080u);
            s.vertex(8,8);s.vertex(reverse?8:168,reverse?168:8);s.vertex(reverse?168:8,reverse?8:168);
            // Top/left and subpixel coverage, scissor clipping and queue reset.
            s.ad(0x40,2u|(7ull<<16u)|(2ull<<32u)|(7ull<<48u));
            s.sprite(0,0,12,12,0xFFAA2299u);
            s.ad(0,3);s.vertex(32,32,0,false);s.vertex(96,32,0,false);s.vertex(32,96,0,false);
        },psm);
    }
    for(const unsigned scan:{0u,2u,3u})for(const unsigned fba:{0u,1u}) {
        paired(gpu,"SCANMSK/FBA/FBMSK",[&](Stream& s){
            s.sprite(0,0,width,height,0x80402010u);s.ad(0x22,scan);s.ad(0x4A,fba);
            s.ad(0x4C,(1u<<16u)|(0x0040FF00ull<<32u));s.sprite(1,1,15,15,0x12345678u);
        });
    }
    paired(gpu,"unknown SCANMSK/FBA bit joins",[](Stream& s){
        s.upload(0,0,64,32,0x8000000080000000ull);
        s.sprite(0,0,8,8,0x80000000u);s.sprite(8,0,16,8,0x00112233u);
    },0,0,false,false);
}
void check_pipeline(std::shared_ptr<GsGpu> gpu) {
    // AREF truth table and all AFAIL paths, with both depth and framebuffer.
    for(unsigned func=0;func<8u;++func)for(unsigned fail=0;fail<4u;++fail) {
        paired(gpu,"alpha compare/AFAIL",[&](Stream& s){
            s.ad(0x4E,32u);s.sprite(0,0,width,height,0x80402010u,7);
            s.ad(0x47,(5u<<16u)|1u|(func<<1u)|(128u<<4u)|(fail<<12u));
            for(unsigned alpha=127;alpha<=129;++alpha)s.sprite((alpha-127u)*8u,0,(alpha-126u)*8u,8,
                (alpha<<24u)|0x00506070u,9);
        });
    }
    for(const unsigned zpsm:{0u,1u})for(const unsigned func:{0u,1u,2u,3u})for(const bool mask:{false,true}) {
        paired(gpu,"depth compare/ZMSK",[&](Stream& s){
            s.ad(0x4E,32u|(zpsm<<24u));s.sprite(0,0,width,height,0x80402010u,7);
            s.ad(0x47,(1u<<16u)|(func<<17u));s.ad(0x4E,32u|(zpsm<<24u)|(mask?1ull<<32u:0));
            for(unsigned z=6;z<=8;++z)s.sprite((z-6u)*8u,0,(z-5u)*8u,8,0x90506070u,z);
        });
    }
    for(const unsigned psm:{0u,1u})for(const unsigned equation:{0x44u,0x82u,0x48u,0x64u,0xA9u})
        for(const bool saturate:{false,true})for(const bool pabe:{false,true}) {
        paired(gpu,"signed blending/clamp/PABE",[&](Stream& s){
            s.sprite(0,0,width,height,0x80604080u);s.ad(0x42,equation|(64ull<<32u));
            s.ad(0x46,saturate?1u:0u);s.ad(0x49,pabe?1u:0u);
            s.sprite(0,0,16,8,0x40010203u,0,0x46u);s.sprite(16,0,32,8,0xF0F1F2F3u,0,0x46u);
        },psm);
    }
}
void check_fallback_and_transfers(std::shared_ptr<GsGpu> gpu) {
    paired(gpu,"unknown destination read retains synchronous refusal",[](Stream& s){
        s.ad(0x42,0x44);s.ad(0,0x46);s.ad(1,0x40010203u);s.vertex(0,0);
        rejects([&]{s.vertex(128,128);},"unwritten bits");
    },0,0,false);
    paired(gpu,"GPU write -> CPU texture/filter -> GPU write",[](Stream& s){
        s.sprite(0,0,width,height,0x80402010u);
        s.ad(0x4C,8u|(1u<<16u));s.sprite(0,0,8,8,0x80806040u);s.ad(0x3F,0);
        s.ad(0x4C,1u<<16u);s.ad(6,256u|(1ull<<14u)|(3ull<<26u)|(3ull<<30u)|(1ull<<34u)|(1ull<<35u));
        s.ad(8,0);s.ad(0x14,0);s.ad(0,0x116);s.ad(1,0x80808080u);
        s.ad(3,0);s.vertex(8*16,8*16);s.ad(3,128u|(128ull<<16u));s.vertex(16*16,16*16);
        s.ad(0x14,0x60);s.ad(0,0x116);s.ad(3,8u|(8ull<<16u));s.vertex(16*16,8*16);
        s.ad(3,136u|(136ull<<16u));s.vertex(24*16,16*16);
        s.sprite(24,8,32,16,0x80FEDCBAu);
        // GPU source -> local-to-local CPU copy -> GPU destination blending.
        s.ad(0x50,(1ull<<16u)|(1ull<<48u));s.ad(0x51,24u|(8ull<<16u)|(32ull<<32u)|(8ull<<48u));
        s.ad(0x52,8u|(8ull<<32u));s.ad(0x53,2);
        s.ad(0x42,0x44);s.sprite(32,8,40,16,0x40000000u,0,0x46);
        s.ad(0x61,0);s.ad(0x60,0xFFFFFFFF12345678ull);s.ad(0x62,0xFFFFFFFF89ABCDEFull);
    });
    paired(gpu,"AA1 CPU fallback with preserved GPU dependencies",[](Stream& s){
        s.ad(0x4E,32u);s.sprite(0,0,width,height,0x80000000u,7);
        s.ad(0x42,0x44);s.ad(0,3u|128u|64u);s.ad(1,0x80402010u);
        s.vertex(40,40,9);s.vertex(104,40,9);s.vertex(40,104,9);
        s.sprite(16,0,24,8,0x80AABBCCu,11);
    },0,16);
    paired(gpu,"CLUT/UV CPU fallback",[](Stream& s){
        s.sprite(0,0,width,height,0x80402010u);
        s.upload(0x20000,0,8,2,0x8080604080806040ull);
        s.upload(0x10000,20,8,2,0xFEDCBA9876543210ull);s.ad(0x3F,0);
        s.ad(6,256u|(2ull<<14u)|(20ull<<20u)|(3ull<<26u)|(1ull<<30u)|
            (1ull<<34u)|(1ull<<35u)|(512ull<<37u)|(1ull<<61u));
        s.ad(8,0);s.ad(0x14,1);s.ad(0,0x116);s.ad(1,0x80808080u);
        s.ad(3,0);s.vertex(0,0);s.ad(3,128u|(32ull<<16u));s.vertex(128,32);
        s.sprite(16,0,24,8,0x80AABBCCu);
    });
    paired(gpu,"transfer readback refusal remains explicit",[](Stream& s){
        s.sprite(0,0,width,height,0x80402010u);s.ad(0x50,(1ull<<16u)|(1ull<<48u));
        s.ad(0x51,0);s.ad(0x52,8u|(1ull<<32u));rejects([&]{s.ad(0x53,1);},"local-to-host");
    });
    Stream source(gpu);source.setup();source.sprite(0,0,width,height,0x80402010u);
    const GsRegisters snapshot=source.gs;
    require(!snapshot.gpu_backend() && snapshot.scanout(0).rgba==source.gs.scanout(0).rgba,
        "copy materializes pending work and detaches the host backend");
    source.sprite(0,0,8,8,0x80ABCDEFu);
    require(snapshot.vram().read(0,64,0,0,0)==0x80402010u && source.gs.vram().read(0,64,0,0,0)==0x80ABCDEFu,
        "a guest-state copy cannot share writable VRAM with later GPU commands");
}
void check_guest(std::shared_ptr<GsGpu> gpu,const std::filesystem::path& output) {
    auto fixture=m29::build();
    // Both processors fetch the same synthetic ROM. Distinguish their actual
    // modeled reset SR.ERL values, then leave the IOP executing a real branch
    // loop instead of feeding it the EE-only LQ instruction in the test card.
    // The original EE body and GIF data are preserved; its relative branches
    // move together and all data addresses remain at their original offsets.
    constexpr std::array<std::uint32_t,7> prefix{
        0x40086000u,0u,0x31080004u,0x15000003u,0u,0x1000FFFFu,0u};
    constexpr auto bytes=prefix.size()*4u;
    require(fixture.stop-m20::kResetVector+4u+bytes<m20::kResetSize,"synthetic reset code has room for the dual-processor prefix");
    std::copy_backward(fixture.rom.begin(),fixture.rom.begin()+m20::kResetSize-bytes,
        fixture.rom.begin()+m20::kResetSize);
    for(std::size_t n=0;n<prefix.size();++n)for(unsigned byte=0;byte<4u;++byte)
        fixture.rom[n*4u+byte]=static_cast<std::uint8_t>(prefix[n]>>(byte*8u));
    fixture.stop+=static_cast<std::uint32_t>(bytes);
    const auto bios=load_bios_rom0(fixture.rom);
    std::string state_hash[2];std::vector<Framebuffer> frames[2];BootReport reports[2];
    for(unsigned run=0;run<2u;++run) {
        EEState state;Memory memory;prepare_ee_boot(bios,nullptr,state,memory);
        if(run)memory.set_gs_gpu_backend(gpu);
        BootOptions options;options.max_instructions=100000;options.stop_at=fixture.stop;
        options.trace=TraceLevel::Full;options.on_frame=[&](const Framebuffer& f){frames[run].push_back(f);};
        reports[run]=run_ee_boot(state,memory,nullptr,options);
        require(reports[run].outcome==BootOutcome::StopAddress,"synthetic guest boundary: "+reports[run].reason);
        const auto frame=memory.gs().scanout(state.retired_instructions);
        require(sha256_hex(frame.rgba)=="43564b222dee0155325d1db289518d63f6a02af60690dd6da83ad69d002e3f54",
            "original synthetic GIF/GS guest produces the RGBA golden digest");
        state_hash[run]=checkpoint_state_sha256(state,memory);
        if(!output.empty()) {
            std::filesystem::create_directories(output);
            CheckpointContext context;context.scenario_sha256=std::string(64,'a');
            const auto path=output/(run?"gpu.ps2ncp":"cpu.ps2ncp");
            save_checkpoint(path,state,memory,reports[run],context);
            EEState restored;Memory other;prepare_ee_boot(bios,nullptr,restored,other);
            (void)load_checkpoint(path,restored,other,context);
            if(run)other.set_gs_gpu_backend(gpu);
            require(checkpoint_state_sha256(restored,other)==state_hash[run],"full save/load preserves GS/GPU boundary state");
            require(other.gs().scanout(restored.retired_instructions).rgba==frame.rgba,"restored original framebuffer");
        }
    }
    require(state_hash[0]==state_hash[1],"full CPU/GPU guest checkpoint state including DMA/IRQ/cycles");
    require(reports[0].retired==reports[1].retired && reports[0].gif_packets==reports[1].gif_packets &&
        boot_checks::normalized(reports[0].trace)==boot_checks::normalized(reports[1].trace),"guest instruction/bus/GIF order unchanged");
    require(frames[0].size()==frames[1].size() && !frames[0].empty(),"same real guest field events");
    for(std::size_t n=0;n<frames[0].size();++n)require(frames[0][n].rgba==frames[1][n].rgba &&
        frames[0][n].ee_cycle==frames[1][n].ee_cycle,"each modeled field has identical pixels and guest cycle");
    if(!output.empty()) {
        EEState state;Memory memory;prepare_ee_boot(bios,nullptr,state,memory);memory.set_gs_gpu_backend(gpu);
        BootOptions options;options.max_instructions=750;options.stop_at=fixture.stop;
        const auto boundary=run_ee_boot(state,memory,nullptr,options);
        require(boundary.outcome==BootOutcome::BudgetExhausted && memory.gs().vram().gpu_primitives()>0,
            "mid-stream checkpoint boundary has executed original GPU primitives");
        CheckpointContext context;context.scenario_sha256=std::string(64,'a');
        const auto path=output/"gpu-mid-stream.ps2ncp";save_checkpoint(path,state,memory,boundary,context);
        EEState restored;Memory other;prepare_ee_boot(bios,nullptr,restored,other);
        (void)load_checkpoint(path,restored,other,context);other.set_gs_gpu_backend(gpu);
        options.max_instructions=100000;const auto end=run_ee_boot(restored,other,nullptr,options);
        require(end.outcome==BootOutcome::StopAddress && checkpoint_state_sha256(restored,other)==state_hash[0],
            "GPU pending work survives checkpoint materialization and full guest continuation");
    }
}
void check_device_batch(GsGpuApi api) {
    std::string unavailable;auto device=create_gs_gpu(unavailable,api);
    require(static_cast<bool>(device),"second device selection: "+unavailable);
    std::vector<std::uint32_t> words(GsVram::kBytes/4u),known(words.size());
    GsGpuCommand initial{};initial.right=8;initial.bottom=8;initial.bw=64;
    initial.color=0x80402010u;initial.fba_known=1;
    auto blend=initial;blend.color=0x40000000u;blend.blending=1;blend.alpha=0x44;blend.saturate=1;
    const std::array commands{initial,blend};
    const auto result=device->execute(words,known,commands);
    require(result.revisions==128u && result.pixel_writes==128u,"one device batch executes ordered dependent primitives");
    std::vector<std::uint32_t> expected_words(words.size()),expected_known(words.size());
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
        const auto at=GsVram::address(0,64,0,x,y)/4u;
        expected_words[at]=0x40201008u;expected_known[at]=0xFFFFFFFFu;
    }
    require(words==expected_words && known==expected_known,
        "device barriers expose first primitive to second without touching any other VRAM bits");
    std::fill(words.begin(),words.end(),0u);std::fill(known.begin(),known.end(),0u);
    const std::array unknown_read{blend};
    rejects([&]{(void)device->execute(words,known,unknown_read);},"unknown VRAM");
    require(std::all_of(words.begin(),words.end(),[](auto n){return n==0u;}) &&
        std::all_of(known.begin(),known.end(),[](auto n){return n==0u;}),
        "a failed device batch commits no fabricated values or knowledge");
    rejects([&]{(void)device->execute(words,known,commands);},
        api==GsGpuApi::DirectX?"earlier failed batch":"stopped");
}
}
int main(int argc,char** argv)try {
    std::string unavailable;
    const std::string selection=argc>2?argv[2]:"vulkan";
    require(selection=="vulkan" || selection=="directx","backend must be vulkan or directx");
    const auto api=selection=="directx"?GsGpuApi::DirectX:GsGpuApi::Vulkan;
    auto gpu=create_gs_gpu(unavailable,api);
    if(!gpu) {
        Stream cpu;cpu.setup();cpu.sprite(0,0,width,height,0x80402010u);
        require(!cpu.gs.gpu_backend() && cpu.gs.vram().read(0,64,0,63,31)==0x80402010u &&
            cpu.gs.scanout(0).rgba.size()==width*height*4u,"CPU execution and scanout work without a GPU backend");
        std::cout<<selection<<" GS backend unavailable: "<<unavailable<<"; CPU raster/scanout passed\n";
        const auto* required=std::getenv("PS2N_REQUIRE_GS_GPU");
        return selection=="vulkan" && required && std::string_view{required}=="1"?1:77;
    }
    check_device_batch(api);check_raster(gpu);check_pipeline(gpu);check_fallback_and_transfers(gpu);
    check_guest(gpu,argc>1?std::filesystem::path{argv[1]}:std::filesystem::path{});
    require(gpu->stats().primitives>0 && gpu->stats().batches>0,"commands were rasterized by the selected GPU");
    std::cout<<"GPU raster, full VRAM/knowledge, fallback and guest checkpoint equivalence passed on "
        <<gpu->device_name()<<"; GPU primitives="<<gpu->stats().primitives<<", batches="<<gpu->stats().batches<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
