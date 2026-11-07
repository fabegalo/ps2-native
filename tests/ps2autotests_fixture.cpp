#include "ps2autotests_alu.hpp"
#include <iostream>
template<class C> int fixture_main(int argc,C** argv) try {
    boot_checks::require(argc==3,"expected file and output ELF");
    const auto program=autotest::program(autotest::cases(std::filesystem::path(argv[1])));
    (void)ps2native::write_binary_if_changed(std::filesystem::path(argv[2]),direct_test::elf(program));return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return fixture_main(argc,argv);}
#else
int main(int argc,char** argv){return fixture_main(argc,argv);}
#endif
