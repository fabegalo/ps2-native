#include "boot_checks.hpp"
#include "direct_program.hpp"
#include "m13_program.hpp"
#include "ps2native/analysis.hpp"
#include "../tools/ee_alias_cli.hpp"
#include <iostream>

using namespace ps2native;
using namespace boot_checks;
int main() {
    try {
        const auto elf=parse_ee_elf(direct_test::elf(direct_test::cpu()));
        const auto report=analyze_ee_image(elf);
        {
            constexpr std::uint32_t words=131073;
            ElfImage large;large.entry=direct_test::text;large.bytes.resize(words*4u);
            large.segments.push_back({large.entry,0,words*4u,words*4u,5});
            direct_test::put(large.bytes,(words-1u)*4u,0x0000000Du);
            auto graph=analyze_control_flow(large,{});
            require(graph.instructions.size()==words,"file-backed code beyond the old M1 instruction ceiling is discovered");
            direct_test::put(large.bytes,(words-1u)*4u,0);
            graph=analyze_control_flow(large,{}, {},true);
            require(graph.instructions.size()==words && graph.unsupported_boundaries.size()==1 &&
                graph.unsupported_boundaries.front().address==large.entry+words*4u,
                "exhausting executable capacity preserves the explicit outside-image boundary");
        }
        for(const auto alias:{0x80000000u,0xA0000000u}) {
            const auto mapped=cli::ee_execution_alias(elf,alias);
            require(mapped.entry==(elf.entry|alias) && mapped.instruction_at(mapped.entry)==elf.instruction_at(elf.entry) && mapped.bytes==elf.bytes,
                "execution alias preserves source bytes and maps the same instruction");
        }
        auto symbolic=elf;symbolic.function_entries.push_back(elf.entry);
        rejects([&]{(void)cli::ee_execution_alias(symbolic,0x80000000u);},"symbol/relocation");
        rejects([&]{(void)cli::ee_execution_alias(elf,0x40000000u);},"invalid");
        std::optional<std::uint32_t> alias;
        require(cli::parse_execution_alias("--execution-alias=0x80000000",alias) && *alias==0x80000000u,"execution alias option");
        rejects([&]{(void)cli::parse_execution_alias("--execution-alias=0",alias);},"duplicate");
        alias.reset();rejects([&]{(void)cli::parse_execution_alias("--execution-alias=0x180000000",alias);},"must be");
        require(report.json.find("\"call_edges\":[{")!=std::string::npos,"direct or proven constant calls exported");
        require(report.json.find("\"unsupported_boundaries\":[]")!=std::string::npos,"translated system opcodes do not create unsupported AOT boundaries");
        require(report.json.find("\"end\":null")!=std::string::npos,"no guessed function limits");
        require(report.dot.find("delay-slot")!=std::string::npos,"CFG retains delay-slot qualification");
        const auto table_program=m13::build_program();
        auto table_bytes=m13::build_elf(table_program,false);
        m13::detail::put32(table_bytes,108,7); // The table's PT_LOAD also permits execution.
        const auto table_report=analyze_ee_image(parse_ee_elf(table_bytes));
        const auto table_edge="\"from_pc\":"+std::to_string(table_program.at("select_a_jr"))+",\"to\":"+std::to_string(table_program.at("a_case3"))+",\"kind\":\"proven bounded jump table\"";
        require(table_report.json.find(table_edge)!=std::string::npos,"analysis exports the CFG's proven targets from executable table storage");
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
        const auto unknown_report=analyze_ee_image(unknown);
        require(unknown_report.json.find("\"instruction_decodable\":false")!=std::string::npos,"unknown instruction classified honestly");
        require(unknown_report.json.find("\"unsupported_boundaries\":[{")!=std::string::npos,"unknown instruction retains an explicit unsupported AOT boundary");
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
