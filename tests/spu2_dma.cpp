// Original R3000A fixtures: real normal-DMA payloads and observable completion.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void sw(m20::Assembler& a,std::uint32_t address,std::uint32_t value){a.la(8,address);a.la(9,value);a.sw(9,0,8);}
void sh(m20::Assembler& a,std::uint32_t address,std::uint32_t value){a.la(8,address);a.la(9,value);a.sh(9,0,8);}
std::vector<std::uint8_t> image(m20::Assembler& a){const auto code=m20::le_words(a.finish());
    std::vector<std::uint8_t> rom(4u*1024u*1024u);std::copy(code.begin(),code.end(),rom.begin());return rom;}
void idle(m20::Assembler& a){a.label("idle");a.beq(0,0,"idle");a.nop();}
void setup(m20::Assembler& a,unsigned core,bool irq,unsigned mode=0u) {
    sw(a,0xBF801070u,0);sw(a,0xBF801078u,0);sw(a,0xBF80157Cu,1);
    sw(a,0xBF8010F4u,0x800000u|(irq && !core ? 0x100000u : 0u));
    sw(a,0xBF801574u,irq && core ? 0x10000u : 0u);sw(a,0xBF801578u,mode==1u ? 0u : 1u);
    sw(a,core ? 0xBF801570u : 0xBF8010F0u,mode==2u ? 0u : core ? 8u : 0x80000u);
    sh(a,0xBF90019Au,0x8000u);sh(a,0xBF90059Au,0x8000u);
    const auto spu=0xBF900000u+core*0x400u,dma=core ? 0xBF801500u : 0xBF8010C0u;
    if(mode!=3u)sh(a,spu+0x1B0u,0);
    sh(a,spu+0x1A8u,mode==4u ? 0u : 0xFu);sh(a,spu+0x1AAu,mode==4u ? 0x2000u : 0x849Fu);
    sh(a,spu+0x19Au,0x8020u);sw(a,dma,0x20000u);sw(a,dma+4u,0x00020004u);
    if(mode==5u)sh(a,0xBF90019Au+(core^1u)*0x400u,0x40u);
}
void payload(Iop& iop,bool unknown=false){for(unsigned n=0;n<32u;++n)if(!unknown || n!=3u)iop.ee_write(0x20000u+n,1,0x31u+n);}
}
int main() try {
    for(unsigned core=0;core<2u;++core)for(const auto latency:{3u,17u})
    for(const bool irq:{false,true})for(const bool unknown:{false,true}) {
        m20::Assembler a(kEeResetVector);setup(a,core,irq);const auto dma=core ? 0xBF801500u : 0xBF8010C0u;
        a.la(8,dma+8u);a.la(9,0x01000201u);a.label("start");a.sw(9,0,8);
        if(irq) {
            a.la(8,0xBF900344u+core*0x400u);a.label("wait");a.lhu(9,0,8);a.nop();
            a.andi(9,9,0x80u);a.beq(9,0,"wait");a.nop();
        } else {
            a.label("wait");a.lw(9,0,8);a.nop();a.r(0,9,9,24,2);a.andi(9,9,1);a.bne(9,0,"wait");a.nop();
        }
        a.la(10,0xA0000100u);a.la(9,1);a.sw(9,0,10);a.la(8,dma);a.lw(9,0,8);a.nop();a.sw(9,4,10);
        a.lhu(9,6,8);a.nop();a.sw(9,8,10);a.la(8,0xBF801070u);a.lw(9,0,8);a.nop();a.andi(9,9,8);a.sw(9,12,10);
        a.la(8,0xBF900344u+core*0x400u);a.lhu(9,0,8);a.nop();a.andi(9,9,0x480u);a.sw(9,16,10);
        sh(a,0xBF90019Au+core*0x400u,0x8000u);a.la(8,0xBF900344u+core*0x400u);a.lhu(9,0,8);a.nop();
        a.andi(9,9,0x480u);a.sw(9,20,10);idle(a);
        const auto start=(a.at("start")-kEeResetVector)/4u;const auto rom=image(a);Iop iop;
        iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.diagnostic_spu2_adma_word_cycles(latency);payload(iop,unknown);
        iop.run_until(start+latency-1u);require(!iop.stopped() && iop.spu2_dma_words(core)==0u,"no normal DMA word precedes its bus edge");
        for(unsigned n=0;n<8u;++n) {
            iop.run_until(start+(n+1u)*latency);
            require(!iop.stopped() && iop.spu2_dma_words(core)==n+1u,"one real 32-bit payload per modeled shared-bus slot");
            for(unsigned half=0;half<2u;++half) {
                const auto index=n*2u+half,address=0xF849Fu+index;
                if(unknown && index==1u)require(!iop.spu2_ram_halfword(address),"opaque source bytes never become known sound samples");
                else require(iop.spu2_ram_halfword(address)==((0x31u+index*2u)|((0x32u+index*2u)<<8u)),
                    "unaligned halfword TSA receives the exact little-endian source payload without rounding");
            }
        }
        iop.run_until(start+8u*latency+150u);
        require(!iop.stopped() && iop.ee_read(0x100u,4)==1u && iop.ee_read(0x104u,4)==0x20020u &&
            iop.ee_read(0x108u,4)==0u && iop.ee_read(0x10Cu,4)==(irq ? 8u : 0u),"CHCR, MADR, BCR and masked DMA IRQ follow final data, on either controller");
        require(!iop.spu2_ram_halfword(0xF849Eu) && !iop.spu2_ram_halfword(0xF84AFu) && iop.spu2_adma_words(core)==0u,
            "normal DMA neither rounds TSA nor fills ADMA input banks or neighboring words");
        require(iop.ee_read(0x110u,4)==0x80u && iop.ee_read(0x114u,4)==0u,
            "STATX ready follows completed payload and clears when the guest disables transfer mode");
    }
    for(unsigned core=0;core<2u;++core)for(unsigned mode=1u;mode<=10u;++mode) {
        m20::Assembler a(kEeResetVector);setup(a,core,true,mode);const auto dma=core ? 0xBF801500u : 0xBF8010C0u;
        if(mode==8u || mode==9u) {
            const auto other=core^1u,base=0xBF900000u+other*0x400u;
            sh(a,base+0x2E0u,mode==8u ? 0xFu : 0xEu);sh(a,base+0x2E2u,mode==8u ? 0u : 0xFFF8u);
            sh(a,base+0x33Cu,mode==8u ? 0xFu : 0xEu);
            sh(a,0xBF900764u+other*0x28u,0);sh(a,0xBF900766u+other*0x28u,0);sh(a,base+0x19Au,0x8080u);
        }
        sw(a,dma+8u,0x01000201u);a.label("pending");
        if(mode==6u)sh(a,0xBF90019Au+core*0x400u,0u);
        if(mode==7u)sw(a,dma,0x21000u);
        if(mode==10u)sh(a,0xBF90033Cu+(core^1u)*0x400u,0xFu);
        for(unsigned n=0;n<300u;++n)a.nop();
        if(mode==1u)sw(a,0xBF801578u,1u);
        if(mode==2u)sw(a,core ? 0xBF801570u : 0xBF8010F0u,core ? 8u : 0x80000u);
        idle(a);const auto boundary=(a.at("pending")-kEeResetVector)/4u;const auto rom=image(a);Iop iop;
        iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.diagnostic_spu2_adma_word_cycles(17u);payload(iop);
        iop.run_until(boundary+200u);
        if(mode<=2u) {
            require(!iop.stopped() && iop.spu2_dma_words(core)==0u,"disabled DMA gate stalls real data without synthetic completion");
            iop.run_until(boundary+600u);require(!iop.stopped() && iop.spu2_dma_words(core)==8u,"reopening DMA gate resumes queued data");
        } else if(mode==9u)require(!iop.stopped() && iop.spu2_dma_words(core)==8u,
            "the throughput experiment permits a disjoint silent effect workspace without inventing DSP data");
        else require(iop.stopped() && iop.spu2_dma_words(core)==0u,"unknown mode, dynamic-buffer target, IRQ, cancellation, register mutation and effect overlap refuse before data");
    }
    {
        m20::Assembler a(kEeResetVector);setup(a,0,false);setup(a,1,false);sh(a,0xBF90019Au,0x8020u);sh(a,0xBF9005AAu,0x949Fu);
        a.la(8,0xBF8010C8u);a.la(9,0x01000201u);a.label("start");a.sw(9,0,8);sw(a,0xBF801508u,0x01000201u);idle(a);
        const auto start=(a.at("start")-kEeResetVector)/4u;const auto rom=image(a);Iop iop;
        iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.diagnostic_spu2_adma_word_cycles(17u);payload(iop);
        iop.run_until(start+136u);
        require(!iop.stopped() && iop.spu2_dma_words(0)+iop.spu2_dma_words(1)==8u,
            "two normal DMA channels share one bounded bus instead of doubling its throughput");
        iop.run_until(start+400u);require(!iop.stopped() && iop.spu2_dma_words(0)==8u && iop.spu2_dma_words(1)==8u &&
            iop.spu2_ram_halfword(0xF849Fu)==0x3231u && iop.spu2_ram_halfword(0xF949Fu)==0x3231u,
            "shared-bus arbitration completes both independent payloads");
    }
    std::cout<<"SPU2 normal DMA payloads, timing, opaque bytes, gates and completion passed\n";return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
