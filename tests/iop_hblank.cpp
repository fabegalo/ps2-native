// Original R3000A fixtures: GS line edges clock both IOP HBLANK counters.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
constexpr std::uint64_t ntsc=2u*kBusCyclesPerNtscLine,pal=2u*kBusCyclesPerPalLine;
std::uint64_t iop_edge(std::uint64_t ee){return (ee+7u)/8u;}
struct Program {
    m20::Assembler a{kEeResetVector};unsigned timer{},pads{};
    explicit Program(unsigned n,std::uint32_t mode=0x100u,std::uint32_t target=3u,std::uint32_t seed=0u):timer(n) {
        a.la(8,0xBF801000);a.sw(0,0x78,8);a.sw(0,0x70,8);
        a.la(8,n==1u ? 0xBF801110 : 0xBF801480);a.la(9,target);a.sw(9,8,8);
        a.la(9,mode);a.sw(9,4,8);a.la(9,seed);a.sw(9,0,8);
    }
    void pair(std::uint64_t before,unsigned offset=0) {
        a.la(8,timer==1u ? 0xBF801110 : 0xBF801480);
        const auto label="pad"+std::to_string(pads++);a.label(label);
        const auto count=(a.at(label)-kEeResetVector)/4u;require(count<=before,"fixture samples after setup");
        for(auto c=count;c<before;++c)a.nop();
        a.label("sample"+std::to_string(pads));
        if(timer==1u){a.lhu(16,0,8);a.lhu(17,0,8);}else{a.lw(16,0,8);a.lw(17,0,8);}
        a.nop();a.la(10,0xA0000100u+offset);a.sw(16,0,10);a.sw(17,4,10);
        a.la(8,0xBF801070);a.lw(9,0,8);a.nop();a.andi(9,9,1u<<(timer==1u ? 5u : 14u));a.sw(9,8,10);
    }
    std::vector<std::uint8_t> rom() {
        a.label("idle");a.beq(0,0,"idle");a.nop();const auto code=m20::le_words(a.finish());
        std::vector<std::uint8_t> out(4u*1024u*1024u);std::copy(code.begin(),code.end(),out.begin());return out;
    }
};
void values(const Iop& iop,unsigned at,std::uint32_t pre,std::uint32_t post) {
    require(!iop.stopped(),"HBLANK fixture runs: "+iop.stop_reason().value_or(""));
    require(iop.ee_read(0x100u+at,4)==pre && iop.ee_read(0x104u+at,4)==post,
        "counter changes only at the first IOP boundary at/after the actual GS edge");
}
}
int main() try {
    for(unsigned timer:{1u,3u})for(unsigned cmod:{2u,3u})for(std::uint64_t epoch:{0u,3u}) {
        const auto line=cmod==2u ? ntsc : pal,edge=iop_edge(epoch+line);
        Program p(timer);p.pair(edge-1u);const auto rom=p.rom();Iop iop;GsRegisters gs;gs.reset(epoch);
        gs.write(kGsSmode1,8,std::uint64_t{cmod}<<kSmode1CmodShift,epoch,{});
        iop.reset(rom,nullptr);iop.run_until(edge+50u,&gs);values(iop,0,0,1);
        require(gs.vertical_clock().state==GsRegisters::VerticalClock::State::Unknown,
            "unwritten SYNCV does not fabricate VBLANK or prevent the known horizontal clock");
    }
    for(unsigned timer:{1u,3u}) {
        const auto edge=iop_edge(2u*ntsc);Program p(timer,0x160u,3u,timer==1u ? 0xFFFEu : 0xFFFFFFFEu);
        p.pair(edge-1u);const auto rom=p.rom();Iop iop;GsRegisters gs;gs.reset(0);
        iop.reset(rom,nullptr);iop.run_until(edge+50u,&gs);values(iop,0,timer==1u ? 0xFFFFu : 0xFFFFFFFFu,0);
        require(iop.ee_read(0x108,4)==(1u<<(timer==1u ? 5u : 14u)),"16/32-bit overflow raises the correct timer INTC source");
    }
    for(unsigned timer:{1u,3u}) {
        Program p(timer,0x158u,1u);p.pair(iop_edge(ntsc)-1u);
        p.a.sw(0,0,8); // Acknowledge the first compare in the separate INTC latch.
        p.pair(iop_edge(2u*ntsc)-1u,16);p.pair(iop_edge(3u*ntsc)-1u,32);
        const auto rom=p.rom();Iop iop;GsRegisters gs;gs.reset(0);iop.reset(rom,nullptr);
        iop.run_until(iop_edge(3u*ntsc)+50u,&gs);values(iop,0,0,1);values(iop,16,1,0);values(iop,32,0,1);
        const auto irq=1u<<(timer==1u ? 5u : 14u);
        require(iop.ee_read(0x108,4)==irq && !iop.ee_read(0x118,4) && iop.ee_read(0x128,4)==irq,
            "the ROM's repeating zero-return mode raises another IRQ only when the next target is reached");
    }
    for(unsigned timer:{1u,3u}) {
        constexpr auto change=7000u;const auto edge=iop_edge(change*8u+ntsc);
        Program p(timer);p.pair(iop_edge(ntsc)-1u);p.pair(edge-1u,16);const auto rom=p.rom();Iop iop;GsRegisters gs;gs.reset(0);
        gs.write(kGsSmode1,8,kSmode1Sint|(2ull<<kSmode1CmodShift),0,{});
        iop.reset(rom,nullptr);iop.run_until(change,&gs);values(iop,0,0,0);
        gs.write(kGsSmode1,8,2ull<<kSmode1CmodShift,change*8u,{});
        iop.run_until(edge+50u,&gs);values(iop,16,0,1);
    }
    for(unsigned timer:{1u,3u})for(bool reset:{false,true}) {
        constexpr auto change=3000u;const auto edge=iop_edge(change*8u+(reset ? ntsc : pal));
        Program p(timer);p.pair(iop_edge(ntsc)-1u);p.pair(edge-1u,16);const auto rom=p.rom();Iop iop;GsRegisters gs;gs.reset(0);
        iop.reset(rom,nullptr);iop.run_until(change,&gs);values(iop,0,0,1);
        if(reset)gs.write(kGsCsr,8,kCsrReset,change*8u,{});
        else gs.write(kGsSmode1,8,3ull<<kSmode1CmodShift,change*8u,{});
        iop.run_until(edge+50u,&gs);values(iop,16,1,2);
    }
    for(unsigned timer:{1u,3u}) {
        const auto edge=iop_edge(ntsc);Program p(timer,0x110u,1u);
        p.a.la(8,0xBF801000);p.a.la(9,1u<<(timer==1u ? 5u : 14u));p.a.sw(9,0x74,8);
        p.a.la(9,1);p.a.sw(9,0x78,8);p.a.la(9,0x401);p.a.mtc0(9,12);p.pair(edge-1u);
        const auto resume=p.a.at("sample1")+4u;const auto rom=p.rom();
        m20::Assembler h(0x80000080);h.la(26,0xA0000000);h.mfc0(27,14);h.nop();h.sw(27,0x180,26);
        h.la(26,0xBF801000);h.sw(0,0x74,26);h.sw(0,0x70,26);h.jr(27);h.raw(0x42000010);
        const auto code=m20::le_words(h.finish());Iop iop;GsRegisters gs;gs.reset(0);iop.reset(rom,nullptr);
        for(unsigned n=0;n<code.size();++n)iop.ee_write(0x80u+n,1,code[n]);
        iop.run_until(edge+100u,&gs);values(iop,0,0,1);
        require(iop.ee_read(0x180,4)==resume,"HBLANK compare IRQ enters the guest handler and resumes at the interrupted PC");
    }
    {
        Program p(1);const auto rom=p.rom();Iop iop;iop.reset(rom,nullptr);iop.run_until(100);
        require(iop.stopped() && iop.stop_reason()->find("HBLANK source unknown")!=std::string::npos,"no attached GS cannot fabricate timer pulses");
        GsRegisters gs;gs.reset(0);iop.reset(rom,nullptr);iop.run_until(100,&gs);
        gs.write(kGsSmode1,8,kSmode1Prst|(2ull<<kSmode1CmodShift),800,{});
        iop.run_until(200,&gs);require(!iop.stopped(),"PRST alone does not stop the horizontal clock used by the ROM kernel");
        gs.write(kGsSmode1,8,0,1600,{});iop.run_until(201,&gs);
        require(iop.stopped() && iop.stop_reason()->find("HBLANK source unknown")!=std::string::npos,"an unsupported clock change cannot keep producing old pulses");
    }
    std::cout<<"IOP HBLANK clocks, fractional boundaries, pause/reset/retime, overflow and guest IRQ delivery passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
