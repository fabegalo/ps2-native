#include "boot_checks.hpp"
#include "direct_program.hpp"
#include "m13_program.hpp"
#include "ps2native/analysis.hpp"
#include <iostream>

using namespace ps2native;
using namespace boot_checks;
int main() {
    try {
        const auto elf=parse_ee_elf(direct_test::elf(direct_test::cpu()));
        const auto report=analyze_ee_image(elf);
        require(report.json.find("\"call_edges\":[{")!=std::string::npos,"direct or proven constant calls exported");
        require(report.json.find("\"interpreter_boundaries\":[{")!=std::string::npos,"system opcodes remain explicit interpreter boundaries");
        require(report.json.find("\"end\":null")!=std::string::npos,"no guessed function limits");
        require(report.dot.find("delay-slot")!=std::string::npos,"CFG retains delay-slot qualification");
        const auto hints=parse_ghidra_csv("Name,Start,End,Size\r\nuser callback,0x00100200,0x0010020c,12\r\n");
        require(hints.size()==1 && hints[0].start==0x100200,"Ghidra CSV import");
        AnalysisOptions options;options.ghidra=hints;
        require(analyze_ee_image(elf,options).json.find("\"trusted_extent\":false")!=std::string::npos,"external ranges remain hints");
        rejects([&]{(void)parse_ghidra_csv("Name,Start,End,Size\nf,0x100000,0x100004,8\n");},"range");
        rejects([&]{(void)parse_ghidra_csv("Name,Start,End,Size\nf,0x100001,0x100005,4\n");},"range");
        rejects([&]{(void)parse_ghidra_csv("address,name\n");},"header");
        options.ghidra={{0x100200,0x200000,"oversized"}};
        rejects([&]{(void)analyze_ee_image(elf,options);},"leaves");
        const auto original=elf.bytes;options={};
        const auto hash=sha256_hex(original);(void)analyze_ee_image(elf,options);
        require(elf.bytes==original && sha256_hex(elf.bytes)==hash,"analysis never reapplies relocations or patches input");
        m20::Assembler a(direct_test::text);a.raw(0x7000003Fu);
        const auto unknown=parse_ee_elf(direct_test::elf({{{direct_test::text,5,m20::le_words(a.finish())}},0,{},{}}));
        require(analyze_ee_image(unknown).json.find("\"interpreter_decodable\":false")!=std::string::npos,"unknown instruction classified honestly");
        // Original ELF32 IRX metadata fixture: two sections, one typed module.
        std::vector<std::uint8_t> irx(0x180);
        irx[0]=0x7f;irx[1]='E';irx[2]='L';irx[3]='F';irx[4]=irx[5]=irx[6]=1;
        direct_test::put(irx,16,0xff80,2);direct_test::put(irx,18,8,2);direct_test::put(irx,20,1);
        direct_test::put(irx,32,0x80);direct_test::put(irx,40,52,2);direct_test::put(irx,46,40,2);direct_test::put(irx,48,2,2);
        direct_test::put(irx,0x80+40+4,0x70000080u);direct_test::put(irx,0x80+40+16,0x100);direct_test::put(irx,0x80+40+20,32);
        require(analyze_irx_metadata(irx).json.find("\"architecture\":\"IOP\"")!=std::string::npos,"IRX metadata does not decode R5900 or create fake AOT coverage");
        auto bad=irx;direct_test::put(bad,0x80+40+20,0x10000);
        rejects([&]{(void)analyze_irx_metadata(bad);},"outside file");
        std::cout<<"analysis: conservative CFG, Ghidra hints, unsupported encodings and IRX metadata passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
