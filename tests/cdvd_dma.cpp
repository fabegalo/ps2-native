#include "bios_program.hpp"
#include "boot_checks.hpp"
#include "iso_image.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
std::vector<std::uint8_t> program(unsigned blocks,bool enabled=true) {
    m20::Assembler a(0xBFC00000u);
    const auto sw=[&](std::uint32_t address,std::uint32_t value){a.la(8,address);a.la(9,value);a.sw(9,0,8);};
    a.la(8,0xBF402006u);a.ori(9,0,0x80u);a.sb(9,0,8);
    sw(0xBF801578u,1);sw(0xBF8010F0u,enabled?0x8000u:0);
    sw(0xBF80157Cu,1);sw(0xBF8010F4u,0x00880000u);
    sw(0xBF8010B0u,0x10000u);sw(0xBF8010B4u,(blocks<<16u)|32u);sw(0xBF8010B8u,0x41000200u);
    a.label("idle");a.beq(0,0,"idle");a.nop();
    auto rom=m20::build_firmware().rom;const auto code=m20::le_words(a.finish());
    std::copy(code.begin(),code.end(),rom.begin());return rom;
}
void command(Iop& iop,unsigned sectors,bool cav=false,unsigned speed=5) {
    for(unsigned j=0;j<11;++j)iop.ee_cdvd_write(0x1F402005u,1,j==4?sectors:j==9?(speed|(cav?0x80u:0u)):0u);
    iop.ee_cdvd_write(0x1F402004u,1,6);
}
}
int main(int argc,char** argv) try {
    require(argc==2,"fixture directory");
    auto bytes=iso_test::build_iso({});
    for(unsigned j=0;j<4096;++j)bytes[j]=static_cast<std::uint8_t>((j*37u+11u)^(j>>8u));
    const auto path=std::filesystem::path(argv[1])/"cdvd-dma-own.iso";
    (void)write_binary_if_changed(path,bytes);auto disc=std::make_shared<DiscImage>(path);
    for(unsigned blocks:{16u,32u}) {
        Iop iop;iop.reset(program(blocks),nullptr);iop.mount_disc(disc,Iop::DiscMedium::Ps2Cd);
        iop.run_until(100);require(!iop.stopped(),iop.stop_reason().value_or(""));
        command(iop,2);require(iop.ee_cdvd_read(0x1F402005u,1)&0x80u,"read is asynchronous");
        iop.run_until(20000);rejects([&]{(void)iop.ee_read(0x10000,4);},"not been written");
        require(iop.ee_cdvd_read(0x1F402008u,1)==0,"no IRQ before sector delivery");
        iop.run_until(50000);require(!iop.stopped(),iop.stop_reason().value_or(""));
        const auto transferred=blocks*128u;
        for(unsigned j=0;j<transferred;++j)require(iop.ee_read(0x10000+j,1)==bytes[j],"DMA transfers actual image bytes");
        if(blocks==16) {
            require(iop.ee_cdvd_read(0x1F402008u,1)==0 && (iop.ee_cdvd_read(0x1F402005u,1)&0x80u),
                    "exhausted DMA receiver stalls remaining sector without false CDVD completion");
            rejects([&]{(void)iop.ee_read(0x10800,4);},"not been written");
        } else {
            require(iop.ee_cdvd_read(0x1F402008u,1)==2 && !(iop.ee_cdvd_read(0x1F402005u,1)&0x80u),
                    "CDVD completes only after every requested sector was transferred");
            iop.ee_cdvd_write(0x1F402008u,1,3);
            require(iop.ee_cdvd_read(0x1F402008u,1)==0,"command reason is W1C");
        }
    }
    Iop dvd;dvd.reset(program(32),nullptr);dvd.mount_disc(disc,Iop::DiscMedium::Ps2Dvd,true);
    dvd.run_until(100);command(dvd,2,true,3);
    dvd.run_until(13000);
    rejects([&]{(void)dvd.ee_read(0x10000,4);},"not been written");
    dvd.run_until(40000);require(!dvd.stopped(),dvd.stop_reason().value_or(""));
    for(unsigned j=0;j<4096;++j)require(dvd.ee_read(0x10000+j,1)==bytes[j],
        "CD-style command on DVD transfers 2048 data bytes per sector without a raw envelope");
    require(dvd.ee_cdvd_read(0x1F402008u,1)==2,"DVD completion follows actual DMA delivery");
    Iop disabled;disabled.reset(program(16,false),nullptr);disabled.mount_disc(disc,Iop::DiscMedium::Ps2Cd);
    disabled.run_until(100);command(disabled,1);disabled.run_until(50000);
    require(!disabled.stopped(),disabled.stop_reason().value_or(""));
    rejects([&]{(void)disabled.ee_read(0x10000,4);},"not been written");
    require(disabled.ee_cdvd_read(0x1F402008u,1)==0,"DPCR-disabled channel cannot consume data or complete");
    rejects([&]{disabled.ee_cdvd_write(0x1F402004u,1,0);},"busy");
    Iop cav;cav.reset(program(16),nullptr);cav.mount_disc(disc,Iop::DiscMedium::Ps2Cd);
    cav.run_until(100);rejects([&]{command(cav,1,true);},"CAV radial timing is not verified");
    cav.reset(program(16),nullptr);cav.mount_disc(disc,Iop::DiscMedium::Ps2Cd,true);
    cav.run_until(100);command(cav,1,true);cav.run_until(30000);
    require(!cav.stopped() && cav.ee_read(0x10000,1)==bytes[0],"explicit timing experiment still transfers actual bytes");
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
