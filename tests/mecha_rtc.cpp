#include "bios_program.hpp"
#include "boot_checks.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/io.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
constexpr std::uint64_t epoch=1709132399ull; // 2024-02-28 23:59:59 JST, supplied input.
std::array<std::uint8_t,8> read(Iop& iop) {
    iop.ee_cdvd_write(0x1F402016u,1,8);std::array<std::uint8_t,8> result{};
    for(auto& byte:result)byte=static_cast<std::uint8_t>(iop.ee_cdvd_read(0x1F402018u,1));
    return result;
}
}
int main(int argc,char** argv)try {
    require(argc==2,"RTC checkpoint directory");
    auto rom=m20::build_firmware().rom;m20::Assembler idle(0xBFC00000u);
    idle.label("idle");idle.beq(0,0,"idle");idle.nop();const auto bytes=m20::le_words(idle.finish());
    std::copy(bytes.begin(),bytes.end(),rom.begin());Iop iop;iop.reset(rom,nullptr);
    rejects([&]{(void)read(iop);},"initial time has not been supplied");
    iop.reset(rom,nullptr);
    const auto unknown=iop.diagnostic_ram_observed(0x1000);
    require(unknown.unknown==~0u,"IOP diagnostics retain unwritten byte uncertainty without a guest bus access");
    iop.ee_write(0x1000,1,0xA5);
    const auto partial=iop.diagnostic_ram_observed(0x1000);
    require(partial.value==0xA5 && partial.unknown==0xFFFFFF00u && iop.cycle()==0,
        "public IOP snapshots expose exact written bytes and masks without advancing device time");
    rejects([&]{iop.configure_rtc(0);},"2000..2099");
    rejects([&]{iop.configure_rtc(4102412400ull);},"2000..2099");
    iop.configure_rtc(epoch);
    require(read(iop)==std::array<std::uint8_t,8>{0,0x59,0x59,0x23,0,0x28,2,0x24},"SCMD 08 returns the supplied healthy RTC in JST/BCD");
    iop.run_until(36863999u);require(!iop.stopped(),iop.stop_reason().value_or(""));
    require(read(iop)[1]==0x59,"RTC does not tick before a complete 36.864 MHz emulated second");
    iop.run_until(36864000u);
    require(read(iop)==std::array<std::uint8_t,8>{0,0,0,0,0,0x29,2,0x24},"RTC rolls midnight into February 29 without host wall-clock sampling");
    rejects([&]{iop.configure_rtc(epoch);},"before execution");
    iop.reset(rom,nullptr);rejects([&]{(void)read(iop);},"initial time has not been supplied");

    Memory memory;EEState state;memory.reset_for_boot(rom);state.pc=0xBFC00000u;
    memory.set_clock(&state.retired_instructions,&state.stall_cycles);memory.configure_rtc(epoch);
    memory.write8(0xBF402016u,8);
    require(memory.read8(0xBF402018u)==0,"real RTC read snapshots its status before checkpointing the pending bytes");
    const auto file=std::filesystem::path(argv[1])/"rtc.ps2ncp";
    BootReport boundary;boundary.outcome=BootOutcome::StopAddress;boundary.pc=state.pc;
    const CheckpointContext context{sha256_hex(rom),"RTC supplied in UTC"};
    save_checkpoint(file,state,memory,boundary,context);
    Memory restored;EEState resumed;restored.reset_for_boot(rom);resumed.pc=state.pc;
    restored.set_clock(&resumed.retired_instructions,&resumed.stall_cycles);
    rejects([&]{(void)load_checkpoint(file,resumed,restored,context);},"identity differs");
    restored.configure_rtc(epoch+1u);
    rejects([&]{(void)load_checkpoint(file,resumed,restored,context);},"identity differs");
    restored.configure_rtc(epoch);(void)load_checkpoint(file,resumed,restored,context);
    require(checkpoint_state_sha256(state,memory)==checkpoint_state_sha256(resumed,restored),"matching RTC binding restores every pending protocol byte and device cycle");
    // The earlier RTC probe had the same field codec and immutable binding,
    // with read-only IOP snapshot methods in its header. Audit that exact
    // transition without publishing any firmware or real checkpoint bytes.
    const auto current=read_file(file);auto info=inspect_checkpoint(file);
    std::vector<std::uint8_t> payload(current.end()-info.payload_bytes,current.end());
    const std::array<std::uint8_t,8> st_tag{'G','S','S','T','U','N','K','1'};
    const auto st_at=std::search(payload.begin(),payload.end(),st_tag.begin(),st_tag.end());
    require(st_at!=payload.end(),"checkpoint contains the tagged per-vertex ST uncertainty extension");
    payload.erase(st_at,st_at+32);
    // An old header requires its actual old payload, without numerical voices.
    // This reset fixture has no optional loop addresses or decoder history.
    const std::array<std::uint8_t,8> audio_tag{'S','P','U','2','P','C','M','1'};
    const auto audio_at=std::search(payload.begin(),payload.end(),audio_tag.begin(),audio_tag.end());
    require(audio_at!=payload.end(),"checkpoint contains the tagged numerical SPU2 extension");
    constexpr auto audio_bytes=8u+48u*(3u+16u+12u+1u+64u+40u)+2u;
    require(static_cast<std::size_t>(payload.end()-audio_at)>=audio_bytes,"complete reset SPU2 extension");
    payload.erase(audio_at,audio_at+audio_bytes);
    const std::string old_schema="3d2c143346ea6328bb9cb49c48abfc09bfe426f4cabb88a2051625384ed54b1f";
    info.schema_sha256=old_schema;info.payload_sha256=sha256_hex(payload);info.payload_bytes=payload.size();
    std::vector<std::uint8_t> prior{'P','S','2','N','C','P','0','1'};
    const auto integer=[&](std::uint64_t value,unsigned size){for(unsigned i=0;i<size;++i)prior.push_back(static_cast<std::uint8_t>(value>>(8u*i)));};
    for(const auto* value:{&info.schema_sha256,&info.runtime_sha256,&info.bindings_sha256,&info.scenario_sha256,&info.host_metadata,&info.payload_sha256}) {
        integer(value->size(),8);prior.insert(prior.end(),value->begin(),value->end());
    }
    integer(info.retired,8);integer(info.cycles,8);integer(info.payload_bytes,8);integer(info.pc,4);
    prior.insert(prior.end(),payload.begin(),payload.end());
    const auto old_file=file.parent_path()/"rtc-prior-header.ps2ncp";write_file_atomically(old_file,prior);
    rejects([&]{(void)load_checkpoint(old_file,resumed,restored,context);},"schema");
    auto reviewed=context;reviewed.allow_runtime_change=true;(void)load_checkpoint(old_file,resumed,restored,reviewed);
    require(checkpoint_state_sha256(state,memory)==checkpoint_state_sha256(resumed,restored),"audited RTC header transition preserves every wire field");
    for(auto expected:{0x59u,0x59u,0x23u,0u,0x28u,2u,0x24u})
        require(restored.read8(0xBF402018u)==expected,"RTC FIFO snapshot survives complete checkpoint restoration");
    std::cout<<"MECHACON RTC UTC/JST, BCD, cycle tick, leap day, reset and checkpoint input binding passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
