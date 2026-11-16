#include "ps2native/engine/console.hpp"
#include "ps2/os.hpp"
#include <cstdio>
int main(int argc,char** argv) {
    if(argc!=2) { std::fprintf(stderr,"usage: ps2n-iop-run ORIGINAL.irx\n"); return 2; }
    ps2native::engine::Console console;
    try {
        console.initialize_sif(); console.load_iop(ps2::os::path(argv[1])); console.run_iop();
        std::fwrite(console.iop_output.data(),1,console.iop_output.size(),stdout);
        std::fprintf(stderr,"[engine] IOP interpreter instructions=%llu logical_tick=%llu result=%d\n",static_cast<unsigned long long>(console.iop_instructions),static_cast<unsigned long long>(console.ticks),console.iop_result);
        return console.iop_result;
    } catch(const std::exception& e) {
        std::fwrite(console.iop_output.data(),1,console.iop_output.size(),stdout);
        std::fprintf(stderr,"[engine] %s\n",e.what()); return 1;
    }
}
