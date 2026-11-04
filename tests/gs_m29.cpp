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
    rejects([&] { (void)GsVram::address(0,0,0,0,0); }, "buffer width");
    rejects([&] { (void)GsVram::address(0,64,2,0,0); }, "pixel format 2");
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
}
void setup_transfer(GsRegisters& gs, unsigned psm, unsigned w, unsigned h, unsigned x=0, unsigned y=0) {
    gs.general_write(0x50, (1ull<<48) | (std::uint64_t{psm}<<56));
    gs.general_write(0x51, (std::uint64_t{x}<<32) | (std::uint64_t{y}<<48));
    gs.general_write(0x52, w | (std::uint64_t{h}<<32)); gs.general_write(0x53,0);
}
void check_transfer_gif() {
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
    rejects([&] { setup_transfer(gs,0x13,1,1); }, "pixel format 19");
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
void check_drawing_scanout() {
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
    gs.general_write(0,3); rejects([&]{gs.general_write(5,0);},"point/sprite only");
    gs.general_write(0,6u|16u); rejects([&]{gs.general_write(5,0);},"texture");
    configure_draw(gs); gs.general_write(0x47,1); gs.general_write(5,0);
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
        else if(mode=="guest") {
            require(argc==4,"ROM and BMP output arguments"); check_guest(argv[2],argv[3]);
        } else throw std::runtime_error("unknown GS test case");
        std::cout<<"M2.9 GS checks passed\n"; return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
