// Original synthetic microprograms and GIF packets, never Sony content.
#include "boot_checks.hpp"
#include "ps2native/vu_control.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void pair(VuControl& vu,unsigned at,std::uint32_t low,std::uint32_t high=0x2FF) {
    vu.write_memory(0x11008000u+at*8u,8,{low|(std::uint64_t{high}<<32u),0});
}
void qword(VuControl& vu,unsigned address,std::uint64_t low,std::uint64_t high) {
    vu.write_memory(0x1100C000u+address*16u,8,{low,0});vu.write_memory(0x1100C008u+address*16u,8,{high,0});
}
void setup(VuControl& vu) {
    pair(vu,0,0x100103FF);pair(vu,1,0x80000EFC);pair(vu,2,0x8000033C,0x400002FF);pair(vu,3,0x8000033C);
    qword(vu,1023,(1ull<<60)|1,0xE);qword(vu,0,0xFFFFFFFF11223344ull,0x62);
    qword(vu,1,(1ull<<60)|0x8001u,0xE);qword(vu,2,0xFFFFFFFFAABBCCDDull,0x62);
}
}
int main() try {
    for(unsigned bit=0;bit<8u;++bit) {
        VuControl vu;Gif gif;GsRegisters gs;gs.reset(0);
        pair(vu,0,0x800006FC);pair(vu,1,0x8000033C,0x400002FF);pair(vu,2,0x8000033C);
        qword(vu,0,(2ull<<60)|0x8001u,0x12); // STQ then RGBA
        vu.write_memory(0x1100C010u,8,{0x400000003F800000ull,(1ull<<bit)|(1ull<<(32u+bit))});
        vu.write_memory(0x1100C018u,8,{0x3D088800u,0xFFFFFFFF00000000ull|(1ull<<bit)});
        qword(vu,2,0x0000002200000011ull,0x0000004400000033ull);
        vu.start_micro(1,0,0);vu.advance_micro(10,&gif,&gs);
        require(!vu.path1_transfer_pending() && gif.packets().size()==1 &&
            gs.general(2)==0x400000003F800000ull && gs.general(1)==0x3D08880044332211ull,
            "real PATH1 preserves all retained STQ bits while the documented low mantissa bits and unused word disappear at GS input");
    }
    for(const unsigned bit:{8u,23u,31u}) {
        Gif gif;GsRegisters gs;gif.reset();gs.reset(0);gif.request_path1();
        gif.write_path1((2ull<<60)|0x8001u,0x12,gs);
        rejects([&]{gif.write_path1_observed({0x400000003F800000ull,0},{0x3D088800u,1ull<<bit},gs);},"retained GIF/GS field");
        require(!gs.general(2) && gif.path1_requested(),"retained Q mantissa/exponent/sign uncertainty refuses atomically before writing ST or finishing a packet");
    }
    {
        GsRegisters gs;gs.reset(0);gs.general_write(1,0x3F8000FF44332211ull);gs.general_write(2,0xBF8000FF3F8000FFull);
        require(gs.general(1)==0x3F80000044332211ull && gs.general(2)==0xBF8000003F800000ull,
            "raw GS register writes apply the same documented STQ input precision as GIF PACKED");
    }
    {
        VuControl vu;Gif gif;GsRegisters gs;gs.reset(0);setup(vu);vu.start_micro(1,0,0);
        vu.advance_micro(2,&gif,&gs);
        require(vu.path1_transfer_pending() && (gif.read_stat()&0x100u) && (vu.read(29)&0x1000u) && gif.packets().empty(),"XGKICK latches a real queued PATH1 request and VPU status without inline GIF execution");
        rejects([&]{gif.write_fifo(0x8000u,0,gs);},"pending PATH1");
        vu.advance_micro(4,&gif,&gs);
        require(((gif.read_stat()>>10u)&3u)==1u && !(gif.read_stat()&0x100u),"the first real GIFtag establishes PATH1 and clears its wait indication");
        vu.advance_micro(6,&gif,&gs);require(gs.read(kGsSiglblid+4u,4)==0x11223344,"the next bus beat reads the actual wrapped VU1 payload and writes GS LABEL");
        vu.advance_micro(8,&gif,&gs);
        require(!vu.micro_running(1) && vu.path1_transfer_pending() && vu.read(29)==0x1000u && gif.packets().empty(),"E completion does not fabricate the end of a still-transferring chained GIF packet");
        Vif vif;vif.write_fifo(0x11000000,0,gif,gs,&vu,8);
        require((vif.observe_status().value&15u)==10u,"FLUSH reports a GIF wait while the VU itself is Ready");
        advance_vpus(10,&vif,nullptr,&vu,gif,gs);
        require(!vif.processing() && !vu.path1_transfer_pending() && gif.read_stat()==0 &&
            gs.read(kGsSiglblid+4u,4)==0xAABBCCDD && gif.packets().size()==1 && gif.packets()[0].starts_with("PATH1"),
            "real EOP completes the chained transfer, releases FLUSH and records the actual PATH1 packet");
    }
    {
        VuControl vu;Gif gif;GsRegisters gs;gs.reset(0);setup(vu);vu.start_micro(1,0,0);vu.advance_micro(2,&gif,&gs);
        gif.write_ctrl(8);vu.advance_micro(8,&gif,&gs);
        require(!vu.micro_running(1) && vu.path1_transfer_pending() && gif.packets().empty(),"GIF stop retains the request and source address while the independent VU finishes");
        gif.write_ctrl(0);vu.advance_micro(10,&gif,&gs);
        vu.write_memory(0x1100C000u,8,{0xFFFFFFFF55667788ull,0});
        vu.advance_micro(12,&gif,&gs);require(gs.read(kGsSiglblid+4u,4)==0x55667788,
            "the transfer reads each live source qword at delivery instead of snapshotting future bytes at XGKICK");
        vu.advance_micro(16,&gif,&gs);require(!vu.path1_transfer_pending() && gif.packets().size()==1,"GIF restart completes all actual remaining qwords");
    }
    {
        VuControl vu;Gif gif;GsRegisters gs;gs.reset(0);
        vu.write_vector(31,{{8,0},{0,0}});vu.write(2,0x421);vu.vector_store_increment(31,2,8);
        pair(vu,0,0x800006FC);pair(vu,1,0x80000EFC,(15u<<21u)|(3u<<6u)|0x1Bu);
        pair(vu,2,0x10010064,(15u<<21u)|(4u<<6u)|0x1Bu);pair(vu,3,0x8000033C,0x400002FF);pair(vu,4,0x8000033C);
        qword(vu,0,(1ull<<60)|0x8003u,0xE);
        for(unsigned n=1;n<=3;++n)qword(vu,n,0xFFFFFFFF00000000ull|n,0x62);
        qword(vu,8,(1ull<<60)|0x8001u,0xE);qword(vu,9,0xFFFFFFFFAABBCCDDull,0x62);
        vu.start_micro(1,0,0);vu.advance_micro(8,&gif,&gs);
        require(vu.diagnostic_micro_instructions(1)==2 && vu.observe_data(0x403C).value==0x3F800000 &&
            !vu.observe_data(0x403C).unknown && vu.observe_data(0x404C).unknown==~0u && vu.observe_data(0x4210).value==8,
            "a second XGKICK executes its paired upper exactly once while the next LIW stalls and earlier writebacks keep progressing");
        vu.advance_micro(9,&gif,&gs);require(vu.diagnostic_micro_instructions(1)==2 && gif.packets().size()==1 &&
            vu.path1_transfer_pending() && (gif.read_stat()&0x100u),"the first actual EOP releases the queued request without inline delivery of the second packet");
        vu.advance_micro(12,&gif,&gs);require(vu.diagnostic_micro_instructions(1)==5 && gif.packets().size()==1,
            "the following LIW resumes after the first EOP while the second packet still streams independently");
        vu.advance_micro(16,&gif,&gs);require(!vu.micro_running(1) && !vu.path1_transfer_pending() && gif.packets().size()==2 &&
            gs.read(kGsSiglblid+4u,4)==0xAABBCCDD && vu.observe_data(0x4210).value==100 && vu.observe_data(0x404C).value==0x3F800000,
            "the queued transfer uses its latched VI source despite a later VI change and preserves both real packets and upper results");
    }
    {
        VuControl vu;Gif gif;GsRegisters gs;gs.reset(0);
        vu.write_vector(31,{{8,0},{0,0}});vu.write(2,0x421);vu.vector_store_increment(31,2,8);
        pair(vu,0,0x800006FC);pair(vu,1,0x80000EFC,(15u<<21u)|(3u<<6u)|0x1Bu);
        pair(vu,2,0x8000033C,0x400002FF);pair(vu,3,0x8000033C);
        qword(vu,0,(1ull<<60)|0x8001u,0xE);qword(vu,1,0xFFFFFFFF11223344ull,0x62);
        qword(vu,8,(1ull<<60)|0x8005u,0xE);for(unsigned n=9;n<=13;++n)qword(vu,n,0xFFFFFFFFAABBCC00ull|n,0x62);
        gif.write_ctrl(8);vu.start_micro(1,0,0);vu.advance_micro(8,&gif,&gs);
        require(vu.diagnostic_micro_instructions(1)==2 && !vu.observe_data(0x403C).unknown && gif.packets().empty(),
            "GIF pause keeps both real requests pending while the second upper result still retires");
        gif.write_ctrl(0);vu.advance_micro(17,&gif,&gs);
        require(!vu.micro_running(1) && vu.path1_transfer_pending() && gif.packets().size()==1,
            "VU E can finish after queue release while the resumed second packet is still transferring");
        Vif vif;vif.write_fifo(0x11000000,0,gif,gs,&vu,17);require((vif.observe_status().value&15u)==10u,"FLUSH waits for the actual queued packet tail after VU Ready");
        advance_vpus(23,&vif,nullptr,&vu,gif,gs);
        require(!vif.processing() && !vu.path1_transfer_pending() && gif.packets().size()==2 && gs.read(kGsSiglblid+4u,4)==0xAABBCC0D,
            "only the second real EOP releases FLUSH after a paused queued launch");
    }
    {
        VuControl vu;Gif gif;GsRegisters gs;gs.reset(0);pair(vu,0,0x800006FC);pair(vu,1,0x8000033C);
        pair(vu,2,0x8000033C,0x400002FF);pair(vu,3,0x8000033C);
        vu.start_micro(1,0,0);vu.advance_micro(1,&gif,&gs);
        rejects([&]{vu.advance_micro(3,&gif,&gs);},"unknown VU1 packet bits");
        require(gif.packets().empty() && !gs.vram().pixel_writes(),"unwritten packet bytes cannot become fabricated graphics");
    }
    {
        VuControl vu;Gif gif;GsRegisters gs;gs.reset(0);setup(vu);vu.start_micro(1,0,0);vu.advance_micro(2,&gif,&gs);
        gif.write_ctrl(1);rejects([&]{vu.advance_micro(4,&gif,&gs);},"GIF reset interrupted");
        require(gif.packets().empty(),"GIF reset cannot silently resume a stale source packet");
    }
    std::cout<<"Real clocked PATH1 qwords, chained EOP/wrap, independent E, live bytes, GIF stop/reset and FLUSH passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
