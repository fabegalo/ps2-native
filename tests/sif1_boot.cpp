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
std::vector<std::uint8_t> receiver(std::uint32_t masks,std::uint32_t master=1u<<23,bool stop_receiver=false) {
    m20::Assembler a(0xBFC00000u);
    store(a,0xBF801570u,0x8000u);store(a,0xBF801578u,1);
    store(a,0xBF80157Cu,1);store(a,0xBF8010F4u,master);store(a,0xBF801574u,masks);
    store(a,0xBF801538u,0x41000300u);
    if(stop_receiver)store(a,0xBF801538u,0);
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
void arm(Iop& iop,std::vector<std::uint8_t>& ram,std::uint32_t qwc,bool chain=false,std::uint32_t chcr=0x184u,unsigned low=0u) {
    iop.attach_ee_ram(ram);iop.ee_dma_write(0x1000E000u,1);
    // No PCR write: the documented clear PCE makes unknown CDE irrelevant.
    iop.ee_dma_write(0x1000C410u,0x100u|low);iop.ee_dma_write(0x1000C420u,qwc);
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
    for(unsigned low=1u;low<16u;++low) {
        auto ram=packet();Iop iop;iop.reset(receiver(1u<<19),nullptr);arm(iop,ram,2,false,0x184u,low);
        require(iop.ee_dma_read(0x1000C410u)==0x100u,"SIF1 normal MADR cannot select a sub-qword source");
        iop.run_until(400);received(iop,true,true);
        require(iop.ee_dma_read(0x1000C410u)==0x120u,"SIF1 low-bit aliases advance over the same genuine tag and payload");
    }
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
void next_source_chain() {
    for(const bool inline_payload : {false,true}) {
        auto ram=packet();
        put(ram,0,0x20000000u|(inline_payload ? 1u : 0u));put(ram,4,0x200u);
        if(inline_payload)std::copy_n(ram.begin()+0x100u,16,ram.begin()+16u);
        put(ram,0x200u,inline_payload ? 1u : 2u);put(ram,0x204u,inline_payload ? 0x110u : 0x100u);
        Iop iop;iop.reset(receiver(1u<<19),nullptr);arm(iop,ram,0,true);
        iop.run_until(1);
        require(!iop.stopped() && iop.ee_dma_read(0x1000C430u)==0x200u && !(iop.ee_dma_read(0x1000E010u)&0x40u),
                "NEXT selects its actual target without premature source completion");
        iop.run_until(400);received(iop,true,true);
        require(iop.ee_dma_read(0x1000C430u)==0x210u,"NEXT follows its address, not adjacent payload or sequential bytes");
    }
    for(const auto target : {0x201u,0x800u,0x80000200u}) {
        auto ram=packet();put(ram,0,0x20000000u);put(ram,4,target);
        Iop iop;iop.reset(receiver(1u<<19),nullptr);arm(iop,ram,0,true);iop.run_until(10);
        require(iop.stopped() && iop.stop_reason()->find("NEXT tag outside")!=std::string::npos &&
                !iop.sif1_metrics().bytes && !(iop.ee_dma_read(0x1000E010u)&0x40u),
                "invalid NEXT address fails before exposing bytes or successful completion");
    }
}
void refused_packets() {
    for(const auto tag : {0x10000002u,0x04000002u}) {
        auto ram=packet();put(ram,0,tag);put(ram,4,0x100u);
        Iop iop;iop.reset(receiver(1u<<19),nullptr);arm(iop,ram,0,true);iop.run_until(400);
        require(iop.stopped() && iop.stop_reason()->find("only REFE/NEXT/REF")!=std::string::npos,"unsupported source tag fails explicitly");
        require(!iop.sif1_metrics().bytes && !(iop.ee_dma_read(0x1000E010u)&0x40u),"unsupported tag cannot produce bytes or completion");
    }
    std::unordered_map<std::uint32_t,std::uint32_t> unknown{{0,1}};
    auto ram=packet();put(ram,0,2);put(ram,4,0x100u);
    Iop iop;iop.reset(receiver(1u<<19),nullptr);arm(iop,ram,0,true);iop.attach_ee_ram(ram,&unknown);iop.run_until(400);
    require(iop.stopped() && iop.stop_reason()->find("unknown RAM bits")!=std::string::npos,"symbolic source tags cannot be consumed as zero");
    auto tag_irq=packet(0x40000400u);Iop ambiguous;ambiguous.reset(receiver(1u<<10),nullptr);arm(ambiguous,tag_irq,2);ambiguous.run_until(400);
    require(ambiguous.stopped() && ambiguous.stop_reason()->find("IRQ without END")!=std::string::npos,"unverified non-END IRQ behavior stays refused");
}
void block_control_halfwords() {
    for(const unsigned first : {0u,1u})for(const bool complete : {false,true}) {
        m20::Assembler a(0xBFC00000u);
        a.la(8,0xBF801524u);a.la(9,first ? 0x1234u : 0x5678u);a.sh(9,first*2u,8);
        a.lhu(10,first*2u,8);a.nop();a.la(11,0xA0000100u);a.sw(10,0,11);
        if(complete){a.la(9,first ? 0x5678u : 0x1234u);a.sh(9,(1u-first)*2u,8);}
        a.lw(10,0,8);a.nop();a.sw(10,4,11);
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),rom.begin());Iop iop;iop.reset(rom,nullptr);iop.run_until(150);
        if(complete)require(!iop.stopped() && iop.ee_read(0x104u,4)==0x12345678u,"both BCR halves preserve their actual written bits");
        else require(iop.stopped() && iop.stop_reason()->find("register read before write")!=std::string::npos,
                     "one written half cannot invent the opposite half of a 32-bit read");
        // If the later full read stopped, its earlier byte result is private;
        // gpr still retains the actual preceding halfword load.
        if(complete)require(iop.ee_read(0x100u,4)==(first ? 0x1234u : 0x5678u),"LHU reads only the established BCR half");
    }
    m20::Assembler a(0xBFC00000u);store(a,0xBF801524u,0xAAAABBBBu);
    a.la(8,0xBF801524u);a.la(9,0xCAFEu);a.sh(9,2,8);a.lw(10,0,8);a.nop();
    a.la(11,0xA0000100u);a.sw(10,0,11);a.label("idle");a.beq(0,0,"idle");a.nop();
    const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
    std::copy(code.begin(),code.end(),rom.begin());Iop iop;iop.reset(rom,nullptr);iop.run_until(150);
    require(!iop.stopped() && iop.ee_read(0x100u,4)==0xCAFEBBBBu,"SH after a full BCR write preserves the opposite halfword");
}
std::vector<std::uint8_t> partial_sender(unsigned words,bool next_known,std::uint32_t source) {
    m20::Assembler a(0xBFC00000u);
    store(a,0xBF801570u,0x800u);store(a,0xBF801578u,1);store(a,0xBF80157Cu,1);
    store(a,0xBF8010F4u,1u<<23);store(a,0xBF801574u,1u<<18);
    store(a,0xA0000400u,0x80000000u|source);store(a,0xA0000404u,words);
    store(a,0xA0000408u,0x70000000u|((words+3u)/4u));store(a,0xA000040Cu,0x400u);
    if(next_known){store(a,0xA0000410u,0xCAFEBABEu);store(a,0xA0000414u,0x13579BDFu);}
    for(unsigned w=0;w<words;++w)store(a,0xA0000000u|(source+w*4u),0xCA000000u+w);
    store(a,0xBF80152Cu,0x400u);store(a,0xBF801528u,0x01000701u);
    a.label("poll");a.la(8,0xBF801520u);a.lw(9,0,8);a.nop();a.la(10,0xA0000080u);a.sw(9,0,10);
    a.beq(0,0,"poll");a.nop();
    const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
    std::copy(code.begin(),code.end(),rom.begin());return rom;
}
void partial_sif0_packets() {
    for(const unsigned words : {1u,2u,3u,5u,6u,7u})for(const bool boundary : {false,true}) {
        const auto source=boundary ? kIopRamBytes-words*4u : 0x500u;
        std::vector<std::uint8_t> ram(0x800u,0xAAu);Iop iop;iop.reset(partial_sender(words,true,source),nullptr);
        iop.attach_ee_ram(ram);iop.ee_dma_write(0x1000E000u,1);iop.ee_dma_write(0x1000C020u,0);iop.ee_dma_write(0x1000C000u,0x184u);
        iop.run_until(600);
        require(!iop.stopped(),iop.stop_reason().value_or("running"));
        const auto rounded=(words+3u)/4u*4u;
        for(unsigned w=0;w<rounded;++w) {
            const auto at=0x400u+w*4u;std::uint32_t actual=0;
            for(unsigned b=0;b<4;++b)actual|=std::uint32_t{ram[at+b]}<<(b*8u);
            const auto expected=w<words ? 0xCA000000u+w : words>4 ? 0xCA000000u+w%4u :
                                w==1 ? 0x400u : w==2 ? 0xCAFEBABEu : 0x13579BDFu;
            require(actual==expected,"partial packet uses measured bus lanes, without reading past its actual source");
        }
        require(ram[0x3FFu]==0xAAu && ram[0x400u+rounded*4u]==0xAAu,"partial packet leaves adjacent EE bytes intact");
        require(iop.ee_read(0x80u,4)==source+words*4u,"IOP MADR advances by actual words, without padding");
        require(iop.sif0_metrics().bytes==rounded*4u && (iop.ee_dma_read(0x1000E010u)&0x20u),"EE completion follows the actual rounded bus transfer");
    }
    std::vector<std::uint8_t> ram(0x800u,0xAAu);Iop unknown;unknown.reset(partial_sender(1,false,0x500u),nullptr);
    unknown.attach_ee_ram(ram);unknown.ee_dma_write(0x1000E000u,1);unknown.ee_dma_write(0x1000C020u,0);unknown.ee_dma_write(0x1000C000u,0x184u);
    unknown.run_until(600);
    require(unknown.stopped() && unknown.stop_reason()->find("unknown payload requires EE RAM knowledge tracking")!=std::string::npos,
            "undefined following IOP tag cannot become fake zero payload padding");
    require(!unknown.sif0_metrics().bytes && ram[0x400u]==0xAAu && !(unknown.ee_dma_read(0x1000E010u)&0x20u),
            "unknown retained lanes fail before exposing payload or a completion IRQ");
}
void idle_sif0_receiver_stop() {
    std::vector<std::uint8_t> ram(0x800u,0xAAu);Iop iop;iop.reset(partial_sender(6,true,0x500u),nullptr);
    iop.attach_ee_ram(ram);iop.ee_dma_write(0x1000E000u,1);iop.ee_dma_write(0x1000C020u,0);iop.ee_dma_write(0x1000C000u,0x184u);
    iop.ee_dma_write(0x1000C000u,0);iop.run_until(300);
    require(!iop.stopped() && !iop.sif0_metrics().bytes && ram[0x400u]==0xAAu && !(iop.ee_dma_read(0x1000E010u)&0x20u),
            "idle receiver stop leaves later real IOP data buffered without a false EE completion");
    iop.ee_dma_write(0x1000C000u,0x184u);iop.run_until(320);
    require(iop.sif0_metrics().bytes==32 && ram[0x414u]==5 && (iop.ee_dma_read(0x1000E010u)&0x20u),
            "rearmed receiver drains the original buffered packet without dropping bytes");
    iop.reset(partial_sender(6,true,0x500u),nullptr);iop.attach_ee_ram(ram);iop.ee_dma_write(0x1000E000u,1);
    iop.ee_dma_write(0x1000C020u,0);iop.ee_dma_write(0x1000C000u,0x184u);
    for(unsigned cycle=1;cycle<300 && !iop.ee_dma_read(0x1000C020u);++cycle)iop.run_until(cycle);
    require(iop.ee_dma_read(0x1000C020u)!=0,"receiver has consumed a real tag and awaits payload");
    rejects([&]{iop.ee_dma_write(0x1000C000u,0);},"write while busy");
    iop.set_ee_dma_suspended(true);iop.ee_dma_write(0x1000C000u,0);
    require(!(iop.ee_dma_read(0x1000E010u)&0x20u),"suspended active receiver stops without manufacturing successful completion");
}
void idle_iop_receiver_stop() {
    Iop idle;idle.reset(receiver(1u<<19,1u<<23,true),nullptr);idle.run_until(400);
    require(!idle.stopped() && !idle.ee_read(0x80u,4) && !(idle.ee_read(0x84u,4)&(1u<<27)) && !idle.ee_read(0x88u,4),
            "IOP disarms an empty SIF1 receiver without a fabricated completion flag or interrupt");
    auto ram=packet();Iop active;active.reset(receiver(1u<<19,1u<<23,true),nullptr);arm(active,ram,1);active.run_until(400);
    require(active.stopped() && active.stop_reason()->find("IOP SIF1 register write while busy")!=std::string::npos &&
            active.stop_reason()->find("remaining=4")!=std::string::npos,
            "IOP cannot silently stop a tag whose actual payload is still incomplete");
    require(!active.sif1_metrics().bytes && !completions(active),"refused payload abort cannot manufacture bytes or destination completion");
}
}
int main() try {
    transfer_masks();incomplete_packet();source_chain();next_source_chain();refused_packets();block_control_halfwords();partial_sif0_packets();idle_sif0_receiver_stop();idle_iop_receiver_stop();
    std::cout<<"SIF1 source chains, exact payload, END/IRQ masks and incomplete packets passed\n";
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
