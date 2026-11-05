// Independent 8x8 source grid and exact expected filter samples.
#include "boot_checks.hpp"
#include "ps2native/gs_registers.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
std::uint32_t source(unsigned x,unsigned y){return 0x80000000u|(x*16u)|(y*16u<<8u)|((x+y)*8u<<16u);}
void setup(GsRegisters& gs,bool flush=true,bool cold_experiment=false,bool cold_reset=true,unsigned precision=0) {
    if(cold_reset)gs.reset(0);
    if(cold_experiment)gs.diagnostic_texture_cache_reset_invalid();
    if(precision)gs.diagnostic_texture_precision(precision);
    gs.general_write(0,0x116);gs.general_write(1,0x3F80000080808080ull);
    gs.general_write(0x18,0);gs.general_write(0x1A,1);gs.general_write(0x4C,1u<<16u);
    gs.general_write(0x40,(7ull<<16u)|(7ull<<48u));gs.general_write(0x47,3u<<16u);
    gs.general_write(0x4E,1ull<<32u);gs.general_write(0x22,0);gs.general_write(0x4A,0);
    gs.general_write(0x50,(32ull|(1ull<<16u))<<32u);gs.general_write(0x51,0);
    gs.general_write(0x52,8u|(8ull<<32u));gs.general_write(0x53,0);
    for(unsigned n=0;n<64;n+=4)gs.image_write(source(n%8,n/8)|(std::uint64_t{source((n+1)%8,(n+1)/8)}<<32u),
        source((n+2)%8,(n+2)/8)|(std::uint64_t{source((n+3)%8,(n+3)/8)}<<32u));
    if(flush)gs.general_write(0x3F,0);
    gs.general_write(6,32ull|(1ull<<14u)|(3ull<<26u)|(3ull<<30u)|(1ull<<34u)|(1ull<<35u));
    gs.general_write(8,0);gs.general_write(0x14,0x60);
}
void pixel(GsRegisters& gs,unsigned x,unsigned y,unsigned u,unsigned v) {
    gs.general_write(0,0x116);gs.general_write(3,u|(std::uint64_t{v}<<16u));
    gs.general_write(5,x*16u|(std::uint64_t{y*16u}<<16u));
    gs.general_write(3,(u+16u)|(std::uint64_t{v+16u}<<16u));
    gs.general_write(5,(x+1u)*16u|(std::uint64_t{(y+1u)*16u}<<16u));
}
}
int main() try {
    for(const bool negative:{false,true}) {
        GsRegisters st;setup(st);st.general_write(0,0x16);st.general_write(0x14,0);
        st.general_write(1,0x3F00000080808080ull); // first Q must not control sprite division
        const auto start=negative?0xBE000000u:0x3E000000u;
        st.general_write(2,start|(std::uint64_t{start}<<32));st.general_write(5,0);
        st.general_write(1,0x4000000080808080ull);
        st.general_write(2,0x3EC000003EC00000ull);st.general_write(5,16u|(16ull<<16));
        require(st.vram().read(0,64,0,0,0)==(negative?source(7,7):source(0,0)),
            "STQ sprite divides both vertices by the second Q exactly, preserving signed coordinates and wrap");
    }
    {
        GsRegisters st;setup(st);st.general_write(0,0x16);st.general_write(0x14,0);
        st.general_write(2,0);st.general_write(5,0);
        st.general_write(1,0x4040000080808080ull);st.general_write(2,0x3F8000003F800000ull);
        const auto before=st.vram().pixel_writes();rejects([&]{st.general_write(5,16u|(16ull<<16));},"fractional STQ");
        require(st.vram().pixel_writes()==before,"STQ division cannot silently round nonrepresentable 10.4 coordinates");
    }
    {
        GsRegisters st;setup(st);st.general_write(0,0x16);st.general_write(0x14,0);
        st.general_write_observed(2,{0,0x100u});st.general_write(5,0);
        st.general_write(2,0x3F0000003F000000ull);
        const auto before=st.vram().pixel_writes();rejects([&]{st.general_write(5,16u|(16ull<<16));},"unknown coordinate bits");
        require(st.vram().pixel_writes()==before,"a later known ST latch cannot erase uncertainty in an earlier vertex");
    }
    GsRegisters gs;setup(gs);pixel(gs,0,0,8,8);
    require(gs.vram().read(0,64,0,0,0)==source(0,0),"half-texel coordinates sample their exact texel center without neighbor blending");
    pixel(gs,1,0,16,16);require(gs.vram().read(0,64,0,1,0)==0x80080808u,
        "integer texel coordinates average four independently known surrounding texels and all RGBA channels");
    pixel(gs,2,0,12,20);require(gs.vram().read(0,64,0,2,0)==0x80080C04u,
        "quarter and three-quarter subtexel positions retain all four UV fractional bits");
    gs.general_write(8,5);pixel(gs,3,0,0,0);require(gs.vram().read(0,64,0,3,0)==source(0,0),
        "negative half-texel tap indices clamp independently at each texture edge");
    gs.general_write(8,0);pixel(gs,4,0,0,0);require(gs.vram().read(0,64,0,4,0)==0x80383838u,
        "repeat applies separately to all four half-texel taps across both texture borders");
    for(const auto lod:{0ull,1ull,0x18ull}) {
        gs.general_write(0x14,lod);pixel(gs,5,0,16,16);
        require(gs.vram().read(0,64,0,5,0)==source(1,1),"nearest level-zero filters ignore unused MIP maximum levels and preserve previous nearest coordinates");
    }
    for(const auto lod:{0x0000000100000041ull,0x0000080000000021ull}) {
        gs.general_write(0x14,lod);pixel(gs,6,0,16,16);
        require(gs.vram().read(0,64,0,6,0)==0x80080808u,"a fixed strictly positive/negative K selects the documented minification/magnification filter");
    }
    gs.general_write(0x14,0x20);rejects([&]{pixel(gs,7,0,16,16);},"dynamic LOD");
    gs.general_write(0x14,0x21);rejects([&]{pixel(gs,7,0,16,16);},"zero-LOD boundary");
    for(const auto q:{0x3F000000u,0xBF000000u,0x3EFFFFFFu,0x40000000u,0xC0000000u,0x40000001u}) {
        GsRegisters dynamic;setup(dynamic);dynamic.general_write(0x14,0x20);
        dynamic.general_write(1,(std::uint64_t{q}<<32)|0x80808080u);pixel(dynamic,0,0,16,16);
        require(dynamic.vram().read(0,64,0,0,0)==((q&0x7FFFFFFFu)<0x3F800000u ? source(1,1) : 0x80080808u),
            "UV dynamic LOD uses absolute Q exponent bounds to prove minification/magnification without approximate host logarithms");
    }
    for(const bool experiment:{false,true}) {
        GsRegisters zero;setup(zero,true,false,true,experiment?32u:0u);zero.general_write(0x14,0x20);
        zero.general_write(1,0x80808080u);
        if(experiment) {
            pixel(zero,0,0,16,16);require(zero.vram().read(0,64,0,0,0)==source(1,1),
                "explicit zero-Q experiment selects positive saturated LOD without replacing source color or UV");
        } else rejects([&]{pixel(zero,0,0,16,16);},"exceptional Q");
    }
    gs.general_write(0x14,0x80);rejects([&]{pixel(gs,7,0,16,16);},"mipmap");
    gs.general_write(0x14,0x60);const auto before=gs.vram().pixel_writes();
    rejects([&]{pixel(gs,7,0,9,9);},"fractional bilinear truncation");
    require(gs.vram().pixel_writes()==before,"unproven intermediate rounding cannot fabricate even one fragment");
    const auto upload=[](GsRegisters& device,unsigned pointer,std::uint32_t color) {
        device.general_write(0x50,(std::uint64_t{pointer}|(1ull<<16u))<<32u);device.general_write(0x51,0);
        device.general_write(0x52,8u|(8ull<<32u));device.general_write(0x53,0);
        for(unsigned n=0;n<16;++n)device.image_write(color|(std::uint64_t{color}<<32u),color|(std::uint64_t{color}<<32u));
    };
    GsRegisters cache;setup(cache);upload(cache,32,0x80402010u);pixel(cache,0,0,8,8);
    require(cache.vram().read(0,64,0,0,0)==0x80402010u,"a cold page loads actual new bytes after an upload without a redundant global invalidation");
    upload(cache,64,0x80808080u);pixel(cache,1,0,8,8);
    require(cache.vram().read(0,64,0,1,0)==0x80402010u,"transfers into unrelated pages cannot make an unchanged cached texture stale");
    upload(cache,32,0x80201008u);const auto cached_before=cache.vram().pixel_writes();
    rejects([&]{pixel(cache,2,0,8,8);},"cached texture page changed");
    require(cache.vram().pixel_writes()==cached_before,"modified cached texels cannot silently read through to new VRAM bytes");
    cache.general_write(0x3F,0);pixel(cache,2,0,8,8);
    require(cache.vram().read(0,64,0,2,0)==0x80201008u,"explicit TEXFLUSH discards the stale page and exposes real uploaded bytes");
    cache.general_write(0x50,64ull|(1ull<<16u)|((32ull|(1ull<<16u))<<32u));
    cache.general_write(0x51,0);cache.general_write(0x52,8u|(8ull<<32u));cache.general_write(0x53,2);
    rejects([&]{pixel(cache,3,0,8,8);},"cached texture page changed");
    cache.general_write(0x3F,0);pixel(cache,3,0,8,8);
    require(cache.vram().read(0,64,0,3,0)==0x80808080u,"local VRAM copies dirty the same cached pages as host uploads");
    // Real render-to-texture ping-pong: an earlier source becomes the draw
    // target while a separate known page is the active texture.
    upload(cache,64,0x80442210u);
    cache.general_write(6,64ull|(1ull<<14u)|(3ull<<26u)|(3ull<<30u)|(1ull<<34u)|(1ull<<35u));
    cache.general_write(0x4C,1u|(1u<<16u));pixel(cache,0,0,8,8);
    require(cache.vram().read(8192,64,0,0,0)==0x80442210u,
        "drawing to an earlier sampled page writes actual bytes from a disjoint active texture");
    cache.general_write(0x4C,1u<<16u);
    cache.general_write(6,32ull|(1ull<<14u)|(3ull<<26u)|(3ull<<30u)|(1ull<<34u)|(1ull<<35u));
    const auto before_drawn_texture=cache.vram().pixel_writes();
    rejects([&]{pixel(cache,4,0,8,8);},"cached texture page changed");
    require(cache.vram().pixel_writes()==before_drawn_texture,"a later sample cannot invent cache invalidation after drawing changed that texture");
    cache.general_write(0x3F,0);pixel(cache,4,0,8,8);
    require(cache.vram().read(0,64,0,4,0)==0x80442210u,"guest TEXFLUSH exposes the real rendered texture bytes");
    GsRegisters uninitialized;setup(uninitialized,false);
    GsRegisters bounded;setup(bounded);upload(bounded,0x3FE0u,0x80402010u);
    bounded.general_write(6,0x3FE0ull|(1ull<<14u)|(10ull<<26u)|(10ull<<30u)|(1ull<<34u)|(1ull<<35u));
    pixel(bounded,0,0,8,8);
    require(bounded.vram().read(0,64,0,0,0)==0x80402010u,
        "actual sprite UV pages prove disjoint drawing even when unused nominal texture dimensions wrap across the framebuffer");
    bounded.general_write(0x4C,511u|(1u<<16u));const auto bounded_before=bounded.vram().pixel_writes();
    rejects([&]{pixel(bounded,0,0,8,8);},"cache feedback");
    require(bounded.vram().pixel_writes()==bounded_before,"actual source-page feedback remains refused before its first write");
    rejects([&]{pixel(uninitialized,0,0,8,8);},"initial TEXFLUSH");
    GsRegisters diagnostic;setup(diagnostic,false,true);pixel(diagnostic,0,0,8,8);
    require(diagnostic.vram().read(0,64,0,0,0)==source(0,0) && !diagnostic.general(0x3F),
        "explicit cold-tag experiment reads actual VRAM and fabricates neither a TEXFLUSH command nor texture data");
    rejects([&]{diagnostic.diagnostic_texture_cache_reset_invalid();},"precede guest activity");
    upload(diagnostic,32,0x80402010u);rejects([&]{pixel(diagnostic,1,0,8,8);},"cached texture page changed");
    diagnostic.write(kGsCsr,8,kCsrReset,1,{});
    require(!diagnostic.general(0x3F) && diagnostic.vram().read(8192,64,0,1,1)==0x80402010u,
        "experimental soft reset preserves VRAM and does not fabricate a texture invalidation command");
    setup(diagnostic,false,false,false);pixel(diagnostic,0,0,8,8);
    require(diagnostic.vram().read(0,64,0,0,0)==source(0,0),"soft reset preserves the explicitly selected cache-tag experiment");
    setup(diagnostic,false);rejects([&]{pixel(diagnostic,0,0,8,8);},"initial TEXFLUSH");
    const auto fractional=[](GsRegisters& device) {
        device.general_write(0x14,0);device.general_write(0,0x116);
        device.general_write(3,8u|(8ull<<16u));device.general_write(5,32ull<<16u);
        device.general_write(3,24u|(24ull<<16u));device.general_write(5,48u|(48ull<<16u));
    };
    GsRegisters ideal;setup(ideal,true,false,true,1);fractional(ideal);
    require(ideal.vram().read(0,64,0,0,2)==source(0,0) && ideal.vram().read(0,64,0,1,2)==source(0,0) &&
        ideal.vram().read(0,64,0,2,2)==source(1,0),"nominal-DDA experiment uses actual coordinates/VRAM and floors rational subtexel positions");
    ideal.general_write(0x14,0x60);rejects([&]{pixel(ideal,0,0,9,9);},"bilinear truncation");
    rejects([&]{ideal.diagnostic_texture_precision(3);},"before guest activity");
    GsRegisters staged;setup(staged,true,false,true,2);pixel(staged,0,0,9,9);
    require(staged.vram().read(0,64,0,0,0)==0x80000101u,"separable-filter experiment exposes both truncation stages rather than rounding a host bilinear sample");
    rejects([&]{fractional(staged);},"fractional UV DDA");
    setup(staged);rejects([&]{pixel(staged,0,0,9,9);},"bilinear truncation");
    GsRegisters triangle_only;setup(triangle_only,true,false,true,4);
    rejects([&]{fractional(triangle_only);},"fractional UV DDA");
    triangle_only.general_write(0x14,0x60);rejects([&]{pixel(triangle_only,0,0,9,9);},"bilinear truncation");
    GsRegisters invalid;invalid.reset(0);rejects([&]{invalid.diagnostic_texture_precision(64);},"valid modes");
    std::cout<<"Exact level-zero bilinear/nearest, subtexel centers, independent edge wrapping, fixed LOD and explicit rounding boundaries passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
