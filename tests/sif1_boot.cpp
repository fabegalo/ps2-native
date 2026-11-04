// Authored EE/IOP DMA packets; no firmware, RPC replies or Sony code.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>

using namespace ps2native;
using namespace boot_checks;
namespace {
void put(std::vector<std::uint8_t>& ram,std::size_t at,std::uint32_t value) {
    for(unsigned b=0;b<4;++b)ram.at(at+b)=static_cast<std::uint8_t>(value>>(b*8u));
}
void store(m20::Assembler& a,std::uint32_t address,std::uint32_t value) {
    a.la(8,address);a.la(9,value);a.sw(9,0,8);
}
std::vector<std::uint8_t> receiver(std::uint32_t masks,std::uint32_t master=1u<<23) {
    m20::Assembler a(0xBFC00000u);
    store(a,0xBF801570u,0x8000u);store(a,0xBF801578u,1);
    store(a,0xBF80157Cu,1);store(a,0xBF8010F4u,master);store(a,0xBF801574u,masks);
    store(a,0xBF801538u,0x41000300u);
    a.label("poll");
    a.la(8,0xBF801538u);a.lw(9,0,8);a.nop();a.la(10,0xA0000080u);a.sw(9,0,10);
    a.la(8,0xBF801574u);a.lw(9,0,8);a.nop();a.sw(9,4,10);
    a.la(8,0xBF801070u);a.lw(9,0,8);a.nop();a.andi(9,9,8);a.sw(9,8,10);
    a.beq(0,0,"poll");a.nop();
    const auto code=m20::le_words(a.finish());
    std::vector<std::uint8_t> rom(4u*1024u*1024u);std::copy(code.begin(),code.end(),rom.begin());return rom;
}
std::vector<std::uint8_t> packet(std::uint32_t destination_tag=0xC0000400u) {
    std::vector<std::uint8_t> ram(0x800u);
    put(ram,0x100u,destination_tag);put(ram,0x104u,4);
    for(unsigned w=0;w<4;++w)put(ram,0x110u+4u*w,0xCA110000u+w);
    return ram;
}
void arm(Iop& iop,std::vector<std::uint8_t>& ram,std::uint32_t qwc,bool chain=false,std::uint32_t chcr=0x184u) {
    iop.attach_ee_ram(ram);iop.ee_dma_write(0x1000E000u,1);
    // No PCR write: the documented clear PCE makes unknown CDE irrelevant.
    iop.ee_dma_write(0x1000C410u,0x100u);iop.ee_dma_write(0x1000C420u,qwc);
    if(chain)iop.ee_dma_write(0x1000C430u,0);
    iop.ee_dma_write(0x1000C400u,chain ? chcr : 0x101u);
}
std::size_t completions(const Iop& iop) {
    return static_cast<std::size_t>(std::count_if(iop.events().begin(),iop.events().end(),[](const auto& e) {
        return e.text=="SIF1 IOP destination complete";
    }));
}
void received(Iop& iop,bool flag,bool irq) {
    require(!iop.stopped(),iop.stop_reason().value_or("IOP running"));
    for(unsigned w=0;w<4;++w)require(iop.ee_read(0x400u+w*4u,4)==0xCA110000u+w,"IOP receives exact nonzero source bytes");
    require(iop.sif1_metrics().bytes==16,"payload byte count excludes destination tag and padding");
    require(!(iop.ee_read(0x80u,4)&0x01000000u),"IOP STR clears after actual final payload");
    require(bool(iop.ee_read(0x84u,4)&(1u<<27))==flag,"completion and IQE masks control the actual flag");
    require(bool(iop.ee_read(0x88u,4)&8u)==irq,"DICR master controls delivery to I_STAT");
    require(completions(iop)==1,"completed packet raises one destination completion event");
}
void transfer_masks() {
    for(const auto masks : {0u,1u<<19,1u<<10,(1u<<19)|(1u<<10)}) {
        auto ram=packet();Iop iop;iop.reset(receiver(masks),nullptr);arm(iop,ram,2);iop.run_until(400);
        received(iop,masks!=0,masks!=0);
    }
    auto ram=packet();Iop masked;masked.reset(receiver(1u<<10,0),nullptr);arm(masked,ram,2);masked.run_until(400);
    received(masked,true,false);
    auto no_irq_tag=packet(0x80000400u);Iop irq_only;irq_only.reset(receiver(1u<<10),nullptr);
    arm(irq_only,no_irq_tag,2);irq_only.run_until(400);received(irq_only,false,false);
}
void incomplete_packet() {
    auto ram=packet();Iop iop;iop.reset(receiver(1u<<19),nullptr);arm(iop,ram,1);iop.run_until(400);
    require(!iop.stopped() && !iop.sif1_metrics().bytes,"tag alone does not fabricate payload");
    require((iop.ee_read(0x80u,4)&0x01000000u) && !(iop.ee_read(0x84u,4)&(1u<<27)) && !iop.ee_read(0x88u,4),
            "receiver remains busy without a premature completion flag or interrupt");
    rejects([&]{(void)iop.ee_read(0x400u,4);},"has not been written");
    iop.ee_dma_write(0x1000C410u,0x110u);iop.ee_dma_write(0x1000C420u,1);iop.ee_dma_write(0x1000C400u,0x101u);
    iop.run_until(500);received(iop,true,true);
}
void source_chain() {
    for(const bool split : {false,true}) {
        auto ram=packet();put(ram,0,split ? 0x30000001u : 2u);put(ram,4,0x100u);
        // TTE=0: the upper tag qword lanes are not transferred to the receiver.
        put(ram,8,0xDEADBEEFu);put(ram,12,0xFEEDFACEu);
        if(split){put(ram,16,1);put(ram,20,0x110u);}
        Iop iop;iop.reset(receiver(1u<<19),nullptr);arm(iop,ram,0,true);iop.run_until(400);received(iop,true,true);
        require(iop.ee_dma_read(0x1000C430u)==(split ? 32u : 16u),"REF advances TADR to the next real source tag");
        require(iop.ee_dma_read(0x1000C410u)==0x120u && !iop.ee_dma_read(0x1000C420u),"source MADR/QWC follow the actual payload");
        require(!(iop.ee_dma_read(0x1000C400u)&0x100u) && (iop.ee_dma_read(0x1000E010u)&0x40u),"REFE ends the EE DMA once");
    }
    for(const bool tie : {false,true}) {
        auto ram=packet();put(ram,0,0xB0000001u);put(ram,4,0x100u);put(ram,16,1);put(ram,20,0x110u);
        Iop iop;iop.reset(receiver(1u<<19),nullptr);arm(iop,ram,0,true,tie ? 0x184u : 0x104u);iop.run_until(400);
        require(!iop.stopped(),iop.stop_reason().value_or("running"));
        require(iop.ee_dma_read(0x1000C430u)==(tie ? 16u : 32u),"source tag IRQ terminates only when CHCR.TIE is enabled");
        if(tie)require(iop.sif1_metrics().bytes==0 && completions(iop)==0,"EE source IRQ cannot complete an incomplete IOP packet");
        else received(iop,true,true);
    }
}
void refused_packets() {
    for(const auto tag : {0x10000002u,0x04000002u}) {
        auto ram=packet();put(ram,0,tag);put(ram,4,0x100u);
        Iop iop;iop.reset(receiver(1u<<19),nullptr);arm(iop,ram,0,true);iop.run_until(400);
        require(iop.stopped() && iop.stop_reason()->find("only REFE/REF")!=std::string::npos,"unsupported source tag fails explicitly");
        require(!iop.sif1_metrics().bytes && !(iop.ee_dma_read(0x1000E010u)&0x40u),"unsupported tag cannot produce bytes or completion");
    }
    std::unordered_map<std::uint32_t,std::uint32_t> unknown{{0,1}};
    auto ram=packet();put(ram,0,2);put(ram,4,0x100u);
    Iop iop;iop.reset(receiver(1u<<19),nullptr);arm(iop,ram,0,true);iop.attach_ee_ram(ram,&unknown);iop.run_until(400);
    require(iop.stopped() && iop.stop_reason()->find("unknown RAM bits")!=std::string::npos,"symbolic source tags cannot be consumed as zero");
    auto tag_irq=packet(0x40000400u);Iop ambiguous;ambiguous.reset(receiver(1u<<10),nullptr);arm(ambiguous,tag_irq,2);ambiguous.run_until(400);
    require(ambiguous.stopped() && ambiguous.stop_reason()->find("IRQ without END")!=std::string::npos,"unverified non-END IRQ behavior stays refused");
}
}
int main() try {
    transfer_masks();incomplete_packet();source_chain();refused_packets();
    std::cout<<"SIF1 source chains, exact payload, END/IRQ masks and incomplete packets passed\n";
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
