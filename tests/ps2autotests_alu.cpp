#include "ps2autotests_alu.hpp"
#include "ps2native/boot.hpp"
#include <iostream>
template<class C> int test_main(int argc,C** argv) try {
    using namespace ps2native;using namespace boot_checks;
    require(argc==3,"expected file and generated ELF");
    const auto cases=autotest::cases(std::filesystem::path(argv[1]));
    const auto program=autotest::program(cases);
    const auto image=parse_ee_elf(read_file(std::filesystem::path(argv[2])));
    for(const bool native:{false,true}) {
        EEState state;Memory memory;const auto* module=native ? &aot_module() : nullptr;
        prepare_direct_elf(image,module,state,memory);
        BootOptions options;options.max_instructions=10000;options.stop_at=program.stop;
        const auto report=run_ee_boot(state,memory,module,options);
        require(state.pc==program.stop,"ALU program reaches matched boundary: "+report.reason);
        if(native)require(report.aot_instructions==report.retired,"entire ALU program executes native translation");
        for(std::size_t i=0;i<cases.size();++i) {
            const auto at=autotest::outputs+static_cast<std::uint32_t>(i*16);
            require(memory.read64(at)==cases[i].expected.low && memory.read64(at+8)==cases[i].expected.high,
                std::string(native ? "AOT: " : "interpreter: ")+cases[i].label);
        }
    }
    std::cout<<"120 independent ps2autotests ALU results match in interpreter and AOT\n";return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return test_main(argc,argv);}
#else
int main(int argc,char** argv){return test_main(argc,argv);}
#endif
