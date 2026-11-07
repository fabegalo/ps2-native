#include "ps2native/io.hpp"
#include <filesystem>
template<class C> int run(int argc,C** argv) {
    if(argc!=2)return 2;
    std::vector<std::uint8_t> bytes(0x224);
    const auto put=[&](unsigned at,std::uint32_t value,unsigned count=4){for(unsigned b=0;b<count;++b)bytes[at+b]=static_cast<std::uint8_t>(value>>(b*8u));};
    bytes[0]=0x7F;bytes[1]='E';bytes[2]='L';bytes[3]='F';bytes[4]=1;bytes[5]=1;bytes[6]=1;
    put(16,2,2);put(18,8,2);put(20,1);put(24,0x100000);put(28,52);put(36,0x20924001);
    put(40,52,2);put(42,32,2);put(44,2,2);
    const auto segment=[&](unsigned at,unsigned file,unsigned address,unsigned size){put(at,1);put(at+4,file);put(at+8,address);put(at+12,address);put(at+16,size);put(at+20,size);put(at+24,5);put(at+28,4);};
    segment(52,0x100,0x180,16);segment(84,0x200,0x100000,36);
    const std::uint32_t handler[]{0x401A7000,0x275A0004,0x409A7000,0x42000018};
    const std::uint32_t main[]{0x40024800,0x24031234,0x4083E000,0x4004E000,0x0000000C,
        0x2405FFFF,0x20A60001,0x34071000,0xCC000000};
    for(unsigned n=0;n<std::size(handler);++n)put(0x100+4*n,handler[n]);
    for(unsigned n=0;n<std::size(main);++n)put(0x200+4*n,main[n]);
    ps2native::write_file_atomically(std::filesystem::path(argv[1]),bytes);return 0;
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return run(argc,argv);}
#else
int main(int argc,char** argv){return run(argc,argv);}
#endif
