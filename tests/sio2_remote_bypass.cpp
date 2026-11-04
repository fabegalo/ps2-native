// Original fixtures for the SDK's revision-gated remote-control bit RMW.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
std::vector<std::uint8_t> program(unsigned mode) {
    m20::Assembler a(kEeResetVector);a.la(8,0xBF808284);a.la(10,0xA0000100);
    if(mode==4){a.la(9,1);a.sw(9,0,8);}
    else {
        if(mode==5)a.lhu(11,0,8);else if(mode==6)a.lw(11,4,8);else a.lw(11,0,8);
        a.nop();
        if(mode==1){a.andi(12,11,1);a.addiu(12,12,1);}
        if(mode==2)a.addiu(12,11,1);
        if(mode==7){a.sw(11,0,10);a.lw(11,0,10);a.nop();}
        if(mode==8){a.la(9,0xBF808268);a.lw(11,0,9);a.nop();}
        if(mode==9)a.ori(11,11,4);
        if(mode==10){a.la(9,~3u);a.r(11,9,11,0,0x24);}
        if(mode==11)a.sw(11,0,8); // No established bypass command.
        if(mode==13 || mode==14) {
            a.la(9,1);a.r(mode==13 ? 11u : 9u,mode==13 ? 9u : 11u,11,0,0x25);
        } else a.ori(11,11,1);
        a.addu(12,11,0);a.sw(12,0,8);
        if(mode==3)a.sw(12,0,8); // Old token after a completed RMW.
        a.lw(11,0,8);a.nop();a.andi(12,11,1);a.sw(12,4,10);
        if(mode==12)a.addiu(12,11,1); // Whole word still opaque after enabling.
        a.la(9,~1u);
        a.r(mode==14 ? 9u : 11u,mode==14 ? 11u : 9u,11,0,0x24);
        a.sw(11,0,8);a.lw(11,0,8);a.nop();a.andi(12,11,1);a.sw(12,8,10);
    }
    a.la(12,1);a.sw(12,12,10);a.label("idle");a.beq(0,0,"idle");a.nop();
    const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
    std::copy(code.begin(),code.end(),rom.begin());return rom;
}
}
int main() try {
    for(const auto board:{"SCPH-70004:GH-032-11","SCPH-70004:GH-032-32"})for(unsigned mode=0;mode<15u;++mode) {
        const auto* profile=find_console_hardware(board);const auto rom=program(mode);Iop iop;iop.reset(rom,profile);
        require(!iop.remote_bypass_enabled(),"no remote-control reset value is invented");iop.run_until(300);
        if(mode==0 || mode==13 || mode==14) {
            require(!iop.stopped() && iop.remote_bypass_enabled()==false && iop.ee_read(0x104,4)==1 &&
                iop.ee_read(0x108,4)==0 && iop.ee_read(0x10C,4)==1,
                "SDK read/OR/set/read/AND/clear reaches the actual bit latch in either operand order");
            require(iop.events().size()==2,"only the two genuine latch updates produce events");
        } else {
            require(iop.stopped(),"cold/upper-bit consumption, bad width, stale/mixed/copied/modified samples cannot fake a command");
            if(mode==3 || mode==12)require(iop.remote_bypass_enabled()==true,"rejected stale writes and upper-bit use retain the last established bit");
            else require(!iop.remote_bypass_enabled(),"rejected writes never establish a remote-control state");
        }
        iop.reset(rom,profile);require(!iop.remote_bypass_enabled(),"machine reset removes earlier latch knowledge and source tokens");
    }
    for(unsigned revision:{0x22u,0x23u,0x2Eu}) {
        auto profile=*find_console_hardware("SCPH-70004:GH-032-32");profile.iop_prid=revision;const auto rom=program(0);
        Iop iop;iop.reset(rom,&profile);iop.run_until(300);
        require(iop.stopped()==(revision<0x23u),"direct bypass control is available only for documented IOP revisions");
    }
    for(const bool missing_revision:{false,true}) {
        auto profile=*find_console_hardware("SCPH-70004:GH-032-32");profile.iop_prid.reset();const auto rom=program(0);
        Iop iop;iop.reset(rom,missing_revision ? &profile : nullptr);iop.run_until(300);
        require(iop.stopped() && iop.stop_reason()->find("selected IOP PRId")!=std::string::npos,
            "absent hardware identity cannot imply remote hardware support");
    }
    std::cout<<"SIO2 remote bypass bit, RMW provenance, revision gates and opaque upper fields passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
