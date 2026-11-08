#include "bios_program.hpp"
#include "boot_checks.hpp"
#include "ps2native/checkpoint.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
int main(int argc,char** argv) try {
    SmapFifos smap;
    smap.write_phy(false,{0x8000,0},0);
    rejects([&]{smap.write_phy(true,{0x2020,0},0);},"explicit --assume-smap-phy-cycles");
    smap.diagnostic_phy_cycles(5);smap.write_phy(true,{0x2020,0},0);
    require(smap.read_phy(true,4).value==0x2020 && smap.read_phy(true,5).value==0xA020,"MDIO completes only at its selected deadline");
    smap.write_phy(true,{0x1020,0},6);
    require(smap.read_phy(true,11).value==0x9020 && smap.read_phy(false,11).value==0 && smap.read_phy(false,11).unknown==0x7FFF,"PHY reset clears without inventing strapped configuration");
    smap.write_phy(false,{0x6100,0},12);smap.write_phy(true,{0x2020,0},12);
    rejects([&]{smap.write_phy(false,{0,0},13);},"transaction is pending");
    require(smap.read_phy(true,17).value==0xA020 && smap.valid(),"loopback configuration is applied through a completed MDIO write");
    rejects([&]{smap.write_phy(true,{0x1021,0},18);},"known BMCR transactions");
    smap.reset();
    require(smap.interrupt_status().unknown==0xFFFF,"SPEED pending status begins unknown");
    smap.clear_interrupts({0x7C,0});
    require(smap.interrupt_status().unknown==0xFF83 && smap.interrupt_status().value==0,"W1C establishes only cleared SMAP sources");
    rejects([&]{smap.clear_interrupts({0x80,0});},"known interrupt bits");
    rejects([&]{smap.clear_interrupts({0,1});},"known interrupt bits");
    smap.write_configuration(0x04,{0x814C,0});smap.write_configuration(0x06,{0x8000,0});
    require(smap.read_configuration(0x04).value==0x814C && smap.read_configuration(0x06).value==0x8000,"MODE1 retains high-halfword-first configuration");
    smap.write_configuration(0x16,{0x1C0,0});
    require(smap.read_configuration(0x16).unknown==0xFE3F,"EMAC W1C preserves uncleared unknown sources");
    rejects([&]{smap.write_configuration(0x5C,{0,0});},"not implemented");
    rejects([&]{(void)smap.read_descriptor(0);},"byte-swap mode");
    smap.write_bd_mode({1,0});
    require(smap.read_descriptor(0).unknown==0xFFFF,"unwritten descriptor bits remain unknown");
    smap.write_descriptor(0,{0x1280,0x1200});
    require(smap.read_descriptor(0).value==0x80 && smap.read_descriptor(0).unknown==0x1200,"descriptor roundtrip retains partial knowledge");
    smap.write_bd_mode({0,0});
    require(smap.read_descriptor(0).value==0x8000 && smap.read_descriptor(0).unknown==0x12,"swap mode changes the bus view of shared storage");
    smap.write_descriptor(1022,{0x1234,0});smap.write_bd_mode({1,0});
    require(smap.read_descriptor(1022).value==0x3412,"both descriptor rings share halfword swap semantics");
    rejects([&]{smap.write_descriptor(1024,{0,0});},"aligned halfword");
    rejects([&]{(void)smap.read_descriptor(1);},"aligned halfword");
    smap.power_reset();smap.write_bd_mode({0,0});
    require(smap.read_descriptor(1022).unknown==0xFFFF,"power loss invalidates descriptor contents");
    smap.power_reset();
    rejects([&]{(void)smap.read_bd_mode();},"no established value");
    smap.write_bd_mode({1,0});require(smap.read_bd_mode().value==1 && smap.read_bd_mode().unknown==0xFE,"only descriptor byte-swap selection reads back");
    rejects([&]{smap.write_bd_mode({2,0});},"known swap bit");
    rejects([&]{smap.write_bd_mode({0,1});},"known swap bit");
    require(smap.read_bd_mode().value==1,"refused descriptor mode writes preserve the selection");
    smap.write_bd_mode({0,0});require(smap.read_bd_mode().value==0,"known unswapped selection is retained");
    rejects([&]{(void)smap.read_mode0(false,0);},"no established value");
    rejects([&]{smap.write_mode0(false,{0x2000,0},10);},"explicit --assume-smap-reset-cycles");
    smap.diagnostic_reset_cycles(5);
    smap.write_mode0(true,{0,0},9);
    rejects([&]{(void)smap.read_mode0(false,9);},"no established value");
    smap.write_mode0(false,{0x2000,0},10);
    require(smap.read_mode0(false,14).value==0x2000 && smap.read_mode0(false,14).unknown==0xDFFF,
            "only the high-halfword reset bit is known before its deadline");
    require(smap.read_mode0(false,15).value==0 && smap.read_mode0(true,15).unknown==0xFFFF,
            "reset clears at its deadline without fabricating reserved bits");
    smap.write_mode0(false,{0x2000,0},20);
    rejects([&]{smap.write_mode0(false,{0x400,0},21);},"known software reset");
    rejects([&]{smap.write_mode0(false,{0x2000,1},21);},"known software reset");
    rejects([&]{smap.write_mode0(true,{1,0},21);},"zero low halfword");
    rejects([&]{smap.write_mode0(false,{0x2000,0},~std::uint64_t{0});},"overflows");
    require(smap.valid() && smap.read_mode0(false,24).value==0x2000 && smap.read_mode0(false,25).value==0,
            "refused requests cannot modify the pending reset");
    smap.power_reset();rejects([&]{(void)smap.read_mode0(false,26);},"no established value");
    rejects([&]{(void)smap.read_bd_mode();},"no established value");
    smap.write_mode0(false,{0x2000,0},30);require(smap.read_mode0(false,35).value==0,"power reset retains external timing only");
    smap.reset();rejects([&]{smap.write_mode0(false,{0x2000,0},40);},"explicit --assume-smap-reset-cycles");
    for(unsigned mode=0;mode<4;++mode) {
        m20::Assembler a(kEeResetVector);a.lui(8,0xB000);a.la(9,0x2000);
        if(mode==0)a.sw(9,0x2000,8);
        else if(mode==1)a.sb(9,0x2000,8);
        else {
            a.sh(9,0x2000,8);
            if(mode==2)a.lw(10,0x2000,8);
            else {a.lhu(10,0x2000,8);a.nop();a.andi(10,10,0x8000);a.beq(10,0,"idle");a.nop();}
        }
        a.label("idle");a.beq(0,0,"idle");a.nop();
        const auto code=m20::le_words(a.finish());std::vector<std::uint8_t> rom(4u*1024u*1024u);
        std::copy(code.begin(),code.end(),rom.begin());Iop iop;iop.reset(rom,find_console_hardware("SCPH-70004:GH-032-11"));
        iop.diagnostic_dev9_power_reset(5);iop.diagnostic_smap_reset_cycles(20);iop.run_until(100);
        require(iop.stopped(),"unsupported widths and consumption of MODE0 idle bits remain explicit");
        require(iop.stop_reason()->find(mode==3 ? "bits other than software reset" : "halfword")!=std::string::npos,
                "rejection identifies the unsupported access or unknown bits");
    }
    if(argc!=2)return 2;
    auto raw=m20::build_firmware().rom;
    const std::string version="0200EC20040614";
    const auto at=std::search(raw.begin(),raw.end(),std::begin(m20::kRomver),std::end(m20::kRomver)-1);
    std::copy(version.begin(),version.end(),at);
    m20::Assembler a(kEeResetVector);a.lui(8,0xB000);a.la(9,0x2000);a.sh(9,0x2000,8);a.label("requested");
    a.la(9,1);a.sb(9,0x102,8);a.sh(0,0x2002,8);a.lhu(10,0x2000,8);a.nop();a.andi(10,10,0x2000);a.sw(10,0x100,0);
    a.label("poll");a.lhu(10,0x2000,8);a.nop();a.andi(10,10,0x2000);a.bne(10,0,"poll");a.nop();
    a.sw(10,0x104,0);a.lbu(10,0x102,8);a.nop();a.andi(10,10,1);a.sw(10,0x108,0);
    a.la(9,0x80);a.sh(9,0x3200,8);a.sb(0,0x102,8);a.lhu(10,0x3200,8);a.nop();a.sw(10,0x10C,0);
    a.la(9,0x7C);a.sh(9,0x128,8);a.la(9,0x814C);a.sh(9,0x2004,8);a.sh(0,0x2006,8);
    a.la(9,0xFFFF);a.sw(9,0x114,0);a.la(9,0x8000);a.sh(9,0x205C,8);a.la(9,0x2020);a.sh(9,0x205E,8);
    a.label("mdio-write");a.lhu(10,0x205E,8);a.nop();a.andi(10,10,0x8000);a.beq(10,0,"mdio-write");a.nop();
    a.la(9,0x1020);a.sh(9,0x205E,8);
    a.label("mdio-read");a.lhu(10,0x205E,8);a.nop();a.andi(10,10,0x8000);a.beq(10,0,"mdio-read");a.nop();
    a.lhu(10,0x205C,8);a.nop();a.andi(10,10,0x8000);a.sw(10,0x114,0);
    a.la(9,0x6100);a.sh(9,0x205C,8);a.la(9,0x2020);a.sh(9,0x205E,8);
    a.label("phy-loopback");a.lhu(10,0x205E,8);a.nop();a.andi(10,10,0x8000);a.beq(10,0,"phy-loopback");a.nop();
    a.la(9,1);a.sb(9,0x1000,8);a.sb(9,0x1030,8);
    a.label("fifo-reset");a.lbu(10,0x1000,8);a.nop();a.andi(10,10,1);a.bne(10,0,"fifo-reset");a.nop();
    a.lbu(10,0x1030,8);a.nop();a.andi(10,10,1);a.bne(10,0,"fifo-reset");a.nop();
    a.lhu(10,0x1004,8);a.nop();a.addiu(10,10,0x1000);a.sw(10,0x12C,0);
    a.la(9,0x1800);a.sh(9,0x2000,8);a.sh(0,0x2002,8);a.la(9,0x3F00);a.sh(9,0x2010,8);a.sh(0,0x2012,8);
    a.la(9,0x34333231u);a.sw(9,0x1100,8);a.la(9,0x38373635u);a.sw(9,0x1100,8);a.la(9,0x39);a.sw(9,0x1100,8);
    a.sh(0,0x3008,8);a.la(9,9);a.sh(9,0x3004,8);a.la(9,0x1000);a.sh(9,0x3006,8);
    a.la(9,0x8000);a.sh(9,0x3200,8);a.sb(0,0x1010,8);a.la(9,0x8200);a.sh(9,0x3000,8);
    a.la(9,1);a.sw(9,0x128,0);a.la(9,0x8000);a.sh(9,0x2008,8);a.sh(0,0x200A,8);
    a.label("packet");a.lhu(10,0x28,8);a.nop();a.andi(10,10,0x34);a.la(9,0x34);a.bne(10,9,"packet");a.nop();
    for(unsigned n=0;n<4;++n){a.lw(10,0x1200,8);a.nop();if(n==3)a.andi(10,10,0xFF);a.sw(10,0x118+4*n,0);}
    a.la(9,2);a.sw(9,0x128,0);
    a.label("idle");a.beq(0,0,"idle");a.nop();
    const auto code=m20::le_words(a.finish());std::copy(code.begin(),code.end(),raw.begin());
    const auto bios=load_bios_rom0(raw);const auto* hardware=find_console_hardware("SCPH-70004:GH-032-11");
    auto state=std::make_unique<EEState>(),resumed=std::make_unique<EEState>();
    auto memory=std::make_unique<Memory>(),restored=std::make_unique<Memory>();
    const auto prepare=[&](EEState& s,Memory& m) {
        prepare_ee_boot(bios,nullptr,s,m,hardware);m.diagnostic_dev9_power_reset(5);m.diagnostic_smap_reset_cycles(20);m.diagnostic_smap_phy_cycles(1000);m.diagnostic_smap_loopback_cycles(500);
        m.write32(0xA0001000u,0x1000FFFFu);m.write32(0xA0001004u,0);s.pc=0x80001000u;
    };
    prepare(*state,*memory);prepare(*resumed,*restored);
    BootOptions options;options.max_trace_events=32;options.max_instructions=32;
    const auto boundary=run_ee_boot(*state,*memory,nullptr,options);
    require(boundary.outcome==BootOutcome::BudgetExhausted && !memory->iop().stopped() && memory->iop().pc()==a.at("requested"),
            "production MMIO starts a reset before its first status poll");
    const std::filesystem::path file=std::filesystem::path(argv[1])/(sha256_hex(raw)+"-pending-emac.ps2ncp");
    ps2native::create_directories(file.parent_path());CheckpointContext context{sha256_hex(raw),"synthetic pending EMAC reset"};
    save_checkpoint(file,*state,*memory,boundary,context);(void)load_checkpoint(file,*resumed,*restored,context);
    require(checkpoint_state_sha256(*state,*memory)==checkpoint_state_sha256(*resumed,*restored),"checkpoint retains pending EMAC reset");
    options.max_instructions=512;
    for(auto pair:{std::pair{state.get(),memory.get()},std::pair{resumed.get(),restored.get()}}) {
        const auto result=run_ee_boot(*pair.first,*pair.second,nullptr,options);
        require(result.outcome==BootOutcome::BudgetExhausted && !pair.second->iop().stopped() &&
                pair.second->iop().ee_read(0x100,4)==0x2000 && pair.second->iop().ee_read(0x104,4)==0 && pair.second->iop().ee_read(0x108,4)==1 && pair.second->iop().ee_read(0x10C,4)==0x8000,
                "guest polling sees busy and original reset completion after restore");
    }
    require(checkpoint_state_sha256(*state,*memory)==checkpoint_state_sha256(*resumed,*restored),"EMAC continuation preserves complete state");
    const auto exact=checkpoint_state_sha256(*state,*memory);
    BootReport completed;completed.outcome=BootOutcome::BudgetExhausted;completed.pc=state->pc;
    save_checkpoint(file,*state,*memory,completed,context);
    prepare(*resumed,*restored);(void)load_checkpoint(file,*resumed,*restored,context);
    require(checkpoint_state_sha256(*resumed,*restored)==exact,"initialized descriptors, cleared interrupts and EMAC configuration survive restore");
    options.max_instructions=25000;
    for(auto pair:{std::pair{state.get(),memory.get()},std::pair{resumed.get(),restored.get()}}) {
        const auto result=run_ee_boot(*pair.first,*pair.second,nullptr,options);
        require(result.outcome==BootOutcome::BudgetExhausted && !pair.second->iop().stopped() && pair.second->iop().ee_read(0x114,4)==0,"pending PHY write, reset and read complete through guest MMIO after restore");
    }
    require(checkpoint_state_sha256(*state,*memory)==checkpoint_state_sha256(*resumed,*restored),"pending PHY continuation retains complete state");
    require(memory->iop().ee_read(0x128,4)==1,"packet checkpoint is inside the pending loopback test");
    save_checkpoint(file,*state,*memory,completed,context);
    prepare(*resumed,*restored);(void)load_checkpoint(file,*resumed,*restored,context);
    options.max_instructions=8000;
    for(auto pair:{std::pair{state.get(),memory.get()},std::pair{resumed.get(),restored.get()}}) {
        const auto result=run_ee_boot(*pair.first,*pair.second,nullptr,options);
        require(result.outcome==BootOutcome::BudgetExhausted && !pair.second->iop().stopped() && pair.second->iop().ee_read(0x128,4)==2,"loopback descriptor and interrupt completion survive restore");
        require(pair.second->iop().ee_read(0x12C,4)==0x1000,"known FIFO pointer remains usable in address arithmetic");
        require(pair.second->iop().ee_read(0x118,4)==0x34333231u && pair.second->iop().ee_read(0x11C,4)==0x38373635u && pair.second->iop().ee_read(0x120,4)==0xF4392639u && pair.second->iop().ee_read(0x124,4)==0xCBu,"RX FIFO retains payload and independent CRC32 check vector 123456789");
    }
    require(checkpoint_state_sha256(*state,*memory)==checkpoint_state_sha256(*resumed,*restored),"loopback continuation retains complete guest state");
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
