// Original fixtures only; no console firmware or translated code.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include <iostream>
using namespace ps2native;
using namespace boot_checks;
namespace {
void put(std::vector<std::uint8_t>& b,unsigned at,std::uint32_t v) {
    for(unsigned j=0;j<4;++j)b.at(at+j)=static_cast<std::uint8_t>(v>>(8u*j));
}
std::vector<std::uint8_t> companion() {
    std::vector<std::uint8_t> b(256,0);
    const char* names[]={"RESET","ROMDIR","EXTINFO","DVDVERX"};
    const unsigned sizes[]={16,80,12,6};
    for(unsigned i=0;i<4;++i) {
        const auto at=16u+i*16u;
        std::copy_n(names[i],std::char_traits<char>::length(names[i]),b.begin()+at);
        b[at+10]=i==1 ? 12 : 0;put(b,at+12,sizes[i]);
    }
    b[98]=8;b[99]=3; // EXTINFO comment, eight bytes including NUL/padding.
    const std::string build="fixture",version="0.01X\n";
    std::copy(build.begin(),build.end(),b.begin()+100);
    std::copy(version.begin(),version.end(),b.begin()+112);
    put(b,0,0x1234ABCD);put(b,128,0x24021234);put(b,132,0xAC021004);
    put(b,136,0x0B800022);put(b,140,0); // J BE000088 + NOP: remain in the supplied ROM.
    return b;
}
}
int main() try {
    auto raw=m20::build_firmware().rom;
    m20::Assembler a(0xBFC00000u);
    a.la(8,0xBE000000u);a.lw(9,0,8);a.nop();a.sw(9,0x1000,0);
    a.la(8,0xBE000080u);a.jr(8);a.nop();
    const auto code=m20::le_words(a.finish());std::copy(code.begin(),code.end(),raw.begin());
    const auto bios=load_bios_rom0(raw);const auto bytes=companion();
    ConsoleHardware hardware{};hardware.model="fixture";hardware.mainboard="board-A";
    hardware.romver=bios.version.romver;
    Rom1Identity identity{bios.sha256,bios.version.romver,"fixture","board-A",sha256_hex(bytes),
                          "fixture","0.01X","original synthetic test pairing"};
    const auto rom1=load_bios_rom1(bytes,bios,hardware,identity);
    for(unsigned field=0;field<8;++field) {
        auto bad=identity;
        switch(field) {
            case 0:bad.rom0_sha256[0]^=1;break;case 1:bad.romver+="x";break;
            case 2:bad.model+="x";break;case 3:bad.mainboard+="x";break;
            case 4:bad.sha256[0]^=1;break;case 5:bad.build+="x";break;
            case 6:bad.dvd_version+="x";break;default:bad.provenance.clear();break;
        }
        rejects([&]{(void)load_bios_rom1(bytes,bios,hardware,bad);},"ROM1 rejected");
    }
    auto corrupt=bytes;corrupt[44]=96;auto corrupt_id=identity;corrupt_id.sha256=sha256_hex(corrupt);
    rejects([&]{(void)load_bios_rom1(corrupt,bios,hardware,corrupt_id);},"ROMDIR size");
    Memory memory;EEState state;prepare_ee_boot(bios,nullptr,state,memory,&hardware,nullptr,nullptr,&rom1);
    require(memory.read32(0xBE000000)==0x1234ABCD && memory.read32(0x9E000000)==0x1234ABCD,
            "EE cached and uncached aliases see actual companion bytes");
    bus_fault([&]{memory.write32(0xBE000000,0);},BusFault::RomWrite,"ROM1");
    bus_fault([&]{(void)memory.read32(0xBE000100);},BusFault::Unmapped,"ROM1");
    bus_fault([&]{(void)memory.read32(0xBE400000);},BusFault::Device,"ROM2");
    state.retired_instructions=300;memory.sync_iop();
    require(memory.iop().ee_read(0x1000,4)==0x1234ABCD && memory.iop().ee_read(0x1004,4)==0x1234,
            "IOP data loads and instruction fetches use the same ROM1 bytes");
    prepare_ee_boot(bios,nullptr,state,memory,&hardware);
    bus_fault([&]{(void)memory.read32(0xBE000000);},BusFault::Unmapped,"ROM1");
    state.retired_instructions=300;memory.sync_iop();
    require(memory.iop().stopped() && memory.iop().stop_reason()->find("ROM1 bytes are not supplied")!=std::string::npos,
            "reset without a companion clears IOP mapping, never reuses another profile's ROM1");
    auto altered=rom1;altered.bytes[0]^=1;
    rejects([&]{prepare_ee_boot(bios,nullptr,state,memory,&hardware,nullptr,nullptr,&altered);},"SHA-256 mismatch");
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
