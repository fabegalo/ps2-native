#include "vu_aot_fixture.hpp"
#include "ps2native/io.hpp"
#include <filesystem>
#include <iostream>
template<class Character> int fixture_main(int argc,Character** argv) try {
    if(argc!=3)return 2;
    const bool ee=std::filesystem::path(argv[2])=="ee";
    const auto bytes=ee ? vu_aot_fixture::ee_image() : vu_aot_fixture::micro_image();
    ps2native::write_file_atomically(std::filesystem::path(argv[1]),bytes);return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return fixture_main(argc,argv);}
#else
int main(int argc,char** argv){return fixture_main(argc,argv);}
#endif
