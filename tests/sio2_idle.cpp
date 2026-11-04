// Inactive serial controller knowledge only; no invented peripheral replies.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void store(m20::Assembler& a,std::uint32_t address,std::uint32_t value) {
    a.la(8,address);a.la(9,value);a.sw(9,0,8);
}
Iop run(unsigned ending) {
    m20::Assembler a(kEeResetVector);
    if(ending!=6)store(a,0xBF801070u,0); // Establish the actual pending latch.
    store(a,0xBF808268u,0x3BCu);store(a,0xBF801074u,1u<<17);store(a,0xBF801078u,1);
    a.la(9,0x00400401u);a.mtc0(9,m20::Status);a.nop();
    a.la(8,0xBF801070u);a.lw(10,0,8);a.nop();a.la(11,1u<<17);a.r(10,11,10,0,0x24);
    a.bne(10,0,"failed");a.nop();
    if(ending==1)store(a,0xBF808268u,0x3BDu);
    else if(ending==2){a.la(8,0xBF808268u);a.lw(10,0,8);}
    else if(ending==3)store(a,0xBF808268u,0x100u);
    else if(ending==4)store(a,0xBF801548u,0x01000000u);
    else if(ending==5)store(a,0xBF808260u,0x81u);
    a.label("idle");a.beq(0,0,"idle");a.nop();
    a.label("failed");a.raw(0xFFFFFFFFu);
    const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
    std::copy(code.begin(),code.end(),rom.begin());Iop iop;iop.reset(rom,nullptr);iop.run_until(150);return iop;
}
}
int main() try {
    const auto idle=run(0);
    require(!idle.stopped() && idle.cycle()==150 && idle.gpr(10)==0,"enabled idle SIO2 IRQ cannot stop the CPU or invent an interrupt");
    const std::string causes[]={"","SIO2 transfer is not modeled","SIO2 control reset/status readback is not verified",
                                "SIO2 control configuration is not verified","DMA transfers are not modeled","device not implemented"};
    for(unsigned ending=1;ending<6;++ending){
        const auto iop=run(ending);
        require(iop.stopped() && iop.stop_reason()->find(causes[ending])!=std::string::npos,
                "unsupported serial activity stays explicit: "+iop.stop_reason().value_or("running"));
    }
    const auto cold=run(6);
    require(cold.stopped() && cold.stop_reason()->find("SIO2")!=std::string::npos,
            "unacknowledged cold pending state cannot be guessed clear");
    std::cout<<"SIO2 idle IRQ is known; unknown transfer, DMA, FIFO and readback fail explicitly\n";
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
