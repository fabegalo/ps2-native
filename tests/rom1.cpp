// Original fixtures only; no console firmware or translated code.
#include "bios_program.hpp"
#include "boot_checks.hpp"
#include "../tools/dev_process.hpp"
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
int main(int argc, char** argv) try {
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
    if(argc==3) {
        // Synthetic firmware with a known profile's ROMVER, never a Sony dump.
        const std::string version="0200EC20040614";
        const auto at=std::search(raw.begin(),raw.end(),std::begin(m20::kRomver),std::end(m20::kRomver)-1);
        require(at!=raw.end(),"fixture ROMVER found");
        std::copy(version.begin(),version.end(),at);
        const std::filesystem::path root=argv[2],tool=argv[1];
        ps2native::create_directories(root);
        const auto rom0_path=root/"fixture.bin",rom1_path=root/"companion.bin";
        auto cli_bytes=bytes;cli_bytes[70]='E'; // DVDVERE for the selected test profile.
        write_file_atomically(rom0_path,raw);write_file_atomically(rom1_path,cli_bytes);
        std::vector<std::filesystem::path> args{rom0_path,"--console=SCPH-70004:GH-032-11",
            "--expect-sha256="+sha256_hex(raw),"--rom1="+path_utf8(rom1_path),
            "--expect-rom1-sha256="+sha256_hex(cli_bytes),"--rom1-build=fixture",
            "--rom1-dvd-version=0.01X","--rom1-provenance=original synthetic test pairing","--max-instructions=1"};
        const auto check=[&](const char* name,int expected,const std::string& message) {
            const auto log=root/(std::string(name)+".log");
            const auto result=dev::process(tool,args,log,10);
            const auto text=read_file(log);const std::string output(text.begin(),text.end());
            require(!result.timed_out && result.exit_code==expected && output.find(message)!=std::string::npos,
                    std::string(name)+": "+output);
        };
        check("paired",0,"# rom1 sha256 "+sha256_hex(cli_bytes));
        const auto good=args;
        args[4]="--expect-rom1-sha256="+std::string(64,'0');check("wrong-hash",1,"SHA-256 mismatch");
        args=good;args.erase(args.begin()+2);check("unpinned-rom0",1,"--rom1 requires");
        args=good;args.erase(args.begin()+7);check("no-provenance",1,"explicit compatibility provenance");
        args=good;args.erase(args.begin()+3);check("no-rom1",1,"ROM1 identity options require --rom1");
        args=good;args.emplace_back("--assume-sio2-disconnected=8192");
        check("disconnected",0,"# DIAGNOSTIC SIO2 all four ports disconnected; timeout 8192");
        args.back()="--assume-sio2-disconnected=0";check("zero-timeout",1,"SIO2 timeout cycles must be");
        args.back()="--assume-sio2-disconnected=4294967296";check("large-timeout",1,"SIO2 timeout cycles must be");
        args=good;args.emplace_back("--assume-sio2-reset-clears-completion");
        check("reset-completion",0,"# DIAGNOSTIC SIO2 CTRL reset clears pending completion");
        args=good;args.emplace_back("--assume-spu2-7ca-latch");
        check("spu2-7ca",0,"# DIAGNOSTIC SPU2 0x7CA retained as a latch");
        args=good;args.emplace_back("--assume-spu2-reserved-latches");
        check("spu2-reserved",0,"# DIAGNOSTIC SPU2 unassigned registers retained as latches");
        args=good;args.emplace_back("--assume-sio2-dma-word-cycles=4");
        check("sio2-dma",0,"# DIAGNOSTIC SIO2 DMA word timing 4");
        args.back()="--assume-sio2-dma-word-cycles=0";check("zero-dma",1,"SIO2 DMA word cycles must be");
        args=good;args.emplace_back("--assume-dev9-power-reset=0");
        check("dev9-power",0,"# DIAGNOSTIC DEV9 power reset bits 0/2 = 0");
        args.back()="--assume-dev9-power-reset=2";check("invalid-dev9-power",1,"DEV9 power reset must be");
        args=good;args.emplace_back("--assume-dev9-control-latches");
        check("dev9-controls",0,"# DIAGNOSTIC DEV9 controls retained as latches");
        args=good;args.emplace_back("--assume-spu2-effect-address-reserved-zero");
        check("spu2-effect-address",0,"# DIAGNOSTIC SPU2 effect-address reserved readback bits zero");
        args=good;args.emplace_back("--assume-spu2-dma-wrap-to-zero");
        check("spu2-wrap",0,"# DIAGNOSTIC SPU2 normal DMA wraps to sound RAM word zero");
        args=good;args.emplace_back("--assume-forbid-dvdp-error-bit-clear");
        check("forbid-dvdp",0,"# DIAGNOSTIC MECHACON ForbidDVDP reply bit 7 clear");
        args=good;args.emplace_back("--assume-sio2-port-timing=0");
        check("port-timing",0,"# DIAGNOSTIC SIO2 RECV2 port timing bits 4/5 = 0");
        args.back()="--assume-sio2-port-timing=4";check("invalid-port-timing",1,"SIO2 port timing must be");
        args=good;args.emplace_back("--assume-dev9-power-settle-cycles=1");
        check("dev9-settling",0,"# DIAGNOSTIC DEV9 power settling 1");
        args.back()="--assume-dev9-power-settle-cycles=0";check("zero-dev9-settling",1,"DEV9 power settling cycles must be");
        args=good;args.emplace_back("--assume-smap-reset-cycles=3");
        check("smap-reset",0,"# DIAGNOSTIC SMAP FIFO reset completion 3");
        args.back()="--assume-smap-reset-cycles=0";check("zero-smap-reset",1,"SMAP reset cycles must be");
        args=good;args.emplace_back("--trace-out="+path_utf8(rom1_path));
        check("protected-trace",1,"boot output must not overwrite firmware input");
        args.back()="--frame-out="+path_utf8(rom1_path);
        check("protected-frame",1,"boot output must not overwrite firmware input");
        require(sha256_file(rom1_path)==sha256_hex(cli_bytes),"ROM1 input remains unchanged");
    }
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
