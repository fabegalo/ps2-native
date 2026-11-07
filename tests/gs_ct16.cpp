// Hardware layout and transmission fixtures from GS manual 4.1.5/8.3.2.
#include "boot_checks.hpp"
#include <array>
#include <iostream>
#include <set>
#include <string_view>
#include "ps2native/gs_gpu.hpp"
using namespace ps2native;
using namespace boot_checks;
namespace {
void setup(GsRegisters& gs,unsigned psm,unsigned width,unsigned height,unsigned x=0,unsigned y=0) {
    gs.general_write(0x50,(1ull<<48)|(std::uint64_t{psm}<<56));
    gs.general_write(0x51,(std::uint64_t{x}<<32)|(std::uint64_t{y}<<48));
    gs.general_write(0x52,width|(std::uint64_t{height}<<32));gs.general_write(0x53,0);
}
void drawing(GsRegisters& gs,unsigned psm,std::shared_ptr<GsGpu> gpu) {
    gs.reset(0);gs.set_gpu_backend(std::move(gpu));gs.general_write(0,0);gs.general_write(0x1A,1);gs.general_write(0x18,0);
    gs.general_write(0x4C,(1u<<16u)|(psm<<24u));gs.general_write(0x40,(15ull<<16u)|(15ull<<48u));gs.general_write(0x47,3u<<16u);gs.general_write(0x4E,1ull<<32u);gs.general_write(0x22,0);gs.general_write(0x4A,0);gs.general_write(0x45,0);gs.general_write(0x46,1);gs.general_write(0x49,0);
}
}
int main(int argc,char** argv) try {
    std::shared_ptr<GsGpu> gpu;
    if(argc>1){require(std::string_view(argv[1])=="directx","expected optional directx backend");std::string reason;gpu=create_gs_gpu(reason,GsGpuApi::DirectX);require(bool(gpu),reason);}

    constexpr std::array<std::array<unsigned,3>,17> common{{
        {0,0,0},{1,0,4},{2,0,16},{4,0,32},{8,0,2},{0,1,8},{0,2,64},{0,4,128},
        {16,0,512},{0,8,256},{63,63,8190},{64,0,8192},{0,64,16384},{64,64,24576},
        {15,0,54},{0,7,200},{15,7,254}
    }};
    for(unsigned psm:{2u,10u}) {
        GsVram vram;
        for(const auto& v:common)require(GsVram::address(0,128,psm,v[0],v[1])==v[2],"16-bit page/column diagram vector");
        require(GsVram::address(0,64,psm,32,0)==(psm==2u ? 2048u : 4096u) &&
            GsVram::address(0,64,psm,0,16)==(psm==2u ? 1024u : 2048u) &&
            GsVram::address(0,64,psm,0,32)==(psm==2u ? 4096u : 1024u),"16S has its independently specified block order");
        std::set<unsigned> locations;
        for(unsigned y=0;y<128u;++y)for(unsigned x=0;x<128u;++x) {
            require(locations.insert(GsVram::address(0,128,psm,x,y)).second,"four-page halfword mapping is bijective");
            vram.write(0,128,psm,x,y,(y<<8u)|x);
        }
        for(unsigned y=0;y<128u;++y)for(unsigned x=0;x<128u;++x)
            require(vram.read(0,128,psm,x,y)==((y<<8u)|x),"every packed halfword preserves its neighbors");
        vram.invalidate();vram.write(0,64,psm,0,0,0x1234);
        rejects([&]{(void)vram.read(0,64,psm,8,0);},"unwritten bits");
        vram.write(0,64,psm,8,0,0xABCD);require(vram.word_at(0)==0xABCD1234,"X bit 3 selects the upper halfword");
        vram.write(0,64,psm,0,0,0xFFFF,0xFF00);require(vram.word_at(0)==0xABCD12FF,"raw halfword preservation mask leaves the adjacent pixel intact");
        vram.write(0,64,psm,0,0,0x12FF,0,0,true);require(vram.read(0,64,psm,0,0)==0x12FF,"equal conditional writes retain knowledge");
        vram.write(0,64,psm,0,0,0x12FE,0,0,true);rejects([&]{(void)vram.read(0,64,psm,0,0);},"unwritten bits");
        require(vram.read(0,64,psm,8,0)==0xABCD,"conditional uncertainty is confined to its halfword");
        vram.write(0,64,psm,0,0,0,0,0x8000);rejects([&]{(void)vram.read(0,64,psm,0,0);},"unwritten bits");
        require(GsVram::address(GsVram::kBytes-256,64,psm,0,8)==0,"16-bit addresses wrap at four MiB");
        require(GsVram::address(256,64,psm,8,0)==258,"non-page-aligned buffer bases retain halfword lanes");
        GsRegisters gs;gs.reset(0);setup(gs,psm,4,3,2047,2047);
        gs.image_write(0x0004000300020001ull,0x0008000700060005ull);
        gs.image_write(0x000C000B000A0009ull,~0ull);
        for(unsigned n=0;n<12u;++n)require(gs.vram().read(0,64,psm,(2047+n%4)&2047u,(2047+n/4)&2047u)==n+1,
            "real IMAGE words pack four halfwords per doubleword and wrap coordinates");
        const auto count=gs.vram().pixel_writes();gs.general_write(0x54,~0ull);
        require(count==12u && gs.vram().pixel_writes()==count,"final quadword padding and later inactive HWREG writes are discarded");
        rejects([&]{setup(gs,psm,2,1);},"hardware alignment");
        setup(gs,psm,8,1);gs.image_write(0x0004000300020001ull,0x0008000700060005ull);
        gs.general_write(0x50,(1ull<<16)|(std::uint64_t{psm}<<24)|(1ull<<48)|(std::uint64_t{psm}<<56));
        gs.general_write(0x51,1ull|(2ull<<32)|(2ull<<59));gs.general_write(0x52,4ull|(1ull<<32));gs.general_write(0x53,2);
        for(unsigned n=0;n<4u;++n)require(gs.vram().read(0,64,psm,n+2,0)==n+2,"right-to-left local copy preserves overlapping packed pixels");
    }
    for(unsigned psm:{2u,10u}) {
        GsRegisters gs;gs.reset(0);setup(gs,psm,4,2,3,4);
        gs.image_write(0x0000000003E0001Full,0x000000007FFF7C00ull);
        gs.write(kGsPmode,8,0xFF25,0,{});gs.write(kGsSmode2,8,0,0,{});
        gs.write(kGsDispfb1,8,(1u<<9)|(std::uint64_t{psm}<<15)|(3ull<<32)|(4ull<<43),0,{});
        gs.write(kGsDisplay1,8,(1ull<<32)|(1ull<<44),0,{});
        const std::vector<std::uint8_t> expected{248,0,0,255,0,248,0,255,0,0,248,255,248,248,248,255};
        auto& vram=const_cast<GsVram&>(gs.vram());vram.write(0,64,psm,3,4,0x1F,0,0x8000);
        require(gs.scanout(12).rgba==expected,"CT16/16S scanout uses real swizzled RGB5, source offset and zero-bit expansion without consuming alpha");
        vram.write(0,64,psm,3,4,0x1F,0,1u);
        rejects([&]{(void)gs.scanout(12);},"unwritten bits");
    }
    for(unsigned psm:{2u,10u})for(const bool aem:{false,true}) {
        GsRegisters gs;gs.reset(0);gs.set_gpu_backend(gpu);setup(gs,psm,4,2);
        gs.image_write(0x801F001F80000000ull,0x83E07FFF7C0003E0ull);gs.general_write(0x3F,0);
        gs.general_write(0,0x116u);gs.general_write(1,0x3F80000080808080ull);gs.general_write(0x18,0);gs.general_write(0x1A,1);
        gs.general_write(0x4C,1u|(1u<<16u));gs.general_write(0x40,(7ull<<16u)|(7ull<<48u));gs.general_write(0x47,3u<<16u);gs.general_write(0x4E,1ull<<32u);gs.general_write(0x22,0);gs.general_write(0x4A,0);
        gs.general_write(6,(1ull<<14u)|(std::uint64_t{psm}<<20u)|(3ull<<26u)|(3ull<<30u)|(1ull<<34u)|(1ull<<35u));gs.general_write(8,0);gs.general_write(0x14,0);
        gs.general_write(0x3B,0x9100000023ull|(aem ? 0x8000ull : 0ull));
        const auto draw=[&](unsigned x,unsigned y,unsigned u,unsigned v) {gs.general_write(0,0x116u);gs.general_write(3,u*16u|(std::uint64_t{v*16u}<<16u));gs.general_write(5,x*16u|(std::uint64_t{y*16u}<<16u));gs.general_write(5,(x+1u)*16u|(std::uint64_t{(y+1u)*16u}<<16u));};
        const std::array<std::uint32_t,8> expected{aem?0u:0x23000000u,0x91000000u,0x230000F8u,0x910000F8u,0x2300F800u,0x23F80000u,0x23F8F8F8u,0x9100F800u};
        for(unsigned n=0;n<8u;++n){draw(n%4u,n/4u,n%4u,n/4u);require(gs.vram().read(8192,64,0,n%4u,n/4u)==expected[n],"CT16 texture expansion uses zero-filled RGB5 and TEXA TA0/TA1, with AEM applying only to an all-zero raw texel");}
        require(!gpu || gs.vram().cpu_fallbacks()>0u,"unsupported compute texture formats retain the real CPU sampling path");
        auto& alpha_vram=const_cast<GsVram&>(gs.vram());alpha_vram.write(0,64,psm,0,0,0x1Fu,0,0x8000u);gs.general_write(0x3F,0);
        gs.general_write(0x3B,0x8000000080ull|(aem?0x8000ull:0ull));draw(4,1,0,0);
        require(gs.vram().read(8192,64,0,4,1)==0x800000F8u,"equal TEXA alpha values do not consume an unknown CT16 selector for nonblack RGB");
        alpha_vram.write(0,64,psm,0,0,0,0,0x8000u);gs.general_write(0x3F,0);
        if(aem)rejects([&]{draw(5,1,0,0);},"unwritten bits");else {draw(5,1,0,0);require(gs.vram().read(8192,64,0,5,1)==0x80000000u,"equal alpha remains independent of the selector for black without AEM");}
        gs.general_write(0x3B,aem?0x8000ull:0ull);draw(6,1,0,0);
        require(gs.vram().read(8192,64,0,6,1)==0u,"zero TEXA endpoints remain proven even for black AEM with unknown selector");
        gs.general_write(0x3B,0x9100000023ull);rejects([&]{draw(7,1,0,0);},"unwritten bits");
        gs.general_write(6,(1ull<<14u)|(std::uint64_t{psm}<<20u)|(3ull<<26u)|(3ull<<30u)|(1ull<<35u));
        auto& vram=const_cast<GsVram&>(gs.vram());vram.write(0,64,psm,0,0,0x1Fu,0,0x8000u);gs.general_write(0x3F,0);
        draw(4,0,0,0);require(gs.vram().read(8192,64,0,4,0)==0x800000F8u,"RGB-only CT16 sampling consumes no unknown alpha bit and retains the real fragment alpha");
        vram.write(0,64,psm,0,0,0x1Eu,0,1u);gs.general_write(0x3F,0);rejects([&]{draw(5,0,0,0);},"unwritten bits");
    }
    for(unsigned psm:{2u,10u}) {
        GsRegisters gs;gs.reset(0);gs.set_gpu_backend(gpu);setup(gs,psm,4,2,0,64);gs.image_write(0x001F001F001F001Full,0x001F001F001F001Full);gs.general_write(0x3F,0);
        gs.general_write(0,0x116u);gs.general_write(1,0x80808080u);gs.general_write(0x18,0);gs.general_write(0x1A,1);gs.general_write(0x4C,4u|(1u<<16u));gs.general_write(0x40,(7ull<<16u)|(7ull<<48u));gs.general_write(0x47,3u<<16u);gs.general_write(0x4E,1ull<<32u);gs.general_write(0x22,0);gs.general_write(0x4A,0);
        gs.general_write(6,(1ull<<14u)|(std::uint64_t{psm}<<20u)|(3ull<<26u)|(7ull<<30u)|(1ull<<35u));gs.general_write(8,0);gs.general_write(0x14,0);
        const auto draw=[&]{gs.general_write(0,0x116u);gs.general_write(3,1024ull<<16u);gs.general_write(5,0);gs.general_write(5,16u|(16ull<<16u));};
        draw();require(gs.vram().read(32768,64,0,0,0)==0x800000F8u,"CT16 texture row 64 addresses its actual second 64x64 page");
        setup(gs,psm,4,2);gs.image_write(0,0);draw();require(gs.vram().read(32768,64,0,0,0)==0x800000F8u,"a cold first-page upload cannot invalidate a cached second-page CT16 texel");
        setup(gs,psm,4,2,0,64);gs.image_write(0x001F001F001F001Full,0x001F001F001F001Full);rejects(draw,"TEXFLUSH");gs.general_write(0x3F,0);draw();
    }
    for(unsigned psm:{2u,10u}) {
        GsRegisters gs;drawing(gs,psm,gpu);auto& vram=const_cast<GsVram&>(gs.vram());
        const auto point=[&](unsigned x,unsigned y,std::uint32_t color,unsigned z=0){gs.general_write(1,color);gs.general_write(5,x*16u|(std::uint64_t{y*16u}<<16u)|(std::uint64_t{z}<<32u));};
        point(0,0,0x804080F8u);point(8,0,0x404080F8u);
        require(vram.read(0,64,psm,0,0)==0xA21Fu && vram.read(0,64,psm,8,0)==0x221Fu,"drawing packs each actual halfword from RGB high five bits and source A7");
        gs.general_write(0x4A,1);point(8,0,0x404080F8u);require(vram.read(0,64,psm,8,0)==0xA21Fu,"FBA forces the packed alpha bit");gs.general_write(0x4A,0);
        for(unsigned x:{0u,8u})vram.write(0,64,psm,x,0,0xFFFFu);
        gs.general_write(0x4C,(1u<<16u)|(psm<<24u)|(0x800000F8ull<<32u));point(0,0,0);point(8,0,0);
        require(vram.read(0,64,psm,0,0)==0x801Fu && vram.read(0,64,psm,8,0)==0x801Fu,"FRAME mask uses pre-conversion channel positions for both physical halfwords");
        gs.general_write(0x4C,(1u<<16u)|(psm<<24u)|(7ull<<32u));point(0,0,0);point(8,0,0);
        require(vram.read(0,64,psm,0,0)==0 && vram.read(0,64,psm,8,0)==0,"discarded low RGB bits of FBMSK cannot preserve packed color bits");
        gs.general_write(0x4C,(1u<<16u)|(psm<<24u));vram.write(0,64,psm,0,0,0x8000u);gs.general_write(0x47,1u|(3u<<12u));point(0,0,0xF8u);
        require(vram.read(0,64,psm,0,0)==0x801Fu,"RGB_ONLY alpha-test failure preserves packed destination alpha");gs.general_write(0x47,3u<<16u);
        gs.general_write(0x45,1);gs.general_write(0x44,0x34u);point(0,0,0x807F7F7Fu);point(1,0,0x807F7F7Fu);
        require(vram.read(0,64,psm,0,0)==0xBDEFu && vram.read(0,64,psm,1,0)==0xC210u,"signed DIMX offsets use window X/Y before RGB5 quantization");
        point(0,0,0x80000000u);require(vram.read(0,64,psm,0,0)==0x8000u,"COLCLAMP saturates negative dithered channels");gs.general_write(0x46,0);point(0,0,0x80000000u);
        require(vram.read(0,64,psm,0,0)==0xFFFFu,"disabled COLCLAMP retains the low eight bits before RGB5 packing");gs.general_write(0x45,0);gs.general_write(0x46,1);
        gs.general_write(0,64u);gs.general_write(0x42,0x64u|(64ull<<32u));vram.write(0,64,psm,0,0,0x4000u,0,0x8000u);point(0,0,0x80000080u);
        require(vram.read(0,64,psm,0,0)==0xA008u,"packed destination RGB is expanded before FIX blending without consuming an unused alpha bit");
        gs.general_write(0x42,0x98u);vram.write(0,64,psm,0,0,0x8000u,0,0x7FFFu);point(0,0,0x80000080u);
        require(vram.read(0,64,psm,0,0)==0x8010u,"packed destination alpha one is the coefficient 0x80 without consuming destination RGB");
        vram.write(0,64,psm,0,0,0u,0,0x7FFFu);point(0,0,0x80000080u);require(vram.read(0,64,psm,0,0)==0x8000u,"packed destination alpha zero is the coefficient zero");
        gs.general_write(0,0);gs.general_write(0x4E,1u|(std::uint64_t{psm}<<24u));gs.general_write(0x47,(1u<<16u)|(2u<<17u));
        for(unsigned x:{0u,1u}){vram.write(0,64,psm,x,0,0);vram.write_depth(8192,64,psm,x,0,x?25u:15u);point(x,0,0x800000F8u,20u);}
        require(vram.read(0,64,psm,0,0)==0x801Fu && vram.read_depth(8192,64,psm,0,0)==20u && vram.read(0,64,psm,1,0)==0u && vram.read_depth(8192,64,psm,1,0)==25u,"Z16/Z16S tests and writes share the real packed color/depth storage and preserve a failed comparison");
        require(!gpu || gs.vram().cpu_fallbacks()>0u,"packed framebuffers retain CPU formatting while a GPU backend is selected");
    }
    std::cout<<"PSMCT16/16S swizzle, halfword knowledge, real IMAGE packing, wrap, overlap and RGB scanout passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
