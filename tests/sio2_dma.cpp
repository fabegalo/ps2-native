#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void put(m20::Assembler& a,std::uint32_t at,std::uint32_t value){a.la(8,at);a.la(9,value);a.sw(9,0,8);}
std::vector<std::uint8_t> image(m20::Assembler& a) {
    const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
    std::copy(code.begin(),code.end(),rom.begin());return rom;
}
void idle(m20::Assembler& a){a.label("idle");a.beq(0,0,"idle");a.nop();}
void input_fifo() {
    m20::Assembler a(kEeResetVector);put(a,0xBF801570,0x80000);put(a,0xBF801578,1);
    put(a,0xBF801574,1u<<20);put(a,0xBF8010F4,1u<<23);put(a,0xBF80157C,1);
    put(a,0xBF801540,0x400);put(a,0xBF801544,0x00030024);put(a,0xBF801548,0x01000201);
    a.label("armed");for(unsigned n=0;n<300;++n)a.nop();
    a.la(8,0xBF801540);a.la(10,0xA0000700);
    for(unsigned n=0;n<3;++n){a.lw(9,n*4u,8);a.nop();a.sw(9,n*4u,10);}
    idle(a);const auto rom=image(a);Iop iop;iop.reset(rom,nullptr);iop.diagnostic_sio2_dma_word_cycles(2);
    for(unsigned n=0;n<432;++n)iop.ee_write(0x400+n,1,(n*13u+7u)&0xFFu);
    iop.run_until((a.at("armed")-kEeResetVector)/4u);
    require(iop.sio2_dma_words(0)==0,"start store cannot transfer data before its next instruction boundary");
    iop.run_until(1000);
    require(!iop.stopped() && iop.sio2_dma_words(0)==64 && !iop.sio2_dma_words(1),"full 256-byte input FIFO backpressures the real producer");
    for(unsigned n=0;n<256;++n)require(iop.sio2_input_byte(n)==((n*13u+7u)&0xFFu),"FIFO retains each real source byte, including unclocked data");
    require(!iop.sio2_input_byte(256),"bounded FIFO has no fabricated extra byte");
    require(iop.ee_read(0x700,4)==0x500 && iop.ee_read(0x704,4)==0x00020024 && iop.ee_read(0x708,4)==0x01000201,
            "FIFO backpressure preserves remaining block count and STR without a false completion");
    rejects([&]{iop.diagnostic_sio2_dma_word_cycles(1);},"before guest execution");
}
void flow(bool dma_gate,bool channel_gate,bool irq_mask) {
    m20::Assembler a(kEeResetVector);put(a,0xBF801570,channel_gate ? 0x880000 : 0);put(a,0xBF801578,dma_gate ? 1 : 0);
    put(a,0xBF801574,irq_mask ? 3u<<20 : 0);put(a,0xBF8010F4,1u<<23);put(a,0xBF80157C,1);
    put(a,0xBF801070,0);put(a,0xBF808268,0x3BC);
    put(a,0xBF808200,(8u<<18)|(8u<<8)|0x70);put(a,0xBF808204,0);
    put(a,0xBF801540,0x400);put(a,0xBF801544,0x00010002);put(a,0xBF801548,irq_mask ? 0x41000201 : 0x01000201);
    a.label("armed");
    a.la(8,0xBF801548);a.label("wait");a.lw(9,0,8);a.nop();a.lui(10,0x0100);a.r(9,10,9,0,0x24);a.bne(9,0,"wait");a.nop();
    put(a,0xBF801550,0x500);put(a,0xBF801554,0x00010002);put(a,0xBF801558,irq_mask ? 0x41000200 : 0x01000200);
    put(a,0xBF808268,0xB1);a.label("started");
    for(unsigned n=0;n<100;++n)a.nop();
    a.la(10,0xA0000600);a.la(8,0xBF801540);a.lw(9,0,8);a.nop();a.sw(9,0,10);
    a.lw(9,4,8);a.nop();a.sw(9,4,10);a.lw(9,8,8);a.nop();a.sw(9,8,10);
    a.la(8,0xBF801550);a.lw(9,0,8);a.nop();a.sw(9,12,10);a.lw(9,4,8);a.nop();a.sw(9,16,10);
    a.lw(9,8,8);a.nop();a.sw(9,20,10);
    a.la(8,0xBF801574);a.lw(9,0,8);a.nop();a.sw(9,24,10);a.la(8,0xBF801070);a.lw(9,0,8);a.nop();a.andi(9,9,8);a.sw(9,28,10);
    idle(a);const auto rom=image(a);Iop iop;iop.reset(rom,nullptr);iop.diagnostic_sio2_dma_word_cycles(4);iop.diagnostic_sio2_infrared_timeout(32);
    iop.ee_write(0x400,4,0x03020461);iop.ee_write(0x404,4,0x07060504);iop.ee_write(0x4FF,1,0xA5);iop.ee_write(0x508,1,0x5A);
    if(!dma_gate || !channel_gate) {
        iop.run_until(500);require(!iop.stopped() && !iop.sio2_dma_words(0) && !iop.sio2_dma_words(1),"disabled global/channel gate keeps source pending and destination untouched");return;
    }
    const auto due=(a.at("started")-kEeResetVector)/4u-1u+32u;
    iop.run_until(due+1u);
    require(!iop.stopped() && iop.sio2_dma_words(0)==2 && !iop.sio2_dma_words(1),"RX DMA cannot complete before real serial output reaches the FIFO");
    iop.run_until(700);require(!iop.stopped(),iop.stop_reason().value_or("running"));
    require(iop.sio2_dma_words(0)==2 && iop.sio2_dma_words(1)==2 && iop.ee_read(0x500,4)==~0u && iop.ee_read(0x504,4)==~0u,
            "known RAM input reaches the serial consumer and only real absent-wire bytes reach output RAM");
    require(iop.ee_read(0x4FF,1)==0xA5 && iop.ee_read(0x508,1)==0x5A,"DMA preserves both destination neighbors");
    require(iop.ee_read(0x600,4)==0x408 && iop.ee_read(0x604,4)==2 && iop.ee_read(0x608,4)==(irq_mask ? 0x40000201u : 0x201u) &&
            iop.ee_read(0x60C,4)==0x508 && iop.ee_read(0x610,4)==2 && iop.ee_read(0x614,4)==(irq_mask ? 0x40000200u : 0x200u),
            "MADR, BCR block count and CHCR STR reflect actual words and completion on both channels");
    require((iop.ee_read(0x618,4)&0x30000000u)==(irq_mask ? 0x30000000u : 0u) && iop.ee_read(0x61C,4)==(irq_mask ? 8u : 0u),
            "DICR2 channel masks gate completion flags and actual INTC DMA delivery");
    iop.reset(rom,nullptr);iop.run_until(700);require(iop.stopped() && iop.stop_reason()->find("explicit word-cycle timing")!=std::string::npos,
            "reset removes unmeasured DMA and serial timing instead of silently enabling an experiment");
}

void unplugged_padded(bool enabled) {
    m20::Assembler a(kEeResetVector);put(a,0xBF801570,0x880000);put(a,0xBF801578,1);
    put(a,0xBF801070,0);put(a,0xBF808268,0x3BC);
    put(a,0xBF808200,(4u<<18)|(4u<<8)|0x72);put(a,0xBF808204,0);
    put(a,0xBF801540,0x400);put(a,0xBF801544,0x00010024);put(a,0xBF801548,0x01000201);
    a.la(8,0xBF801548);a.label("drain");a.lw(9,0,8);a.nop();a.lui(10,0x100);a.r(9,10,9,0,0x24);
    a.bne(9,0,"drain");a.nop();
    put(a,0xBF801550,0x800);put(a,0xBF801554,0x00010024);put(a,0xBF801558,0x41000200);
    put(a,0xBF808268,0x3B1);idle(a);
    const auto rom=image(a);Iop iop;iop.reset(rom,nullptr);iop.diagnostic_sio2_dma_word_cycles(1);
    iop.diagnostic_sio2_unplugged_ports(enabled ? 4u : 1u,32);
    for(unsigned n=0;n<144;++n)iop.ee_write(0x400+n,1,n ? n==1 ? 0x11 : 0 : 0x81);
    iop.ee_write(0x7FF,1,0xA5);iop.ee_write(0x890,1,0x5A);iop.run_until(700);
    if(!enabled) {
        require(iop.stopped() && iop.stop_reason()->find("port is not established")!=std::string::npos &&
                !iop.sio2_dma_words(1),"a different unplugged-port mask cannot silently invent this Memory Card's absence");return;
    }
    require(!iop.stopped() && iop.sio2_dma_words(0)==36 && iop.sio2_dma_words(1)==36,
            "both channels transfer the entire real 0x90-byte descriptor slice");
    require(iop.ee_read(0x800,4)==~0u && iop.ee_read(0x7FF,1)==0xA5 && iop.ee_read(0x890,1)==0x5A,
            "only clocked absent-wire bytes are known and both DMA destination bounds remain intact");
    for(unsigned n=4;n<144;++n)rejects([&]{(void)iop.ee_read(0x800+n,1);},"has not been written");
    iop.reset(rom,nullptr);iop.diagnostic_sio2_dma_word_cycles(1);iop.diagnostic_sio2_infrared_timeout(32);
    for(unsigned n=0;n<144;++n)iop.ee_write(0x400+n,1,n ? 0 : 0x81);
    iop.run_until(700);require(iop.stopped() && iop.stop_reason()->find("port is not established")!=std::string::npos,
            "reset removes the Memory Card absence hypothesis even when a separate IR experiment is enabled");
}


void late_irq_enable() {
    m20::Assembler a(kEeResetVector);put(a,0xBF801070,0);put(a,0xBF808268,0xB0);
    put(a,0xBF808200,(1u<<18)|(1u<<8)|0x40);put(a,0xBF808204,0);
    a.la(8,0xBF808260);a.la(9,0x61);a.sb(9,0,8);put(a,0xBF808268,0xB1);
    for(unsigned n=0;n<20;++n)a.nop();
    const auto capture=[&](unsigned offset) {
        a.la(8,0xBF801070);a.lw(9,0,8);a.nop();a.la(11,1u<<17);a.r(9,11,9,0,0x24);
        a.la(10,0xA0000100);a.sw(9,offset,10);
    };
    capture(0);put(a,0xBF808268,0x3B0);capture(4);
    put(a,0xBF801070,~(1u<<17));put(a,0xBF808268,0x3B0);capture(8);
    put(a,0xBF808268,0xB0);put(a,0xBF808268,0x3B0);capture(12);
    put(a,0xBF808280,1);put(a,0xBF801070,~(1u<<17));put(a,0xBF808268,0x3B0);capture(16);
    idle(a);Iop iop;iop.reset(image(a),nullptr);iop.diagnostic_sio2_infrared_timeout(8);iop.run_until(500);
    require(!iop.stopped(),iop.stop_reason().value_or("running"));
    require(iop.ee_read(0x100,4)==0 && iop.ee_read(0x104,4)==(1u<<17),
            "enabling completion IRQ delivers an already pending SIO2 source to INTC");
    require(iop.ee_read(0x108,4)==0 && iop.ee_read(0x10C,4)==(1u<<17) && iop.ee_read(0x110,4)==0,
            "INTC acknowledgement alone creates no source edge; mask re-enable does, while SIO2 W1C removes the source");
}


void reset_completion(bool experiment) {
    m20::Assembler a(kEeResetVector);put(a,0xBF801070,0);put(a,0xBF808268,0x3B0);
    put(a,0xBF808200,(1u<<18)|(1u<<8)|0x40);put(a,0xBF808204,0);
    a.la(8,0xBF808260);a.la(9,0x61);a.sb(9,0,8);put(a,0xBF808268,0x3B1);
    for(unsigned n=0;n<20;++n)a.nop();put(a,0xBF808268,0xC);
    a.la(10,0xA0000100);a.la(8,0xBF808280);a.lw(9,0,8);a.nop();a.sw(9,0,10);
    const auto capture=[&](unsigned offset) {
        a.la(8,0xBF801070);a.lw(9,0,8);a.nop();a.la(11,1u<<17);a.r(9,11,9,0,0x24);a.sw(9,offset,10);
    };
    capture(4);put(a,0xBF801070,~(1u<<17));put(a,0xBF808268,0x3B0);capture(8);idle(a);
    Iop iop;iop.reset(image(a),nullptr);iop.diagnostic_sio2_infrared_timeout(8);
    if(experiment)iop.diagnostic_sio2_reset_clears_completion();iop.run_until(500);
    if(!experiment) {
        require(iop.stopped() && iop.stop_reason()->find("reset effect on pending completion is unverified")!=std::string::npos,
                "an unmeasured reset transition cannot silently retain or clear a pending device source");return;
    }
    require(!iop.stopped() && iop.ee_read(0x100,4)==0 && iop.ee_read(0x104,4)==(1u<<17) && !iop.ee_read(0x108,4),
            "explicit reset-latch hypothesis clears only the device source, independently of already latched INTC status");
    rejects([&]{iop.diagnostic_sio2_reset_clears_completion();},"before execution");
}

void bad_input(unsigned which) {
    m20::Assembler a(kEeResetVector);put(a,0xBF801570,0x80000);put(a,0xBF801578,1);
    put(a,0xBF801540,which==1 ? 0x1FFFFC : 0x400);put(a,0xBF801544,0x10002);
    put(a,0xBF801548,which==2 ? 0x01000200 : 0x01000201);idle(a);
    Iop iop;iop.reset(image(a),nullptr);iop.diagnostic_sio2_dma_word_cycles(1);
    iop.ee_write(0x400,4,0x12345678);iop.run_until(400);
    require(iop.stopped() && iop.sio2_dma_words(0)==(which ? 0u : 1u),"unknown source/range/wrong direction never manufactures remaining bytes or completion");
}
}
int main() try {reset_completion(false);reset_completion(true);late_irq_enable();input_fifo();for(bool mask:{false,true})flow(true,true,mask);flow(false,true,false);flow(true,false,false);
    unplugged_padded(true);unplugged_padded(false);for(unsigned n=0;n<3;++n)bad_input(n);std::cout<<"SIO2 real FIFO DMA, gates, bounded transfers, backpressure and completion IRQs passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
