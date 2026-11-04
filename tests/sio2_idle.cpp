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
    if(ending!=8)store(a,0xBF808268u,0x3BCu);
    store(a,0xBF801074u,1u<<17);store(a,0xBF801078u,1);
    a.la(9,0x00400401u);a.mtc0(9,m20::Status);a.nop();
    a.la(8,0xBF801070u);a.lw(10,0,8);a.nop();a.la(11,1u<<17);a.r(10,11,10,0,0x24);
    a.bne(10,0,"failed");a.nop();
    if(ending==1)store(a,0xBF808268u,0x3BDu);
    else if(ending==2){a.la(8,0xBF808268u);a.lw(10,0,8);a.nop();a.beq(10,0,"idle");a.nop();}
    else if(ending==3)store(a,0xBF808268u,0x100u);
    else if(ending==4)store(a,0xBF801548u,0x01000000u);
    else if(ending==5)store(a,0xBF808260u,0x81u);
    else if(ending>=7) {
        a.la(8,0xBF808268u);
        if(ending==14){a.la(12,0);a.raw((0x26u<<26)|(8u<<21)|(12u<<16));} // LWR: no opaque LW proof.
        else a.lw(12,0,8);
        a.nop();
        if(ending==16 || ending==17) {
            a.addu(13,ending==16 ? 12u : 0u,ending==16 ? 0u : 12u);
            a.addu(12,13,0); // Copy back without consuming the opaque status.
        }
        if(ending==11) {
            a.la(8,0xA0000300u);a.lw(13,0,8);a.nop();a.la(14,~0xDu);
            a.r(13,14,13,0,0x24u);a.r(12,13,12,0,0x25u);
        }
        if(ending!=13)a.ori(12,12,ending==9 ? 1u : ending==12 ? 0x4Cu : 0xCu);
        if(ending==10)store(a,0xBF808268u,0x3BCu); // Invalidate the older sample.
        a.la(8,0xBF808268u);a.sw(12,0,8);
        if(ending==7) {
            a.lw(12,0,8);a.nop();a.ori(12,12,0xC);a.sw(12,0,8);
            a.la(8,0xA0000000u);a.sw(12,0x100,8);a.andi(10,12,1);a.sw(10,0x104,8);
        }
        if(ending==15)a.sw(12,0,8); // Reuse after a successful reset is stale.
    }
    a.label("idle");a.beq(0,0,"idle");a.nop();
    a.label("failed");a.raw(0xFFFFFFFFu);
    const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
    std::copy(code.begin(),code.end(),rom.begin());Iop iop;iop.reset(rom,nullptr);iop.run_until(150);return iop;
}
}
int main() try {
    {
        m20::Assembler a(kEeResetVector);
        store(a,0xBF801070u,0);store(a,0xBF808268u,0x3BCu);
        for(unsigned n=0;n<16;++n)store(a,0xBF808200u+n*4u,0x001C0700u|n);
        a.la(8,0xBF808200u);a.la(10,0xA0000100u);
        for(unsigned n=0;n<16;++n){a.lw(9,n*4u,8);a.nop();a.sw(9,n*4u,10);}
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),rom.begin());Iop iop;iop.reset(rom,nullptr);iop.run_until(300);
        require(!iop.stopped(),"all sixteen SIO2 command descriptors can be programmed without starting a transfer");
        for(unsigned n=0;n<16;++n)require(iop.ee_read(0x100u+n*4u,4)==(0x001C0700u|n),
                                       "SEND3 slots retain distinct descriptor words");
    }
    for(const unsigned mode : {0u,1u,2u}) {
        m20::Assembler a(kEeResetVector);a.la(8,0xBF80823Cu);
        if(!mode){a.lw(9,0,8);a.nop();a.addiu(10,9,1);}
        else if(mode==1)a.raw((0x25u<<26)|(8u<<21)|(9u<<16)); // LHU
        else a.raw((0x29u<<26)|(8u<<21)); // SH
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),rom.begin());Iop iop;iop.reset(rom,nullptr);iop.run_until(30);
        require(iop.stopped() && iop.stop_reason()->find(mode ? "only 32-bit" : "before any write")!=std::string::npos,
                "SEND3 has no invented reset value or unverified narrow accesses");
    }
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
    const auto reset=run(7);
    require(!reset.stopped() && reset.ee_read(0x104,4)==0,"read/ORI-reset/SW uses known command bits and preserves the opaque sample");
    rejects([&]{(void)reset.ee_read(0x100,4);},"has not been written");
    for(const unsigned ending : {16u,17u}) {
        const auto moved=run(ending);
        require(!moved.stopped(),"ADDU moves preserve an opaque SIO2 reset sample in either operand order");
    }
    const std::string reset_causes[]={"start bit is unknown","SIO2 transfer is not modeled","current opaque control sample",
                                     "current opaque control sample","current opaque control sample","both reset bits",
                                     "requires a 32-bit read","current opaque control sample"};
    for(unsigned ending=8;ending<=15;++ending) {
        const auto refused=run(ending);
        require(refused.stopped() && refused.stop_reason()->find(reset_causes[ending-8])!=std::string::npos,
                "unknown, stale, mixed or modified samples cannot forge a control reset: "+refused.stop_reason().value_or("running"));
    }
    std::cout<<"SIO2 idle IRQ is known; unknown transfer, DMA, FIFO and readback fail explicitly\n";
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
