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
        iop.load_program(sender(bad==0?6u:64u,bad==1?0x30000u:0x10000u,bad==2),0x1000,0x1000);
        iop.attach_ee_ram(ram);arm(iop);iop.run_until(1800);
        require(iop.stopped() && iop.sif0_metrics().bytes==0,"malformed/outside/undefined source fails before payload");
        require(iop.ee_dma_read(0x1000C000u)&0x100u,"failure cannot manufacture successful EE completion");
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
}
int main(){try{transfer();backpressure();refusal();irq_entry();return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
