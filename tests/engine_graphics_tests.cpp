#include "ps2native/engine/console.hpp"
#include "gs/gs.hpp"
#include "gs/gpu/gpu.hpp"
#include "gs/raster.hpp"
#include <cstdio>
#include <format>
#include <cstring>
#include <stdexcept>
using namespace ps2;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static void upload(unsigned base,unsigned psm,unsigned w,unsigned h,const std::vector<std::uint8_t>& bytes){
 gs::write(0x50,(std::uint64_t{base}<<32)|(1ull<<48)|(std::uint64_t{psm}<<56));gs::write(0x51,0);gs::write(0x52,w|(std::uint64_t{h}<<32));gs::write(0x53,0);gs::write_image(bytes.data(),bytes.size()/16);
}
int main(){
 try{
  ps2native::engine::Console c;c.initialize_graphics();
  check(gs::gpu::mode()==gs::gpu::Mode::primary,"GS compute must be primary");
  check(!gs::gpu::attach_surface({gs::gpu::SurfaceTarget::Win32,nullptr,0,nullptr,nullptr,640,480}),"Invalid native surface must be rejected before driver calls");
  for(unsigned fixture=0;fixture<7;++fixture){
   const unsigned formats[]={0,1,2,10,0,0,0};const auto psm=formats[fixture];
   gs::render_reset();
   const std::uint32_t seed=fixture==4 ? 0x80808080 : 0;
   std::vector<std::uint32_t> initial(1u<<20,seed),clut(512);
   std::memcpy(c.gs_vram.data(),initial.data(),4u<<20);gs::gpu::write_state(initial,1,clut);
   gs::write(0x4c,(1ull<<16)|(std::uint64_t{psm}<<24)|(fixture==4 ? 0xffull<<32 : 0));
   gs::write(0x4e,(1ull<<32)|0x100);gs::write(0x40,(31ull<<16)|(31ull<<48));gs::write(0x47,fixture==5 ? 0x70000 : 0x30000);
   gs::write(0x42,0x44);gs::write(0x46,1);
   if(fixture==5){gs::write(0x4e,0x100);const auto address=gs::address32(0x100*32,1,0,0);check(address<(4u<<20),"Depth allocation");}
   if(fixture==6){
    gs::gpu::g_current_mode=gs::gpu::Mode::mirror; // Feed identical transfer/CLUT inputs to the independent CPU oracle.
    std::vector<std::uint8_t> palette(1024),texture(1024,8);
    for(unsigned i=0;i<256;++i)store(palette.data()+4*i,0x80000000u|i|(i<<8)|((255-i)<<16));
    upload(0x100,0,16,16,palette);upload(0x200,0x13,32,32,texture);
    gs::write(0x06,0x200ull|(1ull<<14)|(0x13ull<<20)|(5ull<<26)|(5ull<<30)|(1ull<<34)|(1ull<<35)|(0x100ull<<37)|(1ull<<61));
    gs::gpu::g_current_mode=gs::gpu::Mode::primary;
   }
   gs::write(0x00,6|(fixture==4 ? 0x40 : 0)|(fixture==6 ? 0x110 : 0));gs::write(0x01,fixture==4 ? 0x3f800000400000ffull : 0x3f800000800000ffull);
   const auto before=c.gs_vram;
   gs::write(0x03,0);gs::write(0x05,0);gs::write(0x03,(512ull<<16)|512);gs::write(0x05,(512ull<<16)|512);
   std::vector<std::uint8_t> gpu(4u<<20);gs::gpu::download(gpu.data());
   c.gs_vram=before;gs::raster::draw(c.gs_state,c.gs_state.queue,2);
   if(gpu!=c.gs_vram){std::size_t at=0;while(gpu[at]==c.gs_vram[at])++at;throw std::runtime_error(std::format("GS differential fixture {} PSM {} offset {:X}: GPU {:02X} CPU {:02X}",fixture,psm,at,gpu[at],c.gs_vram[at]));}
   if(fixture==0)check(gs::read_pixel(0,0,1,16,16)==0x800000ff,"Guest primitive color must reach VRAM");
   std::printf("graphics: compute/CPU full-VRAM fidelity fixture=%u psm=%u PASS\n",fixture,psm);
  }
  // Identical guest packets through the three real ingress paths, including VIF MPG/UNPACK -> VU1 XGKICK.
  const std::uint64_t packet[]={0x1000000000008009ull,0x0e,0x10000,0x4c,1ull<<32,0x4e,
    (31ull<<16)|(31ull<<48),0x40,0x30000,0x47,6,0,0x3f800000800000ffull,1,0,5,(512ull<<16)|512,5,0,0x61};
  std::vector<std::uint8_t> reference;
  for(unsigned path=1;path<=4;++path){
   gs::render_reset();std::vector<std::uint32_t> zero(1u<<20),clut(512);gs::gpu::write_state(zero,1,clut);
   c.vif_state={};c.gif_paths={};c.gif_scans={};c.gif_held={};
   std::memcpy(c.ram.data()+0x1000,packet,sizeof(packet));
   if(path==3){ps2::st32(0x1000e000,1);ps2::st32(0x1000a010,0x1000);ps2::st32(0x1000a020,10);ps2::st32(0x1000a000,0x101);}
   else if(path==2){const std::uint32_t direct=0x5000000a;vif::transfer_words(1,&direct,1);vif::transfer(1,reinterpret_cast<const std::uint8_t*>(packet),10);}
   else {
    const std::uint32_t unpack=0x6c0a0000;vif::transfer_words(1,&unpack,1);vif::transfer(1,reinterpret_cast<const std::uint8_t*>(packet),10);
    const std::uint32_t program[]={0x4a020000,0x800006fc,0x400002ff,0x8000033c,0x000002ff};vif::transfer_words(1,program,5);
    std::array<std::uint8_t,16384> image=c.vu1_code;
    std::array<ps2native::engine::VuPairFn,2048> functions{};
    functions[0]=&ps2native::engine::native_vu_pair<0x800006fc,0x400002ff>;
    functions[1]=&ps2native::engine::native_vu_pair<0x8000033c,0x000002ff>;
    const ps2native::engine::VuProgram native={1,image.data(),16384,functions.data()};
    c.vu_programs.clear();if(path==4)ps2native::engine::register_vu_programs(&native,1);
    const std::uint32_t start[]={0x03000025,0x0400005b,0x02000000,0x14000000,0x13000000};vif::transfer_words(1,start,5);
    check(!c.vu[1].busy && c.vu[1].unit.top==37 && c.vu[1].unit.itop==91,"VIF MSCAL/FLUSHA and TOP/ITOP latches");
    check(c.vu[1].pairs==2 && (path==4 ? c.vu[1].aot_pairs==2 : c.vu[1].interpreted_pairs==2),"VU1 XGKICK executes actual native/interpreted pairs");
    c.vu_programs.clear();
   }
   std::vector<std::uint8_t> memory(4u<<20);gs::gpu::download(memory.data());
   check(ps2::load<std::uint32_t>(memory.data())==0x800000ff,"GIF ingress must draw the guest color");
   if(path==1)reference=memory;else check(memory==reference,"GIF PATH1/2/3 full-VRAM matched boundaries");
   std::printf("graphics: GIF ingress=%u full-VRAM PASS\n",path);
  }
  const auto before=c.graphics_frames;c.advance_to(294912000u/60u);
  check(c.graphics_frames==before+1 && (c.intc_status&4) && (gs::read_privileged(0x12001000)&0x2000),"VBlank event and GS field share the Console clock");
  gs::write_privileged(0x12000000,1);gs::write_privileged(0x12000070,1ull<<9);gs::write_privileged(0x12000080,(31ull<<32)|(31ull<<44));
  std::vector<std::uint32_t> pixels;std::uint32_t w=0,h=0;
  check(gs::display(pixels,w,h)&&w==32&&h==32,"GS compute framebuffer readback");
  std::puts("graphics: real GPU differential/readback PASS; synthetic device regressions, not gameplay");
 }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
