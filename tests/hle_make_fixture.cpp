#include "hle_fixture.hpp"
#include "ps2native/io.hpp"
#include <sstream>
template<class C> int run(int argc,C** argv) {
    if(argc!=3)return 2;
    const auto p=hle_test::program();
    ps2native::write_file_atomically(argv[1],direct_test::elf(p));
    std::ostringstream roots;for(auto pc:p.roots)roots<<pc<<'\n';
    (void)ps2native::write_text_if_changed(argv[2],roots.str());
    return 0;
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return run(argc,argv);}
#else
int main(int argc,char** argv){return run(argc,argv);}
#endif
