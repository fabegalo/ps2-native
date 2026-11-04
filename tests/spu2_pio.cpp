// Original guest programs exercise real FIFO payloads, clock edges and RAM.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
std::vector<std::uint8_t> image(m20::Assembler& a) {
    const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
    std::copy(code.begin(),code.end(),rom.begin());return rom;
}
void put(m20::Assembler& a,std::uint32_t address,std::uint16_t value) {
    a.la(8,address);a.la(9,value);a.sh(9,0,8);
}
void idle(m20::Assembler& a) {a.label("idle");a.beq(0,0,"idle");a.nop();}
std::uint16_t payload(unsigned n) {return static_cast<std::uint16_t>(0x1200u+73u*n);}
}
int main() try {
    DiagnosticAssumptions assumptions;require(assumptions.empty(),"PIO is not a default assumption");
    assumptions.spu2_pio_word_cycles=3;require(!assumptions.empty(),"PIO throughput explicitly marks a diagnostic execution");
    for(const unsigned latency : {3u,17u}) {
        m20::Assembler a(kEeResetVector);
        put(a,0xBF90019Au,0x8000u);put(a,0xBF90059Au,0x8000u);
        put(a,0xBF9001A8u,0xFu);put(a,0xBF9001AAu,0xFFF8u);
        a.la(8,0xBF9001ACu);
        for(unsigned n=0;n<32;++n){a.la(9,payload(n));a.sh(9,0,8);}
        a.la(10,0xA0000100u);a.la(8,0xBF90019Au);a.la(9,0x8010u);
        a.label("start");a.sh(9,0,8);
        a.la(8,0xBF900344u);a.lhu(9,0,8);a.nop();a.andi(9,9,0x400u);a.sw(9,0,10);
        a.label("wait");a.lhu(9,0,8);a.nop();a.andi(9,9,0x400u);a.bne(9,0,"wait");a.nop();
        a.la(9,1);a.sw(9,4,10);idle(a);
        const auto start=(a.at("start")-kEeResetVector)/4u;const auto rom=image(a);
        Iop iop;iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);
        rejects([&]{iop.diagnostic_spu2_pio_word_cycles(0);},"positive");
        iop.diagnostic_spu2_pio_word_cycles(latency);iop.run_until(start);
        require(!iop.stopped() && iop.spu2_pio_words(0)==0 && !iop.spu2_ram_halfword(0xFFFF8u),
                "queued FIFO data does not reach RAM before PIO starts");
        iop.run_until(start+latency-1u);
        require(!iop.stopped() && iop.spu2_pio_words(0)==0,"no transfer precedes its explicit modeled bus edge");
        for(unsigned n=0;n<32;++n) {
            iop.run_until(start+(n+1u)*latency);
            require(!iop.stopped() && iop.spu2_pio_words(0)==n+1u &&
                iop.spu2_ram_halfword((0xFFFF8u+n)&0xFFFFFu)==payload(n),"each bus edge commits exactly the next real halfword, including RAM wrap");
        }
        iop.run_until(start+32u*latency+80u);
        require(!iop.stopped() && iop.ee_read(0x100,4)==0x400u && iop.ee_read(0x104,4)==1u,
                "guest observes busy while data is pending and exits its poll only after RAM receives every halfword");
        require(!iop.spu2_ram_halfword(24u) && !iop.spu2_ram_halfword(0x5000u),"untouched sound RAM never becomes fabricated zero data");
        rejects([&]{iop.spu2_ram_halfword(0x100000u);},"outside");
        rejects([&]{iop.diagnostic_spu2_pio_word_cycles(5);},"before guest execution");
        iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.run_until(start);
        require(iop.stopped() && !iop.spu2_ram_halfword(0xFFFF8u),"reset removes both the PIO timing experiment and all previously established RAM bytes");
    }
    for(unsigned mode=0;mode<10;++mode) {
        m20::Assembler a(kEeResetVector);
        put(a,0xBF90019Au,0x8000u);if(mode!=1)put(a,0xBF90059Au,0x8000u);
        put(a,0xBF9001A8u,0u);if(mode!=2)put(a,0xBF9001AAu,0x2800u);
        if(mode==3)put(a,0xBF9001AAu,1u);
        a.la(8,0xBF9001ACu);a.la(9,0xABCDu);
        if(mode==4)a.sw(9,0,8);
        else for(unsigned n=0;n<(mode==5 ? 33u : 1u);++n)a.sh(9,0,8);
        put(a,0xBF90019Au,mode==6 ? 0x8020u : 0x8010u);
        if(mode==7)put(a,0xBF9001AAu,0x3000u);
        else if(mode==8)put(a,0xBF90019Au,0u);
        else if(mode==9)put(a,0xBF90059Au,0x40u);
        idle(a);const auto rom=image(a);Iop iop;iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);
        if(mode)iop.diagnostic_spu2_pio_word_cycles(1024);
        iop.run_until(800);
        require(iop.stopped() && iop.spu2_pio_words(0)==0 && !iop.spu2_ram_halfword(0x2800u),
                "unselected timing, unknown IRQ/address, unaligned start, widths, overflow, DMA and busy reconfiguration cannot fabricate a completed upload");
    }
    {
        m20::Assembler a(kEeResetVector);
        put(a,0xBF90019Au,0x8000u);put(a,0xBF90059Au,0x8000u);
        for(unsigned core=0;core<2;++core) {
            put(a,0xBF9001A8u+core*0x400u,0u);put(a,0xBF9001AAu+core*0x400u,0x2800u);
            put(a,0xBF9001ACu+core*0x400u,static_cast<std::uint16_t>(0x1234u+core));
            put(a,0xBF90019Au+core*0x400u,0x8010u);
            a.la(8,0xBF900344u+core*0x400u);const auto wait="wait"+std::to_string(core);
            a.label(wait);a.lhu(9,0,8);a.nop();a.andi(9,9,0x400u);a.bne(9,0,wait);a.nop();
        }
        idle(a);const auto rom=image(a);Iop iop;iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);
        iop.diagnostic_spu2_pio_word_cycles(9);iop.run_until(1000);
        require(!iop.stopped() && iop.spu2_pio_words(0)==1 && iop.spu2_pio_words(1)==1 &&
            iop.spu2_ram_halfword(0x2800u)==0x1235u,"both cores transfer actual data into the same physical sound RAM");
    }
    std::cout<<"SPU2 FIFO payloads, exact modeled bus edges, busy polling, shared RAM, wrap and refusals passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
