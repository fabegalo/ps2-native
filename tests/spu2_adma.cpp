// Original R3000A programs and PCM data. No firmware RPC or DMA reply stubs.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void sw(m20::Assembler& a,std::uint32_t address,std::uint32_t value) {a.la(8,address);a.la(9,value);a.sw(9,0,8);}
void sh(m20::Assembler& a,std::uint32_t address,std::uint16_t value) {a.la(8,address);a.la(9,value);a.sh(9,0,8);}
std::vector<std::uint8_t> image(m20::Assembler& a) {
    const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);std::copy(code.begin(),code.end(),rom.begin());return rom;
}
void idle(m20::Assembler& a) {a.label("idle");a.beq(0,0,"idle");a.nop();}
std::uint16_t left(unsigned block,unsigned sample) {return static_cast<std::uint16_t>((block+1u)*1000u+sample);}
std::uint16_t right(unsigned block,unsigned sample) {return static_cast<std::uint16_t>(-static_cast<std::int32_t>(left(block,sample)));}
void payload(Iop& iop,std::uint32_t base=0x20000u,unsigned bias=0) {
    for(unsigned block=0;block<3;++block)for(unsigned n=0;n<256;++n) {
        iop.ee_write(base+block*1024u+n*2u,2,left(block+bias,n));
        iop.ee_write(base+512u+block*1024u+n*2u,2,right(block+bias,n));
    }
}
void setup(m20::Assembler& a,unsigned core,bool interrupt,unsigned mode=0) {
    sw(a,0xBF801070u,0);sw(a,0xBF801078u,0);sw(a,0xBF80157Cu,1);
    sw(a,0xBF8010F4u,0x800000u|(interrupt && !core ? 0x100000u : 0u));
    sw(a,0xBF801574u,interrupt && core ? 0x10000u : 0u);
    sw(a,0xBF801578u,mode==1 ? 0u : 1u);
    if(mode!=4)sw(a,core ? 0xBF801570u : 0xBF8010F0u,mode==2 ? 0u : core ? 8u : 0x80000u);
    sh(a,0xBF90019Au,0x8000u);sh(a,0xBF90059Au,0x8000u);
    sh(a,0xBF900198u+core*0x400u,(mode==3 || mode==10) ? (core ? 0xFFCu : 0xFF0u) : core ? 0xF3Cu : 0xF30u);
    if(mode==10){sh(a,0xBF90076Cu+core*0x28u,0);sh(a,0xBF90076Eu+core*0x28u,0);}
    sh(a,0xBF9001B0u+core*0x400u,static_cast<std::uint16_t>(1u<<core));
    const auto dma=core ? 0xBF801500u : 0xBF8010C0u;
    sw(a,dma,0x20000u);sh(a,dma+4u,16u);sh(a,dma+6u,mode==5 ? 1u : 48u);
}
}
int main() try {
    for(unsigned core=0;core<2;++core)for(const bool interrupt : {false,true})for(const bool zero_gain : {false,true}) {
        m20::Assembler a(kEeResetVector);setup(a,core,interrupt,zero_gain ? 10u : 0u);
        const auto dma=core ? 0xBF801500u : 0xBF8010C0u;
        a.la(8,dma+8u);a.la(9,0x01000201u);a.label("start");a.sw(9,0,8);
        a.la(8,0xBF9001B0u+core*0x400u);a.lhu(9,0,8);a.nop();a.andi(11,9,1u<<core);
        a.la(10,0xA0000110u);a.sw(11,0,10);a.bne(9,0,"busy");a.nop();
        a.addiu(9,9,1); // an incorrect zero branch would consume unverified status bits
        a.label("busy");a.la(8,dma+8u);
        a.label("wait");a.lw(9,0,8);a.nop();a.raw((9u<<16)|(9u<<11)|(24u<<6)|2u);a.andi(9,9,1);a.bne(9,0,"wait");a.nop();
        a.la(8,0xBF801070u);a.lw(9,0,8);a.nop();a.andi(9,9,8u);a.la(10,0xA0000100u);a.sw(9,0,10);
        a.la(8,dma);a.lw(9,0,8);a.nop();a.sw(9,4,10);a.lhu(9,6,8);a.nop();a.sw(9,8,10);idle(a);
        const auto start=(a.at("start")-kEeResetVector)/4u;const auto rom=image(a);
        Iop iop;iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.diagnostic_spu2_adma_word_cycles(1);payload(iop);
        iop.run_until(start+512u);
        require(!iop.stopped() && iop.spu2_adma_words(core)==512 && !iop.diagnostic_ram32(0x100),
                "first two PCM banks contain actual DMA data before the command is allowed to complete");
        iop.run_until(start+4096u);
        require(!iop.stopped() && iop.spu2_adma_words(core)==512 && !iop.diagnostic_ram32(0x100),
                "full double buffers backpressure the third block without an invented completion");
        iop.run_until(300000u);
        require(!iop.stopped() && iop.spu2_adma_words(core)==768 && iop.ee_read(0x100,4)==(interrupt ? 8u : 0u) &&
            iop.ee_read(0x104,4)==0x20C00u && iop.ee_read(0x108,4)==0 && iop.ee_read(0x110,4)==(1u<<core),
                "MADR/BCR, CHCR polling and the masked channel IRQ follow real payload completion on both DMA controllers");
        for(unsigned bank=0;bank<2;++bank)for(unsigned n=0;n<256;++n) {
            const auto block=bank==0 ? 2u : 1u,base=0x2000u+core*0x400u+bank*0x100u;
            require(iop.spu2_ram_halfword(base+n)==left(block,n) && iop.spu2_ram_halfword(base+0x200u+n)==right(block,n),
                    "PCM left/right planes and both MEMIN banks preserve every source halfword");
        }
        const auto frames=iop.spu2_input_frames(core);const auto sample=iop.spu2_last_input(core);
        require(frames>256 && frames<512 && sample && sample->left==static_cast<std::int16_t>(left(1u,(frames-1u)%256u)) &&
            sample->right==static_cast<std::int16_t>(-static_cast<std::int32_t>(left(1u,(frames-1u)%256u))),
                "48 kHz input consumption reads the real signed stereo samples in order");
        require(iop.spu2_last_sample(core) && iop.spu2_last_sample(core)->left==0 && iop.spu2_last_sample(core)->right==0,
                "closed input gates or known zero gains keep output quiet despite nonzero PCM data");
    }
    for(unsigned mode=1;mode<=2;++mode) {
        m20::Assembler a(kEeResetVector);setup(a,0,true,mode);sw(a,0xBF8010C8u,0x01000201u);
        a.label("paused");for(unsigned n=0;n<2000;++n)a.nop();sw(a,mode==1 ? 0xBF801578u : 0xBF8010F0u,mode==1 ? 1u : 0x80000u);idle(a);
        const auto paused=(a.at("paused")-kEeResetVector)/4u;const auto rom=image(a);
        Iop iop;iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.diagnostic_spu2_adma_word_cycles(1);payload(iop);
        iop.run_until(paused+1000u);require(!iop.stopped() && !iop.spu2_adma_words(0),"DMACEN and channel priority independently hold real DMA requests");
        iop.run_until(300000u);require(!iop.stopped() && iop.spu2_adma_words(0)==768,"enabling the DMA gate resumes the same buffered transfer");
    }
    for(unsigned mode=0;mode<10;++mode) {
        m20::Assembler a(kEeResetVector);setup(a,0,true,mode==8 ? 10u : mode<6 ? mode : 0u);sw(a,0xBF8010C8u,0x01000201u);
        if(mode==6)sh(a,0xBF900198u,0xFF0u);
        if(mode==7)sh(a,0xBF9001B0u,0u);
        if(mode==8)sh(a,0xBF90076Cu,1u);
        if(mode==9){a.la(8,0xBF9001B0u);a.lhu(9,0,8);a.nop();a.addiu(9,9,1);}
        idle(a);const auto rom=image(a);Iop iop;iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);
        if(mode)iop.diagnostic_spu2_adma_word_cycles(16);
        if(mode!=1 && mode!=2)payload(iop); // two held-gate cases are verified above
        if(mode==1 || mode==2)continue;
        iop.run_until(3000u);require(iop.stopped() && iop.spu2_adma_words(0)<768,
                "missing experiment, audible routing, unknown priority, partial blocks and cancellation cannot fabricate an upload");
    }
    {
        m20::Assembler a(kEeResetVector);setup(a,0,true);
        sw(a,0xBF801574u,0x10000u);sw(a,0xBF801570u,8u);
        sh(a,0xBF900598u,0xF3Cu);sh(a,0xBF9005B0u,2u);
        sw(a,0xBF801500u,0x22000u);sw(a,0xBF801504u,0x00300010u);
        a.la(8,0xBF8010C8u);a.la(9,0x01000201u);a.label("startboth");a.sw(9,0,8);
        sw(a,0xBF801508u,0x01000201u);
        a.label("waitboth");a.la(8,0xBF8010C8u);a.lw(9,0,8);a.nop();a.raw((9u<<16)|(9u<<11)|(24u<<6)|2u);a.andi(9,9,1);a.bne(9,0,"waitboth");a.nop();
        a.la(8,0xBF801508u);a.lw(9,0,8);a.nop();a.raw((9u<<16)|(9u<<11)|(24u<<6)|2u);a.andi(9,9,1);a.bne(9,0,"waitboth");a.nop();
        a.la(10,0xA0000100u);a.la(8,0xBF801070u);a.lw(9,0,8);a.nop();a.andi(9,9,8u);a.sw(9,0,10);
        a.la(8,0xBF8010F4u);a.lw(9,0,8);a.nop();a.raw((9u<<16)|(9u<<11)|(28u<<6)|2u);a.andi(9,9,1);a.sw(9,4,10);
        a.la(8,0xBF801574u);a.lw(9,0,8);a.nop();a.raw((9u<<16)|(9u<<11)|(24u<<6)|2u);a.andi(9,9,1);a.sw(9,8,10);idle(a);
        const auto start=(a.at("startboth")-kEeResetVector)/4u;
        const auto rom=image(a);Iop iop;iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.diagnostic_spu2_adma_word_cycles(1);
        payload(iop);payload(iop,0x22000u,8u);iop.run_until(start+100u);
        require(!iop.stopped() && iop.spu2_adma_words(0)+iop.spu2_adma_words(1)==100 &&
            iop.spu2_adma_words(0)>45 && iop.spu2_adma_words(1)>45,"one shared bus slot per clock edge bounds aggregate throughput and fairly serves both controllers");
        iop.run_until(4096u);
        require(!iop.stopped() && iop.spu2_adma_words(0)==512 && iop.spu2_adma_words(1)==512 && !iop.diagnostic_ram32(0x100),
                "concurrent controllers fairly fill their own two banks and backpressure independently");
        iop.run_until(300000u);
        require(!iop.stopped() && iop.spu2_adma_words(0)==768 && iop.spu2_adma_words(1)==768 && iop.ee_read(0x100,4)==8 &&
            iop.ee_read(0x104,4)==1 && iop.ee_read(0x108,4)==1,"both real transfers set their distinct DMA controller flags before the guest finishes polling");
        for(unsigned core=0;core<2;++core)for(unsigned n=0;n<256;++n) {
            const auto base=0x2000u+core*0x400u;
            require(iop.spu2_ram_halfword(base+n)==left(2u+core*8u,n) && iop.spu2_ram_halfword(base+0x200u+n)==right(2u+core*8u,n),
                    "shared bus arbitration preserves each controller's distinct payload and RAM destination");
        }
    }
    {
        m20::Assembler a(kEeResetVector);setup(a,0,true);sw(a,0xBF8010C8u,0x01000201u);idle(a);const auto rom=image(a);
        Iop iop;iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.diagnostic_spu2_adma_word_cycles(1);
        iop.ee_write(0x20000u,4,0x89ABCDEFu);iop.run_until(1000);
        require(iop.stopped() && iop.spu2_adma_words(0)==1 && iop.spu2_ram_halfword(0x2000u)==0xCDEFu &&
            iop.spu2_ram_halfword(0x2001u)==0x89ABu && !iop.spu2_ram_halfword(0x2002u),"unknown source bytes stop at the exact first missing word after preserving prior real transfers");
    }
    for(unsigned core=0;core<2;++core) {
        Iop iop;
        for(const bool retained : {true,false}) {
            m20::Assembler a(kEeResetVector);setup(a,core,true);
            const auto dma=core ? 0xBF801500u : 0xBF8010C0u;
            sw(a,dma+8u,0x01000201u);a.la(8,dma+8u);
            a.label("wait");a.lw(9,0,8);a.nop();a.raw((9u<<16)|(9u<<11)|(24u<<6)|2u);a.andi(9,9,1);a.bne(9,0,"wait");a.nop();
            a.la(8,0xBF9001B0u+core*0x400u);a.lhu(9,0,8);a.nop();a.andi(11,9,1u<<core);
            a.la(10,0xA0000120u);a.sw(11,0,10);a.sh(0,0,8);
            a.lhu(9,0,8);a.nop();a.andi(11,9,1u<<core);a.sw(11,4,10);
            a.addiu(9,9,1);a.sw(9,8,10);idle(a);const auto rom=image(a);
            iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.diagnostic_spu2_adma_word_cycles(1);
            if(retained)iop.diagnostic_spu2_admas_control_latch();payload(iop);iop.run_until(300000u);
            require(iop.spu2_adma_words(core)==768,"post-DMA readback never replaces actual data movement");
            if(retained)require(!iop.stopped() && iop.diagnostic_ram32(0x120u)==(1u<<core) && iop.diagnostic_ram32(0x124u)==0 && iop.diagnostic_ram32(0x128u)==1,
                "separate whole-control latch hypothesis retains known writes and explicit disable only when requested");
            else require(iop.stopped() && !iop.diagnostic_ram32(0x120u) && iop.stop_reason()->find("inactive/readback")!=std::string::npos,
                         "reset removes the readback experiment and completed DMA does not invent inactive status");
        }
    }
    {
        m20::Assembler a(kEeResetVector);a.la(8,0xBF9001B0u);a.lhu(9,0,8);a.nop();idle(a);
        const auto rom=image(a);Iop iop;iop.reset(rom,nullptr);iop.diagnostic_spu2_admas_control_latch();iop.run_until(20);
        require(iop.stopped(),"retained-enable experiment never invents an ADMAS cold value before a guest write");
    }
    std::cout<<"SPU2 ADMA payloads, stereo banks, backpressure, sample clock, DMA gates and actual completion IRQs passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
