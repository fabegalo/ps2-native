#include "direct_program.hpp"
#include "boot_checks.hpp"
#include "ps2native/boot.hpp"
#include <iostream>

namespace {
using namespace ps2native;
using namespace boot_checks;
using namespace m20;
// Original IOP program configures the real channel and supplies its own tags.
std::vector<std::uint8_t> sender(unsigned words=64,unsigned destination=0x10000u,bool undefined=false) {
    return direct_test::iop_sender(words,destination,undefined);
}
void arm(Iop& iop) {
    iop.ee_dma_write(0x1000E020u,0);
    iop.ee_dma_write(0x1000E000u,1);
    iop.ee_dma_write(0x1000C020u,0);
    iop.ee_dma_write(0x1000C000u,0x184u);
}
void transfer() {
    std::vector<std::uint8_t> ram(0x20000,0xAA);
    std::unordered_map<std::uint32_t,std::uint32_t> unknown{{0x10000u,0xFFFFFFFFu},{0x10100u,1u}};
    Iop iop;iop.reset({},nullptr);iop.load_program(sender(),0x1000,0x1000);iop.attach_ee_ram(ram);
    iop.attach_ee_ram(ram,&unknown);
    arm(iop);iop.run_until(1800);
    require(!iop.stopped(),"source/destination execute: "+iop.stop_reason().value_or(""));
    require(iop.sif0_metrics().bytes==256,"real DMA payload length");
    for(unsigned w=0;w<64;++w) {
        const auto p=0x10000u+w*4u;
        const auto value=std::uint32_t{ram[p]}|(std::uint32_t{ram[p+1]}<<8u)|
            (std::uint32_t{ram[p+2]}<<16u)|(std::uint32_t{ram[p+3]}<<24u);
        require(value==0xCA000000u+w,"IOP payload copied in order");
    }
    require(ram[0xFFFF]==0xAA && ram[0x10100]==0xAA,"destination bounds preserved");
    require(!unknown.contains(0x10000u) && unknown.contains(0x10100u),"DMA defines only the overwritten words in symbolic RAM");
    require(!(iop.ee_dma_read(0x1000C000u)&0x100u) && iop.ee_dma_read(0x1000C010u)==0x10100,
            "MADR/QWC advance and chain completion clears STR");
    require((iop.ee_dma_read(0x1000E010u)&0x20u) && !iop.ee_dma_interrupt_pending(),"status independent of interrupt mask");
    iop.ee_dma_write(0x1000E010u,1u<<21u);
    require(iop.ee_dma_interrupt_pending(),"pending channel completion asserts DMAC INT1 when unmasked");
    iop.ee_dma_write(0x1000E010u,0x20u);
    require(!iop.ee_dma_interrupt_pending(),"W1C acknowledges interrupt");
}
void backpressure() {
    std::vector<std::uint8_t> ram(0x20000,0xAA);Iop iop;
    iop.reset({},nullptr);iop.load_program(sender(),0x1000,0x1000);iop.attach_ee_ram(ram);
    iop.run_until(1800);
    require(!iop.stopped() && iop.sif0_metrics().peak_fifo_words==32 &&
            iop.sif0_metrics().source_full_cycles>0 && iop.sif0_metrics().bytes==0,
            "full FIFO stalls producer until receiver starts");
    arm(iop);iop.run_until(2200);
    require(!iop.stopped() && iop.sif0_metrics().bytes==256,"stalled source resumes without dropping bytes");
    Iop empty;empty.reset({},nullptr);empty.load_program(sender(),0x1000,0x1000);empty.attach_ee_ram(ram);
    arm(empty);empty.ee_dma_write(0x1000E000u,0);empty.run_until(1800);
    require((empty.ee_dma_read(0x1000C000u)&0x100u) && empty.sif0_metrics().bytes==0,"DMAE disabled retains armed receiver");
    empty.ee_dma_write(0x1000E000u,1);empty.run_until(2200);
    require(empty.sif0_metrics().bytes==256,"DMAE reenable drains buffered data");
}
void refusal() {
    for(unsigned bad=0;bad<3;++bad) {
        std::vector<std::uint8_t> ram(0x20000,0xAA);Iop iop;iop.reset({},nullptr);
        iop.load_program(sender(bad==0?1u:64u,bad==1?0x30000u:0x10000u,bad==2),0x1000,0x1000);
        iop.attach_ee_ram(ram);arm(iop);iop.run_until(1800);
        require(iop.stopped() && iop.sif0_metrics().bytes==0,"unknown retained lanes/outside/undefined source fails before payload");
        require(iop.ee_dma_read(0x1000C000u)&0x100u,"failure cannot manufacture successful EE completion");
    }
}

void opaque_payload() {
    std::vector<std::uint8_t> ram(0x20000,0xAA);
    std::unordered_map<std::uint32_t,std::uint32_t> unknown{{0x10000,~0u},{0x10100,1u}};
    Iop iop;iop.reset({},nullptr);iop.load_program(sender(64,0x10000,true),0x1000,0x1000);
    for(unsigned n=0;n<256;++n)if(n!=5 && n!=13 && n!=254)iop.ee_write(0x5000+n,1,(n+0x35u)&0xFFu);
    iop.attach_ee_ram(ram,&unknown);iop.run_until(1800);
    require(!iop.stopped() && iop.sif0_metrics().peak_fifo_words==32 && !iop.sif0_metrics().bytes &&
            ram[0x10000]==0xAA && unknown.at(0x10000)==~0u,"opaque payload stalls in the bounded FIFO without prematurely changing EE RAM");
    arm(iop);iop.run_until(2200);
    require(!iop.stopped() && iop.sif0_metrics().bytes==256,"SIF0 transports opaque bytes without decoding or fabricating an RPC reply");
    for(unsigned w=0;w<64;++w) {
        std::uint32_t expected=0;
        for(unsigned b=0;b<4;++b) {
            const auto n=w*4u+b;
            if(n==5 || n==13 || n==254)expected|=0xFFu<<(b*8u);
            else require(ram[0x10000+n]==((n+0x35u)&0xFFu),"all independently known payload bytes survive FIFO backpressure");
        }
        const auto it=unknown.find(0x10000+w*4u);
        require(expected ? it!=unknown.end() && it->second==expected : it==unknown.end(),
                "destination metadata describes exactly the unknown source byte lanes");
    }
    require(unknown.at(0x10100)==1 && ram[0xFFFF]==0xAA && ram[0x10100]==0xAA,
            "payload defines no byte or knowledge beyond the real transfer");
    require(!(iop.ee_dma_read(0x1000C000)&0x100) && (iop.ee_dma_read(0x1000E010)&0x20),
            "actual payload completion, independent of data knowledge, drives the EE DMA interrupt flag");
}
void opaque_retained_lanes() {
    std::vector<std::uint8_t> ram(0x20000,0xAA);
    std::unordered_map<std::uint32_t,std::uint32_t> unknown;
    Iop iop;iop.reset({},nullptr);iop.load_program(sender(1),0x1000,0x1000);
    iop.attach_ee_ram(ram,&unknown);arm(iop);iop.run_until(1800);
    require(!iop.stopped() && iop.sif0_metrics().bytes==16 && ram[0x10003]==0xCA &&
            !unknown.contains(0x10000) && !unknown.contains(0x10004) &&
            unknown.at(0x10008)==~0u && unknown.at(0x1000C)==~0u,
            "unobserved retained bus lanes remain opaque while supplied payload and destination-tag lanes remain known");
}
void opaque_tags_refuse() {
    using direct_test::store;
    for(unsigned missing:{0u,4u,8u,12u}) {
        Assembler a(0x1000);store(a,0xBF801570,0x800);store(a,0xBF801578,1);store(a,0xBF80157C,1);
        store(a,0xBF80152C,0x4000);store(a,0xBF801528,0x01000701);
        a.label("idle");a.beq(zero,zero,"idle");a.nop();
        std::vector<std::uint8_t> ram(0x20000,0xAA);std::unordered_map<std::uint32_t,std::uint32_t> unknown;
        Iop iop;iop.reset({},nullptr);iop.load_program(le_words(a.finish()),0x1000,0x1000);
        const std::uint32_t tag[]{0x80005000u,4,0x70000001u,0x10000};
        for(unsigned n=0;n<16;++n)if(n!=missing)iop.ee_write(0x4000+n,1,(tag[n/4]>>(8*(n%4)))&0xFFu);
        for(unsigned n=0;n<4;++n)iop.ee_write(0x5000+n*4,4,0x12345678);
        iop.attach_ee_ram(ram,&unknown);arm(iop);iop.run_until(600);
        require(iop.stopped() && iop.stop_reason()->find("SIF0 tag consumes undefined")!=std::string::npos &&
                !iop.sif0_metrics().bytes && (iop.ee_dma_read(0x1000C000)&0x100) && ram[0x10000]==0xAA,
                "unknown IOP source or EE destination tag fields cannot become guessed addresses, counts or completion");
    }
}

void suspension() {
    std::vector<std::uint8_t> ram(0x20000,0xAA); Iop iop;
    iop.reset({},nullptr);iop.load_program(sender(),0x1000,0x1000);iop.attach_ee_ram(ram);
    arm(iop);iop.set_ee_dma_suspended(true);iop.run_until(1800);
    require(iop.sif0_metrics().bytes==0 && ram[0x10000]==0xAA &&
            (iop.ee_dma_read(0x1000C000u)&0x100u),"CPND keeps EE destination busy with unchanged RAM");
    iop.set_ee_dma_suspended(false);iop.run_until(2200);
    require(iop.sif0_metrics().bytes==256,"CPND resume drains the real buffered IOP payload");
    ram.assign(0x20000,0); direct_test::put(ram,0,0x80004000u);direct_test::put(ram,4,64);
    for(unsigned i=0;i<64;++i)direct_test::put(ram,16+i*4,0x01020300u+i);
    iop.reset({},nullptr);iop.load_program(direct_test::iop_receiver(),0x1000,0x1000);iop.attach_ee_ram(ram);
    iop.ee_dma_write(0x1000E020u,0);iop.ee_dma_write(0x1000E000u,1);
    iop.ee_dma_write(0x1000C410u,0);iop.ee_dma_write(0x1000C420u,17);iop.ee_dma_write(0x1000C400u,0x101);
    iop.set_ee_dma_suspended(true);iop.run_until(1800);
    require(iop.sif1_metrics().bytes==0 && iop.ee_dma_read(0x1000C420u)==17 &&
            !(iop.ee_dma_read(0x1000E010u)&0x40u),"CPND prevents source progress and false completion IRQ");
    iop.set_ee_dma_suspended(false);iop.run_until(2600);
    require(!iop.stopped() && iop.sif1_metrics().bytes==256 && iop.ee_read(0x4000,4)==0x01020300u,
            "CPND resumes SIF1 without dropping the first payload word");
}
void chain_source() {
    for(unsigned mode=0;mode<3;++mode) {
        std::vector<std::uint8_t> ram(0x4000,0);
        direct_test::put(ram,0x1000,mode==1 ? 0xB0000001u : 0x30000001u); // REF, optional tag IRQ
        direct_test::put(ram,0x1004,0x2000);
        direct_test::put(ram,0x1010,16);direct_test::put(ram,0x1014,mode==2 ? 0x4000 : 0x2100);
        direct_test::put(ram,0x2000,0x80004000u);direct_test::put(ram,0x2004,64);
        for(unsigned i=0;i<64;++i)direct_test::put(ram,0x2100+i*4,0x01020300u+i);
        Iop iop;iop.reset({},nullptr);iop.load_program(direct_test::iop_receiver(true,0x41000300u),0x1000,0x1000);
        // Reset PCE=0 permits DMA even with unspecified CDE/CPC bits.
        iop.attach_ee_ram(ram);iop.ee_dma_write(0x1000E000,1);
        iop.ee_dma_write(0x1000C420,0);iop.ee_dma_write(0x1000C430,0x1000);iop.ee_dma_write(0x1000C400,0x184);
        iop.run_until(2000);
        if(mode==0) {
            require(!iop.stopped() && iop.sif1_metrics().bytes==256,"REF then REFE feed the real SIF1 FIFO");
            require(iop.ee_read(0x4000,4)==0x01020300u && iop.ee_read(0x40FC,4)==0x0102033Fu,"chain preserves first and last payload");
            require(iop.ee_dma_read(0x1000C430)==0x1020 && iop.ee_dma_read(0x1000C410)==0x2200 &&
                    (iop.ee_dma_read(0x1000E010)&0x40),"chain addresses and source completion reflect consumed tags");
        } else if(mode==1) {
            require(!iop.stopped() && iop.sif1_metrics().bytes==0 && iop.ee_dma_read(0x1000C430)==0x1010 &&
                    (iop.ee_dma_read(0x1000E010)&0x40),"TIE stops at IRQ REF after its qword, without inventing remaining payload");
        } else {
            require(iop.stopped() && iop.sif1_metrics().bytes==0 && !(iop.ee_dma_read(0x1000E010)&0x40),
                    "out-of-range reference refuses before payload or successful completion");
        }
    }
}
void irq_entry() {
    Memory memory;EEState state;
    prepare_direct_elf(parse_ee_elf(direct_test::elf(direct_test::cpu())),nullptr,state,memory);
    memory.load_iop_program(sender(),0x1000,0x1000);
    state.pc=0x80002000;state.retired_instructions=0;
    state.set_cp0(Cop0::Status,kStatusIE|kStatusEIE|kStatusIM3);
    memory.write32(0xB000E020,0);memory.write32(0xB000E000,1);
    memory.write32(0xB000E010,1u<<21u);memory.write32(0xB000C020,0);memory.write32(0xB000C000,0x184);
    state.retired_instructions=16000;
    require(poll_ee_interrupts(state,memory) && state.pc==0x80000200 &&
            (state.cp0(Cop0::Cause)&kStatusIM3) && state.cp0(Cop0::Epc)==0x80002000,
            "SIF0 completion enters architectural EE interrupt vector through IP3");
}
void source_tag_irq() {
    using direct_test::store;
    for(unsigned enabled=0;enabled<2;++enabled) {
        Assembler a(0x1000u);
        store(a,0xBF801570,0x800);store(a,0xBF801578,1);store(a,0xBF80157C,1);
        store(a,0xBF8010F4,1u<<23);store(a,0xBF801574,enabled<<9);
        store(a,0x4000,0x00005000);store(a,0x4004,8);
        store(a,0x4008,0x10000002);store(a,0x400C,0x10000);
        store(a,0x4010,0xC0005020);store(a,0x4014,8);
        store(a,0x4018,0x70000002);store(a,0x401C,0x10020);
        for(unsigned w=0;w<16;++w)store(a,0x5000+w*4,0xAB000000u+w);
        store(a,0xBF80152C,0x4000);store(a,0xBF801528,0x01000701);
        a.label("observe");a.la(t0,0xBF801574);a.lw(t1,0,t0);a.nop();a.la(t0,0x6000);a.sw(t1,0,t0);
        a.la(t0,0xBF801070);a.lw(t1,0,t0);a.nop();a.andi(t1,t1,8);a.la(t0,0x6004);a.sw(t1,0,t0);
        a.beq(zero,zero,"observe");a.nop();
        std::vector<std::uint8_t> ram(0x20000,0xAA);Iop iop;iop.reset({},nullptr);
        iop.load_program(le_words(a.finish()),0x1000,0x1000);iop.attach_ee_ram(ram);arm(iop);iop.run_until(1000);
        require(!iop.stopped() && iop.sif0_metrics().bytes==64,"SIF0 delivers the chain ending in END+IRQ");
        require(ram[0x10000]==0 && ram[0x1003C]==15 && ram[0x10040]==0xAA,"SIF0 exact payload and destination bounds");
        require((iop.ee_read(0x6000,4)&(1u<<26))==(enabled<<26) && iop.ee_read(0x6004,4)==enabled*8u,
                "SIF0 IQE reaches DICR2 and I_STAT despite masked completion");
    }
}
void late_master_enable() {
    using direct_test::store;
    for(unsigned gate:{0u,1u,3u}) for(bool acknowledge:{false,true}) {
        Assembler a(0x1000u);
        store(a,0xBF801570,0x8000);store(a,0xBF801578,1);store(a,0xBF80157C,gate);
        store(a,0xBF8010F4,0);store(a,0xBF801574,1u<<10);
        store(a,0xBF801538,0x41000300);
        a.la(t0,0xBF801538);a.label("busy");a.lw(t1,0,t0);a.nop();a.lui(t2,0x100);
        a.r(t1,t2,t1,0,0x24);a.bne(t1,zero,"busy");a.nop();
        const auto observe=[&](unsigned offset) {
            a.la(t0,0xBF801070);a.lw(t1,0,t0);a.nop();a.andi(t1,t1,8);
            a.la(t0,0x6000);a.sw(t1,static_cast<std::int16_t>(offset),t0);
        };
        observe(0);
        if(acknowledge)store(a,0xBF801574,(1u<<10)|(1u<<27));
        store(a,0xBF8010F4,1u<<23);observe(4);
        store(a,0xBF801070,0xFFFFFFF7);
        store(a,0xBF8010F4,1u<<23);observe(8);
        store(a,0xBF8010F4,0);store(a,0xBF8010F4,1u<<23);observe(12);
        a.la(t0,0xBF8010F4);a.lw(t1,0,t0);a.nop();a.la(t0,0x6000);a.sw(t1,16,t0);
        a.label("idle");a.beq(zero,zero,"idle");a.nop();
        std::vector<std::uint8_t> ram(0x2000,0);
        direct_test::put(ram,0,0xC0004000);direct_test::put(ram,4,4);
        direct_test::put(ram,16,0x12345678);
        Iop iop;iop.reset({},nullptr);iop.load_program(le_words(a.finish()),0x1000,0x1000);
        iop.attach_ee_ram(ram);iop.ee_dma_write(0x1000E000,1);
        iop.ee_dma_write(0x1000C410,0);iop.ee_dma_write(0x1000C420,2);iop.ee_dma_write(0x1000C400,0x101);
        iop.run_until(600);
        require(!iop.stopped() && iop.ee_read(0x4000,4)==0x12345678,"late master enable preserves real payload");
        const auto irq=gate==1 && !acknowledge ? 8u : 0u;
        require(iop.ee_read(0x6000,4)==0 && iop.ee_read(0x6004,4)==irq,
                "enabling master delivers a pending tag IRQ, respecting acknowledgement and DMACINTEN");
        require(iop.ee_read(0x6008,4)==0 && iop.ee_read(0x600C,4)==irq,
                "I_STAT acknowledge does not retrigger without a new master edge");
        require(bool(iop.ee_read(0x6010,4)&0x80000000u)==bool((gate&1u) && !acknowledge),
                "DMACINTEN bit zero gates master flag while bit one gates only INTC delivery");
    }
}
void stop_idle_receiver() {
    using direct_test::store;
    for(bool pending:{false,true}) {
        Assembler a(0x1000u);
        store(a,0xBF801570,0x8000);store(a,0xBF801578,1);store(a,0xBF80157C,1);
        store(a,0xBF8010F4,1u<<23);store(a,0xBF801574,1u<<19);
        store(a,0xBF801538,0x41000300);
        for(unsigned n=0;n<8;++n)a.nop();
        store(a,0xBF801538,0);
        a.la(t0,0xBF801538);a.lw(t1,0,t0);a.nop();a.la(t0,0x6000);a.sw(t1,0,t0);
        a.la(t0,0xBF801574);a.lw(t1,0,t0);a.nop();a.la(t0,0x6000);a.sw(t1,4,t0);
        store(a,0xBF801538,0x41000300);
        a.label("idle");a.beq(zero,zero,"idle");a.nop();
        std::vector<std::uint8_t> ram(0x2000,0);
        direct_test::put(ram,0,0x80004000);direct_test::put(ram,4,4);
        direct_test::put(ram,16,0x12345678);
        Iop iop;iop.reset({},nullptr);iop.load_program(le_words(a.finish()),0x1000,0x1000);
        iop.attach_ee_ram(ram);iop.ee_dma_write(0x1000E000,1);
        const auto send=[&](unsigned qwc) {
            iop.ee_dma_write(0x1000C410,0);iop.ee_dma_write(0x1000C420,qwc);iop.ee_dma_write(0x1000C400,0x101);
        };
        if(pending)send(1); // Tag received, waiting for its real payload.
        iop.run_until(200);
        if(pending)require(iop.stopped() && iop.stop_reason()->find("write while busy")!=std::string::npos &&
                           !iop.sif1_metrics().bytes,"stopping an incomplete payload remains refused");
        else {
            require(!iop.stopped() && !iop.ee_read(0x6000,4) && !(iop.ee_read(0x6004,4)&(1u<<27)),
                    "idle receiver stops without inventing a completion flag");
            send(2);iop.run_until(400);
            require(!iop.stopped() && iop.sif1_metrics().bytes==16 && iop.ee_read(0x4000,4)==0x12345678,
                    "rearmed receiver transfers the actual next packet");
        }
    }
}
void partial_source() {
    for(unsigned words:{1u,2u,3u,5u,6u,7u}) {
        std::vector<std::uint8_t> ram(0x20000,0xAA);Iop iop;iop.reset({},nullptr);
        iop.load_program(sender(words),0x1000,0x1000);
        iop.ee_write(0x4010,4,0xCAFEBABEu);iop.ee_write(0x4014,4,0x12345678u);
        iop.attach_ee_ram(ram);arm(iop);iop.run_until(1800);
        const auto rounded=(words+3u)/4u*4u;
        require(!iop.stopped() && iop.sif0_metrics().bytes==rounded*4u,"partial SIF0 packet fills exactly its EE qwords");
        for(unsigned w=0;w<rounded;++w) {
            const auto p=0x10000+w*4;
            const auto value=std::uint32_t{ram[p]}|(std::uint32_t{ram[p+1]}<<8)|
                             (std::uint32_t{ram[p+2]}<<16)|(std::uint32_t{ram[p+3]}<<24);
            const auto expected=w<words ? 0xCA000000u+w : words>4 ? 0xCA000000u+(w%4u) :
                                w==1 ? 0x10000u : w==2 ? 0xCAFEBABEu : 0x12345678u;
            require(value==expected,"partial packet preserves measured retained lane semantics, without reading past payload");
        }
        require(ram[0x10000+rounded*4]==0xAA,"partial qword leaves following EE bytes untouched");
    }
}
}
int main(){try{transfer();backpressure();refusal();opaque_payload();opaque_retained_lanes();opaque_tags_refuse();suspension();chain_source();source_tag_irq();late_master_enable();stop_idle_receiver();partial_source();irq_entry();return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
