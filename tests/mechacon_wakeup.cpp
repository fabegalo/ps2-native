// Original fixtures for the documented SCMD 22 wire protocol. No firmware data.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
std::vector<std::uint8_t> program(unsigned mode) {
    m20::Assembler a(kEeResetVector);a.la(8,0xBF402000);a.la(10,0xA0000100);
    if(mode==1){a.la(9,0);a.sb(9,0x17,8);}
    a.la(9,0x22);a.sb(9,0x16,8);
    for(unsigned n=0;n<10u;++n) {
        a.lbu(9,0x17,8);a.nop();a.sw(9,0x20+4*n,10);
        a.lbu(11,0x18,8);a.nop();a.sb(11,n,10);
        if(mode==2+n)a.addiu(12,11,1);
        if(mode==14 && n==3u){a.andi(12,11,0x80);a.addiu(12,12,1);a.sw(12,0x50,10);}
    }
    a.lbu(9,0x17,8);a.nop();a.sw(9,0x48,10);
    a.la(9,1);a.sw(9,0x4C,10);
    if(mode==12){a.lbu(11,0x18,8);a.nop();a.addiu(12,11,1);a.beq(12,0,"idle");a.nop();}
    a.label("idle");a.beq(0,0,"idle");a.nop();const auto code=m20::le_words(a.finish());
    std::vector<std::uint8_t> rom(4u*1024u*1024u);std::copy(code.begin(),code.end(),rom.begin());return rom;
}
}
int main() try {
    for(const auto board:{"SCPH-70004:GH-032-11","SCPH-70004:GH-032-32"})for(unsigned mode=0;mode<14u;++mode) {
        const auto* profile=find_console_hardware(board);const auto rom=program(mode);
        NvmImage nvm;nvm.bytes.resize(kNvmBytes,0xA5);nvm.model_name=profile->model;
        nvm.layout=profile->mechacon->layout;nvm.sha256=std::string(64,'a');const auto saved=nvm;
        Iop iop;iop.reset(rom,mode==13 ? nullptr : profile,&nvm);
        for(unsigned n=0;n<0x50u;n+=4u)iop.ee_write(0x100+n,4,0x12345678);
        iop.run_until(500);
        if(mode==0 || mode==3) {
            require(!iop.stopped(),"all ten opaque result bytes travel without manufacturing alarm state");
            for(unsigned n=0;n<10u;++n) {
                require(!iop.diagnostic_ram32(0x100+n/4u*4u),"each alarm/status byte remains unknown in RAM");
                require(iop.ee_read(0x120+4*n,4)==0,"FIFO remains ready before every documented result byte");
            }
            require(iop.ee_read(0x101,1)==0,"front-panel power-on cause follows the actual reset event");
            require(iop.ee_read(0x14C,4)==1 && iop.ee_read(0x148,4)==0x40,
                "the tenth result empties the FIFO and firmware continues");
            require(iop.ee_read(0x10A,1)==0x34 && iop.ee_read(0x10B,1)==0x12,
                "opaque byte transport preserves adjacent untouched RAM");
        } else {
            const auto expected=mode==1 ? "takes no parameters" : mode==12 ? "no result byte is pending" :
                mode==13 ? "requires the selected MECHACON profile" : mode==2 ? "status is unmeasured" :
                "battery-backed alarm/flags/user data";
            require(iop.stopped() && iop.stop_reason()->find(expected)!=std::string::npos,
                "malformed input, absent profile, opaque consumption and FIFO overread refuse explicitly");
        }
        require(nvm.bytes==saved.bytes && nvm.sha256==saved.sha256,"alarm reads never rewrite or reinterpret the supplied NVM");
        iop.reset(rom,profile,&nvm);require(!iop.stopped(),"reset clears pending result/error state");
    }
    for(unsigned mode:{0u,2u,3u,4u,5u,6u,14u}) {
        const auto* profile=find_console_hardware("SCPH-70004:GH-032-32");const auto rom=program(mode);
        Iop iop;iop.reset(rom,profile);iop.diagnostic_wakeup_alarm_disabled();iop.run_until(500);
        if(mode==0 || mode==3 || mode==4 || mode==14) {
            require(!iop.stopped() && iop.ee_read(0x102,1)==0xFF,
                "only the explicitly selected disabled-alarm sentinel becomes a known whole byte");
            require(!iop.diagnostic_ram32(0x100) && !iop.diagnostic_ram32(0x104),
                "status and alarm date remain unknown in the experiment");
            if(mode==14)require(iop.ee_read(0x150,4)==1,"only bit 7 of the minute byte can be consumed");
            rejects([&]{(void)iop.ee_read(0x103,1);},"undefined");
        } else require(iop.stopped(),"the experiment cannot define status, minute low bits or date");
        rejects([&]{iop.diagnostic_wakeup_alarm_disabled();},"before execution");
        iop.reset(rom,profile);iop.run_until(500);
        if(mode==4 || mode==14)require(iop.stopped(),"reset removes the alarm-control assumptions");
    }
    for(unsigned cause=0;cause<4u;++cause) {
        const auto* profile=find_console_hardware("SCPH-70004:GH-032-32");const auto rom=program(0);Iop iop;
        iop.reset(rom,profile,nullptr,false,std::nullopt,{},false,false,static_cast<Iop::PowerEvent>(cause));iop.run_until(500);
        require(!iop.stopped() && iop.ee_read(0x101,1)==cause,"each documented external power event reaches the real result FIFO");
        require(!iop.diagnostic_ram32(0x100),"power event does not manufacture a clock status or alarm value");
        iop.reset(rom,profile);iop.run_until(500);require(iop.ee_read(0x101,1)==0,"new user power-on does not retain an old timer/reset cause");
    }
    {
        const auto rom=program(0);Iop iop;
        rejects([&]{iop.reset(rom,nullptr,nullptr,false,std::nullopt,{},false,false,static_cast<Iop::PowerEvent>(255));},"power event is invalid");
    }
    DiagnosticAssumptions input;require(input.empty(),"default alarm state stays unmeasured");
    input.mechacon_wakeup_alarm_disabled=true;require(!input.empty(),"alarm-control selection always marks execution diagnostic");
    std::cout<<"ReadWakeUpTime framing, ten opaque bytes, FIFO bounds, profiles and NVM preservation passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
