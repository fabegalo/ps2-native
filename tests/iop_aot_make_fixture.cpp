#include "iop_aot_fixture.hpp"
#include "ps2native/io.hpp"
#include <filesystem>
template<class C> int run(int argc,C** argv) {if(argc!=2)return 2;ps2native::write_file_atomically(std::filesystem::path(argv[1]),iop_aot_fixture::image());return 0;}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return run(argc,argv);}
#else
int main(int argc,char** argv){return run(argc,argv);}
#endif
