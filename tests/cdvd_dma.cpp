#include "bios_program.hpp"
#include "boot_checks.hpp"
#include "iso_image.hpp"
#include "iop_native_fixture.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
std::vector<std::uint8_t> program(unsigned blocks,bool enabled=true,std::uint8_t howto=0x80u,unsigned words_per_block=32u) {
    m20::Assembler a(0xBFC00000u);
    const auto sw=[&](std::uint32_t address,std::uint32_t value){a.la(8,address);a.la(9,value);a.sw(9,0,8);};
    a.la(8,0xBF402006u);a.ori(9,0,howto);a.sb(9,0,8);
    sw(0xBF801578u,1);sw(0xBF8010F0u,enabled?0x8000u:0);
    sw(0xBF80157Cu,1);sw(0xBF8010F4u,0x00880000u);
    sw(0xBF8010B0u,0x10000u);sw(0xBF8010B4u,(blocks<<16u)|words_per_block);sw(0xBF8010B8u,0x41000200u);
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
            require(iop.ee_cdvd_read(0x1F402008u,1)==1 && !(iop.ee_cdvd_read(0x1F402005u,1)&0x80u),
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
    require(dvd.ee_cdvd_read(0x1F402008u,1)==1,"DVD completion follows actual DMA delivery");
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
    {
        iso_test::IsoOptions dual_options;dual_options.sectors=dual_options.written=64;
        const auto layer=iso_test::build_iso(dual_options);
        auto dual=layer;dual.resize(112u*2048u);
        std::copy(layer.begin(),layer.end(),dual.begin()+48u*2048u);
        const auto dual_path=std::filesystem::path(argv[1])/"cdvd-dual-own.iso";
        (void)write_binary_if_changed(dual_path,dual);
        auto layout=std::make_shared<DiscImage>(dual_path,DiscImage::DiagnosticDvdGeometry{48,true});
        rejects([&]{DiscImage bad(dual_path,DiscImage::DiagnosticDvdGeometry{49,true});},"inconsistent");
        std::array<std::uint8_t,2048> sector{};
        DiscImage normal(dual_path);rejects([&]{normal.read_sector(100,sector);},"outside");
        layout->read_sector(100,sector);
        require(std::equal(sector.begin(),sector.end(),dual.begin()+100u*2048u),"explicit layout exposes real second-volume sectors");
        rejects([&]{layout->read_sector(112,sector);},"outside");
        Iop dual_toc;dual_toc.reset(program(129,true,0x84u,4u),nullptr);dual_toc.mount_disc(layout,Iop::DiscMedium::Ps2Dvd);
        dual_toc.run_until(100);dual_toc.ee_cdvd_write(0x1F402005u,1,0);dual_toc.ee_cdvd_write(0x1F402004u,1,9);
        dual_toc.run_until(30000);require(!dual_toc.stopped(),dual_toc.stop_reason().value_or(""));
        require(dual_toc.ee_read(0x1000E,1)==0x31 && dual_toc.ee_read(0x1000F,1)==0x10 &&
            dual_toc.ee_read(0x10015,1)==0xFD && dual_toc.ee_read(0x10016,1)==0 && dual_toc.ee_read(0x10017,1)==0x0F &&
            dual_toc.ee_read(0x10019,1)==3 && dual_toc.ee_read(0x1001A,1)==0 && dual_toc.ee_read(0x1001B,1)==0x2F,
            "explicit OTP geometry encodes independent ECMA-267 end addresses and dual-layer flags");
        rejects([&]{(void)dual_toc.ee_read(0x10000,1);},"not been written");
        rejects([&]{(void)dual_toc.ee_read(0x1001C,1);},"not been written");
        for(const bool crossing:{false,true}) {
            Iop read;read.reset(program(43,true,0x8Cu,12u),nullptr);read.mount_disc(layout,Iop::DiscMedium::Ps2Dvd);
            read.run_until(100);
            for(unsigned j=0;j<11;++j)read.ee_cdvd_write(0x1F402005u,1,j==0 ? (crossing?47u:16u) : j==4 ? (crossing?2u:1u) : j==9 ? 2u : 0u);
            if(crossing) {
                rejects([&]{read.ee_cdvd_write(0x1F402004u,1,8);},"second-layer sectors are not modeled");
                rejects([&]{(void)read.ee_read(0x10000,1);},"not been written");
            } else {
                read.ee_cdvd_write(0x1F402004u,1,8);read.run_until(1200000);
                require(!read.stopped() && read.ee_cdvd_read(0x1F402008u,1)==1,"dual-layer media completes actual first-layer DMA");
                require(read.ee_read(0x10000,1)==0x20 && read.ee_read(0x10001,1)==3 && read.ee_read(0x10003,1)==16,"first-layer raw sector carries its physical sector ID");
                for(unsigned n=0;n<2048;++n)require(read.ee_read(0x1000C+n,1)==dual[16u*2048u+n],"first-layer raw read contains original PVD bytes");
                rejects([&]{(void)read.ee_read(0x10004,1);},"not been written");
            }
        }
    }
    Iop toc;toc.reset(program(129,true,0x84u,4u),nullptr);toc.mount_disc(disc,Iop::DiscMedium::Ps2Dvd);
    toc.run_until(100);toc.ee_cdvd_write(0x1F402005u,1,0u);toc.ee_cdvd_write(0x1F402004u,1,9u);
    require(toc.ee_cdvd_read(0x1F402005u,1)&0x80u,"GetToc is asynchronous");
    toc.run_until(10000);rejects([&]{(void)toc.ee_read(0x1000Cu,4);},"not been written");
    toc.run_until(30000);require(!toc.stopped(),toc.stop_reason().value_or(""));
    require(toc.ee_cdvd_read(0x1F402008u,1)==1 && !(toc.ee_cdvd_read(0x1F402005u,1)&0x80u),
        "GetToc completes after 2064-byte DMA3 transfer");
    const auto end_psn=static_cast<std::uint32_t>(0x030000u+disc->image_size()/2048u-1u);
    require(toc.ee_read(0x1000Cu,1)==0x01u && toc.ee_read(0x1000Du,1)==0x02u &&
        toc.ee_read(0x1000Eu,1)==0x01u && toc.ee_read(0x1000Fu,1)==0x00u &&
        toc.ee_read(0x10010u,1)==0x00u && toc.ee_read(0x10011u,1)==0x03u &&
        toc.ee_read(0x10012u,1)==0x00u && toc.ee_read(0x10013u,1)==0x00u &&
        toc.ee_read(0x10014u,1)==0x00u && toc.ee_read(0x10015u,1)==((end_psn>>16u)&0xFFu) &&
        toc.ee_read(0x10016u,1)==((end_psn>>8u)&0xFFu) && toc.ee_read(0x10017u,1)==(end_psn&0xFFu) &&
        toc.ee_read(0x10018u,4)==0u && toc.ee_cdvd_read(0x1F402013u,1)==0x12u,
        "GetToc populates ECMA-267 single-layer DVD-ROM Physical Format Information geometry and 4x DVD speed");
    rejects([&]{(void)toc.ee_read(0x10000u,1);},"not been written");
    rejects([&]{(void)toc.ee_read(0x1001Cu,1);},"not been written");
    rejects([&]{(void)toc.ee_read(0x1080Fu,1);},"not been written");
    Iop toc_cd;toc_cd.reset(program(129,true,0x84u,4u),nullptr);toc_cd.mount_disc(disc,Iop::DiscMedium::Ps2Cd);
    toc_cd.run_until(100);require(toc_cd.ee_cdvd_read(0x1F402013u,1)==0x04u,"mounted CD reports nominal 24x spindle speed");
    toc_cd.ee_cdvd_write(0x1F402005u,1,0u);
    rejects([&]{toc_cd.ee_cdvd_write(0x1F402004u,1,9u);},"GetToc requires DVD media");
    Iop toc_howto;toc_howto.reset(program(32,true,0x80u,32u),nullptr);toc_howto.mount_disc(disc,Iop::DiscMedium::Ps2Dvd);
    toc_howto.run_until(100);toc_howto.ee_cdvd_write(0x1F402005u,1,0u);
    rejects([&]{toc_howto.ee_cdvd_write(0x1F402004u,1,9u);},"HOWTO 0x84");
    Iop toc_param;toc_param.reset(program(129,true,0x84u,4u),nullptr);toc_param.mount_disc(disc,Iop::DiscMedium::Ps2Dvd);
    toc_param.run_until(100);toc_param.ee_cdvd_write(0x1F402005u,1,1u);
    rejects([&]{toc_param.ee_cdvd_write(0x1F402004u,1,9u);},"layer-0 parameter 00");
    for(const std::uint8_t ncmd:{2u,4u}) {
        toc.ee_cdvd_write(0x1F402008u,1,3u);
        toc.ee_cdvd_write(0x1F402004u,1,ncmd);
        require((toc.ee_cdvd_read(0x1F402005u,1)&0x80u) && toc.ee_cdvd_read(0x1F402008u,1)==0,
            "Standby and Pause are asynchronous");
        toc.run_until(toc.cycle()+5u);
        require(!toc.stopped() && toc.ee_cdvd_read(0x1F402008u,1)==1 && !(toc.ee_cdvd_read(0x1F402005u,1)&0x80u),
            "Standby and Pause complete with IRQ2");
    }
    toc.ee_cdvd_write(0x1F402008u,1,3u);
    for(unsigned j=0;j<4;++j)toc.ee_cdvd_write(0x1F402005u,1,j==0 ? 1u : 0u);
    toc.ee_cdvd_write(0x1F402004u,1,5u);
    require(toc.ee_cdvd_read(0x1F402005u,1)&0x80u,"Seek is asynchronous");
    toc.run_until(50000);
    require(!toc.stopped() && toc.ee_cdvd_read(0x1F402008u,1)==1 && !(toc.ee_cdvd_read(0x1F402005u,1)&0x80u),
        "Seek completes after head movement");
    for(unsigned j=0;j<4;++j)toc.ee_cdvd_write(0x1F402005u,1,0xFFu);
    rejects([&]{toc.ee_cdvd_write(0x1F402004u,1,5u);},"outside mounted disc image");
    Iop read_dvd;read_dvd.reset(program(86,true,0x8Cu,12u),nullptr);read_dvd.mount_disc(disc,Iop::DiscMedium::Ps2Dvd,true);
    read_dvd.run_until(100);
    for(unsigned j=0;j<11;++j)read_dvd.ee_cdvd_write(0x1F402005u,1,j==4 ? 2u : j==9 ? 0x83u : 0u);
    read_dvd.ee_cdvd_write(0x1F402004u,1,8u);
    require(read_dvd.ee_cdvd_read(0x1F402005u,1)&0x80u,"ReadDvd is asynchronous");
    read_dvd.run_until(40000);require(!read_dvd.stopped(),read_dvd.stop_reason().value_or(""));
    require(read_dvd.ee_cdvd_read(0x1F402008u,1)==1 && !(read_dvd.ee_cdvd_read(0x1F402005u,1)&0x80u),
        "ReadDvd completes after 2064-byte DVD frame transfers");
    for(unsigned s=0;s<2u;++s) {
        const auto base=0x10000u+s*2064u,psn=0x030000u+s;
        require(read_dvd.ee_read(base+0u,1)==0x20u &&
            read_dvd.ee_read(base+1u,1)==((psn>>16u)&0xFFu) &&
            read_dvd.ee_read(base+2u,1)==((psn>>8u)&0xFFu) &&
            read_dvd.ee_read(base+3u,1)==(psn&0xFFu),
            "ReadDvd populates ECMA-267 single-layer Data Area Sector ID");
        rejects([&]{(void)read_dvd.ee_read(base+4u,1);},"not been written");
        rejects([&]{(void)read_dvd.ee_read(base+11u,1);},"not been written");
        for(unsigned j=0;j<2048u;++j)
            require(read_dvd.ee_read(base+12u+j,1)==bytes[s*2048u+j],"ReadDvd transfers 2048 user data bytes at +0x0C");
        rejects([&]{(void)read_dvd.ee_read(base+2060u,1);},"not been written");
        rejects([&]{(void)read_dvd.ee_read(base+2063u,1);},"not been written");
    }
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
