#include "experimental_program.hpp"
#include "ps2native/io.hpp"
template<class C> int run(int argc,C** argv) {
    if(argc!=3)return 2;
    ps2native::write_file_atomically(argv[1],experimental_test::firmware());
    ps2native::write_file_atomically(argv[2],experimental_test::ee_elf());
    return 0;
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return run(argc,argv);}
#else
int main(int argc,char** argv){return run(argc,argv);}
#endif
