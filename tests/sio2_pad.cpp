// Authored digital-pad packets through the guest-visible serial FIFO and DMA.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void sw(m20::Assembler& a,std::uint32_t p,std::uint32_t v){a.la(8,p);a.la(9,v);a.sw(9,0,8);}
std::vector<std::uint8_t> rom(m20::Assembler& a){a.label("idle");a.beq(0,0,"idle");a.nop();
    const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> b(4u*1024u*1024u);std::copy(code.begin(),code.end(),b.begin());return b;}
void packet(m20::Assembler& a,unsigned port,unsigned size,unsigned command){
    sw(a,0xBF808200u+port*4u,(size<<18u)|(size<<8u)|0x40u|port);
    a.la(8,0xBF808260u);
    for(unsigned n=0;n<size;++n){a.la(9,n==0 ? 1u : n==1 ? command : 0u);a.sb(9,0,8);}
}
void fifo(unsigned ports,std::uint16_t buttons,unsigned cycles,unsigned bad=0u) {
    m20::Assembler a(kEeResetVector);sw(a,0xBF801070u,0);sw(a,0xBF801078u,0);sw(a,0xBF808268u,0x3BCu);
    for(unsigned p=0;p<ports;++p)packet(a,p,bad==2 ? 4u : 5u,bad==1 ? 0x43u : 0x42u);
    sw(a,0xBF808200u+ports*4u,0);sw(a,0xBF808268u,0x3B1u);a.label("started");
    a.la(8,0xBF80826Cu);a.lw(9,0,8);a.nop();a.andi(9,9,0x1000u);a.la(10,0xA0000200u);a.sw(9,0,10);
    for(unsigned n=0;n<128;++n)a.nop();
    a.la(8,0xBF808264u);a.la(10,0xA0000300u);
    for(unsigned n=0;n<5u*ports;++n){a.lbu(9,0,8);a.nop();a.sb(9,n,10);}
    a.la(8,0xBF80826Cu);a.lw(9,0,8);a.nop();a.sw(9,16,10);
    a.la(8,0xBF801070u);a.lw(9,0,8);a.nop();a.la(11,1u<<17u);a.r(9,11,9,0,0x24u);a.sw(9,20,10);
    const auto data=rom(a);Iop iop;iop.reset(data,nullptr);
    for(unsigned p=0;p<(bad==3 ? 1u : ports);++p)iop.diagnostic_sio2_digital_pad(p,buttons,cycles);
    const auto due=(a.at("started")-kEeResetVector)/4u-1u+cycles;
    iop.run_until(due-1u);
    if(bad){require(iop.stopped(),"unknown peripheral command, packet size or unconfigured port refuses");return;}
    require(!iop.stopped() && iop.ee_read(0x200u,4)==0u,"serial completion is not asserted before the explicit packet clock");
    rejects([&]{(void)iop.ee_read(0x300u,1);},"has not been written");
    iop.run_until(1000);require(!iop.stopped(),iop.stop_reason().value_or("running"));
    const unsigned expected[]={0xFFu,0x41u,0x5Au,unsigned((buttons|6u)&0xFFu),unsigned(buttons>>8u)};
    for(unsigned n=0;n<ports*5u;++n)require(iop.ee_read(0x300u+n,1)==expected[n%5u],"FIFO returns protocol ID and the configured active-low button bits");
    require(iop.ee_read(0x310u,4)==(ports==2u ? 0x1200u : 0x1100u) && (iop.ee_read(0x314u,4)&(1u<<17u)),
        "status counts the addressed ports and completion drives the serial IRQ");
    rejects([&]{iop.diagnostic_sio2_digital_pad(0,0,cycles);},"before execution");
    iop.reset(data,nullptr);iop.diagnostic_sio2_infrared_timeout(cycles);iop.run_until(1000);
    require(iop.stopped(),"reset never silently attaches a controller");
}
void dma() {
    m20::Assembler a(kEeResetVector);sw(a,0xBF801070u,0);sw(a,0xBF808268u,0x3BCu);
    sw(a,0xBF801570u,0x880000u);sw(a,0xBF801578u,1);sw(a,0xBF801574u,0);sw(a,0xBF8010F4u,0);sw(a,0xBF80157Cu,1);
    sw(a,0xBF808200u,(5u<<18u)|(5u<<8u)|0x70u);sw(a,0xBF808204u,0);
    sw(a,0xBF801540u,0x400);sw(a,0xBF801544u,0x10002);sw(a,0xBF801548u,0x01000201u);
    for(unsigned n=0;n<30;++n)a.nop();
    sw(a,0xBF801550u,0x500);sw(a,0xBF801554u,0x10002);sw(a,0xBF801558u,0x01000200u);sw(a,0xBF808268u,0x3B1u);
    const auto data=rom(a);Iop iop;iop.reset(data,nullptr);iop.diagnostic_sio2_digital_pad(0,0x1234u,32);iop.diagnostic_sio2_dma_word_cycles(4);
    iop.ee_write(0x400u,4,0x00004201u);iop.ee_write(0x404u,4,0);iop.run_until(1000);
    require(!iop.stopped() && iop.sio2_dma_words(0)==2u && iop.sio2_dma_words(1)==2u,"pad packet traverses both actual DMA channels");
    require(iop.ee_read(0x500u,4)==0x365A41FFu && iop.ee_read(0x504u,1)==0x12u,"DMA carries the same controller bytes as PIO");
    for(unsigned n=5;n<8;++n)rejects([&]{(void)iop.ee_read(0x500u+n,1);},"has not been written");
}
}
int main() try {
    for(unsigned ports=1;ports<=2;++ports)for(const auto buttons:{0xFFFFu,0xA55Au,0u})for(const auto cycles:{32u,64u})fifo(ports,static_cast<std::uint16_t>(buttons),cycles);
    for(unsigned bad=1;bad<=3;++bad)fifo(2,0xFFFFu,32,bad);
    Iop iop;iop.reset({},nullptr);rejects([&]{iop.diagnostic_sio2_digital_pad(2,0,32);},"port 0/1");
    rejects([&]{iop.diagnostic_sio2_digital_pad(0,0,0);},"positive packet");
    iop.diagnostic_sio2_digital_pad(0,0,32);rejects([&]{iop.diagnostic_sio2_unplugged_ports(1,32);},"conflicts");
    rejects([&]{iop.diagnostic_sio2_digital_pad(1,0,64);},"share");
    iop.reset({},nullptr);iop.diagnostic_sio2_unplugged_ports(1,32);rejects([&]{iop.diagnostic_sio2_digital_pad(0,0,32);},"unplugged");
    dma();std::cout<<"Digital pad serial payload, button state, timing, status and DMA passed\n";return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
