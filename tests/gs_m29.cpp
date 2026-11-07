#include "boot_checks.hpp"
#include "gs_program.hpp"
#include <array>
#include <iostream>
#include <set>

namespace {
using namespace ps2native;
using namespace boot_checks;
constexpr std::uint64_t tag(unsigned loops, unsigned regs = 1u, unsigned format = 0u, bool eop = true) {
    return loops | (eop ? 1ull << 15 : 0u) | (std::uint64_t{format} << 58) | (std::uint64_t{regs & 15u} << 60);
}
void check_vram() {
    GsVram vram;
    rejects([&] { (void)vram.read(0, 64, 0, 0, 0); }, "unwritten bits");
    // Independent byte-address vectors from GS page/block/column diagrams.
    constexpr std::array<std::array<unsigned, 3>, 15> vectors{{
        {0,0,0}, {1,0,4}, {2,0,16}, {0,1,8}, {0,2,64}, {0,4,128},
        {8,0,256}, {0,8,512}, {16,0,1024}, {0,16,2048}, {32,0,4096},
        {63,31,8188}, {64,0,8192}, {0,32,16384}, {64,32,24576}
    }};
    for (const auto& v : vectors) require(GsVram::address(0,128,0,v[0],v[1]) == v[2], "PSMCT32 address vector");
    std::set<unsigned> addresses;
    for (unsigned y=0; y<64; ++y) for (unsigned x=0; x<128; ++x) {
        const auto address = GsVram::address(0,128,0,x,y);
        require(addresses.insert(address).second, "four-page swizzle is bijective");
        vram.write(0,128,0,x,y,0x80000000u | (y<<16) | x);
        require(vram.word_at(address) == (0x80000000u | (y<<16) | x), "raw swizzled storage");
    }
    vram.write(0,128,1,0,0,0x00554433u);
    require(vram.read(0,128,0,0,0)==0x80554433u, "PSMCT24 preserves high byte");
    vram.write(0,128,0,0,0,0x11223344u,0xFF00FF00u);
    require(vram.read(0,128,0,0,0)==0x80224444u, "bitwise framebuffer mask");
    require(GsVram::address(GsVram::kBytes-256,64,0,8,0)==0u, "VRAM 4 MiB wrap");
    require(GsVram::address(0,0,0,0,32)==0u && GsVram::address(0,0,0,64,32)==8192u,
            "zero page-row stride aliases rows without changing horizontal pages");
    vram.write(0,0,0,3,2,0x80765432u);
    require(vram.read(0,0,0,3,34)==0x80765432u,"zero-stride reads consume the actual aliased VRAM word");
    rejects([&] { (void)GsVram::address(0,63,0,0,0); }, "buffer width");
    rejects([&] { (void)GsVram::address(0,64,3,0,0); }, "pixel format 3");
    vram.invalidate(); rejects([&] { (void)vram.read(0,128,0,0,0); }, "unwritten bits");
    constexpr std::array<std::array<unsigned,3>,10> depth_vectors{{
        {0,0,6144},{8,0,6146},{16,0,6656},{32,0,2048},{0,8,6400},
        {0,16,4096},{0,32,7168},{63,63,2046},{64,0,14336},{0,64,22528}
    }};
    for (const auto& v : depth_vectors)
        require(GsVram::depth_address(0,128,10,v[0],v[1])==v[2],"PSMZ16S diagram address");
    for (unsigned psm : {0u,1u,2u,10u}) {
        vram.invalidate(); addresses.clear();
        for (unsigned y=0;y<(psm<2u?32u:64u);++y) for(unsigned x=0;x<64;++x) {
            require(addresses.insert(GsVram::depth_address(0,64,psm,x,y)).second,"depth page bijection");
            vram.write_depth(0,64,psm,x,y,y*64+x);
        }
        for (unsigned y=0;y<(psm<2u?32u:64u);++y) for(unsigned x=0;x<64;++x)
            require(vram.read_depth(0,64,psm,x,y)==y*64+x,"depth packing preserves adjacent pixels");
    }
    vram.invalidate(); vram.write_depth(0,64,10,0,0,0x1234);
    rejects([&]{(void)vram.read_depth(0,64,10,8,0);},"unwritten bits");
    vram.write_depth(0,64,10,8,0,0xABCD);
    require(vram.word_at(6144)==0xABCD1234u,"depth halfword packing");
    vram.write(0,64,0,0,0,0x12345678);
    vram.write(0,64,0,0,0,0x12345678,0,0,true);
    require(vram.read(0,64,0,0,0)==0x12345678,"conditional equal write keeps knowledge");
    vram.write(0,64,0,0,0,0x12345678,0,0x80000000u);
    require(vram.read(0,64,1,0,0)==0x345678,"unknown alpha does not contaminate RGB");
    rejects([&]{(void)vram.read(0,64,0,0,0);},"unwritten bits");
    vram.write(0,64,0,0,0,0x12345679,0,0,true);
    rejects([&]{(void)vram.read(0,64,1,0,0);},"unwritten bits");
    vram.write(0,64,0,0,0,0x12345678);
    require(vram.read(0,64,0,0,0)==0x12345678,"unconditional write resolves uncertainty");
    constexpr std::array<std::array<unsigned,3>,15> indexed_vectors{{
        {0,0,0},{1,0,4},{2,0,16},{4,0,32},{8,0,2},{0,1,8},{0,2,33},
        {0,3,41},{0,4,96},{0,6,65},{0,8,128},{16,0,256},{0,16,512},
        {128,0,8192},{0,64,16384}
    }};
    for (const auto& v : indexed_vectors)
        require(GsVram::address(0,256,19,v[0],v[1])==v[2],"PSMT8 diagram address");
    vram.invalidate(); addresses.clear();
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x) {
        require(addresses.insert(GsVram::address(0,256,19,x,y)).second,"PSMT8 four-page bijection");
        vram.write(0,256,19,x,y,(x+3*y)&255u);
    }
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x)
        require(vram.read(0,256,19,x,y)==((x+3*y)&255u),"PSMT8 adjacent byte preservation");
    require(GsVram::address(0,64,19,0,64)==0u && GsVram::address(0,192,19,0,64)==8192u,
            "indexed BW drops its low 64-texel unit when forming the page-row stride");
    vram.invalidate(); addresses.clear();
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x) {
        const auto nibble=GsVram::address(0,256,20,x,y)*2u+((y>>1)&1u);
        require(addresses.insert(nibble).second,"PSMT4 four-page nibble bijection");
        vram.write(0,256,20,x,y,(x+3*y)&15u);
    }
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x)
        require(vram.read(0,256,20,x,y)==((x+3*y)&15u),"PSMT4 neighboring nibble preservation");
    constexpr std::array<std::array<unsigned,3>,10> nibble_vectors{{
        {0,0,0},{8,0,1},{16,0,2},{24,0,3},{4,2,0},{0,4,96},
        {32,0,512},{0,16,256},{64,0,2048},{0,128,16384}
    }};
    for(const auto& v:nibble_vectors)
        require(GsVram::address(0,256,20,v[0],v[1])==v[2],"PSMT4 diagram address");
    vram.invalidate();vram.write(0,128,20,0,0,10);
    rejects([&]{(void)vram.read(0,128,20,4,2);},"unwritten bits");
    vram.write(0,128,20,4,2,11);
    require(vram.read(0,128,19,0,0)==0xBA,"PSMT4 low/high nibbles share actual physical byte");
}
void setup_transfer(GsRegisters& gs, unsigned psm, unsigned w, unsigned h, unsigned x=0, unsigned y=0) {
    gs.general_write(0x50, (1ull<<48) | (std::uint64_t{psm}<<56));
    gs.general_write(0x51, (std::uint64_t{x}<<32) | (std::uint64_t{y}<<48));
    gs.general_write(0x52, w | (std::uint64_t{h}<<32)); gs.general_write(0x53,0);
}
void check_transfer_gif() {
    {
        GsRegisters indexed;indexed.reset(0);indexed.diagnostic_texture_precision(8);
        indexed.general_write(0x50,(2ull<<48)|(19ull<<56));indexed.general_write(0x51,0);
        indexed.general_write(0x52,12ull|(2ull<<32));indexed.general_write(0x53,0);
        indexed.image_write(0x0807060504030201ull,0x100F0E0D0C0B0A09ull);
        indexed.image_write(0x1817161514131211ull,~0ull);
        for(unsigned p=0;p<24;++p)require(indexed.vram().read(0,128,19,p%12,p/12)==p+1,
            "explicit unaligned transfer experiment preserves the real byte stream across 64-bit row boundaries");
        rejects([&]{(void)indexed.vram().read(0,128,19,12,0);},"unwritten bits");
        rejects([&]{(void)indexed.vram().read(0,128,19,0,2);},"unwritten bits");
    }
    {
        GsRegisters indexed;indexed.reset(0);Gif stream;stream.reset();
        indexed.general_write(0x50,(2ull<<48)|(20ull<<56));
        indexed.general_write(0x51,(28ull<<32)|(3ull<<48));
        indexed.general_write(0x52,8ull|(3ull<<32));indexed.general_write(0x53,0);
        stream.write_fifo(tag(1,0,2),0,indexed);
        stream.write_fifo(0xFEDCBA9876543210ull,0xFFFFFFFF76543210ull,indexed);
        for(unsigned p=0;p<24;++p)
            require(indexed.vram().read(0,128,20,28+p%8,3+p/8)==(p&15u),"IMAGE4 nibble order and final padding");
        rejects([&]{(void)indexed.vram().read(0,128,20,28,6);},"unwritten bits");
        indexed.general_write(0x51,30ull<<32);
        rejects([&]{indexed.general_write(0x53,0);},"hardware alignment");
    }
    {
        GsRegisters indexed; indexed.reset(0); Gif stream; stream.reset();
        indexed.general_write(0x50,(2ull<<48)|(19ull<<56));
        indexed.general_write(0x51,(14ull<<32)|(3ull<<48));
        indexed.general_write(0x52,8ull|(3ull<<32)); indexed.general_write(0x53,0);
        stream.write_fifo(tag(2,0,2),0,indexed);
        stream.write_fifo(0x0807060504030201ull,0x100F0E0D0C0B0A09ull,indexed);
        stream.write_fifo(0x1817161514131211ull,~0ull,indexed);
        for(unsigned p=0;p<24;++p)
            require(indexed.vram().read(0,128,19,14+p%8,3+p/8)==p+1,"IMAGE8 rows/columns/blocks and qword padding");
        rejects([&]{(void)indexed.vram().read(0,128,19,14,6);},"unwritten bits");
        indexed.general_write(0x51,15ull<<32);
        rejects([&]{indexed.general_write(0x53,0);},"hardware alignment");
    }
    GsRegisters gs; gs.reset(0);
    Gif gif; gif.reset();
    setup_transfer(gs,0,2,3,7,7); // block boundaries and final qword padding
    gif.write_fifo(tag(2,0,2),0,gs);
    gif.write_fifo(0x2222222211111111ull,0x4444444433333333ull,gs);
    gif.write_fifo(0x6666666655555555ull,0xDEADBEEFDEADBEEFull,gs);
    for (unsigned p=0; p<6; ++p)
        require(gs.vram().read(0,64,0,7+p%2,7+p/2)==0x11111111u*(p+1), "IMAGE32 rectangle/padding");
    require(gif.read_stat()==0u, "IMAGE completed");
    const auto revision = gs.vram().revision(); gs.image_write(0,0);
    require(gs.vram().revision()==revision,"inactive IMAGE/HWREG documented NOP");
    setup_transfer(gs,1,8,1);
    gif.write_fifo(tag(2,1,3),0,gs);
    gif.write_fifo(0x0807060504030201ull,0x100F0E0D0C0B0A09ull,gs);
    gif.write_fifo(0x1817161514131211ull,0,gs);
    for (unsigned p=0; p<8; ++p)
        require(gs.vram().read(0,64,1,p,0)==(3*p+1u)+((3*p+2u)<<8)+((3*p+3u)<<16), "IMAGE24 crosses qwords");
    rejects([&] { (void)gs.vram().read(0,64,0,0,0); }, "unwritten bits");
    setup_transfer(gs,0,2,1,0,2); gs.general_write(0x54,0xBBAA998877665544ull);
    require(gs.vram().read(0,64,0,1,2)==0xBBAA9988u, "HWREG");
    // Local copy, distinct rows, then overlap with right-to-left traversal.
    gs.general_write(0x50,(1u<<16)|(1ull<<48));
    gs.general_write(0x51,(2u<<16)|(1ull<<32)|(2ull<<48)|(2ull<<59));
    gs.general_write(0x52,2u|(1ull<<32)); gs.general_write(0x53,2u);
    require(gs.vram().read(0,64,0,1,2)==0x77665544u && gs.vram().read(0,64,0,2,2)==0xBBAA9988u,
            "local copy preserves overlap with DIR");
    rejects([&] { gs.general_write(0x53,1u); }, "local-to-host");
    rejects([&] { setup_transfer(gs,0x15,1,1); }, "pixel format 21");
    rejects([&] { setup_transfer(gs,0,3,1); }, "hardware alignment");
    rejects([&] { setup_transfer(gs,1,6,1); }, "hardware alignment");
    setup_transfer(gs,0,2,1); gs.general_write(0x53,3u);
    const auto cancelled = gs.vram().revision(); gs.general_write(0x54,0);
    require(gs.vram().revision()==cancelled,"TRXDIR off cancels upload");
    setup_transfer(gs,0,2,2,2047u,2047u);
    gs.general_write(0x51,(2047ull<<32)|(2047ull<<48)|(3ull<<59));
    gs.general_write(0x53,0); // upload ignores local-copy DIR
    gs.image_write(0x2222222211111111ull,0x4444444433333333ull);
    require(gs.vram().read(0,64,0,2047,2047)==0x11111111u && gs.vram().read(0,64,0,0,2047)==0x22222222u &&
            gs.vram().read(0,64,0,2047,0)==0x33333333u && gs.vram().read(0,64,0,0,0)==0x44444444u,
            "TRXPOS coordinates wrap modulo 2048");
    // REGLIST with an odd register count: the upper final half is not a tag.
    gif.write_fifo(tag(1,3,1),0xF10u,gs);
    gif.write_fifo(6,0x3F80000080402010ull,gs); gif.write_fifo(~0ull,~0ull,gs);
    require(gs.general(0)==6u && gs.general(1)==0x3F80000080402010ull && gif.read_stat()==0u,"REGLIST odd padding");
    rejects([&] { gif.write_fifo(tag(1,1,1),0xEu,gs); }, "invalid register descriptor");
    // PRE and PACKED RGBA/STQ/UV unpacking use a distinct layout from A+D.
    gif.write_fifo(tag(1,1)|(1ull<<46)|(6ull<<47),1u,gs);
    gif.write_fifo(0xBB000000AAull,0xDD000000CCull,gs);
    require(gs.general(0)==6u && gs.general(1)==0x3F800000DDCCBBAAull,"PACKED RGBA and PRE");
    gif.write_fifo(tag(1,2),0x12u,gs);
    gif.write_fifo(0x1234,0x40000000,gs); gif.write_fifo(1,2,gs);
    require((*gs.general(1)>>32)==0x40000000u,"STQ supplies Q");
    gif.write_fifo(tag(1,1),3u,gs); gif.write_fifo(0x123400001234ull,~0ull,gs);
    require(gs.general(3)==0x12341234u,"PACKED UV");
    // Empty tags ignore PRE/descriptors and can terminate a chained packet.
    gif.write_fifo(tag(0,1)|(1ull<<46),0xBu,gs);
    require(gif.read_stat()==0u,"empty tag ignores descriptor");
}
void configure_draw(GsRegisters& gs, bool raster_state = true) {
    gs.general_write(0,6); gs.general_write(0x1A,1); gs.general_write(0x18,0);
    gs.general_write(0x4C,1u<<16); gs.general_write(0x40,(63ull<<16)|(63ull<<48));
    gs.general_write(0x47,3u<<16); gs.general_write(0x4E,1ull<<32);
    gs.general_write(0x45,0); gs.general_write(0x49,0);
    if (raster_state) { gs.general_write(0x22,0); gs.general_write(0x4A,0); }
    gs.general_write(0x46,1); gs.general_write(1,0x80402010);
}
void check_texture24() {
    for(unsigned tfx:{0u,1u})for(bool tcc:{false,true})for(bool aem:{false,true})for(unsigned alpha:{0u,64u,128u,255u}) {
        GsRegisters gs;gs.reset(0);configure_draw(gs);
        const auto upload=[&](unsigned x,unsigned y) {
            gs.general_write(0x50,(512ull<<32u)|(2ull<<48u)|(1ull<<56u));
            gs.general_write(0x51,(std::uint64_t{x}<<32u)|(std::uint64_t{y}<<48u));
            gs.general_write(0x52,8u|(1ull<<32u));gs.general_write(0x53,0);
            // Eight RGB24 texels cross 64-bit and IMAGE quadword boundaries.
            std::array<std::uint64_t,3> packed{};
            for(unsigned pixel=1;pixel<8u;++pixel)for(unsigned byte=0;byte<3u;++byte) {
                const auto at=pixel*3u+byte;
                packed[at/8u]|=std::uint64_t{(0x123456u>>(byte*8u))&255u}<<((at%8u)*8u);
            }
            gs.image_write(packed[0],packed[1]);gs.image_write(packed[2],~0ull);
        };
        for(unsigned y:{0u,32u})for(unsigned x:{0u,64u})upload(x,y);
        rejects([&]{(void)gs.vram().read(0x20000,128,0,1,0);},"unwritten bits");
        gs.general_write(0x3F,0);
        const auto tex=512ull|(2ull<<14u)|(1ull<<20u)|(7ull<<26u)|(6ull<<30u)|
            (std::uint64_t{tcc}<<34u)|(std::uint64_t{tfx}<<35u);
        gs.general_write(6,tex);gs.general_write(8,0);gs.general_write(0x14,1);
        gs.general_write(1,0x40808080u);gs.general_write(0,0x116);
        const auto sprite=[&](unsigned dest,unsigned u,unsigned v) {
            gs.general_write(3,(u*16u)|(std::uint64_t{v*16u}<<16u));gs.general_write(5,dest*16u);
            gs.general_write(3,((u+1u)*16u)|(std::uint64_t{(v+1u)*16u}<<16u));gs.general_write(5,(dest+1u)*16u|(16ull<<16u));
        };
        if(tcc)gs.general_write(0x3B,alpha|(aem ? 1u<<15u : 0u));
        // TCC=RGB deliberately leaves TEXA unwritten and keeps vertex alpha.
        for(unsigned i=0;i<4u;++i) {
            const auto u=(i&1u) ? 65u : 1u,v=(i&2u) ? 32u : 0u;
            sprite(i,u,v);
            const auto expected_alpha=tcc ? (tfx ? alpha : alpha/2u) : 64u;
            require(gs.vram().read(0,64,0,i,0)==(0x123456u|(expected_alpha<<24u)),
                "RGB24 samples all four CT32-layout pages, expands TA0 and applies TCC/MODULATE/DECAL without reading the unused high byte");
        }
        sprite(4,0,0);
        const auto black_alpha=tcc ? (aem ? 0u : tfx ? alpha : alpha/2u) : 64u;
        require(gs.vram().read(0,64,0,4,0)==black_alpha<<24u,"RGB24 AEM clears alpha only for black texels");
        upload(64,32);
        rejects([&]{sprite(5,65,32);},"TEXFLUSH");
    }
    GsRegisters missing;missing.reset(0);configure_draw(missing);
    // A concrete RGB24 texel cannot establish its alpha without TEXA.
    missing.general_write(0x50,(512ull<<32u)|(1ull<<48u)|(1ull<<56u));
    missing.general_write(0x51,0);missing.general_write(0x52,8u|(1ull<<32u));missing.general_write(0x53,0);
    missing.image_write(0,0);missing.image_write(0,0);missing.general_write(0x3F,0);
    missing.general_write(6,512ull|(1ull<<14u)|(1ull<<20u)|(3ull<<26u)|(1ull<<34u));
    missing.general_write(8,0);missing.general_write(0x14,1);missing.general_write(0,0x116);missing.general_write(3,0);missing.general_write(5,0);
    missing.general_write(3,16u|(16u<<16u));
    rejects([&]{missing.general_write(5,16u|(16u<<16u));},"register 59");
}
void check_textured_sprites() {
    GsRegisters gs;gs.reset(0);configure_draw(gs);
    const auto upload=[&](unsigned base,unsigned psm,unsigned bw,unsigned width,unsigned height,const std::vector<std::uint64_t>& data) {
        gs.general_write(0x50,(std::uint64_t{base/256u}<<32)|(std::uint64_t{bw/64u}<<48)|(std::uint64_t{psm}<<56));
        gs.general_write(0x51,0);gs.general_write(0x52,width|(std::uint64_t{height}<<32));gs.general_write(0x53,0);
        for(std::size_t i=0;i<data.size();i+=2)gs.image_write(data[i],i+1<data.size()?data[i+1]:0);
    };
    std::vector<std::uint64_t> palette;
    for(unsigned i=0;i<16;i+=2)palette.push_back((0x80604020ull+i)|((0x80604021ull+i)<<32));
    upload(0x20000,0,64,8,2,palette);
    upload(0x10000,20,128,8,2,{0xFEDCBA9876543210ull});
    const std::uint64_t tex=256u|(2ull<<14)|(20ull<<20)|(3ull<<26)|(1ull<<30)|(1ull<<34)|(512ull<<37);
    rejects([&]{gs.general_write(6,tex|(1ull<<61));},"TEXFLUSH");
    gs.general_write(0x3F,0);gs.general_write(6,tex|(1ull<<61));
    gs.general_write(0x14,1);gs.general_write(8,0);gs.general_write(0x42,0x44);gs.general_write(1,0x80808080);
    const auto sprite=[&](unsigned x,unsigned y,unsigned width=8,unsigned u0=0,unsigned u1=128) {
        gs.general_write(0,0x156);gs.general_write(3,u0);gs.general_write(5,x*16u|(std::uint64_t{y*16u}<<16));
        gs.general_write(3,u1|(32u<<16));gs.general_write(5,(x+width)*16u|(std::uint64_t{(y+2u)*16u}<<16));
    };
    sprite(0,0);
    for(unsigned i=0;i<16;++i)require(gs.vram().read(0,64,0,i%8,i/8)==0x80604020u+i,
        "T4 CSM1 palette, nearest UV, modulate and full alpha blend produce exact source colors without reading unknown destination");
    for(auto& p:palette)p=0x8040201080402010ull;
    upload(0x20000,0,64,8,2,palette);gs.general_write(0x3F,0);
    gs.general_write(6,tex);sprite(8,0);
    require(gs.vram().read(0,64,0,8,0)==0x80604020u,"CLD=0 preserves copied palette after source VRAM changes");
    gs.general_write(0x16,tex|(1ull<<61));sprite(16,0);
    require(gs.vram().read(0,64,0,16,0)==0x80402010u && ((*gs.general(6)&0x3FFFu)==256u),
            "TEX2 reloads palette and preserves texture location/dimensions");
    rejects([&]{gs.general_write(6,tex|(4ull<<61));},"unknown CBP");
    gs.general_write(6,tex|(2ull<<61));gs.general_write(6,tex|(4ull<<61));
    gs.general_write(8,1);sprite(24,0,8,112,240); // clamp every U to texel 7
    require(gs.vram().read(0,64,0,31,0)==0x80402010u,"clamp accepts coordinates beyond width");
    gs.general_write(8,0);sprite(32,0,16); // exact half-rate UV increments
    require(gs.vram().read(0,64,0,32,0)==gs.vram().read(0,64,0,33,0),"nearest scaled sprite repeats texel");
    rejects([&]{sprite(0,4,3,0,16);},"fractional UV DDA");
    gs.general_write(0x14,0x21);rejects([&]{sprite(0,4);},"zero-LOD boundary");gs.general_write(0x14,1);
    upload(0x10000,20,128,8,2,{0});rejects([&]{sprite(0,4);},"TEXFLUSH");
    gs.general_write(0x3F,0);gs.general_write(0x4C,8u|(1u<<16));
    rejects([&]{sprite(0,0);},"aliases sampled texture");

    gs.reset(0);configure_draw(gs);palette.clear();
    for(unsigned i=0;i<256;i+=2)palette.push_back((0x20400000ull+i)|((0x20400001ull+i)<<32));
    upload(0x20000,0,64,16,16,palette);
    upload(0x10000,19,128,8,1,{0x0000000000181008ull});
    const std::uint64_t tex8=256u|(2ull<<14)|(19ull<<20)|(3ull<<26)|(1ull<<35)|(512ull<<37);
    gs.general_write(0x3F,0);gs.general_write(6,tex8|(1ull<<61));
    gs.general_write(0x14,1);gs.general_write(8,0);gs.general_write(0x42,0x44);
    sprite(0,0);
    require(gs.vram().read(0,64,0,0,0)==0x80400010 && gs.vram().read(0,64,0,1,0)==0x80400008 &&
            gs.vram().read(0,64,0,2,0)==0x80400018,"T8 CSM1 exchanges index bits 3/4; DECAL RGB keeps fragment alpha");
    gs.general_write(6,tex8|(1ull<<34));
    gs.general_write(0x47,(3u<<16)|1u|(5u<<1)|(0x40u<<4));sprite(0,0);
    require(gs.vram().read(0,64,0,0,0)==0x80400010,
            "alpha comparison uses textured DECAL alpha before blending, not vertex alpha");
    gs.general_write(0x47,3u<<16);gs.general_write(6,tex8);
    sprite(8,0,8,128,256);
    require(gs.vram().read(0,64,0,8,0)==0x80400010,"repeat wraps a full texture width");
    rejects([&]{gs.general_write(6,tex8|(1ull<<55)|(1ull<<61));},"CSM1");
    gs.write(kGsCsr,8,kCsrReset,1,{});configure_draw(gs);gs.general_write(0x3F,0);
    gs.general_write(6,tex8);gs.general_write(0x14,1);gs.general_write(8,0);gs.general_write(0x42,0x44);
    rejects([&]{sprite(0,0);},"unloaded CLUT");

    // Source-alpha blend with negative intermediate and final saturation.
    GsRegisters blend;blend.reset(0);configure_draw(blend);blend.general_write(0,0);
    blend.general_write(1,0x80000080);blend.general_write(5,0);
    blend.general_write(0,0x40);blend.general_write(0x42,0x44);blend.general_write(1,0x40000001);blend.general_write(5,0);
    require(blend.vram().read(0,64,0,0,0)==0x40000040,"signed blend floors a negative half before adding destination");
    blend.general_write(0x42,0x82);blend.general_write(1,0x80808080);blend.general_write(5,0);
    require(blend.vram().read(0,64,0,0,0)==0x80000000,"negative blend saturates to zero");
}
void check_symbolic_blending() {
    for(unsigned psm:{0u,1u}) {
        GsRegisters symbolic;symbolic.reset(0);configure_draw(symbolic);symbolic.general_write(0,0x40);
        symbolic.general_write(0x4C,(1u<<16)|(std::uint64_t{psm}<<24));
        auto& vram=const_cast<GsVram&>(symbolic.vram());
        for(unsigned unknown:{0xFFFFFFu,0x0055AAu,0x00000Fu})for(unsigned source:{0u,90u,255u})
        for(unsigned factor:{8u,128u,255u})for(bool clamp:{false,true}) {
            constexpr unsigned initial=0x807F4012u;
            vram.write(0,64,psm,0,0,initial,0,unknown);
            symbolic.general_write(0x46,clamp);symbolic.general_write(0x42,0x64ull|(std::uint64_t{factor}<<32));
            symbolic.general_write(1,0x08000000u|source|(source<<8)|(source<<16));symbolic.general_write(5,0);
            unsigned expected=0,varying=0;
            for(unsigned channel=0;channel<3;++channel) {
                const auto shift=channel*8u,known=255u^((unknown>>shift)&255u),old=(initial>>shift)&255u;
                bool first=true;unsigned reference=0;
                for(unsigned value=0;value<256u;++value)if((value&known)==(old&known)) {
                    // Independently evaluate floor(((Cs-Cd)*FIX)/128)+Cd.
                    const auto product=(static_cast<int>(source)-static_cast<int>(value))*static_cast<int>(factor);
                    const auto result=product/128-(product<0 && product%128!=0)+static_cast<int>(value);
                    const auto formatted=clamp ? static_cast<unsigned>(std::clamp(result,0,255)) : static_cast<unsigned>(result)&255u;
                    if(first){reference=formatted;first=false;}varying|=(reference^formatted)<<shift;
                }
                expected|=reference<<shift;
            }
            const auto known=vram.known_bits()[0]&0xFFFFFFu;
            require(known==(~varying&0xFFFFFFu) && ((vram.raw_words()[0]^expected)&known)==0,
                "symbolic CT32/24 destination blending matches every admissible byte, including negative sums, clamp and wrap");
            if(varying)rejects([&]{(void)vram.read(0,64,1,0,0);},"unwritten bits");
            symbolic.general_write(0,0);symbolic.general_write(1,0x80123456);symbolic.general_write(5,0);
            require(vram.read(0,64,1,0,0)==0x123456,"an actual overwrite establishes RGB after symbolic blending");
            symbolic.general_write(0,0x40);
        }
    }
}
void check_drawing_scanout() {
    // Truth table around AREF, independently of blending/frame masks.
    constexpr bool comparison[8][3]={{false,false,false},{true,true,true},
        {true,false,false},{true,true,false},{false,true,false},
        {false,true,true},{false,false,true},{true,false,true}};
    for(unsigned mode=0;mode<8;++mode)for(unsigned sample=0;sample<3;++sample) {
        GsRegisters a;a.reset(0);configure_draw(a);a.general_write(0,0);
        a.general_write(0x4E,1u|(10u<<24));a.general_write(5,5ull<<32);
        const auto source=((127u+sample)<<24)|0x112233u;
        a.general_write(1,source);a.general_write(0x47,(3u<<16)|1u|(mode<<1)|(128u<<4));
        a.general_write(5,7ull<<32);
        require(a.vram().read(0,64,0,0,0)==(comparison[mode][sample]?source:0x80402010u) &&
            a.vram().read_depth(8192,64,10,0,0)==(comparison[mode][sample]?7u:5u),
            "all eight alpha comparisons gate both color and depth before blending");
    }
    for(unsigned fail=0;fail<4;++fail) {
        GsRegisters a;a.reset(0);configure_draw(a);a.general_write(0,0);
        a.general_write(0x4E,1u|(10u<<24));a.general_write(5,5ull<<32);
        a.general_write(0x4C,(1u<<16)|(0xFF00ull<<32));a.general_write(1,0x22112233u);
        a.general_write(0x47,(3u<<16)|1u|(fail<<12));a.general_write(5,7ull<<32);
        const unsigned expected[]={0x80402010u,0x22112033u,0x80402010u,0x80112033u};
        require(a.vram().read(0,64,0,0,0)==expected[fail] &&
            a.vram().read_depth(8192,64,10,0,0)==(fail==2u?7u:5u),
            "AFAIL KEEP/FB_ONLY/ZB_ONLY/RGB_ONLY preserve framebuffer mask and alpha");
    }
    { GsRegisters a;a.reset(0);configure_draw(a);a.general_write(0,0);
      a.general_write(0x47,0x50001u);a.general_write(5,0);
      require(a.vram().pixel_writes()==0,"alpha KEEP avoids consuming unwritten destination depth"); }
    check_textured_sprites();
    check_texture24();
    {
        GsRegisters experiment;experiment.reset(0);
        require(!experiment.general(0x22),"normal reset has no assumed SCANMSK");
        rejects([&]{experiment.diagnostic_scanmask_reset(1);},"valid mask");
        experiment.diagnostic_scanmask_reset(2);
        require(experiment.general(0x22)==2,"explicit experiment records the chosen reset mask");
        experiment.general_write(0x22,3);experiment.write(kGsCsr,8,kCsrReset,1,{});
        require(experiment.general(0x22)==2,"soft reset reapplies only the explicit hypothesis");
        rejects([&]{experiment.diagnostic_scanmask_reset(0);},"before guest activity");
        experiment.reset(0);require(!experiment.general(0x22),"new normal profile discards the hypothesis");
        experiment.diagnostic_scanmask_reset(0);configure_draw(experiment,false);
        experiment.general_write(0,0);experiment.general_write(5,0);
        require(experiment.vram().read(0,64,1,0,0)==0x402010u,
                "explicit normal scan mask lets actual drawing commands establish RGB");
    }
    {
        GsRegisters symbolic; symbolic.reset(0); configure_draw(symbolic,false);
        setup_transfer(symbolic,0,2,1); symbolic.image_write(0,0);
        symbolic.general_write(0,0); symbolic.general_write(1,0); symbolic.general_write(5,0);
        require(symbolic.vram().read(0,64,1,0,0)==0,"unknown scan mask preserves equal black RGB");
        symbolic.general_write(1,0xFFFFFF); symbolic.general_write(5,0);
        rejects([&]{(void)symbolic.vram().read(0,64,1,0,0);},"unwritten bits");
        symbolic.general_write(0x22,0); symbolic.general_write(5,0);
        require(symbolic.vram().read(0,64,1,0,0)==0xFFFFFF,"guest scan mask write proves RGB");
        rejects([&]{(void)symbolic.vram().read(0,64,0,0,0);},"unwritten bits");
        symbolic.general_write(0x4A,1); symbolic.general_write(5,0);
        require(symbolic.vram().read(0,64,0,0,0)==0x80FFFFFF,"guest FBA write proves alpha");
    }
    {
        GsRegisters depth; depth.reset(0); configure_draw(depth);
        depth.general_write(0x4E,1u|(10u<<24));
        depth.general_write(5,99ull<<32); depth.general_write(5,32u|(32ull<<16)|(7ull<<32));
        require(depth.vram().read_depth(8192,64,10,0,0)==7,"sprite uses last vertex depth");
        depth.general_write(0,0); depth.general_write(0x47,5u<<16); depth.general_write(1,0x11223344);
        depth.general_write(5,6ull<<32);
        require(depth.vram().read(0,64,0,0,0)==0x80402010,"GEQUAL rejects less depth");
        depth.general_write(5,7ull<<32);
        require(depth.vram().read(0,64,0,0,0)==0x11223344,"GEQUAL accepts equal depth");
        depth.general_write(0x47,7u<<16); depth.general_write(1,0x55667788);
        depth.general_write(5,7ull<<32);
        require(depth.vram().read(0,64,0,0,0)==0x11223344,"GREATER rejects equal depth");
        depth.general_write(0x4E,1u|(10u<<24)|(1ull<<32)); depth.general_write(5,8ull<<32);
        require(depth.vram().read_depth(8192,64,10,0,0)==7 && depth.vram().read(0,64,0,0,0)==0x55667788,
                "ZMSK preserves depth while color passes");
        depth.general_write(0x22,2); depth.general_write(1,0); depth.general_write(5,8ull<<32);
        require(depth.vram().read(0,64,0,0,0)==0x55667788,"even scanline mask");
    }
    GsRegisters gs; gs.reset(0); configure_draw(gs);
    gs.general_write(5,0); gs.general_write(5,64u*16u | (64ull*16u<<16));
    require(gs.vram().read(0,64,0,63,63)==0x80402010u,"sprite excludes lower/right endpoint");
    gs.general_write(0x18,16u|(16ull<<32)); // offset one pixel
    gs.general_write(0x40,2u|(3ull<<16)|(2ull<<32)|(3ull<<48));
    gs.general_write(1,0x80808080); gs.general_write(5,0); gs.general_write(5,80u|(80ull<<16));
    require(gs.vram().read(0,64,0,1,2)==0x80402010u && gs.vram().read(0,64,0,2,2)==0x80808080u &&
            gs.vram().read(0,64,0,4,3)==0x80402010u,"XYOFFSET, scissor inclusive bounds");
    gs.general_write(0,0); gs.general_write(5,32u|(32ull<<16));
    require(gs.vram().read(0,64,0,2,2)==0x80808080u,"point outside scissor clipped");
    Gif gif; gif.reset();
    gif.write_fifo(tag(1,2)|(1ull<<46),0x51,gs); // PRE point, RGBA + XYZ2
    gif.write_fifo(0x4400000033ull,0x8000000055ull,gs);
    gif.write_fifo(48u|(48ull<<32),0x12345678,gs);
    require(gs.vram().read(0,64,0,2,2)==0x80554433u,"PACKED XYZ2 point writes a pixel");
    require(gs.general(5)==(48u|(48ull<<16)|(0x12345678ull<<32)),"PACKED XYZ2 layout");
    gif.write_fifo(tag(1,1),5,gs);
    gif.write_fifo(48u|(48ull<<32),(1ull<<47),gs); // ADC suppresses kick
    require(gs.vram().read(0,64,0,2,2)==0x80554433u,"PACKED ADC suppresses kick");
    gs.general_write(0,6); gs.general_write(5,32u|(32ull<<16)); gs.general_write(0x0D,64u|(64ull<<16));
    require(gs.vram().read(0,64,0,2,2)==0x80554433u,"XYZ3 suppresses kick");
    gs.general_write(0,1);gs.general_write(5,0);
    rejects([&]{gs.general_write(5,16u);},"line rasterization");
    gs.general_write(0,6u|16u);
    const auto before_stq_vertex=gs.vram().revision();
    gs.general_write(5,0);
    require(gs.vram().revision()==before_stq_vertex,
            "first STQ sprite vertex is queued without sampling or drawing");
    GsRegisters missing_texture;missing_texture.reset(0);configure_draw(missing_texture);
    missing_texture.general_write(0,6u|16u);missing_texture.general_write(5,0);
    rejects([&]{missing_texture.general_write(5,16u|(16ull<<16));},"register 6");
    configure_draw(gs); gs.general_write(0x47,1u<<14); gs.general_write(5,0);
    rejects([&]{gs.general_write(5,64u|(64ull<<16));},"alpha/depth");
    gs.write(kGsPmode,8,0,0,{}); rejects([&]{(void)gs.scanout(0);},"disabled");
    gs.write(kGsPmode,8,0xFF25,0,{}); gs.write(kGsSmode2,8,0,0,{});
    gs.write(kGsDispfb1,8,(1u<<9)|(2ull<<32)|(3ull<<43),0,{});
    gs.write(kGsDisplay1,8,(1u<<23)|(7ull<<32)|(3ull<<44),0,{});
    auto frame=gs.scanout(123);
    require(frame.width==4 && frame.height==4 && frame.stride==16 && frame.ee_cycle==123 && !frame.timing,
            "scanout dimensions, magnification, unknown timing explicit");
    require(frame.rgba[0]==0x80 && frame.rgba[1]==0x80 && frame.rgba[3]==255,"scanout source offset, host alpha");
    gs.write(kGsSmode2,8,1,0,{}); rejects([&]{(void)gs.scanout(0);},"interlaced");
    gs.write(kGsSmode2,8,3,0,{});
    const auto field=gs.scanout(124);
    require(field.width==4 && field.height==2 && field.rgba.size()==32 &&
            std::equal(field.rgba.begin(),field.rgba.end(),frame.rgba.begin()),
            "INT+FFMD extracts consecutive native field rows without invented pixels");
    gs.write(kGsSmode2,8,0,0,{}); gs.write(kGsPmode,8,0xFF27,0,{});
    rejects([&]{(void)gs.scanout(0);},"dual-circuit");
    gs.write(kGsImr,8,0x6000,0,{}); require(gs.latched(kGsImr)==0x6000,"IMR write");
    gs.write(kGsCsr,8,kCsrReset,100,{});
    require(gs.latched(kGsImr)==0x7F00 && !gs.general(0) && !gs.latched(kGsPmode),"soft reset state and IMR masks");
    require(gs.vram().read(0,64,0,0,0)==0x80402010u,"CSR reset preserves VRAM");
    const auto after_reset=gs.vram().revision(); gs.general_write(0x54,0);
    require(gs.vram().revision()==after_reset,"soft reset cancels upload");
    rejects([&]{(void)gs.scanout(0);},"PMODE not written");
    gs.reset(0); rejects([&]{(void)gs.vram().read(0,64,0,0,0);},"unwritten bits");
}
void check_guest(const std::filesystem::path& rom_path, const std::filesystem::path& output) {
    const auto bios=load_bios_rom0(read_file(rom_path)); const auto fixture=m29::build();
    std::vector<Framebuffer> frames[2]; std::vector<std::string> traces[2];
    std::uint64_t retired{};
    for(unsigned run=0;run<2;++run) {
        const auto* module=run?&aot_module():nullptr;
        EEState state; Memory memory; prepare_ee_boot(bios,module,state,memory);
        BootOptions options; options.max_instructions=100000; options.stop_at=fixture.stop;
        options.trace=TraceLevel::Full;
        options.on_frame=[&](const Framebuffer& f){frames[run].push_back(f);};
        const auto report=run_ee_boot(state,memory,module,options);
        require(report.outcome==BootOutcome::StopAddress,"synthetic GS program reached stop: "+report.reason);
        if(run) require(report.aot_instructions>0 && report.interpreted_instructions>0,"hybrid uses both engines");
        else retired=report.retired;
        require(report.retired==retired,"guest retired equivalence");
        traces[run]=normalized(report.trace);
        const auto f=memory.gs().scanout(state.retired_instructions);
        require(f.width==m29::kWidth && f.height==m29::kHeight,"guest scanout size");
        require(f.rgba[0]==0x12 && f.rgba[1]==0x19 && f.rgba[2]==0x24,"guest background");
        const auto letter=(34u*f.width+40u)*4u;
        require(f.rgba[letter]==0xE8 && f.rgba[letter+1]==0xF0 && f.rgba[letter+2]==0xF5,"guest letter sprite");
        const auto digest=sha256_hex(f.rgba);
        std::cout<<"Synthetic GS RGBA8 sha256 "<<digest<<", retired "<<report.retired<<"\n";
        require(digest=="43564b222dee0155325d1db289518d63f6a02af60690dd6da83ad69d002e3f54", "synthetic RGBA8 golden digest");
        save_framebuffer_bmp(output,f);
        const auto bmp=read_file(output);
        require(bmp.size()==54u+f.rgba.size() && bmp[0]=='B' && bmp[1]=='M' && bmp[54]==f.rgba[(f.height-1)*f.stride+2],
                "BMP header and bottom-up BGR order");
        require(report.frames_presented==frames[run].size() && report.frames_presented>0 && !report.frame_unavailable,
                "VBLANK frame events");
    }
    require(traces[0]==traces[1],"guest bus/instruction trace equivalence");
    require(frames[0].size()==frames[1].size(),"frame count equivalence");
    for(std::size_t i=0;i<frames[0].size();++i)
        require(frames[0][i].rgba==frames[1][i].rgba && frames[0][i].ee_cycle==frames[1][i].ee_cycle,
                "frame event cycle/pixel equivalence");
    rejects([&]{save_framebuffer_bmp(output,{});},"invalid RGBA8");
}
}
int main(int argc,char** argv) {
    try {
        require(argc>=2,"GS test case argument");
        const std::string mode=argv[1];
        if(mode=="vram") check_vram();
        else if(mode=="gif") check_transfer_gif();
        else if(mode=="drawing") check_drawing_scanout();
        else if(mode=="symbolic-blend") check_symbolic_blending();
        else if(mode=="guest") {
            require(argc==4,"ROM and BMP output arguments"); check_guest(argv[2],argv[3]);
        } else throw std::runtime_error("unknown GS test case");
        std::cout<<"M2.9 GS checks passed\n"; return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
