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
    const auto different=std::mismatch(a.begin(),a.end(),b.begin());
    if(different.first!=a.end()) {
        const auto word=static_cast<unsigned>(different.first-a.begin());
        throw std::runtime_error(label+": first VRAM difference at byte "+boot_checks::hex8(word*4u)+
            ", expected="+boot_checks::hex8(*different.first)+", actual="+boot_checks::hex8(*different.second));
    }
    const auto ak=cpu.gs.vram().known_bits(),bk=gpu.gs.vram().known_bits();
    const auto unknown=std::mismatch(ak.begin(),ak.end(),bk.begin());
    if(unknown.first!=ak.end())throw std::runtime_error(label+": first knowledge difference at byte "+
        boot_checks::hex8(static_cast<unsigned>(unknown.first-ak.begin())*4u)+", expected="+
        boot_checks::hex8(*unknown.first)+", actual="+boot_checks::hex8(*unknown.second));
    require(cpu.gs.vram().revision()==gpu.gs.vram().revision(),label+": VRAM revision");
    require(cpu.gs.vram().pixel_writes()==gpu.gs.vram().pixel_writes(),label+": proven color writes");
    require(cpu.gif.packets()==gpu.gif.packets(),label+": original GIF packet order");
    require(cpu.gs.observe_csr().value==gpu.gs.observe_csr().value &&
        cpu.gs.observe_csr().unknown==gpu.gs.observe_csr().unknown,label+": CSR flags and uncertainty");
    if(scan) {
        const auto af=cpu.gs.scanout(1234),bf=gpu.gs.scanout(1234);
        require(af.width==bf.width && af.height==bf.height && af.stride==bf.stride && af.rgba.size()==bf.rgba.size(),
            label+": scanout geometry");
        const auto pixel=std::mismatch(af.rgba.begin(),af.rgba.end(),bf.rgba.begin());
        if(pixel.first!=af.rgba.end()) {
            const auto offset=static_cast<unsigned>(pixel.first-af.rgba.begin()),x=(offset%af.stride)/4u,y=offset/af.stride;
            throw std::runtime_error(label+": first RGBA difference at ("+std::to_string(x)+","+std::to_string(y)+
                "), channel "+std::to_string(offset%4u));
        }
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
void check_fused_readback(std::shared_ptr<GsGpu> backend) {
    GsVram cpu,gpu;gpu.set_gpu_backend(backend);
    auto first=rectangle(0x80402010u);first.right=first.bottom=8;
    auto second=first;second.base=2u*GsVram::kPageBytes;second.color=0x80806040u;
    const auto before=backend->stats();
    enqueue_pair(cpu,gpu,first);enqueue_pair(cpu,gpu,second);
    require(backend->stats().submissions==before.submissions,"both original rectangles remain queued before the CPU read");
    require(gpu.read(first.base,64,0,0,0)==cpu.read(first.base,64,0,0,0),
        "CPU read observes its queued GPU rectangle");
    const auto first_read=backend->stats();
    require(first_read.submissions==before.submissions+1u && first_read.batches==before.batches+1u &&
        first_read.readback_bytes==before.readback_bytes+2u*GsVram::kPageBytes,
        "one submission executes both rectangles and reads only the requested page values and masks");
    require(first_read.upload_bytes==before.upload_bytes+4u*GsVram::kPageBytes,
        "fused submission uploads exactly the two initially unknown destination pages");
    require(gpu.read(second.base,64,0,0,0)==cpu.read(second.base,64,0,0,0),
        "the second rectangle remains resident until its own CPU read");
    const auto second_read=backend->stats();
    require(second_read.submissions>=first_read.submissions && second_read.submissions<=first_read.submissions+1u &&
        second_read.readback_syncs==first_read.readback_syncs+1u && second_read.batches==first_read.batches &&
        second_read.upload_bytes==first_read.upload_bytes &&
        second_read.readback_bytes==first_read.readback_bytes+2u*GsVram::kPageBytes,
        "the second page downloads without replaying commands or uploading resident data");
    same_vram(cpu,gpu,"fused pending execution and selective page readback");
    require(backend->stats().submissions==second_read.submissions,
        "full inspection after both selective reads requires no additional synchronization");
}
void check_detach_coherence(std::shared_ptr<GsGpu> backend) {
    GsVram cpu,gpu;gpu.set_gpu_backend(backend);
    auto page=rectangle(0x80402010u);page.right=64;page.bottom=32;
    enqueue_pair(cpu,gpu,page);
    require(gpu.page_known(0,0xFFFFFFFFu),"GPU full-page draw establishes the page proof before detaching");
    gpu.set_gpu_backend({});
    for(auto* v:{&cpu,&gpu}) {
        require(v->page_known(0,0xFFFFFFFFu),"detached CPU page starts fully known");
        v->write(0,64,0,0,0,0x80402010u,0,0x80000000u);
        require(!v->page_known(0,0xFFFFFFFFu),"public CPU page proof observes a newly unknown bit");
        v->write(0,64,0,0,0,0x80402010u);
        require(v->page_known(0,0xFFFFFFFFu),"public CPU page proof observes restored knowledge");
        v->write(0,64,0,0,0,0x80402010u,0,0x80000000u);
    }
    gpu.set_gpu_backend(backend);
    require(!gpu.page_known(0,0xFFFFFFFFu),"reattachment cannot reuse a proof cached before CPU writes");
    enqueue_pair(cpu,gpu,rectangle(0x00001234u,0xFFFF0000u));
    same_vram(cpu,gpu,"detach, CPU knowledge changes and reattached masked GPU write");
}
void check_fused_scanout(std::shared_ptr<GsGpu> backend) {
    Stream cpu,accelerated(backend);cpu.setup();accelerated.setup();
    for(auto* s:{&cpu,&accelerated}) {
        s->gs.write(kGsSyncv,8,2u|(2ull<<42u),0,{});
        s->sprite(0,0,width,height,0x80402010u);
    }
    const auto before=backend->stats();
    const auto expected=cpu.gs.scanout(1234),actual=accelerated.gs.scanout(1234);
    const auto after=backend->stats();
    require(after.batches==before.batches+1u && after.submissions==before.submissions+1u &&
        after.waits<=before.waits+1u && after.readback_syncs==before.readback_syncs+1u &&
        after.readback_bytes==before.readback_bytes+2u*GsVram::kPageBytes,
        "pending scanout downloads its pages with one submission and at most one fence wait");
    require(expected.rgba==actual.rgba && expected.width==actual.width && expected.height==actual.height &&
        expected.stride==actual.stride && expected.ee_cycle==actual.ee_cycle && expected.revision==actual.revision &&
        expected.timing && actual.timing && expected.timing->epoch==actual.timing->epoch &&
        expected.timing->half_line_cycles==actual.timing->half_line_cycles &&
        expected.timing->field_half_lines==actual.timing->field_half_lines &&
        expected.timing->blank_half_lines==actual.timing->blank_half_lines,
        "fused scanout preserves RGBA, geometry, guest cycle, revision and original field timing");
    same(cpu,accelerated,"fused pending scanout full VRAM and knowledge");
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
    require(completed.batches==before.batches+1u && completed.readback_bytes==before.readback_bytes,
        "FINISH completes and TEXFLUSH invalidates without downloading framebuffer contents");
    same(cpu,accelerated,"resident GPU completion, IDs and interrupt ordering");
    require(backend->stats().batches==before.batches+2u,"a later reader completes the raster retained across TEXFLUSH");
}
void check_pending_knowledge(std::shared_ptr<GsGpu> backend) {
    paired(backend,"pending unknown FBA invalidates destination-alpha proof",[](Stream& s){
        s.sprite(0,0,width,height,0x80000000u); // high alpha bit is known despite unknown FBA
        s.sprite(0,0,8,8,0x00112233u); // unknown FBA may now set that bit
        s.ad(0x42,0x58); // (Cs - 0) * Ad + Cd consumes destination alpha
        rejects([&]{s.sprite(0,0,8,8,0x80402010u,0,0x46);},"unwritten bits");
    },0,0,true,false);
    paired(backend,"pending conditional RGB remains symbolic through blending",[](Stream& s){
        s.upload(0,0,width,height,0x8000000080000000ull);
        s.sprite(0,0,8,8,0x80402010u); // unknown SCANMSK joins draw and skip
        s.ad(0x22,0);s.ad(0x42,0x44);
        s.sprite(0,0,8,8,0x40402010u,0,0x46);
        rejects([&]{(void)s.gs.vram().read(0,64,0,0,0);},"unwritten bits");
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
constexpr unsigned texture_base=0x10000u,palette_base=0x30000u;
GsVram& fixture_vram(Stream& s) {
    // Synthetic source setup and explicit unknown-bit injection use the
    // public coherent VRAM writes; the GsRegisters object itself is mutable.
    return const_cast<GsVram&>(s.gs.vram());
}
std::uint32_t texture_grid(unsigned x,unsigned y) {
    return 0x80000000u|(x&255u)|((y&255u)<<8u)|(((3u*x+5u*y)&255u)<<16u);
}
std::uint32_t palette_color(unsigned index,unsigned bank=0) {
    return 0x80000000u|((7u*index+bank)&255u)|(((11u*index+3u*bank)&255u)<<8u)|
        (((13u*index+5u*bank)&255u)<<16u);
}
void palette_source(Stream& s,unsigned base,unsigned count,unsigned bank=0) {
    for(unsigned i=0;i<count;++i) {
        // Independent CSM1 index-to-source layout, also exercised by T8
        // indices 8,16,24 whose two exchanged index bits are distinct.
        const auto x=(i&7u)|((i&16u)>>1u),y=((i&8u)>>3u)|((i&0xE0u)>>4u);
        fixture_vram(s).write(base,64,0,x,y,palette_color(i,bank));
    }
}
void texture_source(Stream& s,unsigned psm) {
    const unsigned w=psm<=1u?128u:256u,h=psm<=1u?64u:psm==19u?128u:256u;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
        fixture_vram(s).write(texture_base,w,psm,x,y,psm<=1u?texture_grid(x,y):(x+3u*y)&(psm==20u?15u:255u));
    if(psm>=19u)palette_source(s,palette_base,psm==20u?16u:256u);
    s.ad(0x3F,0);
}
std::uint64_t texture_register(unsigned psm,unsigned bank=0,unsigned cld=0,bool tcc=true,bool decal=true,
                               unsigned source=texture_base,unsigned palette=palette_base) {
    const unsigned tw=psm<=1u?7u:8u,th=psm<=1u?6u:psm==19u?7u:8u,bw=psm<=1u?128u:256u;
    return source/256u|(std::uint64_t{bw/64u}<<14u)|(std::uint64_t{psm}<<20u)|
        (std::uint64_t{tw}<<26u)|(std::uint64_t{th}<<30u)|(std::uint64_t{tcc}<<34u)|
        (std::uint64_t{decal}<<35u)|(std::uint64_t{palette/256u}<<37u)|
        (std::uint64_t{bank}<<56u)|(std::uint64_t{cld}<<61u);
}
void textured_uv(Stream& s,int x0,int y0,int x1,int y1,unsigned u0,unsigned v0,unsigned u1,unsigned v1,
                 unsigned color=0x80808080u,bool blend=false,unsigned z=0) {
    s.ad(0,0x116u|(blend?64u:0u));s.ad(1,color);
    s.ad(3,u0|(std::uint64_t{v0}<<16u));s.vertex(x0*16,y0*16,z);
    s.ad(3,u1|(std::uint64_t{v1}<<16u));s.vertex(x1*16,y1*16,z);
}
void texture_state(Stream& s,unsigned psm,bool tcc=true,bool decal=true,unsigned bank=0,unsigned cld=1) {
    s.ad(6,texture_register(psm,bank,psm>=19u?cld:0u,tcc,decal));
    s.ad(8,0);s.ad(0x14,0);s.ad(0x3B,0x40u);
}
void check_textured_nearest(std::shared_ptr<GsGpu> gpu) {
    for(const auto psm:{20u,19u,0u,1u}) {
        Stream cpu,accelerated(gpu);
        for(auto* s:{&cpu,&accelerated}) {
            s->setup();texture_source(*s,psm);s->sprite(0,0,width,height,0x80604020u);texture_state(*s,psm);
        }
        const unsigned u=psm<=1u?60u:124u,v=psm<=1u?28u:psm==19u?60u:124u;
        const auto before=accelerated.gs.vram().gpu_primitives();
        for(auto* s:{&cpu,&accelerated})textured_uv(*s,2,2,10,6,u*16u,v*16u,(u+8u)*16u,(v+4u)*16u);
        require(accelerated.gs.vram().gpu_primitives()==before+1u,"nearest format "+std::to_string(psm)+" actually selects GPU texture rasterization");
        same(cpu,accelerated,"nearest texture page edges "+std::to_string(psm));
        for(unsigned y=0;y<4u;++y)for(unsigned x=0;x<8u;++x) {
            const auto expected=psm>=19u?palette_color((u+x+3u*(v+y))&(psm==20u?15u:255u)):
                psm==1u?(texture_grid(u+x,v+y)&0xFFFFFFu)|0x40000000u:texture_grid(u+x,v+y);
            require(accelerated.gs.vram().read(0,64,0,x+2u,y+2u)==expected,
                "independent nearest expected pixel across source pages, format "+std::to_string(psm));
        }
        const auto uploaded=gpu->stats().upload_bytes;
        for(auto* s:{&cpu,&accelerated})textured_uv(*s,12,2,13,3,64,64,80,80);
        accelerated.gs.vram().submit_gpu();
        require(gpu->stats().upload_bytes==uploaded,"read-only texture pages are retained after the first GPU batch");
        for(auto* s:{&cpu,&accelerated}) {
            // Reversed screen axes retain their original endpoint mapping;
            // clipping is applied only to coverage, never to the UV origin.
            s->ad(0x40,3u|(10ull<<16u)|(3ull<<32u)|(7ull<<48u));
            textured_uv(*s,12,9,0,1,0,0,12u*16u,8u*16u);
        }
        same(cpu,accelerated,"inverted axes and clipped UV origin "+std::to_string(psm));
        for(unsigned y=3;y<8u;++y)for(unsigned x=3;x<11u;++x) {
            const unsigned sx=12u-x,sy=9u-y;
            const auto expected=psm>=19u?palette_color((sx+3u*sy)&(psm==20u?15u:255u)):
                psm==1u?(texture_grid(sx,sy)&0xFFFFFFu)|0x40000000u:texture_grid(sx,sy);
            require(accelerated.gs.vram().read(0,64,0,x,y)==expected,"independent inverted/scissor texture pixel");
        }
    }
}
void check_texture_palette_snapshots(std::shared_ptr<GsGpu> gpu) {
    Stream cpu({},16),accelerated(gpu,16);
    for(auto* s:{&cpu,&accelerated}) {
        s->setup();texture_source(*s,20);palette_source(*s,palette_base+8192u,16,7);
        s->sprite(0,0,width,height,0x80000000u);s->ad(0x3F,0);
        s->ad(6,texture_register(20,0,1));
        s->ad(6,texture_register(20,1,1,true,true,texture_base,palette_base+8192u));
        s->ad(8,0);s->ad(0x14,0);
    }
    const auto before=gpu->stats();
    const auto primitives=accelerated.gs.vram().gpu_primitives();
    for(auto* s:{&cpu,&accelerated}) {
        s->ad(6,texture_register(20,0));textured_uv(*s,0,0,8,2,0,0,128,32);
        s->ad(6,texture_register(20,1));textured_uv(*s,8,0,16,2,0,0,128,32);
    }
    require(accelerated.gs.vram().gpu_primitives()==primitives+2u,"two partially loaded T4 banks both rasterize on GPU");
    require(gpu->stats().batches==before.batches,"later TEX0 bank selection does not execute or replace an earlier palette snapshot");
    same(cpu,accelerated,"queued per-command T4 palette bank snapshots");
    for(unsigned y=0;y<2u;++y)for(unsigned x=0;x<8u;++x) {
        const auto i=(x+3u*y)&15u;
        require(accelerated.gs.vram().read(0,64,0,x,y)==palette_color(i) &&
            accelerated.gs.vram().read(0,64,0,x+8u,y)==palette_color(i,7),"independent per-command palette bank values");
    }
    for(auto* s:{&cpu,&accelerated}) {
        // Reloading from changed source memory changes only future draws.
        palette_source(*s,palette_base,16,19);s->ad(0x3F,0);
        s->ad(0x16,texture_register(20,0,1));textured_uv(*s,16,0,24,2,0,0,128,32);
        const auto fallbacks=s->gs.vram().cpu_fallbacks(),primitives_before=s->gs.vram().gpu_primitives();
        s->ad(0x42,0x44);s->ad(0,3u|128u|64u);s->ad(1,0x80402010u);
        s->vertex(26*16,3*16);s->vertex(30*16,3*16);s->vertex(26*16,7*16);
        s->sprite(32,0,40,8,0x80AABBCCu);
        if(s->gs.gpu_backend())require(s->gs.vram().cpu_fallbacks()==fallbacks &&
            s->gs.vram().gpu_primitives()==primitives_before+2u,"CLUT reload -> textured GPU -> AA1 GPU -> GPU preserves explicit routing");
        s->ad(6,texture_register(20,2));s->ad(0,0x116);s->ad(1,0x80808080u);s->ad(3,0);s->vertex(0,64);
        s->ad(3,128u|(32ull<<16u));
        const auto writes=s->gs.vram().pixel_writes();
        rejects([&]{s->vertex(128,96);},"unloaded CLUT");
        require(s->gs.vram().pixel_writes()==writes,"an unloaded T4 bank refuses before its first destination write");
    }
    same(cpu,accelerated,"TEX2 reload and unloaded partial CLUT bank");
    require(accelerated.gs.vram().read(0,64,0,16,0)==palette_color(0,19),"TEX2 reload affects only the later sprite");
}
void check_clut_reload_pending(std::shared_ptr<GsGpu> gpu) {
    Stream cpu,accelerated(gpu);
    for(auto* s:{&cpu,&accelerated}) {
        s->setup();texture_source(*s,20);palette_source(*s,palette_base+8192u,16,7);
        s->sprite(0,0,width,height,0x80000000u);s->ad(0x3F,0);texture_state(*s,20);
    }
    const auto before=gpu->stats();
    const auto primitives=accelerated.gs.vram().gpu_primitives();
    for(auto* s:{&cpu,&accelerated}) {
        textured_uv(*s,0,0,8,2,0,0,128,32);
        s->ad(6,texture_register(20,0,1,true,true,texture_base,palette_base+8192u));
        textured_uv(*s,8,0,16,2,0,0,128,32);
    }
    require(accelerated.gs.vram().gpu_primitives()==primitives+2u && gpu->stats().batches==before.batches,
        "a CPU-resident CLUT reload preserves both pending draws and their independent palette versions");
    for(auto* s:{&cpu,&accelerated})fixture_vram(*s).write(texture_base,256,20,0,0,7);
    require(gpu->stats().batches==before.batches+1u,"CPU overwrite of a queued texture source completes its earlier readers first");
    same(cpu,accelerated,"pending CLUT snapshots survive a later source overwrite");
    require(accelerated.gs.vram().read(0,64,0,0,0)==palette_color(0) &&
        accelerated.gs.vram().read(0,64,0,8,0)==palette_color(0,7),"earlier draws retain original texture indices and palette versions");
    const auto uploaded=gpu->stats().upload_bytes;
    for(auto* s:{&cpu,&accelerated}){s->ad(0x3F,0);textured_uv(*s,16,0,17,1,0,0,16,16);}
    accelerated.gs.vram().submit_gpu();
    require(gpu->stats().upload_bytes==uploaded+2u*GsVram::kPageBytes,"the CPU source edit uploads only its value/mask page");
    same(cpu,accelerated,"later GPU texture sees the ordered partial CPU source change");
    require(accelerated.gs.vram().read(0,64,0,16,0)==palette_color(7,7),"later draw consumes the new index through the new CLUT snapshot");
}
void check_texture_functions(std::shared_ptr<GsGpu> gpu) {
    for(const unsigned psm:{0u,1u,19u,20u})for(const bool tcc:{false,true})for(const bool decal:{false,true}) {
        Stream cpu,accelerated(gpu);
        constexpr auto shade=0x40804020u;
        for(auto* s:{&cpu,&accelerated}) {
            s->setup(1);texture_source(*s,psm);s->sprite(0,0,width,height,0x00808080u);texture_state(*s,psm,tcc,decal);
            textured_uv(*s,0,0,1,1,16,16,32,32,shade);
        }
        same(cpu,accelerated,"TCC/TFX/TEXA CT24 destination");
        auto sampled=psm>=19u?palette_color(4):texture_grid(1,1);
        if(psm==1u)sampled=(sampled&0xFFFFFFu)|0x40000000u;
        unsigned expected=0;
        for(unsigned c=0;c<3u;++c) {
            const auto f=(shade>>(c*8u))&255u,t=(sampled>>(c*8u))&255u;
            expected|=(decal?t:std::min(255u,f*t/128u))<<(c*8u);
        }
        require(accelerated.gs.vram().read(0,64,1,0,0)==expected,"independent MODULATE/DECAL RGB expected result");
    }
    paired(gpu,"CT24 TEXA black AEM and unknown unused high byte",[](Stream& s){
        for(unsigned y=0;y<32u;++y)for(unsigned x=0;x<64u;++x)fixture_vram(s).write(texture_base,64,1,x,y,0);
        s.ad(0x3F,0);s.sprite(0,0,width,height,0x80000000u);
        s.ad(6,texture_register(1,0,0,true,true));s.ad(8,0);s.ad(0x14,0);s.ad(0x3B,0x8040u);
        textured_uv(s,0,0,1,1,0,0,16,16);
        require(s.gs.vram().read(0,64,0,0,0)==0,"AEM black expands to transparent black, independent of unknown source high byte");
    });
}
void check_texture_coordinates(std::shared_ptr<GsGpu> gpu) {
    Stream cpu,accelerated(gpu);
    const auto small_texture=[] {return (texture_register(0)&~((15ull<<26u)|(15ull<<30u)))|(3ull<<26u)|(3ull<<30u);};
    for(auto* s:{&cpu,&accelerated}) {
        s->setup();texture_source(*s,0);s->sprite(0,0,width,height,0x80000000u);
        s->ad(6,small_texture());s->ad(0x14,0);
        const std::array<std::uint64_t,4> clamps{0,5,2u|(2ull<<2u)|(2ull<<4u)|(5ull<<14u)|(2ull<<24u)|(5ull<<34u),
            3u|(3ull<<2u)|(3ull<<4u)|(8ull<<14u)|(3ull<<24u)|(8ull<<34u)};
        for(unsigned n=0;n<clamps.size();++n) {
            s->ad(8,clamps[n]);textured_uv(*s,static_cast<int>(n),0,static_cast<int>(n+1u),1,160,160,176,176);
        }
        s->ad(8,0);s->ad(0,0x16);s->ad(1,0x3F00000080808080ull);
        s->ad(2,0xBE000000BE000000ull);s->vertex(4*16,0);
        s->ad(1,0x4000000080808080ull);s->ad(2,0x3EC000003EC00000ull);s->vertex(5*16,16);
    }
    same(cpu,accelerated,"all clamp modes and exact negative STQ sprite");
    const std::array<unsigned,5> positions{2,7,5,10,7};
    for(unsigned x=0;x<positions.size();++x)require(accelerated.gs.vram().read(0,64,0,x,0)==texture_grid(positions[x],positions[x]),
        "independent repeat/clamp/region-clamp/region-repeat/signed-STQ samples");
    for(auto* s:{&cpu,&accelerated}) {
        s->ad(0,0x16);s->ad(1,0x3F80000080808080ull);s->ad(2,0);s->vertex(0,32);
        s->ad(1,0x4040000080808080ull);s->ad(2,0x3F8000003F800000ull);
        const auto writes=s->gs.vram().pixel_writes();
        rejects([&]{s->vertex(16,48);},"fractional STQ");
        require(s->gs.vram().pixel_writes()==writes,"inexact STQ remains a common refusal before drawing");
        s->gif.reset(); // A refused A+D word was not committed by the parser.
        s->ad(8,0);s->ad(0,0x116);s->ad(3,8u|(8ull<<16u));s->vertex(0,48);
        s->ad(3,24u|(24ull<<16u));
        rejects([&]{s->vertex(48,64);},"fractional UV DDA");
    }
    same(cpu,accelerated,"strict STQ and fractional UV precision refusal");
    for(const unsigned precision:{0u,2u}) {
        Stream a({},precision),b(gpu,precision);
        for(auto* s:{&a,&b}) {
            s->setup();texture_source(*s,0);
            for(unsigned y=0;y<8u;++y)for(unsigned x=0;x<8u;++x)
                fixture_vram(*s).write(texture_base,128,0,x,y,0x80000000u|(x*16u)|(y*16u<<8u)|((x+y)*8u<<16u));
            s->sprite(0,0,width,height,0x80000000u);s->ad(6,small_texture());s->ad(8,0);s->ad(0x14,0x60);
            textured_uv(*s,0,0,1,1,12,20,28,36);
            require(s->gs.vram().read(0,64,0,0,0)==0x80080C04u,"independent exact bilinear quarter/three-quarter sample");
            if(precision) {
                textured_uv(*s,1,0,2,1,9,9,25,25);
                require(s->gs.vram().read(0,64,0,1,0)==0x80000101u,"explicit filter hypothesis truncates horizontal then vertical");
            } else {
                const auto writes=s->gs.vram().pixel_writes();
                rejects([&]{textured_uv(*s,1,0,2,1,9,9,25,25);},"fractional bilinear truncation");
                require(s->gs.vram().pixel_writes()==writes,"physical precision uncertainty is not silently rounded by the GPU");
            }
        }
        same(a,b,"bilinear exact and explicitly selected staged truncation");
    }
    paired(gpu,"explicit nominal rational sprite UV",[](Stream& s){
        texture_source(s,0);s.sprite(0,0,width,height,0x80000000u);texture_state(s,0);
        textured_uv(s,0,2,3,3,8,8,24,24);
        require(s.gs.vram().read(0,64,0,0,2)==texture_grid(0,0) &&
            s.gs.vram().read(0,64,0,1,2)==texture_grid(0,0) &&
            s.gs.vram().read(0,64,0,2,2)==texture_grid(1,0),"rational experiment floors the same 10.4 UV as the CPU");
    },0,1);
}
void check_texture_pipeline(std::shared_ptr<GsGpu> gpu) {
    for(unsigned fail=0;fail<4u;++fail)for(unsigned pabe=0;pabe<2u;++pabe) {
        Stream cpu,accelerated(gpu);
        for(auto* s:{&cpu,&accelerated}) {
            s->setup();texture_source(*s,20);
            for(unsigned i=0;i<3u;++i)fixture_vram(*s).write(palette_base,64,0,i,0,((0x40u+i*0x40u)<<24u)|0x80u);
            s->ad(0x4E,32u);s->sprite(0,0,width,height,0x80402010u,7);
            texture_state(*s,20);s->ad(0x49,pabe);s->ad(0x42,0x44u);
            s->ad(0x4C,(1u<<16u)|(0x0000FF00ull<<32u));
            s->ad(0x47,(1u<<16u)|(2u<<17u)|1u|(5u<<1u)|(0x80u<<4u)|(fail<<12u));
            textured_uv(*s,0,0,3,1,0,0,48,16,0x80808080u,true,9);
        }
        same(cpu,accelerated,"textured alpha/AFAIL/PABE/FBMSK/depth");
        const auto expected=fail==0u || fail==2u?0x80402010u:
            fail==3u?(pabe?0x80002080u:0x80202048u):(pabe?0x40002080u:0x40202048u);
        require(accelerated.gs.vram().read(0,64,0,0,0)==expected &&
            accelerated.gs.vram().read_depth(32u*8192u,64,0,0,0)==(fail==2u?9u:7u),
            "independent textured alpha-fail color/depth expected result");
        require(accelerated.gs.vram().read(0,64,0,1,0)==0x80002080u &&
            accelerated.gs.vram().read(0,64,0,2,0)==0xC00020B8u &&
            accelerated.gs.vram().read_depth(32u*8192u,64,0,1,0)==9u,
            "independent variable source alpha blend, saturation, mask and depth result");
    }
    Stream cpu,accelerated(gpu);
    for(auto* s:{&cpu,&accelerated}) {
        s->setup(0,true,false);texture_source(*s,20);fixture_vram(*s).write(palette_base,64,0,0,0,0x40112233u);
        s->sprite(0,0,width,height,0x80000000u);texture_state(*s,20);
        textured_uv(*s,0,0,1,1,0,0,16,16);
        require(s->gs.vram().read(0,64,1,0,0)==0x112233u,"known textured RGB does not require unknown FBA alpha");
        rejects([&]{(void)s->gs.vram().read(0,64,0,0,0);},"unwritten bits");
    }
    same(cpu,accelerated,"textured alpha preserves unknown FBA proof",false);
}
void check_texture_residency(std::shared_ptr<GsGpu> gpu) {
    Stream cpu,accelerated(gpu);
    for(auto* s:{&cpu,&accelerated}) {
        s->setup();s->sprite(0,0,width,height,0x80402010u);
        s->ad(0x4C,8u|(1u<<16u));s->sprite(0,0,64,32,0x80A06020u);s->ad(0x3F,0);
        s->ad(0x4C,1u<<16u);s->ad(6,256u|(1ull<<14u)|(6ull<<26u)|(5ull<<30u)|(1ull<<34u)|(1ull<<35u));
        s->ad(8,0);s->ad(0x14,0);
    }
    // Establish residency before measuring reuse; TEXFLUSH is not a host fence.
    accelerated.gs.vram().submit_gpu();
    const auto before=gpu->stats();
    const auto primitives=accelerated.gs.vram().gpu_primitives();
    for(auto* s:{&cpu,&accelerated})textured_uv(*s,0,0,8,8,0,0,128,128);
    accelerated.gs.vram().submit_gpu();
    const auto after=gpu->stats();
    require(accelerated.gs.vram().gpu_primitives()==primitives+1u,"GPU-rendered texture is consumed by a later GPU sprite");
    require(after.readback_bytes==before.readback_bytes && after.upload_bytes==before.upload_bytes,
        "resident rendered texture and framebuffer need no VRAM readback or reupload");
    same(cpu,accelerated,"render-to-texture resident dependency");
    require(accelerated.gs.vram().read(0,64,0,0,0)==0x80A06020u,"rendered source expected color");
    for(auto* s:{&cpu,&accelerated}) {
        // A transfer dirties the earlier cached page; only guest TEXFLUSH
        // authorizes sampling the new bytes. Preserve the neighboring words.
        s->upload(texture_base,0,2,1,0x8040201080112233ull);
        const auto writes=s->gs.vram().pixel_writes();
        rejects([&]{textured_uv(*s,8,0,10,1,0,0,32,16);},"cached texture page changed");
        require(s->gs.vram().pixel_writes()==writes,"stale texture cache refuses before writing");
        s->gif.reset();
        s->ad(0x3F,0);textured_uv(*s,8,0,10,1,0,0,32,16);
        s->sprite(16,0,24,8,0x80FEDCBAu);
    }
    same(cpu,accelerated,"resident source partial upload, TEXFLUSH, textured return");
    require(accelerated.gs.vram().read(0,64,0,8,0)==0x80112233u &&
        accelerated.gs.vram().read(0,64,0,9,0)==0x80402010u &&
        accelerated.gs.vram().read(texture_base,64,0,2,0)==0x80A06020u,"partial texture upload preserves its resident neighbors");
    for(auto* s:{&cpu,&accelerated}) {
        s->ad(0x4C,8u|(1u<<16u));s->ad(0,0x116);s->ad(1,0x80808080u);s->ad(3,0);s->vertex(0,0);
        s->ad(3,128u|(128ull<<16u));const auto writes=s->gs.vram().pixel_writes();
        rejects([&]{s->vertex(128,128);},"cache feedback");
        require(s->gs.vram().pixel_writes()==writes,"source/destination alias refuses before the first shader write");
    }
    same(cpu,accelerated,"feedback refusal preserves prior resident work");
    paired(gpu,"unknown indexed nibble never becomes zero",[](Stream& s){
        texture_source(s,20);s.sprite(0,0,width,height,0x80000000u);texture_state(s,20);
        fixture_vram(s).write(texture_base,256,20,0,0,7,0,1);
        const auto writes=s.gs.vram().pixel_writes();
        rejects([&]{textured_uv(s,0,0,1,1,0,0,16,16);},"unwritten bits");
        require(s.gs.vram().pixel_writes()==writes,"unknown source nibble causes synchronous refusal before destination changes");
        s.gif.reset();
        fixture_vram(s).write(texture_base,256,20,0,0,7);textured_uv(s,0,0,1,1,0,0,16,16);
        require(s.gs.vram().read(0,64,0,0,0)==palette_color(7),"later known nibble permits the original palette color");
    });
}
void check_depth16s(std::shared_ptr<GsGpu> gpu) {
    constexpr unsigned zb=32u*8192u;
    for(const unsigned psm:{0u,1u})for(const unsigned zfunc:{2u,3u})for(const bool zmask:{false,true}) {
        Stream cpu,accelerated(gpu);
        for(auto* s:{&cpu,&accelerated}) {
            s->setup(psm);texture_source(*s,20);
            s->ad(0x4C,(2u<<16u)|(std::uint64_t{psm}<<24u));
            s->ad(0x40,(63ull<<16u)|(127ull<<48u));s->ad(0x4E,32u|(10u<<24u));
            s->ad(0x47,3u<<16u);s->sprite(0,0,64,128,0x80402010u,7);
            s->ad(0x40,(63ull<<16u)|(65ull<<48u));
            texture_state(*s,20);s->ad(0x47,(1u<<16u)|(zfunc<<17u));
            s->ad(0x4E,32u|(10u<<24u)|(std::uint64_t{zmask}<<32u));
        }
        const auto primitives=accelerated.gs.vram().gpu_primitives();
        for(auto* s:{&cpu,&accelerated})textured_uv(*s,0,63,64,66,0,0,64u*16u,48,0x80808080u,false,7);
        require(accelerated.gs.vram().gpu_primitives()==primitives+1u,"Z16S comparison selects textured GPU raster across 64-row depth pages");
        same(cpu,accelerated,"PSMZ16S GEQUAL/GREATER equal boundary CT"+std::to_string(psm));
        const auto equal_color=(zfunc==2u?palette_color(3):0x80402010u)&(psm==1u?0xFFFFFFu:0xFFFFFFFFu);
        require(accelerated.gs.vram().read(0,128,psm,0,64)==equal_color,
            "independent Z16S equal-depth comparison result");
        for(auto* s:{&cpu,&accelerated}) {
            textured_uv(*s,0,63,64,66,0,0,64u*16u,48,0x80808080u,false,9);
            textured_uv(*s,0,64,1,65,16,0,32,16,0x80808080u,false,8);
        }
        same(cpu,accelerated,"PSMZ16S write/mask, adjacent halfwords and source dependency");
        require(accelerated.gs.vram().read_depth(zb,128,10,0,64)==(zmask?7u:9u) &&
            accelerated.gs.vram().read_depth(zb,128,10,8,64)==(zmask?7u:9u),
            "independent Z16S mask preserves or changes both halves of the same physical word");
        const auto packed=accelerated.gs.vram().word_at(zb+22528u);
        require(packed==(zmask?0x00070007u:0x00090009u),"independent PSMZ16S page/halfword address vector and atomic neighbor preservation");
        const auto expected=zmask?palette_color(1):palette_color(3);
        require(accelerated.gs.vram().read(0,128,psm,0,64)==(expected&(psm==1u?0xFFFFFFu:0xFFFFFFFFu)),
            "Z16S written depth controls a later textured sprite in original command order");
    }
    for(unsigned fail=0;fail<4u;++fail) {
        Stream cpu,accelerated(gpu);
        for(auto* s:{&cpu,&accelerated}) {
            s->setup();texture_source(*s,20);fixture_vram(*s).write(palette_base,64,0,0,0,0x40112233u);
            s->ad(0x4E,32u|(10u<<24u));s->ad(0x47,3u<<16u);s->sprite(0,0,width,height,0x80402010u,7);
            texture_state(*s,20);s->ad(0x47,(3u<<16u)|1u|(fail<<12u));
            textured_uv(*s,0,0,16,1,0,0,0,0,0x80808080u,false,9);
        }
        same(cpu,accelerated,"PSMZ16S alpha-fail write selection");
        const auto expected=fail==0u || fail==2u?0x80402010u:fail==3u?0x80112233u:0x40112233u;
        require(accelerated.gs.vram().read(0,64,0,0,0)==expected &&
            accelerated.gs.vram().word_at(zb+6144u)==(fail==2u?0x00090009u:0x00070007u),
            "independent alpha-fail selection controls both Z16S halfword writes");
    }
    Stream cpu,accelerated(gpu);
    for(auto* s:{&cpu,&accelerated}) {
        s->setup(0,false);texture_source(*s,20);
        for(unsigned x=0;x<16u;++x) {
            fixture_vram(*s).write(0,64,0,x,0,0x80402010u);
            fixture_vram(*s).write_depth(zb,64,10,x,0,7);
        }
        s->ad(0x4E,32u|(10u<<24u));s->ad(0x47,3u<<16u);texture_state(*s,20);
        textured_uv(*s,0,0,16,1,0,0,0,0,0x80808080u,false,9);
    }
    same(cpu,accelerated,"PSMZ16S conditional known-bit joins in neighboring halves",false);
    const auto at=(zb+6144u)/4u;
    require(accelerated.gs.vram().raw_words()[at]==0x00090009u &&
        accelerated.gs.vram().known_bits()[at]==0xFFF1FFF1u,
        "independent conditional Z16S join preserves every equal bit and both neighboring halfwords");
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
    paired(gpu,"unknown destination blend retains masks and read refusal",[](Stream& s){
        s.ad(0x42,0x44);s.ad(0,0x46);s.ad(1,0x40010203u);s.vertex(0,0);
        s.vertex(128,128);
        rejects([&]{(void)s.gs.vram().read(0,64,0,0,0);},"unwritten bits");
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
    paired(gpu,"AA1 GPU with preserved dependencies",[](Stream& s){
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
m29::Program textured_guest_fixture() {
    auto rom=m20::build_firmware().rom;
    m20::Assembler a(m20::kResetVector);
    using namespace m20;
    unsigned data=0x3000u;
    const auto put64=[&](unsigned at,std::uint64_t value) {
        for(unsigned byte=0;byte<8u;++byte)rom.at(at+byte)=static_cast<std::uint8_t>(value>>(byte*8u));
    };
    const auto privileged=[&](unsigned address,std::uint64_t value) {
        put64(data,value);a.la(t0,address);a.la(t1,kResetVector+data);a.lq(t2,0,t1);a.sd(t2,0,t0);data+=16;
    };
    privileged(0xB2000010u,2u<<13u);privileged(0xB2000020u,0);privileged(0xB2000000u,0xFF25u);
    privileged(0xB2000070u,1u<<9u);privileged(0xB2000080u,((width-1ull)<<32u)|((height-1ull)<<44u));
    privileged(0xB2000060u,2u|(2ull<<42u));
    std::vector<std::pair<std::uint64_t,std::uint64_t>> commands;
    const auto ad=[&](unsigned reg,std::uint64_t value){commands.emplace_back(value,reg);};
    ad(0x1A,1);ad(0x18,0);ad(0x4C,1u<<16u);ad(0x40,((width-1ull)<<16u)|((height-1ull)<<48u));
    ad(0x47,3u<<16u);ad(0x4E,1ull<<32u);ad(0x22,0);ad(0x46,1);ad(0x49,0);ad(0x4A,0);
    ad(0,6);ad(1,0x80402010u);ad(5,0);ad(5,width*16u|(std::uint64_t{height*16u}<<16u));
    const auto transfer=[&](unsigned base,unsigned psm,unsigned bw,unsigned w,unsigned h) {
        ad(0x50,(std::uint64_t{base/256u}<<32u)|(std::uint64_t{bw/64u}<<48u)|(std::uint64_t{psm}<<56u));
        ad(0x51,0);ad(0x52,w|(std::uint64_t{h}<<32u));ad(0x53,0);
    };
    transfer(palette_base,0,64,8,2);
    for(unsigned i=0;i<16u;i+=2u)ad(0x54,palette_color(i)|(std::uint64_t{palette_color(i+1u)}<<32u));
    transfer(texture_base,20,128,8,2);ad(0x54,0xFEDCBA9876543210ull);ad(0x3F,0);
    ad(6,texture_base/256u|(2ull<<14u)|(20ull<<20u)|(3ull<<26u)|(1ull<<30u)|
        (1ull<<34u)|(std::uint64_t{palette_base/256u}<<37u)|(1ull<<61u));
    ad(8,0);ad(0x14,0);ad(0,0x116);ad(1,0x80808080u);ad(3,0);ad(5,0);
    ad(3,128u|(32ull<<16u));ad(5,128u|(32ull<<16u));
    // The second sprite consumes the copied palette, independent of a later
    // register latch change while the first textured command is queued.
    ad(0,0x116);ad(3,0);ad(5,128u);ad(3,128u|(32ull<<16u));ad(5,256u|(32ull<<16u));
    const auto packet=data;
    put64(data,commands.size()|(1ull<<15u)|(1ull<<60u));put64(data+8u,0xEu);data+=16u;
    for(const auto& [low,high]:commands){put64(data,low);put64(data+8u,high);data+=16u;}
    a.la(t0,kResetVector+packet);a.la(t1,0xB0006000u);a.addiu(t2,zero,static_cast<unsigned>(commands.size()+1u));
    a.label("packet");a.lq(t3,0,t0);a.sq(t3,0,t1);a.addiu(t0,t0,16u);a.addiu(t2,t2,0xFFFFu);a.bne(t2,zero,"packet");a.nop();
    a.addiu(t4,zero,15000u);a.label("wait");a.addiu(t4,t4,0xFFFFu);a.bne(t4,zero,"wait");a.nop();
    const auto stop=a.here();a.brk();const auto code=le_words(a.finish());
    require(code.size()<kResetSize,"textured synthetic ROM code fits RESET");
    std::fill(rom.begin(),rom.begin()+kResetSize,0);std::copy(code.begin(),code.end(),rom.begin());
    return {std::move(rom),stop};
}
void check_guest(std::shared_ptr<GsGpu> gpu,const std::filesystem::path& output,bool textured=false) {
    auto fixture=textured?textured_guest_fixture():m29::build();
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
        if(textured) {
            require(frame.width==width && frame.height==height,"textured synthetic guest scanout geometry");
            std::vector<std::uint8_t> expected;
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
                const auto pixel=y<2u && x<16u?palette_color((x%8u)+y*8u):0x80402010u;
                for(unsigned channel=0;channel<3u;++channel)expected.push_back(static_cast<std::uint8_t>(pixel>>(channel*8u)));
                expected.push_back(255u);
            }
            require(frame.rgba==expected,"independent complete textured guest RGBA expected result");
            if(run)require(memory.gs().vram().gpu_primitives()==3u && memory.gs().vram().cpu_fallbacks()==0u,
                "guest GIF executes one clear and two actual textured GPU sprites");
        } else require(sha256_hex(frame.rgba)=="43564b222dee0155325d1db289518d63f6a02af60690dd6da83ad69d002e3f54",
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
        BootOptions options;options.max_instructions=textured?1u:750u;options.stop_at=fixture.stop;
        auto batches_before=gpu->stats().batches;
        auto boundary=run_ee_boot(state,memory,nullptr,options);
        if(textured) {
            while(memory.gs().vram().gpu_primitives()<2u && state.retired_instructions<5000u &&
                  boundary.outcome==BootOutcome::BudgetExhausted)boundary=run_ee_boot(state,memory,nullptr,options);
            require(memory.gs().vram().gpu_primitives()==2u,"checkpoint stops immediately after first guest textured kick");
            batches_before=gpu->stats().batches;
        }
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
void check_textured_triangles(std::shared_ptr<GsGpu> gpu) {
    for(unsigned psm:{0u,1u,19u,20u})for(bool reverse:{false,true})for(bool linear:{false,true})for(bool aa:{false,true}) {
        paired(gpu,"textured triangle affine UV, winding, filtering and AA1",[&](Stream& s) {
            texture_source(s,psm);texture_state(s,psm);
            s.sprite(0,0,width,height,0x80604020u);
            // The normal sprite establishes the exact logical source pages first.
            textured_uv(s,40,0,48,8,0,0,(psm<=1u?128u:256u)*16u,(psm<=1u?64u:psm==19u?128u:256u)*16u);
            s.ad(0x42,0x44u);s.ad(0x14,linear?0x60u:0u);s.ad(0,0x113u|(aa?128u:0u));s.ad(1,0x80808080u);
            const auto before=s.gs.vram().gpu_primitives();
            s.ad(3,16u|(16ull<<16u));s.vertex(32,32);
            s.ad(3,reverse?16u|(144ull<<16u):144u|(16ull<<16u));s.vertex(reverse?32:160,reverse?160:32);
            s.ad(3,reverse?144u|(16ull<<16u):16u|(144ull<<16u));s.vertex(reverse?160:32,reverse?32:160);
            if(s.gs.gpu_backend())require(s.gs.vram().gpu_primitives()==before+1u,
                "textured triangle reaches GPU: psm="+std::to_string(psm)+" reverse="+std::to_string(reverse)+" linear="+std::to_string(linear)+" AA1="+std::to_string(aa));
            if(!linear && !aa) {
                const auto expected=psm>=19u?palette_color(4u):psm==1u?(texture_grid(1u,1u)&0xFFFFFFu)|0x40000000u:texture_grid(1u,1u);
                require(s.gs.vram().read(0,64,0,2,2)==expected,"independent triangle corner texel");
            }
        },0,4u|2u|16u);
    }
    const auto draw_triangle=[](Stream& s,int x=2,unsigned uv=0u) {
        s.ad(0,0x113u);s.ad(1,0x80808080u);
        s.ad(3,uv|(std::uint64_t{uv}<<16u));s.vertex(x*16,32);
        s.ad(3,(uv+128u)|(std::uint64_t{uv}<<16u));s.vertex((x+8)*16,32);
        s.ad(3,uv|(std::uint64_t{uv+128u}<<16u));s.vertex(x*16,160);
    };
    for(unsigned psm:{0u,1u,19u,20u})for(bool partial:{false,true}) {
        paired(gpu,"cold triangle exact tile and unknown unread source bits",[&](Stream& s) {
            texture_source(s,psm);
            if(partial)fixture_vram(s).write(texture_base,psm<=1u?128u:256u,psm,60,31,0,0,1u);
            s.ad(0x3F,0);s.sprite(0,0,width,height,0x80604020u);texture_state(s,psm);
            const auto primitives=s.gs.vram().gpu_primitives(),fallbacks=s.gs.vram().cpu_fallbacks();
            draw_triangle(s,2,16u);
            if(s.gs.gpu_backend())require(partial ? s.gs.vram().cpu_fallbacks()==fallbacks+1u :
                s.gs.vram().gpu_primitives()==primitives+1u,"cold exact tile routes conservatively for source knowledge");
            const auto batches=gpu->stats().batches;s.ad(0x3F,0);
            if(s.gs.gpu_backend())require(gpu->stats().batches==batches,"TEXFLUSH retains an ordered host command queue");
        },0,4u);
    }
    paired(gpu,"TEXFLUSH then overwrite waits for original texture readers",[&](Stream& s) {
        texture_source(s,0);s.sprite(0,0,width,height,0x80604020u);texture_state(s,0);
        draw_triangle(s);const auto batches=gpu->stats().batches;s.ad(0x3F,0);
        if(s.gs.gpu_backend())require(gpu->stats().batches==batches,"cache invalidation does not wait for host completion");
        s.upload(texture_base,0,8,2,0x80C0FFEE80C0FFEEull);draw_triangle(s,12);
        require(s.gs.vram().read(0,64,0,2,2)==texture_grid(0,0) &&
            s.gs.vram().read(0,64,0,12,2)==0x80C0FFEEu,"old and later triangles preserve source versions");
    },0,4u);
    paired(gpu,"queued rendered source feeds a cold triangle in one batch",[&](Stream& s) {
        const auto batches=gpu->stats().batches,readbacks=gpu->stats().readback_bytes;
        s.sprite(0,0,width,height,0x80402010u);
        s.ad(0x4C,8u|(1u<<16u));s.sprite(0,0,64,32,0x80A06020u);s.ad(0x3F,0);
        s.ad(0x4C,1u<<16u);s.ad(6,256u|(1ull<<14u)|(6ull<<26u)|(5ull<<30u)|(1ull<<34u)|(1ull<<35u));
        s.ad(8,0);s.ad(0x14,0);draw_triangle(s);
        if(s.gs.gpu_backend()) {
            require(gpu->stats().batches==batches,"TEXFLUSH leaves producer and consumer queued");
            s.gs.vram().submit_gpu();
            require(gpu->stats().batches==batches+1u && gpu->stats().readback_bytes==readbacks,
                "queued render-to-texture uses one physical batch and no CPU readback");
        }
        require(s.gs.vram().read(0,64,0,2,2)==0x80A06020u,"independent queued render-to-texture pixel");
    },0,4u);
    paired(gpu,"line remains on its CPU raster path",[](Stream& s) {
        s.sprite(0,0,width,height,0x80000000u);s.ad(0,1u);s.ad(1,0x80804020u);
        const auto before=s.gs.vram().gpu_primitives();s.vertex(32,32);s.vertex(160,160);
        if(s.gs.gpu_backend())require(s.gs.vram().gpu_primitives()==before,"line is not a compute rectangle");
    });
}
void check_antialias(std::shared_ptr<GsGpu> gpu) {
    paired(gpu,"line retains its CPU raster path",[](Stream& s) {
        s.sprite(0,0,width,height,0x80000000u);s.ad(0,1u);s.ad(1,0x80804020u);
        const auto before=s.gs.vram().gpu_primitives();s.vertex(32,32);s.vertex(160,160);
        if(s.gs.gpu_backend())require(s.gs.vram().gpu_primitives()==before,"line is not a compute rectangle");
    });
    for(unsigned setting=0;setting<32u;++setting)for(bool reverse:{false,true}) {
        paired(gpu,"AA1 coverage, ABE/PABE, alpha and depth "+std::to_string(setting),[&](Stream& s) {
            s.ad(0x4E,32u);s.sprite(0,0,width,height,0x80706050u,7u);
            s.ad(0x42,0x44u);s.ad(0x49,(setting>>2u)&1u);s.ad(0x4A,(setting>>4u)&1u);
            if(setting&8u)s.ad(0x47,(3u<<16u)|1u|(5u<<1u)|(80u<<4u)|(3u<<12u));
            const auto alpha=(setting&16u) ? 192u : (setting&8u) ? 64u : 128u;
            s.ad(0,3u|128u|((setting&2u)?64u:0u));s.ad(1,(alpha<<24u)|0x00203040u);
            const auto before=s.gs.vram().gpu_primitives(),fallbacks=s.gs.vram().cpu_fallbacks();
            s.vertex(37,41,9u);s.vertex(reverse?37:173,reverse?169:41,9u);
            s.vertex(reverse?173:37,reverse?41:169,9u);
            if(s.gs.gpu_backend())require(s.gs.vram().gpu_primitives()==before+1u && s.gs.vram().cpu_fallbacks()==fallbacks,
                "AA1 uses the selected GPU rather than a hidden CPU fallback");
        },setting&1u,16u);
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
    check_device_batch(api);check_fused_readback(gpu);check_detach_coherence(gpu);check_fused_scanout(gpu);
    check_partial_coherence(gpu);check_resident_batches(gpu);
    check_completion_order(gpu);check_pending_knowledge(gpu);
    check_raster(gpu);check_pipeline(gpu);check_antialias(gpu);check_textured_triangles(gpu);
    check_textured_nearest(gpu);check_texture_palette_snapshots(gpu);check_clut_reload_pending(gpu);check_texture_functions(gpu);
    check_texture_coordinates(gpu);check_texture_pipeline(gpu);check_texture_residency(gpu);check_depth16s(gpu);check_fallback_and_transfers(gpu);
    const auto output=argc>1?std::filesystem::path{argv[1]}:std::filesystem::path{};
    check_guest(gpu,output);check_guest(gpu,output.empty()?output:output/"textured",true);
    require(gpu->stats().primitives>0 && gpu->stats().batches>0,"commands were rasterized by the selected GPU");
    std::cout<<"GPU raster, full VRAM/knowledge, fallback and guest checkpoint equivalence passed on "
        <<gpu->device_name()<<"; GPU primitives="<<gpu->stats().primitives<<", batches="<<gpu->stats().batches<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
