#include "bios_program.hpp"
#include "boot_checks.hpp"
#include "iso_image.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/sha256.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
int main(int argc,char** argv) try {
    require(argc==2,"fixture directory");
    const auto directory=std::filesystem::path(argv[1]);
    iso_test::IsoOptions options;options.sectors=options.written=64;
    const auto layer=iso_test::build_iso(options);auto bytes=layer;bytes.resize(112u*2048u);
    std::copy(layer.begin(),layer.end(),bytes.begin()+48u*2048u);
    const auto path=directory/"dual-checkpoint-own.iso";
    (void)write_binary_if_changed(path,bytes);
    auto rom=m20::build_firmware().rom;rom[0]=rom[1]=255;rom[2]=0;rom[3]=0x10;std::fill_n(rom.begin()+4,4,0);
    const auto bios=load_bios_rom0(rom);
    auto state=std::make_unique<EEState>(),other=std::make_unique<EEState>();
    auto memory=std::make_unique<Memory>(),restored=std::make_unique<Memory>();
    const auto prepare=[&](EEState& s,Memory& m,bool opposite,bool enabled=true) {
        prepare_ee_boot(bios,nullptr,s,m);
        m.mount_disc(std::make_shared<DiscImage>(path,enabled ? std::optional{DiscImage::DiagnosticDvdGeometry{48,opposite}} : std::nullopt),Iop::DiscMedium::Ps2Dvd);
        m.write32(0xA0001000u,0x1000FFFFu);m.write32(0xA0001004u,0);s.pc=0x80001000u;
    };
    prepare(*state,*memory,true);BootOptions boot;boot.max_instructions=8;
    const auto boundary=run_ee_boot(*state,*memory,nullptr,boot);
    require(boundary.outcome==BootOutcome::BudgetExhausted,"valid save boundary");
    CheckpointContext context{sha256_hex(bytes),"synthetic DVD geometry",false,sha256_hex(bytes)};
    const auto checkpoint=directory/"dual-geometry.ps2ncp";save_checkpoint(checkpoint,*state,*memory,boundary,context);
    const auto exact=checkpoint_state_sha256(*state,*memory);
    for(const bool enabled:{false,true}) {
        prepare(*other,*restored,false,enabled);const auto before=checkpoint_state_sha256(*other,*restored);
        rejects([&]{(void)load_checkpoint(checkpoint,*other,*restored,context);},"identity differs");
        require(checkpoint_state_sha256(*other,*restored)==before,"wrong or absent physical-layout input cannot modify the destination");
    }
    prepare(*other,*restored,true);(void)load_checkpoint(checkpoint,*other,*restored,context);
    require(checkpoint_state_sha256(*other,*restored)==exact,"matching explicit DVD layout restores all guest state");
    for(auto pair:{std::pair{state.get(),memory.get()},std::pair{other.get(),restored.get()}}) {
        const auto result=run_ee_boot(*pair.first,*pair.second,nullptr,boot);
        require(result.outcome==BootOutcome::BudgetExhausted,"checkpoint continuation");
    }
    require(checkpoint_state_sha256(*state,*memory)==checkpoint_state_sha256(*other,*restored),"matching DVD layout continues identically");
    std::cout<<"DVD checkpoint binding and continuation passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
