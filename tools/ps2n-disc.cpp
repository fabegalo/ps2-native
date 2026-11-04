#include "ps2native/disc.hpp"
#include "ps2native/sha256.hpp"
#include <iostream>

template<class C> int disc_main(int argc,C** argv) {
    try {
        if(argc<2){std::cerr<<"Usage: ps2n-disc <owned.iso|raw.bin> [--list=DIR] [--extract-boot=out.elf]\n";return 2;}
        ps2native::DiscImage disc{std::filesystem::path(argv[1])};
        std::filesystem::path output;
        std::string directory;
        for(int i=2;i<argc;++i) {
            const auto flag=ps2native::path_utf8(std::filesystem::path(argv[i]));
            if(flag.starts_with("--list="))directory=flag.substr(7);
            else if(flag.starts_with("--extract-boot="))output=std::filesystem::path(argv[i]).native().substr(15);
            else throw std::runtime_error("unknown disc option: "+flag);
        }
        std::cout<<"ISO 9660 volume="<<disc.volume_id()<<" image_bytes="<<disc.image_size()<<'\n';
        for(const auto& e:disc.list(directory))std::cout<<(e.directory?"dir ":"file ")<<e.name<<" lba="<<e.lba<<" bytes="<<e.size<<'\n';
        const auto path=disc.boot_path();const auto elf=disc.boot_elf();
        std::cout<<"BOOT2="<<path<<" elf_sha256="<<ps2native::sha256_hex(elf.bytes)<<" entry=0x"<<std::hex<<elf.entry<<std::dec<<'\n';
        if(!output.empty()) {
            if(ps2native::file_exists(output) && ps2native::files_equivalent(output,std::filesystem::path(argv[1])))
                throw std::runtime_error("boot extraction must not overwrite the disc image");
            (void)ps2native::write_binary_if_changed(output,elf.bytes);
        }
        return 0;
    } catch(const std::exception& e){std::cerr<<"ps2n-disc: "<<e.what()<<'\n';return 1;}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return disc_main(argc,argv);}
#else
int main(int argc,char** argv){return disc_main(argc,argv);}
#endif
