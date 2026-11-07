#include "bios_program.hpp"
#include "boot_checks.hpp"
#include "ps2native/checkpoint.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;

int main(int argc,char** argv) try {
    SmapFifos fifo;
    rejects([&]{(void)fifo.read(false,0);},"no established value");
    rejects([&]{fifo.write(false,{1,0},10);},"explicit --assume-smap-reset-cycles");
    fifo.diagnostic_reset_cycles(5);
    fifo.write(false,{1,0},10);fifo.write(true,{1,0},12);
    require(fifo.read(false,14).value==1 && !fifo.empty(false,14),"TX reset stays busy before its deadline");
    const auto done=fifo.read(false,15);
    require(done.value==0 && done.unknown==0xFC && fifo.empty(false,15)==true,
            "TX reset clears only RESET/DMAEN and establishes an empty FIFO");
    require(fifo.read(true,15).value==1,"RX reset has an independent deadline");
    fifo.write(true,{1,0},16);
    require(fifo.read(true,20).value==1 && fifo.read(true,21).value==0,"repeated reset restarts the selected FIFO deadline");
    rejects([&]{fifo.write(false,{3,0},30);},"DMA and data transfers are not modeled");
    rejects([&]{fifo.write(false,{1,1},30);},"known RESET request");
    rejects([&]{fifo.write(false,{1,0},~std::uint64_t{0});},"overflows");
    require(fifo.valid() && fifo.empty(false,30)==true,"refused writes preserve completed reset state");
    fifo.power_reset();rejects([&]{(void)fifo.read(false,30);},"no established value");
    fifo.write(false,{1,0},30);require(fifo.read(false,35).value==0,"power/reset invalidates FIFO state and retains external timing");
    fifo.reset();rejects([&]{(void)fifo.read(false,30);},"no established value");
    rejects([&]{fifo.write(false,{1,0},30);},"explicit --assume-smap-reset-cycles");

    if(argc!=2)return 2;
    auto raw=m20::build_firmware().rom;
    const std::string version="0200EC20040614";
    const auto at=std::search(raw.begin(),raw.end(),std::begin(m20::kRomver),std::end(m20::kRomver)-1);
    require(at!=raw.end(),"fixture ROMVER found");std::copy(version.begin(),version.end(),at);
    m20::Assembler a(kEeResetVector);
    a.lui(8,0xB000);a.la(9,1);a.sb(9,0x1000,8);
    a.label("requested");
    a.lbu(10,0x1000,8);a.nop();a.andi(10,10,1);a.sw(10,0x100,0);
    a.label("poll");a.lbu(10,0x1000,8);a.nop();a.andi(10,10,1);a.bne(10,0,"poll");a.nop();
    a.sw(10,0x104,0);a.sb(9,0x1030,8);
    a.lbu(10,0x1030,8);a.nop();a.andi(10,10,1);a.sw(10,0x108,0);
    a.label("poll-rx");a.lbu(10,0x1030,8);a.nop();a.andi(10,10,1);a.bne(10,0,"poll-rx");a.nop();
    a.sw(10,0x10C,0);a.label("idle");a.beq(0,0,"idle");a.nop();
    const auto code=m20::le_words(a.finish());std::copy(code.begin(),code.end(),raw.begin());
    {
        m20::Assembler b(kEeResetVector);b.lui(8,0xB000);b.la(9,1);b.sb(9,0x1000,8);
        b.lbu(10,0x1000,8);b.nop();b.andi(10,10,0x80);b.label("unknown");b.beq(10,0,"unknown");b.nop();
        auto unverified=raw;const auto bytes=m20::le_words(b.finish());std::copy(bytes.begin(),bytes.end(),unverified.begin());
        Iop iop;iop.reset(unverified,find_console_hardware("SCPH-70004:GH-032-11"));
        iop.diagnostic_dev9_power_reset(5);iop.diagnostic_smap_reset_cycles(20);iop.run_until(32);
        require(iop.stopped() && iop.stop_reason()->find("SMAP FIFO control unused bits")!=std::string::npos,
                "IOP full register readback never turns reserved bits into known zeros");
    }
    const auto bios=load_bios_rom0(raw);
    const auto* hardware=find_console_hardware("SCPH-70004:GH-032-11");
    auto state=std::make_unique<EEState>(),resumed=std::make_unique<EEState>();
    auto memory=std::make_unique<Memory>(),restored=std::make_unique<Memory>();
    const auto prepare=[&](EEState& s,Memory& m) {
        prepare_ee_boot(bios,nullptr,s,m,hardware);
        m.diagnostic_dev9_power_reset(5);m.diagnostic_smap_reset_cycles(20);
        m.write32(0xA0001000u,0x1000FFFFu);m.write32(0xA0001004u,0);s.pc=0x80001000u;
    };
    prepare(*state,*memory);prepare(*resumed,*restored);
    BootOptions options;options.max_trace_events=32;options.max_instructions=32;
    const auto boundary=run_ee_boot(*state,*memory,nullptr,options);
    require(boundary.outcome==BootOutcome::BudgetExhausted && !memory->iop().stopped(),"production IOP MMIO accepts a timed TX reset");
    require(memory->iop().pc()==a.at("requested"),"checkpoint is after the RESET store and before its first status read");
    const std::filesystem::path file=std::filesystem::path(argv[1])/"pending-smap.ps2ncp";
    ps2native::create_directories(file.parent_path());
    CheckpointContext context{sha256_hex(raw),"synthetic pending SMAP FIFO reset"};
    save_checkpoint(file,*state,*memory,boundary,context);
    (void)load_checkpoint(file,*resumed,*restored,context);
    require(checkpoint_state_sha256(*state,*memory)==checkpoint_state_sha256(*resumed,*restored),"pending reset restores complete state");
    options.max_instructions=512;
    for(auto pair:{std::pair{state.get(),memory.get()},std::pair{resumed.get(),restored.get()}}) {
        const auto result=run_ee_boot(*pair.first,*pair.second,nullptr,options);
        require(result.outcome==BootOutcome::BudgetExhausted && !pair.second->iop().stopped(),"guest reset polling completes");
        require(pair.second->iop().ee_read(0x100,4)==1 && pair.second->iop().ee_read(0x104,4)==0,
                "guest sees busy before completion and clear after the original deadline");
        require(pair.second->iop().ee_read(0x108,4)==1 && pair.second->iop().ee_read(0x10C,4)==0,
                "RX FIFO uses its own control address and completion deadline");
    }
    require(checkpoint_state_sha256(*state,*memory)==checkpoint_state_sha256(*resumed,*restored),"reset continuation preserves complete state");
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
