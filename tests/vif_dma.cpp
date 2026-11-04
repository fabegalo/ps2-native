#include "boot_checks.hpp"
#include "ps2native/iop.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
struct Fixture {
    Iop dma;Vif v0{Vif::Unit::Vu0},v1;Gif gif;GsRegisters gs;
    std::vector<std::uint8_t> ram=std::vector<std::uint8_t>(4096);
    Fixture() {dma.reset({},nullptr);dma.attach_ee_ram(ram);gs.reset(0);}
    void word(unsigned at,std::uint32_t value) {for(unsigned b=0;b<4;++b)ram.at(at+b)=static_cast<std::uint8_t>(value>>(8u*b));}
    void tag(unsigned at,std::uint32_t header,unsigned address,std::uint32_t a=0,std::uint32_t b=0) {
        word(at,header);word(at+4,address);word(at+8,a);word(at+12,b);
    }
    void start(unsigned channel=1,unsigned chcr=0x145u,unsigned qwc=0,unsigned madr=0x300) {
        const auto base=channel==0 ? 0x10008000u : 0x10009000u;
        dma.ee_dma_write(0x1000E000u,1);dma.ee_dma_write(base+0x10,madr);
        dma.ee_dma_write(base+0x20,qwc);dma.ee_dma_write(base+0x30,0x100);
        dma.ee_dma_write(base,chcr);
    }
    void advance(unsigned cycles) {dma.advance_vif_dma(cycles,v0,v1,gif,gs);}
};
void empty_and_timing() {
    for(unsigned channel=0;channel<2;++channel) {
        Fixture f;f.tag(0x100,0x70000000u,0);f.start(channel);
        const auto base=channel==0 ? 0x10008000u : 0x10009000u;
        f.advance(1);require(f.dma.ee_dma_read(base)&0x100u,"END tag has not yet crossed the bus");
        f.advance(2);require(!(f.dma.ee_dma_read(base)&0x100u) &&
            (f.dma.ee_dma_read(0x1000E010u)&(1u<<channel)),"zero-payload END completes after actual tag fetch");
        require(f.dma.ee_dma_read(base+0x30)==0x100u,"END preserves tag address");
    }
    Fixture f;f.tag(0x100,0x70000000u,0);f.start();f.dma.ee_dma_write(0x1000E000u,0);
    f.advance(100);require(f.dma.ee_dma_read(0x10009000u)&0x100u,"DMAE gate pauses before tag");
    f.dma.ee_dma_write(0x1000E000u,1);f.advance(101);
    require(f.dma.ee_dma_read(0x10009000u)&0x100u,"disabled time does not accumulate");
    f.advance(102);require(!(f.dma.ee_dma_read(0x10009000u)&0x100u),"re-enabled bus fetch completes");
}
void tags_and_payload() {
    Fixture f;
    f.tag(0x100,0x10000001u,0,0,0x30000000u); // CNT; TTE begins STROW
    for(unsigned i=0;i<4;++i) f.word(0x110+i*4,0x12340000u+i);
    f.tag(0x120,0x70000000u,0,0,0x07005678u); // END; MARK
    f.start();f.advance(2);
    require(f.v1.processing() && f.dma.ee_dma_read(0x10009020u)==1 &&
            f.dma.ee_dma_read(0x10009030u)==0x110u,"tag halves reach VIF before payload");
    f.advance(4);
    for(unsigned i=0;i<4;++i)require(f.v1.read_register(0x100u+i*16)==0x12340000u+i,"STROW receives exactly four real payload words");
    require(f.dma.ee_dma_read(0x10009030u)==0x120u,"CNT advances TADR with payload consumption");
    f.advance(6);require(f.v1.read_register(0x30)==0x5678u,"next TTE command reaches VIF");
    Fixture refs;
    refs.tag(0x100,0x20000000u,0x200);refs.tag(0x200,0x30000001u,0x300);
    refs.word(0x300,0x07004321u);refs.tag(0x210,0,0x400); // REFE, zero payload
    refs.start(1,0x105);refs.advance(8);
    require(refs.v1.read_register(0x30)==0x4321 && refs.dma.ee_dma_read(0x10009030u)==0x220u &&
            !(refs.dma.ee_dma_read(0x10009000u)&0x100u),"NEXT/REF/REFE follow physical addresses and end chain");
    Fixture irq;irq.tag(0x100,0x90000001u,0);irq.word(0x110,0x07000099u);irq.start(1,0x185);
    irq.advance(2);require(irq.dma.ee_dma_read(0x10009000u)&0x100u,"TIE does not complete before payload");
    irq.advance(4);require(irq.dma.ee_dma_read(0x1000E010u)&2u,"IRQ/TIE completes after payload");
}
void direct_and_failures() {
    Fixture f;f.tag(0x100,0x70000002u,0,0x50000002u,0xFF000000u); // DIRECT with padding
    f.tag(0x110,0x8001u,0x10000000u,0xEu,0); // GIF packed A+D
    f.tag(0x120,0x12345678u,0xFFFFFFFFu,0x62u,0); // LABEL
    f.start();f.advance(4);require(f.gif.packets().empty(),"GIF still waits for its payload");
    f.advance(6);require(f.gif.packets().size()==1 && f.gs.read(kGsSiglblid+4,4)==0x12345678u,
        "TTE DIRECT delivers source data to real GIF PATH2 and GS");
    Fixture bad;bad.word(0x300,0x4B000001u);bad.start(1,0x101,1);
    rejects([&]{bad.advance(2);},"command unsupported");
    require(bad.dma.ee_dma_read(0x10009020u)==1 && !(bad.dma.ee_dma_read(0x1000E010u)&2u),"unmodeled VU command does not fake DMA completion");
    rejects([&]{bad.advance(20);},"command unsupported");
    Fixture unknown;std::unordered_map<std::uint32_t,std::uint32_t> masks{{0x108,1}};
    unknown.dma.attach_ee_ram(unknown.ram,&masks);unknown.tag(0x100,0x70000000u,0);unknown.start();
    rejects([&]{unknown.advance(2);},"unknown source bits");
    require(!(unknown.dma.ee_dma_read(0x1000E010u)&2u),"unknown TTE cannot create completion");
    Fixture overflow;overflow.tag(0x100,0x0000FFFFu,0x1000);overflow.start();
    rejects([&]{overflow.advance(2);},"payload outside");
}
void idle_flush_and_directhl() {
    Fixture f;
    f.tag(0x100,0x10000000u,0,0x11000000u,0); // CNT: FLUSH
    f.tag(0x110,0x30000002u,0x300,0,0x51000002u); // REF: DIRECTHL
    f.tag(0x120,0x70000000u,0);
    f.tag(0x300,0x8001u,0x10000000u,0xEu,0);
    f.tag(0x310,0x12345678u,0xFFFFFFFFu,0x62u,0);
    f.start();f.advance(2);
    require(f.gif.packets().empty() && (f.dma.ee_dma_read(0x10009000u)&0x100u),
            "idle FLUSH does not complete the remaining source chain");
    f.advance(4);require(f.v1.direct_remaining()==2,"DIRECTHL waits for both real payload qwords");
    f.advance(6);require(f.gif.packets().empty(),"partial DIRECTHL has no completed GIF packet");
    f.advance(10);require(f.gif.packets().size()==1 && f.gs.read(kGsSiglblid+4,4)==0x12345678u &&
                         !(f.dma.ee_dma_read(0x10009000u)&0x100u),"DIRECTHL reaches PATH2 and completes after END");

    Fixture paths;
    paths.gif.write_fifo(0x1000000000008001ull,0xEu,paths.gs);
    paths.v1.write_dma_tag(0x11000000u,paths.gif,paths.gs);
    require(((paths.gif.read_stat()>>10)&3u)==3u,"FLUSH does not discard or wait for PATH3");
    rejects([&]{paths.v1.write_dma_tag(0x51000001u,paths.gif,paths.gs);},"waits for GIF PATH3");
    paths.gif.write_fifo(7,0x62,paths.gs);
    paths.gif.write_path2(0x1000000000008001ull,0xEu,paths.gs);
    rejects([&]{paths.v1.write_dma_tag(0x11000000u,paths.gif,paths.gs);},"incomplete GIF PATH2");
    paths.v1.write_dma_tag(0x10ABCDEFu,paths.gif,paths.gs);
    require(((paths.gif.read_stat()>>10)&3u)==2u,"FLUSHE does not consume pending GIF work");
    paths.gif.write_path2(8,0x62,paths.gs);
    paths.v1.write_dma_tag(0x11000000u,paths.gif,paths.gs);
    paths.v0.write_dma_tag(0x10000000u,paths.gif,paths.gs);
    rejects([&]{paths.v0.write_dma_tag(0x11000000u,paths.gif,paths.gs);},"VIF1-only");
    rejects([&]{paths.v0.write_dma_tag(0x51000001u,paths.gif,paths.gs);},"VIF1-only");
}
}
int main() try {empty_and_timing();tags_and_payload();direct_and_failures();idle_flush_and_directhl();
    std::cout<<"VIF DMA normal/chain/TTE, source bytes, timing, IRQ and fail-closed paths passed\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
