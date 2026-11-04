// All byte-bit uncertainty masks across CPU copies and a real SIF0 payload.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void sw(m20::Assembler& a,std::uint32_t at,std::uint32_t value){a.la(8,at);a.la(9,value);a.sw(9,0,8);}
std::vector<std::uint8_t> program(unsigned invalid) {
    m20::Assembler a(kEeResetVector);a.la(8,0xBF402000);
    for(unsigned byte:{2u,0u,unsigned('E'),unsigned('C')}){a.la(9,byte);a.sb(9,0x17,8);}
    a.la(9,0x1A);a.sb(9,0x16,8);a.lbu(11,0x18,8);a.nop(); // Unmeasured reply, all eight bits unknown.
    a.la(10,0xA0002000);a.la(14,0xA0003000);
    for(unsigned mask=0;mask<256;++mask) {
        const auto defined=mask^0xFFu,ones=defined&0xA5u;
        a.andi(12,11,mask);a.ori(12,12,ones);a.sw(12,mask*4u,10);
        a.lw(13,mask*4u,10);a.nop();a.andi(13,13,defined);a.sw(13,mask*4u,14);
        a.la(15,0xA0004000);a.sb(12,mask,15);a.lbu(13,mask,15);a.nop();a.andi(13,13,defined);
        a.la(15,0xA0007000);a.sw(13,mask*4u,15);
        a.la(15,0xA0005000);a.sh(12,mask*2u,15);a.lhu(13,mask*2u,15);a.nop();a.andi(13,13,defined);
        a.la(15,0xA0008000);a.sw(13,mask*4u,15);
    }
    if(invalid==2) {
        a.andi(9,11,1);a.la(8,0xA0009000);a.sw(9,0,8);a.jr(8);a.nop();
    }
    if(invalid==1) {
        a.andi(9,11,1);a.la(8,0x80002000);a.r(8,9,9,0,0x25);a.la(8,0xA0006000);a.sw(9,0,8);
    }
    sw(a,0xBF801570,0x800);sw(a,0xBF801578,1);sw(a,0xBF80157C,1);
    sw(a,0xBF8010F4,1u<<23u);sw(a,0xBF801574,1u<<18u);
    sw(a,0xBF80152C,0x6000);sw(a,0xBF801528,0x01000701);
    a.label("idle");a.beq(0,0,"idle");a.nop();const auto code=m20::le_words(a.finish());
    std::vector<std::uint8_t> rom(4u*1024u*1024u);std::copy(code.begin(),code.end(),rom.begin());return rom;
}
}
int main() try {
    const auto* profile=find_console_hardware("SCPH-70004:GH-032-32");NvmImage nvm;
    nvm.bytes.resize(kNvmBytes);nvm.model_name=profile->model;nvm.layout=profile->mechacon->layout;
    for(unsigned invalid=0;invalid<3;++invalid) {
        const auto rom=program(invalid);Iop iop;iop.reset(rom,profile,&nvm);
        const std::uint32_t tag[]{0x80002000,256,0x70000040,0x10000};
        for(unsigned n=0;n<4;++n)iop.ee_write(0x6000u+n*4u,4,tag[n]);
        std::vector<std::uint8_t> ram(0x20000,0x5A);std::unordered_map<std::uint32_t,std::uint32_t> unknown{{0x10400,1}};
        iop.attach_ee_ram(ram,&unknown);iop.ee_dma_write(0x1000E020,0);iop.ee_dma_write(0x1000E000,1);
        iop.ee_dma_write(0x1000C020,0);iop.ee_dma_write(0x1000C000,0x184);iop.run_until(20000);
        for(unsigned mask=0;mask<256;++mask) {
            const auto ones=(mask^0xFFu)&0xA5u;
            for(const auto base:{0x3000u,0x7000u,0x8000u})require(iop.diagnostic_ram32(base+mask*4u)==ones,
                "known zeros/ones survive every partial-byte mask through SW/LW, SB/LBU and SH/LHU");
        }
        if(invalid) {
            require(iop.stopped() && !iop.sif0_metrics().bytes && ram[0x10000]==0x5A,
                "a single unknown tag/opcode bit cannot become an address, instruction or successful DMA");
            require(iop.stop_reason()->find(invalid==1 ? "SIF0 tag consumes undefined" : "never written")!=std::string::npos,
                "tag and instruction fetches name the unknown data dependency");continue;
        }
        require(!iop.stopped() && iop.sif0_metrics().bytes==1024 && !(iop.ee_dma_read(0x1000C000)&0x100),
            "the real SIF0 FIFO/DMA transfers every word and completes independently of payload knowledge: "+
            iop.stop_reason().value_or("running")+" bytes="+std::to_string(iop.sif0_metrics().bytes));
        require(iop.ee_read(0x2000,4)==0xA5,"a fully established source word remains directly readable");
        rejects([&]{(void)iop.ee_read(0x2004,4);},"undefined");
        for(unsigned mask=0;mask<256;++mask) {
            const auto at=0x10000u+mask*4u,ones=(mask^0xFFu)&0xA5u;
            const auto word=std::uint32_t{ram[at]}|(std::uint32_t{ram[at+1]}<<8u)|
                (std::uint32_t{ram[at+2]}<<16u)|(std::uint32_t{ram[at+3]}<<24u);
            const auto found=unknown.find(at);
            require((word&~mask)==ones && (mask ? found!=unknown.end() && found->second==mask : found==unknown.end()),
                "SIF0 preserves the exact bit mask and every established payload bit in EE RAM");
        }
        require(ram[0xFFFF]==0x5A && ram[0x10400]==0x5A && unknown.at(0x10400)==1,
            "payload completion changes no byte or bit knowledge outside the transfer");
    }
    std::cout<<"256 RAM bit masks across three CPU widths, SIF0 transport and partial tag/opcode refusal passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
