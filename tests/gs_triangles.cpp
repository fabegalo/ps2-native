// Original register streams; no BIOS packets or firmware data.
#include "boot_checks.hpp"
#include "ps2native/gs_registers.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void setup(GsRegisters& gs,unsigned type=3,unsigned precision=0) {
    gs.reset(0);if(precision)gs.diagnostic_texture_precision(precision);
    gs.general_write(0,type);gs.general_write(1,0x80201008);
    gs.general_write(0x18,0);gs.general_write(0x1A,1);gs.general_write(0x4C,1u<<16u);
    gs.general_write(0x40,(7ull<<16u)|(7ull<<48u));gs.general_write(0x47,3u<<16u);
    gs.general_write(0x4E,1ull<<32u);gs.general_write(0x22,0);gs.general_write(0x4A,0);
    gs.general_write(0x50,(1ull<<16u)<<32u);gs.general_write(0x51,0);gs.general_write(0x52,(8ull<<32u)|8u);gs.general_write(0x53,0);
    for(unsigned n=0;n<16;++n)gs.image_write(0x8000000080000000ull,0x8000000080000000ull);
}
void vertex(GsRegisters& gs,unsigned x,unsigned y,unsigned z=0,bool draw=true) {
    gs.general_write(draw ? 5u : 0xDu,(x*16u)|(std::uint64_t{y*16u}<<16u)|(std::uint64_t{z}<<32u));
}
}
int main() try {
    for(const unsigned type:{3u,4u,5u})for(const bool reverse:{false,true})for(const bool abe:{false,true})for(const unsigned a:{0u,64u,128u}) {
        GsRegisters gs;setup(gs,6,16);
        gs.general_write(1,0x80000000u);
        gs.general_write(0x4E,1); // known depth 7, distinct from source depth 9
        vertex(gs,0,0,7);vertex(gs,8,8,7);
        gs.general_write(0,type|128u|(abe?64u:0u));gs.general_write(1,(std::uint64_t{a}<<24)|0x402010u);
        gs.general_write(0x42,0x44);gs.general_write(0x46,1);gs.general_write(0x49,0);
        const auto kick=[&](unsigned x,unsigned y){gs.general_write(5,x|(std::uint64_t{y}<<16)|(9ull<<32));};
        kick(40,40);kick(reverse?40u:104u,reverse?104u:40u);kick(reverse?104u:40u,reverse?40u:104u);
        const auto ca=abe && a!=128u ? a : 64u;
        require(gs.vram().read(0,64,0,2,3)==((ca<<24)|(64u*ca/128u<<16)|(32u*ca/128u<<8)|(16u*ca/128u)),
            "AA1 outer half-pixel edge uses coverage except ABE with non-128 alpha, in both windings and all triangle assemblies");
        const auto ia=abe && a!=128u ? a : 128u;
        require(gs.vram().read(0,64,0,3,3)==((ia<<24)|(64u*ia/128u<<16)|(32u*ia/128u<<8)|(16u*ia/128u)),
            "AA1 interior selects full coverage without scaling the original alpha");
        require(gs.vram().read_depth(8192,64,0,3,3)==9u && gs.vram().read_depth(8192,64,0,2,3)==7u,
            "AA1 writes original interior Z but retains edge Z even when ABE keeps a non-128 source alpha");
        require(gs.vram().read(0,64,0,1,3)==0x80000000u && gs.vram().read(0,64,0,5,5)==0x80000000u,
            "AA1 excludes pixels beyond the one-pixel widened perimeter");
    }
    for(const unsigned fail:{0u,1u,2u,3u}) {
        GsRegisters gs;setup(gs,3u|128u,16);gs.general_write(1,0x80402010u);
        gs.general_write(0x42,0x44);gs.general_write(0x46,1);gs.general_write(0x49,0);
        gs.general_write(0x47,(3u<<16)|1u|(4u<<1)|(128u<<4)|(fail<<12));
        const auto kick=[&](unsigned x,unsigned y){gs.general_write(5,x|(std::uint64_t{y}<<16));};
        kick(40,40);kick(104,40);kick(40,104);
        const auto result=gs.vram().read(0,64,0,2,3);
        require(result==(fail==1u ? 0x40201008u : fail==3u ? 0x80201008u : 0x80000000u),
            "AA1 coverage alpha reaches alpha test and KEEP/FB_ONLY/ZB_ONLY/RGB_ONLY before blending and writes");
    }
    {
        GsRegisters gs;setup(gs,3u|128u);vertex(gs,0,0);vertex(gs,4,0);
        const auto before=gs.vram().pixel_writes();rejects([&]{vertex(gs,0,4);},"antialiasing");
        require(before==gs.vram().pixel_writes(),"normal GS profile retains AA1 refusal without experimental coverage opt-in");
    }
    for(const bool steep:{false,true}) {
        GsRegisters gs;setup(gs,6,16);gs.general_write(0x4E,1);gs.general_write(1,0x80000000u);
        vertex(gs,0,0,12);vertex(gs,8,8,12);
        gs.general_write(0,3u|128u);gs.general_write(1,0x80402010u);
        gs.general_write(0x42,0x44);gs.general_write(0x46,1);gs.general_write(0x49,0);
        const auto draw=[&] {
            vertex(gs,1,1,10);vertex(gs,steep?3u:5u,steep?5u:3u,16);vertex(gs,steep?5u:1u,steep?1u:5u,14);
        };
        gs.general_write(0x47,5u<<16);draw(); // GEQUAL on extrapolated x+y+8
        require(gs.vram().read(0,64,0,steep?1u:2u,steep?2u:1u)==0x80000000u,
            "AA1 depth-tests extrapolated edge Z before color write, for shallow and steep edges");
        gs.general_write(0,3u|128u);gs.general_write(0x47,3u<<16);draw();
        require(gs.vram().read(0,64,0,steep?1u:2u,steep?2u:1u)==0x40201008u,
            "AA1 uses the edge's minor axis for half coverage, for shallow and steep edges");
        require(gs.vram().read_depth(8192,64,0,steep?1u:2u,steep?2u:1u)==12u,
            "AA1 edge extrapolation never overwrites known destination Z");
    }
    {
        // Vertex/ADC processing is independent of line rasterization. BIOS
        // command streams may prepare or suppress a whole strip without a
        // single drawing kick, including unsupported shading/texture modes.
        GsRegisters gs;setup(gs,0x18Au);const auto before=gs.vram().pixel_writes();
        vertex(gs,1,2,0,false);vertex(gs,3,4,0,false);
        require(gs.vram().pixel_writes()==before,"ADC-suppressed line strips assemble real vertices without rasterizing pixels");
        rejects([&]{vertex(gs,5,6);},"10.4 coordinates (48,64) -> (80,96)");
        require(gs.vram().pixel_writes()==before,"unsupported line rasterization still refuses before a fragment is written");
        gs.general_write(0,1);vertex(gs,1,2,0,false);vertex(gs,3,4,0,false);
        vertex(gs,5,6);rejects([&]{vertex(gs,6,6);},"10.4 coordinates (80,96) -> (96,96)");
        require(gs.vram().pixel_writes()==before,"independent lines consume pairs; a suppressed pair cannot leak into the next line");
        gs.general_write(0,0x11); // Textured independent line, UV is unwritten.
        vertex(gs,0,0,0,false);vertex(gs,1,1,0,false);
        require(gs.vram().pixel_writes()==before,"suppressed line kicks do not consume unused texture coordinates or shader state");
        gs.general_write(0,6);vertex(gs,2,2);vertex(gs,4,4);
        require(gs.vram().read(0,64,0,2,2)==0x80201008u && gs.vram().read(0,64,0,0,0)==0x80000000u,
            "PRIM restarts vertex assembly when a sprite follows suppressed lines");
        gs.general_write(0,7);rejects([&]{vertex(gs,0,0,0,false);},"reserved primitive type 7");
    }
    {
        GsRegisters gs;setup(gs);
        gs.general_write_observed(1,{0x80201000u,15u});vertex(gs,0,0,0,false);vertex(gs,4,0,0,false);vertex(gs,0,4,0,false);
        require(gs.vram().read(0,64,0,0,0)==0x80000000u,"ADC-suppressed vertices transport opaque colors without drawing or fabricating bits");
        gs.general_write(0,3);vertex(gs,0,0);vertex(gs,4,0);const auto before=gs.vram().pixel_writes();
        rejects([&]{vertex(gs,0,4);},"unknown fragment color bits");
        require(gs.vram().pixel_writes()==before,"a real drawing kick refuses retained color uncertainty before any fragment write");
        gs.general_write(0,3u|8u);vertex(gs,0,0);gs.general_write(1,0x80402010u);vertex(gs,4,0);
        rejects([&]{vertex(gs,0,4);},"unknown vertex color bits");
        require(gs.vram().pixel_writes()==before,"Gouraud retains each old vertex's uncertainty after a known current color overwrite");
    }
    for(const bool reverse : {false,true}) {
        GsRegisters gs;setup(gs);const auto before=gs.vram().pixel_writes();
        vertex(gs,0,0);vertex(gs,reverse ? 0u : 4u,reverse ? 4u : 0u);
        require(gs.vram().pixel_writes()==before,"two vertex kicks do not draw a triangle");
        vertex(gs,reverse ? 4u : 0u,reverse ? 0u : 4u);
        require(gs.vram().pixel_writes()==before+10u,"triangle coverage uses the top/left rule in both windings");
        gs.general_write(1,0x80402010);vertex(gs,4,0);vertex(gs,4,4);vertex(gs,0,4);
        require(gs.vram().pixel_writes()==before+16u,"two adjacent triangles cover their rectangle exactly once with no shared-edge overdraw");
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
            require(gs.vram().read(0,64,0,x,y)==(x<4u && y<4u ? x+y<4u ? 0x80201008u : 0x80402010u : 0x80000000u),
                "triangle writes match independently enumerated interior and shared-edge pixels");
    }
    {
        GsRegisters gs;setup(gs);const auto before=gs.vram().pixel_writes();
        const auto fixed=[&](unsigned x,unsigned y){gs.general_write(5,x|(std::uint64_t{y}<<16u));};
        fixed(8,8);fixed(72,8);fixed(8,72);fixed(72,8);fixed(72,72);fixed(8,72);
        require(gs.vram().pixel_writes()==before+16u,"fractional vertex positions retain exact fixed-point coverage at integer pixel centers");
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
            require(gs.vram().read(0,64,0,x,y)==(x>=1u && x<=4u && y>=1u && y<=4u ? 0x80201008u : 0x80000000u),
                "subpixel edges cover exactly the independently enumerated shifted rectangle");
    }
    {
        GsRegisters gs;setup(gs,4);const auto before=gs.vram().pixel_writes();
        vertex(gs,0,0,0,false);vertex(gs,4,0,0,false);vertex(gs,0,4);vertex(gs,4,4,0,false);vertex(gs,0,8);
        require(gs.vram().pixel_writes()==before+20u,"XYZ3 advances strip vertices while suppressing only that triangle's drawing kick");
        require(gs.vram().read(0,64,0,1,5)==0x80201008u && gs.vram().read(0,64,0,3,3)==0x80000000u,
            "a strip retains the actual previous two vertices after a suppressed middle triangle");
    }
    {
        GsRegisters gs;setup(gs,5);const auto before=gs.vram().pixel_writes();vertex(gs,4,4);
        for(const auto p : {std::pair{0u,0u},{4u,0u},{8u,0u},{8u,4u},{8u,8u},{4u,8u},{0u,8u},{0u,4u},{0u,0u}})vertex(gs,p.first,p.second);
        require(gs.vram().pixel_writes()==before+64u,"a fan retains its central vertex and covers the eight triangles without gaps or overlap");
    }
    {
        GsRegisters gs;setup(gs);vertex(gs,0,0);vertex(gs,4,0);gs.general_write(0,3);const auto before=gs.vram().pixel_writes();
        vertex(gs,0,4);vertex(gs,0,0);require(gs.vram().pixel_writes()==before,"writing PRIM resets the real vertex queue");
        vertex(gs,4,0);require(gs.vram().pixel_writes()==before+10u,"new PRIM vertices assemble independently from the old partial triangle");
    }
    {
        GsRegisters gs;setup(gs,3u|8u);gs.general_write(1,0x80000000);vertex(gs,0,0);
        gs.general_write(1,0x80000040);vertex(gs,4,0);gs.general_write(1,0x80004000);vertex(gs,0,4);
        require(gs.vram().read(0,64,0,1,1)==0x80001010u,"Gouraud interpolation uses snapshotted per-vertex colors rather than the last RGBA latch for all vertices");
    }
    {
        GsRegisters gs;setup(gs);gs.general_write(0x4E,1);vertex(gs,0,0,0);vertex(gs,4,0,4);vertex(gs,0,4,8);
        require(gs.vram().read_depth(8192,64,0,1,1)==3u,"triangle depth is interpolated from all three actual vertex depths");
    }
    {
        GsRegisters gs;setup(gs,3u|16u|256u);
        gs.general_write(0x50,(32ull|(1ull<<16u))<<32u);gs.general_write(0x51,0);gs.general_write(0x52,(8ull<<32u)|8u);gs.general_write(0x53,0);
        for(unsigned n=0;n<16;++n) {
            const auto a=0x80000000u|((n*4u)<<8u),b=a+0x100u,c=a+0x200u,d=a+0x300u;
            gs.image_write(a|(std::uint64_t{b}<<32u),c|(std::uint64_t{d}<<32u));
        }
        gs.general_write(0x3F,0);gs.general_write(6,32ull|(1ull<<14u)|(3ull<<26u)|(3ull<<30u)|(1ull<<34u)|(1ull<<35u));
        gs.general_write(8,0);gs.general_write(0x14,0);
        gs.general_write(3,0);vertex(gs,0,0);gs.general_write(3,64);vertex(gs,4,0);gs.general_write(3,64ull<<16u);vertex(gs,0,4);
        require(gs.vram().read(0,64,0,1,1)==0x80000900u && gs.vram().read(0,64,0,3,0)==0x80000300u,
            "UV triangles interpolate actual snapshotted texel coordinates and use the shared texture decoder and DECAL operation");
    }
    {
        GsRegisters gs;setup(gs);gs.general_write(0,3u|8u);gs.general_write(1,0x80000000);vertex(gs,0,0);
        gs.general_write(1,0x80000001);vertex(gs,3,0);gs.general_write(1,0x80000000);
        const auto before=gs.vram().pixel_writes();
        rejects([&]{vertex(gs,0,3);},"fractional triangle attribute DDA precision");
        require(gs.vram().pixel_writes()==before,"a fractional gradient is refused even where its first barycentric sample happens to be integral");
    }
    for(const auto precision:{0u,4u}) {
        GsRegisters gs;setup(gs,3u|16u|256u,precision);
        gs.general_write(0x50,(32ull|(1ull<<16u))<<32u);gs.general_write(0x51,0);
        gs.general_write(0x52,(8ull<<32u)|8u);gs.general_write(0x53,0);
        for(unsigned n=0;n<16;++n) {
            const auto a=0x80000000u|((n*4u)<<8u),b=a+0x100u,c=a+0x200u,d=a+0x300u;
            gs.image_write(a|(std::uint64_t{b}<<32u),c|(std::uint64_t{d}<<32u));
        }
        gs.general_write(0x3F,0);gs.general_write(6,32ull|(1ull<<14u)|(3ull<<26u)|(3ull<<30u)|(1ull<<34u)|(1ull<<35u));
        gs.general_write(8,0);gs.general_write(0x14,0);
        gs.general_write(3,0);vertex(gs,0,0);gs.general_write(3,64);vertex(gs,3,0);gs.general_write(3,64ull<<16u);
        const auto before=gs.vram().pixel_writes();
        if(!precision) {
            rejects([&]{vertex(gs,0,3);},"fractional triangle attribute DDA precision");
            require(gs.vram().pixel_writes()==before,"default UV precision refuses before writing the first fragment");
        } else {
            vertex(gs,0,3);
            require(gs.vram().read(0,64,0,1,1)==0x80000900u && gs.vram().read(0,64,0,2,0)==0x80000200u,
                "explicit triangle UV experiment floors rational coordinates and samples actual uploaded texels");
            rejects([&]{gs.diagnostic_texture_precision(4);},"before guest activity");
        }
    }
    for(const bool depth:{false,true}) {
        GsRegisters gs;setup(gs,depth ? 3u : 3u|8u,4);
        if(depth)gs.general_write(0x4E,1);
        gs.general_write(1,0x80000000);vertex(gs,0,0);
        gs.general_write(1,0x80000001);vertex(gs,3,0,depth ? 1u : 0u);gs.general_write(1,0x80000000);
        const auto before=gs.vram().pixel_writes();
        rejects([&]{vertex(gs,0,3);},"fractional triangle attribute DDA precision");
        require(gs.vram().pixel_writes()==before,"UV experiment cannot select undocumented color or depth precision");
    }
    std::cout<<"Triangle/strip/fan queues, winding, top-left coverage, ADC, PRIM reset, exact Gouraud/depth and explicit precision limits passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
