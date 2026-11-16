#include "ps2native/engine/console.hpp"
#include "kzspu2.h"
#include <cstdio>
#include <stdexcept>
using ps2native::engine::Console;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    Console c;c.audio_output=false;
    c.iop_write(0xbf900f60,0x1234,2);check(c.iop_read(0xbf900760,2)==0x1234,"SPU2 write aliases use the mature core register decoder");
    c.iop_write(0xbf90019a,0x20,2);c.iop_write(0xbf9001a8,0,2);c.iop_write(0xbf9001aa,0x5000,2);
    for(unsigned i=0;i<32;++i)c.iop_write(0x1000+i*2,0x4000+i,2);
    const std::uint32_t irq[]={0x8c087300,0,0x25080001,0xac087300,0x03e00008,0x24020001};
    for(unsigned i=0;i<6;++i)c.iop_write(0x7000+i*4,irq[i],4);
    c.iop.gpr[4]=36;c.iop.gpr[5]=1;c.iop.gpr[6]=0x7000;c.iop.gpr[7]=0;c.iop_import("intrman",4);
    c.iop.gpr[4]=36;c.iop_import("intrman",6);c.iop_intr_enabled=false;
    c.iop_write(0xbf8010c0,0x1000,4);c.iop_write(0xbf8010c4,0x10010,4);c.iop_write(0xbf8010c8,0x1000201,4);
    c.advance_to(1024);check(!kzspu2DmaBusy(0) && !kzspu2GetStatus().dmaCount[0],"Disabled DMA channel cannot start or finish");
    c.iop_write(0xbf8010f0,0x80000,4);check(kzspu2DmaBusy(0) && !kzspu2GetStatus().dmaCount[0],"SPU2 DMA completion cannot be fabricated at submission");
    c.advance_to(32768);check(kzspu2GetStatus().dmaCount[0]==1 && c.iop_read(0x7300,4)==0,"Real DMA completes while a masked native IRQ remains pending");
    for(unsigned i=0;i<32;++i)check(kzspu2Memory()[0x5000+i]==0x4000+i,"SPU2 DMA must consume the actual shared IOP RAM");
    c.iop_intr_enabled=true;c.iop_next_tick=c.ticks+8;c.run_iop();
    check(c.iop_read(0x7300,4)==1 && c.pending_iop_irq()==~0u,"DMA completion executes its original IOP interrupt handler exactly once");
    c.run_iop();check(c.iop_read(0x7300,4)==1,"Acknowledged audio DMA IRQ cannot repeat without another transfer");
    c.iop_write(0xbf90059a,0x20,2);c.iop_write(0xbf9005a8,0,2);c.iop_write(0xbf9005aa,0x6000,2);
    c.iop.gpr[4]=40;c.iop.gpr[5]=1;c.iop.gpr[6]=0x7000;c.iop.gpr[7]=0;c.iop_import("intrman",4);
    c.iop.gpr[4]=40;c.iop_import("intrman",6);
    check(c.iop_read(0xbf801574,4)&0x800000,"Core 1 interrupt enable belongs to DICR2");
    c.iop_write(0xbf801500,0x1000,4);c.iop_write(0xbf801504,0x10010,4);c.iop_write(0xbf801508,0x1000201,4);
    c.iop_write(0xbf801570,8,4);c.run_iop();
    check(kzspu2GetStatus().dmaCount[1]==1 && c.iop_read(0x7300,4)==2,"Core 1 DMA uses the second DICR bank and virtual IRQ 40");
    for(unsigned i=0;i<32;++i)check(kzspu2Memory()[0x6000+i]==0x4000+i,"Both SPU2 cores consume shared IOP RAM");
    const auto now=c.ticks;c.reset_iop();check(c.ticks==now && kzspu2Memory()[0x5000]==0x4000,"Idle IOP reset must preserve physical SPU2 RAM and logical time");
    std::puts("engine-spu2: shared clock, real DMA, native masked IRQ and preserved SPU2 RAM PASS; headless device regression");
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
