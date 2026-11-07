#include "ps2native/io.hpp"
#include <filesystem>
template<class C> int run(int argc,C** argv) {
    if(argc!=2)return 2;
    std::vector<std::uint8_t> bytes(4096);
    const std::uint32_t words[]{0x10010003u,0x000002FFu,0x10020807u,0x000002FFu,
        0x8000033Cu,0x400002FFu,0x8000033Cu,0x000002FFu};
    for(unsigned n=0;n<std::size(words);++n)for(unsigned b=0;b<4;++b)bytes[n*4u+b]=static_cast<std::uint8_t>(words[n]>>(b*8u));
    ps2native::write_file_atomically(std::filesystem::path(argv[1]),bytes);return 0;
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return run(argc,argv);}
#else
int main(int argc,char** argv){return run(argc,argv);}
#endif
