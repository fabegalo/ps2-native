#include "direct_program.hpp"
#include "ps2native/io.hpp"
#include "ps2native/boot.hpp"
#include "ps2native/sha256.hpp"
#include <filesystem>
#include <iostream>
template<class C> int fixture_main(int argc,C** argv) {
    try {
        if(argc!=3) return 2;
        const auto mode=ps2native::path_utf8(std::filesystem::path(argv[1]));
        if(mode=="overlay") {
            ps2native::Memory memory;ps2native::EEState state;
            const auto image=ps2native::parse_ee_elf(direct_test::elf(direct_test::cpu()));
            ps2native::prepare_direct_elf(image,nullptr,state,memory);
            const auto code=direct_test::dynamic_code();
            for(unsigned i=0;i<code.size();++i)memory.write8(direct_test::dynamic_address+i,code[i]);
            const auto snapshot=ps2native::snapshot_ee_code(memory,direct_test::dynamic_address,
                static_cast<std::uint32_t>(code.size()),ps2native::sha256_hex(code));
            (void)ps2native::write_binary_if_changed(std::filesystem::path(argv[2]),snapshot.bytes);
            return 0;
        }
        const auto program=direct_test::build(mode);
        const auto bytes=direct_test::elf(program);
        (void)ps2native::write_binary_if_changed(std::filesystem::path(argv[2]),bytes);
        if(!program.iop.empty()) {
            auto iop_path=std::filesystem::path(argv[2]);iop_path.replace_extension(".iop.bin");
            (void)ps2native::write_binary_if_changed(iop_path,program.iop);
        }
        std::cout<<"Direct "<<mode<<" stop=0x"<<std::hex<<program.stop<<'\n';
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return fixture_main(argc,argv);}
#else
int main(int argc,char** argv){return fixture_main(argc,argv);}
#endif
