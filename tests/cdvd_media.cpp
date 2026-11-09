#include "bios_program.hpp"
#include "boot_checks.hpp"
#include "iso_image.hpp"
#include "iop_native_fixture.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
int main(int argc,char** argv) try {
    require(argc==2,"fixture output directory");
    const auto path=std::filesystem::path(argv[1])/"cdvd-media-own.iso";
    (void)write_binary_if_changed(path,iso_test::build_iso({}));
    auto disc=std::make_shared<DiscImage>(path);
    m20::Assembler a(0xBFC00000u);
    a.la(8,0xBF402000u);a.lbu(9,0xA,8);a.nop();a.andi(9,9,0x3F);a.sw(9,0x1000,0);
    a.lbu(9,0xA,8);a.nop();a.i(0x0E,9,9,1);a.andi(9,9,1);a.sw(9,0x100C,0);
    a.lbu(9,0xB,8);a.nop();a.andi(9,9,0x3F);a.sw(9,0x1004,0);
    a.lbu(9,0xF,8);a.nop();a.sw(9,0x1008,0);
    a.label("idle");a.beq(0,0,"idle");a.nop();
    auto rom=m20::build_firmware().rom;const auto code=m20::le_words(a.finish());
    std::copy(code.begin(),code.end(),rom.begin());
    Iop iop;iop.reset(rom,nullptr);
    require(iop.ee_cdvd_read(0x1F40200Fu,1)==0,"an empty emulated drive reports the documented SCECdNODISC type");
    rejects([&]{(void)iop.ee_cdvd_read(0x1F40200Fu,4);},"requires a byte access");
    rejects([&]{(void)iop.ee_cdvd_read(0x1F402006u,1);},"explicit disc");
    require(iop.ee_cdvd_read(0x1F402008u,1)==0,"reading absent media does not fabricate command completion or an interrupt");
    {
        m20::Assembler empty_code(0xBFC00000u);empty_code.la(8,0xBF40200Fu);empty_code.lbu(9,0,8);empty_code.nop();
        empty_code.sw(9,0x1010,0);empty_code.label("idle");empty_code.beq(0,0,"idle");empty_code.nop();
        auto empty_rom=m20::build_firmware().rom;const auto bytes=m20::le_words(empty_code.finish());
        std::copy(bytes.begin(),bytes.end(),empty_rom.begin());
        Iop empty;empty.reset(empty_rom,nullptr);empty.run_until(64);
        require(!empty.stopped() && empty.ee_read(0x1010,4)==0,"IOP firmware consumes the same real no-disc register through a byte load");
        Memory bus;bus.reset_for_boot(empty_rom);std::uint64_t clock=0;bus.set_clock(&clock);
        const auto type=bus.load8_observed(0xBF40200Fu);
        require(!type.value && !type.unknown,"EE SBUS exposes that same known media state without an RPC substitute");
    }
    iop.mount_disc(disc,Iop::DiscMedium::Ps2Cd);
    require(iop.ee_cdvd_read(0x1F40200Fu,1)==0x12,"explicit PS2 CD type");
    rejects([&]{(void)iop.ee_cdvd_read(0x1F40200Au,1);},"bits 6-7");
    iop.ee_cdvd_write(0x1F402016u,1,5);
    require(iop.ee_cdvd_read(0x1F402018u,1)==0,"sticky latch update succeeds while idle");
    iop.ee_cdvd_write(0x1F402004u,1,0); // NOP pending: sticky update is busy.
    iop.ee_cdvd_write(0x1F402016u,1,5);
    require(iop.ee_cdvd_read(0x1F402018u,1)==1,"busy command does not report a completed update");
    iop.run_until(100);
    require(!iop.stopped(),iop.stop_reason().value_or(""));
    require(iop.ee_read(0x100C,4)==1,"DVD status XORI/ANDI consumes only the known tray bit without inventing bits 6-7");
    require(iop.ee_read(0x1000,4)==0xA && iop.ee_read(0x1004,4)==0xA && iop.ee_read(0x1008,4)==0x12,
            "IOP observes actual mounted state, masks undocumented bits and reads the medium type");
    rejects([&]{iop.mount_disc(disc,Iop::DiscMedium::Ps2Dvd);},"active reset state");
    iop.reset(rom,nullptr);
    require(iop.ee_cdvd_read(0x1F40200Fu,1)==0,"reset removes the mounted image and returns to an empty drive");
    iop.mount_disc(disc,Iop::DiscMedium::Ps2Dvd);
    require(iop.ee_cdvd_read(0x1F40200Fu,1)==0x14,"ISO media type is explicit, not guessed from filename");
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
