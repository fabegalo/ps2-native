#include "boot_checks.hpp"
#include "direct_program.hpp"
#include "iso_image.hpp"
#include "ps2native/disc.hpp"
#include "ps2native/sha256.hpp"
#include <iostream>

using namespace ps2native;
using namespace boot_checks;
namespace {
std::vector<std::uint8_t> image(const std::vector<std::uint8_t>& elf) {
    iso_test::IsoOptions options;
    options.sectors=options.written=64;
    options.system_cnf="BOOT2 = cdrom0:\\DIR\\BOOT.ELF;1\r\n";
    auto bytes=iso_test::build_iso(options);
    auto root=bytes.data()+18*2048;
    std::size_t at=0;while(root[at])at+=root[at];
    iso_test::directory_record(root+at,22,2048,true,"DIR");
    auto dir=bytes.data()+22*2048;
    at=iso_test::directory_record(dir,22,2048,true,std::string(1,'\0'));
    at+=iso_test::directory_record(dir+at,18,2048,true,std::string(1,'\1'));
    iso_test::directory_record(dir+at,24,static_cast<std::uint32_t>(elf.size()),false,"BOOT.ELF;1");
    std::copy(elf.begin(),elf.end(),bytes.begin()+24*2048);
    return bytes;
}
std::vector<std::uint8_t> raw(const std::vector<std::uint8_t>& iso,unsigned mode) {
    std::vector<std::uint8_t> result(iso.size()/2048u*2352u);
    for(std::size_t s=0;s<iso.size()/2048u;++s) {
        auto p=result.data()+s*2352u;
        std::fill_n(p+1,10,std::uint8_t{255});p[15]=static_cast<std::uint8_t>(mode);
        std::copy_n(iso.data()+s*2048u,2048u,p+(mode==1?16u:24u));
    }
    return result;
}
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"disc test output directory");
        const auto path=std::filesystem::path(argv[1])/std::filesystem::path(u8"disc-ação.iso");
        const auto elf=direct_test::elf(direct_test::cpu());
        const auto original=image(elf);
        for(unsigned mode=0;mode<3;++mode) {
            const auto bytes=mode?raw(original,mode):original;
            (void)write_binary_if_changed(path,bytes);
            DiscImage disc(path);
            require(disc.volume_id()=="PS2N_TEST" && disc.list().size()==3 && disc.list("dir").size()==1,"directory navigation");
            require(disc.boot_path()=="\\DIR\\BOOT.ELF;1" && disc.boot_elf().bytes==elf,"read actual BOOT2 ELF across sectors");
            require(disc.read("dir/boot.elf")==elf && disc.read("README.TXT")==std::vector<std::uint8_t>({'t','e','s','t'}),"case-insensitive ISO lookup");
            require(sha256_hex(read_file(path))==sha256_hex(bytes),"disc is never modified");
            rejects([&]{(void)disc.read("../BOOT.ELF");},"path component");
            rejects([&]{(void)disc.read("C:\\BOOT.ELF");},"path component");
            rejects([&]{(void)disc.read("missing");},"file missing");
            rejects([&]{(void)disc.read("dir");},"directory as a file");
            std::array<std::uint8_t,2048> sector{};
            rejects([&]{disc.read_sector(64,sector);},"outside");
        }
        auto bad=original;bad[16*2048+84]^=1;
        (void)write_binary_if_changed(path,bad);rejects([&]{DiscImage disc(path);},"both-endian");
        bad=original;bad.resize(bad.size()-1);
        (void)write_binary_if_changed(path,bad);rejects([&]{DiscImage disc(path);},"exceeds disc image");
        bad=original;iso_test::put32_both(bad.data()+16*2048+158,64);
        (void)write_binary_if_changed(path,bad);rejects([&]{DiscImage disc(path);},"extent outside");
        bad=original;bad[22*2048+68+25]|=0x80; // BOOT.ELF multi-extent
        (void)write_binary_if_changed(path,bad);rejects([&]{DiscImage disc(path);(void)disc.boot_elf();},"multi-extent");
        auto raw_bad=raw(original,2);raw_bad[24*2352+18]|=0x20;
        (void)write_binary_if_changed(path,raw_bad);rejects([&]{DiscImage disc(path);(void)disc.boot_elf();},"user-data sector");
        iso_test::IsoOptions ps1;ps1.system_cnf="BOOT=cdrom:\\PSX.EXE;1\r\n";
        (void)write_binary_if_changed(path,iso_test::build_iso(ps1));
        rejects([&]{DiscImage disc(path);(void)disc.boot_elf();},"PlayStation 1");
        iso_test::IsoOptions duplicate;duplicate.system_cnf="BOOT2=cdrom0:\\A;1\nBOOT2=cdrom0:\\B;1\n";
        (void)write_binary_if_changed(path,iso_test::build_iso(duplicate));
        rejects([&]{DiscImage disc(path);(void)disc.boot_path();},"duplicate BOOT2");
        std::cout<<"ISO boot ELF, modes 2048/2352, paths, bounds and explicit refusals passed\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
