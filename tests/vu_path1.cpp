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
