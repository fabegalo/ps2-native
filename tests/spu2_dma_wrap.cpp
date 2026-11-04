// Original fixtures: normal DMA at the sound RAM boundary, no firmware data.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void sw(m20::Assembler& a,std::uint32_t at,std::uint32_t value){a.la(8,at);a.la(9,value);a.sw(9,0,8);}
void sh(m20::Assembler& a,std::uint32_t at,std::uint32_t value){a.la(8,at);a.la(9,value);a.sh(9,0,8);}
void setup(m20::Assembler& a,unsigned core,std::uint32_t target,std::uint32_t bcr=0x00020004u) {
    sw(a,0xBF801070,0);sw(a,0xBF801078,0);sw(a,0xBF80157C,1);
    sw(a,0xBF8010F4,0);sw(a,0xBF801574,0);sw(a,0xBF801578,1);
    sw(a,0xBF8010F0,0x80000);sw(a,0xBF801570,8);
    sh(a,0xBF90019A,0x8000);sh(a,0xBF90059A,0x8000);
    const auto spu=0xBF900000u+core*0x400u,dma=core ? 0xBF801500u : 0xBF8010C0u;
    sh(a,spu+0x1B0,0);sh(a,spu+0x1A8,target>>16u);sh(a,spu+0x1AA,target&0xFFFFu);
    sh(a,spu+0x19A,0x8020);sw(a,dma,0x20000);sw(a,dma+4,bcr);
}
void idle(m20::Assembler& a){a.label("idle");a.beq(0,0,"idle");a.nop();}
std::vector<std::uint8_t> rom_of(m20::Assembler& a) {
    const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
    std::copy(code.begin(),code.end(),rom.begin());return rom;
}
void init(Iop& iop,std::span<const std::uint8_t> rom,unsigned latency,bool wrap,bool opaque=false) {
    iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,true);iop.diagnostic_spu2_adma_word_cycles(latency);
    if(wrap)iop.diagnostic_spu2_dma_wrap_to_zero();
    for(unsigned n=0;n<32;++n)if(!opaque || n!=2u)iop.ee_write(0x20000u+n,1,0x31u+n);
}
}
int main() try {
    for(unsigned core=0;core<2;++core)for(const auto target:{0xFFFF0u,0xFFFF1u,0xFFFF7u,0xFFFFFu})
    for(const bool wrap:{false,true})for(const bool opaque:{false,true}) {
        m20::Assembler a(kEeResetVector);setup(a,core,target);
        const auto dma=core ? 0xBF801500u : 0xBF8010C0u,spu=0xBF900000u+core*0x400u;
        a.la(8,dma+8);a.la(9,0x01000201);a.label("start");a.sw(9,0,8);
        a.label("wait");a.lw(9,0,8);a.nop();a.r(0,9,9,24,2);a.andi(9,9,1);a.bne(9,0,"wait");a.nop();
        a.la(8,spu+0x1AA);a.lhu(9,0,8);a.nop();a.la(10,0xA0000100);a.sw(9,0,10);
        a.la(8,dma);a.lw(9,0,8);a.nop();a.sw(9,4,10);a.lhu(9,6,8);a.nop();a.sw(9,8,10);idle(a);
        const auto start=(a.at("start")-kEeResetVector)/4u;const auto rom=rom_of(a);Iop iop;init(iop,rom,7,wrap,opaque);
        iop.run_until(start+6u);
        if(!wrap && target+16u>0x100000u) {
            require(iop.stopped() && iop.stop_reason()->find("wrap-to-zero experiment")!=std::string::npos &&
                !iop.spu2_dma_words(core),"the unmeasured wrap destination is never a silent default");continue;
        }
        require(!iop.stopped() && !iop.spu2_dma_words(core),"no payload precedes the shared-bus boundary");
        iop.run_until(start+200u);
        require(!iop.stopped() && iop.spu2_dma_words(core)==8u &&
            iop.ee_read(0x100,4)==((target+16u)&0xFFFFu) && iop.ee_read(0x104,4)==0x20020 && !iop.ee_read(0x108,4),
            "TSA, MADR, BCR and completion count every granted word across the exact boundary");
        for(unsigned n=0;n<16;++n) {
            const auto at=(target+n)&0xFFFFFu;const auto word=iop.spu2_ram_halfword(at);
            if(at<0x2800u || (opaque && n==1u))require(!word,"capture visibility and opaque source bytes remain unknown");
            else require(word==((0x31u+n*2u)|((0x32u+n*2u)<<8u)),"every ordinary RAM destination receives its exact source halfword");
        }
        require(!iop.spu2_ram_halfword(target-1u),"the preceding halfword is untouched");
        rejects([&]{iop.diagnostic_spu2_dma_wrap_to_zero();},"before execution");
        if(wrap && target==0xFFFFFu) {
            init(iop,rom,7,false);iop.run_until(start+200u);
            require(iop.stopped() && !iop.spu2_dma_words(core),"reset removes the wrap experiment");
        }
    }
    for(unsigned first=0;first<2;++first) {
        m20::Assembler a(kEeResetVector);setup(a,first,0xFFFF8,0x00011800);setup(a,first^1u,0x2800);
        sh(a,0xBF90019A+first*0x400u,0x8020);
        sw(a,first ? 0xBF801508 : 0xBF8010C8,0x01000201);
        sw(a,first ? 0xBF8010C8 : 0xBF801508,0x01000201);idle(a);
        const auto rom=rom_of(a);Iop iop;init(iop,rom,100,true);iop.run_until(500);
        require(iop.stopped() && iop.stop_reason()->find("concurrent normal DMA destinations overlap")!=std::string::npos &&
            !iop.spu2_dma_words(0) && !iop.spu2_dma_words(1),"overlap in only the wrapped suffix refuses before either channel transfers bytes");
    }
    for(unsigned core=0;core<2;++core) {
        m20::Assembler a(kEeResetVector);setup(a,core,0xFFFF8,0x00011800);
        const auto other=core^1u,spu=0xBF900000u+other*0x400u;
        sh(a,spu+0x2E0,0);sh(a,spu+0x2E2,0x2800);sh(a,spu+0x33C,0);
        sh(a,0xBF900764u+other*0x28u,0);sh(a,0xBF900766u+other*0x28u,0);sh(a,spu+0x19A,0x8080);
        sw(a,core ? 0xBF801508 : 0xBF8010C8,0x01000201);idle(a);
        const auto rom=rom_of(a);Iop iop;init(iop,rom,100,true);iop.run_until(500);
        require(iop.stopped() && iop.stop_reason()->find("overlaps an uncomputed active effect workspace")!=std::string::npos &&
            !iop.spu2_dma_words(core),"effect overlap in only the wrapped suffix refuses before modifying sound RAM");
    }
    std::cout<<"SPU2 DMA boundary, real payload, opaque capture visibility and wrapped overlap checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
