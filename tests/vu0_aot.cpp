#include "boot_checks.hpp"
#include "ps2native/vu_control.hpp"
#include "ps2native/io.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
template<class C> int run(int argc,C** argv) try {
    if(argc!=2)return 2;
    const auto bytes=read_file(std::filesystem::path(argv[1]));
    VuControl native,reference;native.enable_vu0_aot(true);reference.enable_vu0_aot(true);
    for(unsigned at=0;at<bytes.size();++at)for(auto* vu:{&native,&reference})vu->write_memory(0x11000000u+at,1,{bytes[at],0});
    for(auto* vu:{&native,&reference})vu->start_micro(0,0,0,{0,0},{0,0});
    for(unsigned cycle=0;cycle<20;++cycle) {
        native.advance_micro(cycle,nullptr,nullptr);reference.advance_micro(cycle,nullptr,nullptr);
        require(native.diagnostic_micro_pc(0)==reference.diagnostic_micro_pc(0) &&
            native.diagnostic_micro_instructions(0)==reference.diagnostic_micro_instructions(0) &&
            native.next_micro_cycle()==reference.next_micro_cycle(),"VU0 PC, issue, interlocks and drain cycles");
        for(unsigned r:{0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u,15u,16u,17u,18u,20u,21u,22u,27u,28u,29u}) {
            const auto a=native.observe(r),b=reference.observe(r);
            require(a.value==b.value && a.unknown==b.unknown,"VU0 register bits and unknown masks");
        }
    }
    require(!native.micro_running(0) && native.observe(1).value==3 && native.observe(2).value==10,"independent VU0 arithmetic and E termination");
    require(native.vu0_aot_stats().native_instructions==4 && native.vu0_aot_stats().interpreted_instructions==0 &&
        reference.vu0_aot_stats().native_instructions==4 && reference.vu0_aot_stats().interpreted_instructions==0,"offline VU0 artifact executes actual native instructions");
    native.write_memory(0x11000000u,4,{0x10010004u,0});
    native.start_micro(0,0,20,{0,0},{0,0});
    rejects([&]{native.advance_micro(21,nullptr,nullptr);},"missing VU0 AOT");
    require(native.vu0_aot_stats().missing_program>0,"VU0 micro upload invalidates native image selection");
    std::cout<<"VU0 AOT: integer operands, pipeline, E delay/drain, masks and invalidation passed\n";return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return run(argc,argv);}
#else
int main(int argc,char** argv){return run(argc,argv);}
#endif
