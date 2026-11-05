// Bounded original GIF/GS workload benchmark; presentation rate is not game speed.
#include "ps2native/gif.hpp"
#include "ps2native/gs_gpu.hpp"
#include "ps2native/sha256.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace ps2native;
namespace {
using Clock=std::chrono::steady_clock;
using Packet=std::vector<std::pair<std::uint64_t,std::uint64_t>>;
constexpr unsigned width=256,height=160;
std::uint64_t ns(Clock::time_point start){return static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());}
unsigned integer(const char* text,unsigned limit) {
    unsigned result=0;const std::string value{text};
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || !result || result>limit)
        throw std::runtime_error("benchmark argument outside the bounded range");
    return result;
}
void ad(Packet& packet,unsigned reg,std::uint64_t value){packet.emplace_back(value,reg);}
void vertex(Packet& packet,unsigned x,unsigned y,unsigned z=0){ad(packet,5,x*16u|
    (std::uint64_t{y*16u}<<16u)|(std::uint64_t{z}<<32u));}
void rectangle(Packet& packet,unsigned x,unsigned y,unsigned right,unsigned bottom,std::uint32_t color) {
    ad(packet,0,6);ad(packet,1,color);vertex(packet,x,y);vertex(packet,right,bottom);
}
void feed(Gif& gif,GsRegisters& gs,const Packet& commands) {
    for(std::size_t begin=0;begin<commands.size();begin+=32767u) {
        const auto count=std::min(std::size_t{32767},commands.size()-begin);
        gif.write_fifo(count|(1ull<<15u)|(1ull<<60u),0xEu,gs);
        for(std::size_t n=0;n<count;++n)gif.write_fifo(commands[begin+n].first,commands[begin+n].second,gs);
    }
}
struct Renderer {
    GsRegisters gs;Gif gif;
    explicit Renderer(std::shared_ptr<GsGpu> backend={}) {
        gs.reset(0);gif.reset();gs.set_gpu_backend(std::move(backend));
        Packet setup;
        ad(setup,0x1A,1);ad(setup,0x18,0);ad(setup,0x4C,4u<<16u);
        ad(setup,0x40,((width-1ull)<<16u)|((height-1ull)<<48u));
        ad(setup,0x47,3u<<16u);ad(setup,0x4E,1ull<<32u);
        ad(setup,0x45,0);ad(setup,0x49,0);ad(setup,0x22,0);ad(setup,0x46,1);ad(setup,0x4A,0);
        rectangle(setup,0,0,width,height,0x80241912u);feed(gif,gs,setup);
        gs.write(kGsPmode,8,0xFF25u,0,{});gs.write(kGsSmode2,8,0,0,{});
        gs.write(kGsDispfb1,8,4u<<9u,0,{});
        gs.write(kGsDisplay1,8,((width-1ull)<<32u)|((height-1ull)<<44u),0,{});
        (void)gs.scanout(0);
        gif.diagnostic_events(1,{});
    }
};
Packet workload(unsigned count,bool fill) {
    Packet commands;commands.reserve(count*5u);
    for(unsigned n=0;n<count;++n) {
        const auto color=0x80000000u|((n*0x259D33u)&0xFFFFFFu);
        const auto x=n*37u%224u,y=n*23u%128u;
        if(fill)rectangle(commands,0,0,width,height,color);
        else if(n&1u)rectangle(commands,x,y,x+32u,y+32u,color);
        else {
            ad(commands,0,3);ad(commands,1,color);vertex(commands,x,y);
            vertex(commands,x+32u,y);vertex(commands,x,y+32u);
        }
    }
    return commands;
}
struct Sample {std::uint64_t raster_ns{},scanout_ns{};std::string hash;};
Sample run(Renderer& renderer,const Packet& commands,unsigned field) {
    const auto start=Clock::now();feed(renderer.gif,renderer.gs,commands);
    // Includes completion/readback. scanout is timed separately after this.
    renderer.gs.vram().synchronize();const auto raster=ns(start);
    const auto scan_start=Clock::now();const auto frame=renderer.gs.scanout(field);
    const auto scan=ns(scan_start);
    return {raster,scan,sha256_hex(frame.rgba)};
}
void equal(const Renderer& cpu,const Renderer& gpu) {
    const auto a=cpu.gs.vram().raw_words(),b=gpu.gs.vram().raw_words();
    const auto ak=cpu.gs.vram().known_bits(),bk=gpu.gs.vram().known_bits();
    if(!std::equal(a.begin(),a.end(),b.begin(),b.end()) || !std::equal(ak.begin(),ak.end(),bk.begin(),bk.end()) ||
        cpu.gs.vram().revision()!=gpu.gs.vram().revision() ||
        cpu.gs.vram().pixel_writes()!=gpu.gs.vram().pixel_writes())
        throw std::runtime_error("CPU/GPU VRAM, known masks or write counters differ");
}
double median(std::vector<std::uint64_t> values) {
    std::sort(values.begin(),values.end());const auto middle=values.size()/2u;
    return (values.size()%2u ? static_cast<double>(values[middle]) :
        (static_cast<double>(values[middle-1u])+static_cast<double>(values[middle]))/2.0)/1e6;
}
}
int main(int argc,char** argv)try {
    if(argc>5)throw std::runtime_error("usage: ps2n-gs-bench [rounds 1..10] [primitives 1..4096] [small|fill] [vulkan|directx]");
    const auto rounds=argc>1?integer(argv[1],10):5u,primitives=argc>2?integer(argv[2],4096):512u;
    const std::string kind=argc>3?argv[3]:"small";
    if(kind!="small" && kind!="fill")throw std::runtime_error("workload must be small or fill");
    const std::string api=argc>4?argv[4]:"vulkan";
    if(api!="vulkan" && api!="directx")throw std::runtime_error("backend must be vulkan or directx");
    std::string unavailable;auto backend=create_gs_gpu(unavailable,api=="directx"?GsGpuApi::DirectX:GsGpuApi::Vulkan);
    if(!backend){std::cout<<api<<" GS unavailable: "<<unavailable<<'\n';return 77;}
    const auto commands=workload(primitives,kind=="fill");Renderer cpu,gpu(backend);
    // One equal untimed warmup, including pipeline and device-cache use.
    (void)run(cpu,commands,0);(void)run(gpu,commands,0);equal(cpu,gpu);
    std::cout<<"device,"<<backend->device_name()<<"\nworkload,"<<kind<<",primitives,"<<primitives
        <<",rounds,"<<rounds<<"\nround,backend,raster_and_sync_ms,scanout_ms,RGBA_sha256\n";
    std::cout<<std::fixed<<std::setprecision(6);
    const auto before=backend->stats();std::vector<std::uint64_t> cpu_times,gpu_times;
    for(unsigned round=0;round<rounds;++round) {
        Sample c,g;
        if(round&1u){g=run(gpu,commands,round);c=run(cpu,commands,round);}
        else {c=run(cpu,commands,round);g=run(gpu,commands,round);}
        equal(cpu,gpu);if(c.hash!=g.hash)throw std::runtime_error("framebuffer hashes differ");
        cpu_times.push_back(c.raster_ns);gpu_times.push_back(g.raster_ns);
        for(const auto& item:{std::pair{std::string{"cpu"},c},std::pair{api,g}})
            std::cout<<round<<','<<item.first<<','<<static_cast<double>(item.second.raster_ns)/1e6<<','
                <<static_cast<double>(item.second.scanout_ns)/1e6<<','<<item.second.hash<<'\n';
    }
    const auto& after=backend->stats();const auto c=median(cpu_times),g=median(gpu_times);
    std::cout<<"median_raster_and_sync_ms,cpu,"<<c<<','<<api<<','<<g<<",speedup,"<<c/g<<'\n'
        <<"GPU_batches,"<<after.batches-before.batches<<",GPU_primitives,"<<after.primitives-before.primitives<<'\n'
        <<"GPU_timestamp_supported,"<<after.gpu_timestamps<<",GPU_raster_ms,"
        <<static_cast<double>(after.gpu_ns-before.gpu_ns)/1e6<<'\n'
        <<"CPU_upload_ms,"<<static_cast<double>(after.upload_ns-before.upload_ns)/1e6
        <<",CPU_submit_ms,"<<static_cast<double>(after.submit_ns-before.submit_ns)/1e6
        <<",CPU_wait_ms,"<<static_cast<double>(after.wait_ns-before.wait_ns)/1e6
        <<",CPU_readback_ms,"<<static_cast<double>(after.readback_ns-before.readback_ns)/1e6<<'\n'
        <<"upload_bytes,"<<after.upload_bytes-before.upload_bytes<<",readback_bytes,"<<after.readback_bytes-before.readback_bytes<<'\n'
        <<"upload_syncs,"<<after.upload_syncs-before.upload_syncs<<",readback_syncs,"<<after.readback_syncs-before.readback_syncs
        <<",submissions,"<<after.submissions-before.submissions<<",waits,"<<after.waits-before.waits<<'\n'
        <<"result_upload_bytes,"<<after.result_upload_bytes-before.result_upload_bytes<<",result_readback_bytes,"
        <<after.result_readback_bytes-before.result_readback_bytes<<",backend_storage_bytes,"<<after.storage_bytes<<'\n'
        <<"logical_VRAM_bytes,"<<GsVram::kBytes<<",known_mask_bytes,"<<GsVram::kBytes
        <<",max_command_bytes,"<<primitives*sizeof(GsGpuCommand)<<'\n'
        <<"CPU_fallback_primitives,"<<gpu.gs.vram().cpu_fallbacks()<<"\n";
    if(after.primitives==before.primitives)throw std::runtime_error("benchmark did not execute GPU rasterization");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
