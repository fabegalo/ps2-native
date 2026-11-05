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
#include <utility>

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
void same_vram(const GsVram& cpu,const GsVram& gpu,const std::string& label) {
    const auto values=cpu.raw_words(),actual=gpu.raw_words();
    const auto masks=cpu.known_bits(),actual_masks=gpu.known_bits();
    require(std::equal(values.begin(),values.end(),actual.begin(),actual.end()),label+": full values");
    require(std::equal(masks.begin(),masks.end(),actual_masks.begin(),actual_masks.end()),label+": full masks");
    require(cpu.revision()==gpu.revision() && cpu.pixel_writes()==gpu.pixel_writes(),
        label+": guest revisions and writes");
}
GsGpuCommand rectangle(unsigned color,unsigned preserve=0,unsigned psm=0) {
    GsGpuCommand command{};command.right=1;command.bottom=1;command.bw=64;
    command.color=color;command.frame_mask=preserve;command.psm=psm;command.fba_known=1;
    return command;
}
// This reference deliberately uses the public CPU writes instead of the GPU
// packet evaluator. These cases exercise storage coherence, not raster setup.
void enqueue_pair(GsVram& cpu,GsVram& gpu,const GsGpuCommand& command) {
    for(int y=command.top;y<command.bottom;++y)for(int x=command.left;x<command.right;++x)
        cpu.write(command.base,command.bw,command.psm,static_cast<unsigned>(x),static_cast<unsigned>(y),
            command.color,command.frame_mask,0,command.conditional!=0u);
    gpu.enqueue_gpu(command);
}
void check_partial_coherence(std::shared_ptr<GsGpu> backend) {
    GsVram cpu,gpu;gpu.set_gpu_backend(backend);
    for(auto* v:{&cpu,&gpu})v->write(0,64,0,0,0,0xA5123456u);
    enqueue_pair(cpu,gpu,rectangle(0x00806040u,0,1));
    // The CPU changes a middle byte while GPU RGB and the preserved alpha are
    // newer than the host copy. Unknown low bits must survive the next draw.
    for(auto* v:{&cpu,&gpu})v->write(0,64,0,0,0,0x0000AB00u,0xFFFF00FFu,0x00000F00u);
    enqueue_pair(cpu,gpu,rectangle(0xCC000099u,0x00FFFF00u));
    for(auto* v:{&cpu,&gpu}) {
        require(v->read(0,64,19,0,0)==0x99u,"CPU byte read observes GPU write without consuming unknown neighbors");
        rejects([&]{(void)v->read(0,64,0,0,0);},"unwritten bits");
        v->write(0,64,20,0,0,5u);
        v->write(0,64,0,7,1,0x8A765432u); // another word on the same physical page
    }
    enqueue_pair(cpu,gpu,rectangle(0xDD000033u,0x00FFFF00u));
    same_vram(cpu,gpu,"GPU/CPU masked, indexed and unknown writes on one page");

    // A page never observed by the CPU starts unknown. A GPU partial write
    // establishes only its written bits; a later CPU halfword completes it.
    for(auto* v:{&cpu,&gpu})v->invalidate();
    enqueue_pair(cpu,gpu,rectangle(0x00001234u,0xFFFF0000u));
    for(auto* v:{&cpu,&gpu}) {
        require(v->read(0,64,2,0,0)==0x1234u,"GPU known low halfword is readable");
        rejects([&]{(void)v->read(0,64,0,0,0);},"unwritten bits");
        v->write(0,64,2,8,0,0xBEEFu);
    }
    enqueue_pair(cpu,gpu,rectangle(0x00005678u,0xFFFF0000u));
    for(auto* v:{&cpu,&gpu}) {
        require(v->read(0,64,0,0,0)==0xBEEF5678u,"CPU upper halfword survives GPU lower halfword update");
        v->write(0,64,0,0,0,0xBEEF5679u,0,0,true);
    }
    enqueue_pair(cpu,gpu,rectangle(0x88000000u,0x00FFFFFFu));
    same_vram(cpu,gpu,"partial initialization, invalidation and conditional knowledge joins");
    rejects([&]{(void)gpu.word_at(0);},"unwritten bits");

    // Device buffers are caches belonging to a particular logical VRAM. A
    // shared backend must not let another GS instance overwrite resident data.
    GsVram other_cpu,other_gpu;other_gpu.set_gpu_backend(backend);
    enqueue_pair(other_cpu,other_gpu,rectangle(0x80112233u));
    require(other_gpu.read(0,64,0,0,0)==0x80112233u,"second logical VRAM uses its own contents");
    enqueue_pair(cpu,gpu,rectangle(0x99000000u,0x00FFFFFFu));
    same_vram(cpu,gpu,"shared backend preserves first logical VRAM");
    same_vram(other_cpu,other_gpu,"shared backend preserves second logical VRAM");
    enqueue_pair(other_cpu,other_gpu,rectangle(0x80223344u));other_gpu.submit_gpu();
    GsVram moved_cpu(std::move(other_cpu)),moved_gpu(std::move(other_gpu));
    enqueue_pair(moved_cpu,moved_gpu,rectangle(0x80334455u,0xFF000000u));
    same_vram(moved_cpu,moved_gpu,"move construction keeps the resident cache bound to its new owner");
    GsVram assigned_cpu,assigned_gpu;assigned_gpu.set_gpu_backend(backend);
    enqueue_pair(assigned_cpu,assigned_gpu,rectangle(0x80445566u));
    assigned_cpu=std::move(moved_cpu);assigned_gpu=std::move(moved_gpu);
    enqueue_pair(assigned_cpu,assigned_gpu,rectangle(0x80556677u));
    same_vram(assigned_cpu,assigned_gpu,"move assignment replaces pending work and transfers cache ownership");
    {
        GsVram temporary;temporary.set_gpu_backend(backend);
        temporary.enqueue_gpu(rectangle(0x80667788u));temporary.submit_gpu();
    }
    enqueue_pair(assigned_cpu,assigned_gpu,rectangle(0x80778899u));
    same_vram(assigned_cpu,assigned_gpu,"destroyed cache owner cannot contaminate or retain a later GS binding");
}
void check_resident_batches(std::shared_ptr<GsGpu> backend) {
    Stream cpu,accelerated(backend);cpu.setup();accelerated.setup();
    const auto before=backend->stats();
    for(auto* s:{&cpu,&accelerated}) {
        s->sprite(0,0,width,height,0x80402010u);
        for(unsigned n=0;n<256u;++n)s->sprite(0,0,8,8,0x80000000u|n);
    }
    const auto first=backend->stats();
    require(first.batches==before.batches+1u,"queue capacity submits one original GPU batch");
    require(first.upload_bytes-before.upload_bytes<=2u*GsVram::kPageBytes,
        "one touched page uploads values and knowledge, not the complete VRAM");
    require(first.readback_bytes==before.readback_bytes,"automatic GPU batch boundary retains GPU-owned VRAM");
    for(auto* s:{&cpu,&accelerated})
        for(unsigned n=0;n<256u;++n)s->sprite(0,0,8,8,0x81000000u|n);
    const auto second=backend->stats();
    require(second.batches==first.batches+1u && second.upload_bytes==first.upload_bytes &&
        second.readback_bytes==first.readback_bytes,
        "consecutive batches reuse resident values and knowledge without VRAM round trips");
    accelerated.gs.vram().submit_gpu();
    const auto submitted=backend->stats();
    rejects([&]{(void)accelerated.gs.vram().read(0x20000u,64,0,0,0);},"unwritten bits");
    require(backend->stats().readback_bytes==submitted.readback_bytes,
        "CPU read of an unrelated unknown page does not download GPU-owned framebuffer pages");
    require(accelerated.gs.vram().read(0,64,0,0,0)==cpu.gs.vram().read(0,64,0,0,0),
        "CPU read synchronizes the requested GPU-owned pixel");
    require(backend->stats().readback_bytes-submitted.readback_bytes==2u*GsVram::kPageBytes,
        "CPU pixel access downloads only its page values and knowledge");
    same(cpu,accelerated,"multiple resident GPU batches and selective CPU reads");

    // Transfers on an untouched page may not force the rendered page through
    // the host, and a later GPU overwrite must preserve that CPU upload.
    for(auto* s:{&cpu,&accelerated}) {
        s->sprite(8,0,16,8,0x80ABCDEFu);
        s->upload(0x20000u,0,2,2,0x8765432187654321ull);
        s->sprite(0,0,8,8,0x80987654u);
    }
    same(cpu,accelerated,"GPU/host transfer/GPU across disjoint pages");
}
void check_completion_order(std::shared_ptr<GsGpu> backend) {
    Stream cpu,accelerated(backend);cpu.setup();accelerated.setup();
    const auto before=backend->stats();
    for(auto* s:{&cpu,&accelerated}) {
        s->gs.write(kGsImr,8,0x7C00u,0,{});
        s->sprite(0,0,width,height,0x80402010u);
        s->ad(0x61,0); // FINISH executes pending work but does not consume pixels.
        s->ad(0x60,0xFFFFFFFF12345678ull);s->ad(0x62,0xFFFFFFFF89ABCDEFull);
        require((s->gs.observe_csr().value&3u)==3u && (s->gs.observe_csr().unknown&3u)==0u,
            "FINISH and SIGNAL become known after the preceding raster work");
        require(s->gs.read(kGsSiglblid,8)==0x89ABCDEF12345678ull,"SIGNAL/LABEL IDs preserve guest ordering");
        const auto irq=s->gs.take_interrupt_request(0);
        require(irq.pending && !irq.unknown,"completed GPU commands preserve the known GS interrupt request");
        require(!s->gs.take_interrupt_request(0).pending,"GS interrupt is delivered once at this boundary");
        s->sprite(8,0,16,8,0x80806040u);s->ad(0x3F,0);
    }
    const auto completed=backend->stats();
    require(completed.batches==before.batches+2u && completed.readback_bytes==before.readback_bytes,
        "FINISH/SIGNAL/TEXFLUSH completion does not download unused framebuffer contents");
    same(cpu,accelerated,"resident GPU completion, IDs and interrupt ordering");
}
void check_pending_knowledge(std::shared_ptr<GsGpu> backend) {
    paired(backend,"pending unknown FBA invalidates destination-alpha proof",[](Stream& s){
        s.sprite(0,0,width,height,0x80000000u); // high alpha bit is known despite unknown FBA
        s.sprite(0,0,8,8,0x00112233u); // unknown FBA may now set that bit
        s.ad(0x42,0x58); // (Cs - 0) * Ad + Cd consumes destination alpha
        rejects([&]{s.sprite(0,0,8,8,0x80402010u,0,0x46);},"unwritten bits");
    },0,0,true,false);
    paired(backend,"pending conditional write invalidates destination-RGB proof",[](Stream& s){
        s.upload(0,0,width,height,0x8000000080000000ull);
        s.sprite(0,0,8,8,0x80402010u); // unknown SCANMSK joins draw and skip
        s.ad(0x22,0);s.ad(0x42,0x44);
        rejects([&]{s.sprite(0,0,8,8,0x40402010u,0,0x46);},"unwritten bits");
    },0,0,false,true);
    // A rejected GIF payload remains unfinished. Use another original stream
    // to prove a synchronous CPU knowledge refusal has not poisoned the GPU.
    Stream recovered(backend);recovered.setup();recovered.sprite(0,0,width,height,0x80ABCDEFu);
    require(recovered.gs.vram().read(0,64,0,0,0)==0x80ABCDEFu,
        "a separate GIF stream can render on the backend after CPU knowledge refusal");
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
    for(const unsigned order:{0u,2u})paired(gpu,"GPU source with overlapping directional local copy",[&](Stream& s){
        s.sprite(0,0,width,height,0x80000000u);
        for(unsigned x=0;x<16u;++x)s.sprite(x,0,x+1u,8,0x80402000u+x);
        s.ad(0x50,(1ull<<16u)|(1ull<<48u));
        s.ad(0x51,(2ull<<32u)|(std::uint64_t{order}<<59u));
        s.ad(0x52,8u|(8ull<<32u));s.ad(0x53,2);
        s.ad(0x42,0x44);s.sprite(2,0,10,8,0x40000000u,0,0x46);
    });
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
        const auto batches_before=gpu->stats().batches;
        const auto boundary=run_ee_boot(state,memory,nullptr,options);
        require(boundary.outcome==BootOutcome::BudgetExhausted && memory.gs().vram().gpu_primitives()>0 &&
            gpu->stats().batches==batches_before,"mid-stream checkpoint boundary retains pending original GPU primitives");
        CheckpointContext context;context.scenario_sha256=std::string(64,'a');
        const auto path=output/"gpu-mid-stream.ps2ncp";save_checkpoint(path,state,memory,boundary,context);
        require(gpu->stats().batches>batches_before,"checkpoint materialization actually executes the pending GPU batch");
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
    const GsGpuRange pixel{0,1};const auto ranges=std::span{&pixel,1u};
    std::fill(words.begin(),words.end(),0xDEADBEEFu);std::fill(known.begin(),known.end(),0xC0DEC0DEu);
    const auto before_read=device->stats();
    (void)device->execute(words,known,{}, {},ranges);
    require(words.front()==0x40201008u && known.front()==0xFFFFFFFFu &&
        std::all_of(words.begin()+1,words.end(),[](auto n){return n==0xDEADBEEFu;}) &&
        std::all_of(known.begin()+1,known.end(),[](auto n){return n==0xC0DEC0DEu;}),
        "range-only readback leaves every unrequested host value and mask untouched");
    require(device->stats().readback_bytes==before_read.readback_bytes+8u &&
        device->stats().upload_bytes==before_read.upload_bytes && device->stats().batches==before_read.batches,
        "range-only readback accounts values and masks without raster or upload");
    words.front()=0x8ABCDEF0u;known.front()=0xFFFFFFFFu;
    const auto before_upload=device->stats();
    (void)device->execute(words,known,{},ranges,{});
    words.front()=known.front()=0u;
    (void)device->execute(words,known,{}, {},ranges);
    require(words.front()==0x8ABCDEF0u && known.front()==0xFFFFFFFFu &&
        device->stats().upload_bytes==before_upload.upload_bytes+8u,
        "range-only upload becomes the resident value and known-bit mask");
    const auto before_invalid=device->stats();
    for(const GsGpuRange invalid:std::array{GsGpuRange{0,0},
        GsGpuRange{static_cast<std::uint32_t>(words.size()),1},GsGpuRange{1,0xFFFFFFFFu}}) {
        const std::array invalid_ranges{pixel,invalid};
        rejects([&]{(void)device->execute(words,known,{},invalid_ranges,{});},"range is out of bounds");
        rejects([&]{(void)device->execute(words,known,{}, {},invalid_ranges);},"range is out of bounds");
    }
    (void)device->execute(words,known,{}, {},{});
    require(device->stats().submissions==before_invalid.submissions &&
        device->stats().upload_bytes==before_invalid.upload_bytes &&
        device->stats().readback_bytes==before_invalid.readback_bytes,
        "all ranges validate before transfers; an empty operation submits no work");
    std::fill(words.begin(),words.end(),0u);std::fill(known.begin(),known.end(),0u);
    const std::array unknown_read{blend};
    std::string original_failure;
    try{(void)device->execute(words,known,unknown_read);}
    catch(const std::exception& error){original_failure=error.what();}
    require(original_failure.find("unknown VRAM")!=std::string::npos,
        "a failed device batch reports the exact unknown VRAM cause");
    require(std::all_of(words.begin(),words.end(),[](auto n){return n==0u;}) &&
        std::all_of(known.begin(),known.end(),[](auto n){return n==0u;}),
        "a failed device batch commits no fabricated values or knowledge");
    const auto failed_stats=device->stats();std::string repeated_failure;
    try{(void)device->execute(words,known,commands);}
    catch(const std::exception& error){repeated_failure=error.what();}
    require(repeated_failure==original_failure && device->stats().submissions==failed_stats.submissions,
        "stopped device preserves the original failure and never replays a partial batch");
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
    std::cout<<"GPU selected: "<<gpu->device_name()<<'\n'<<std::flush;
    check_device_batch(api);check_partial_coherence(gpu);check_resident_batches(gpu);
    check_completion_order(gpu);check_pending_knowledge(gpu);
    check_raster(gpu);check_pipeline(gpu);check_fallback_and_transfers(gpu);
    check_guest(gpu,argc>1?std::filesystem::path{argv[1]}:std::filesystem::path{});
    require(gpu->stats().primitives>0 && gpu->stats().batches>0,"commands were rasterized by the selected GPU");
    std::cout<<"GPU raster, full VRAM/knowledge, fallback and guest checkpoint equivalence passed on "
        <<gpu->device_name()<<"; GPU primitives="<<gpu->stats().primitives<<", batches="<<gpu->stats().batches<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
