#include "ps2native/engine/console.hpp"
#include "ps2/os.hpp"
#include "iop_cdvd.hpp"
#include <cstdio>
int main(int argc,char** argv) {
    std::filesystem::path rom,disc;std::string profile;auto medium=ps2native::engine::IopCdvd::Medium::Auto;
    bool extended=true;
    const bool boot=argc>2;
    if(boot){for(int n=1;n<argc;n+=2){if(n+1>=argc)return 2;const std::string option=argv[n];if(option=="--rom0")rom=ps2::os::path(argv[n+1]);else if(option=="--disc")disc=ps2::os::path(argv[n+1]);else if(option=="--disc-type"){const std::string type=argv[n+1];if(type=="cd")medium=ps2native::engine::IopCdvd::Medium::Cd;else if(type=="dvd")medium=ps2native::engine::IopCdvd::Medium::Dvd;else return 2;}
            else if(option=="--cdvd-modules"){const std::string family=argv[n+1];if(family!="legacy" && family!="extended")return 2;extended=family=="extended";}
            else if(option=="--profile")profile=argv[n+1];else return 2;}if(rom.empty())return 2;}
    else if(argc!=2)return 2;
    ps2native::engine::Console console;
    try {
        console.initialize_sif();
        if(boot){if(!profile.empty()){console.cdvd->hardware=ps2native::find_console_hardware(profile);if(!console.cdvd->hardware)throw std::runtime_error("Unknown CDVD console profile");}if(!disc.empty())console.cdvd->mount(disc,medium);console.boot_cdvd(rom,extended);}
        else {console.load_iop(ps2::os::path(argv[1]));console.run_iop();}
        std::fwrite(console.iop_output.data(),1,console.iop_output.size(),stdout);
        std::fprintf(stderr,"[engine] IOP interpreter instructions=%llu logical_tick=%llu result=%d\n",static_cast<unsigned long long>(console.iop_instructions),static_cast<unsigned long long>(console.ticks),console.iop_result);
        return console.iop_result;
    } catch(const std::exception& e) {
        std::fwrite(console.iop_output.data(),1,console.iop_output.size(),stdout);
        std::fprintf(stderr,"[engine] %s IOP_PC=%08X tick=%llu\n",e.what(),console.iop.pc,static_cast<unsigned long long>(console.ticks)); return 1;
    }
}
