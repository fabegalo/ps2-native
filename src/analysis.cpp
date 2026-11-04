#include "ps2native/analysis.hpp"
#include "ps2native/diagnostics.hpp"
#include "ps2native/sha256.hpp"
#include <algorithm>
#include <charconv>
#include <sstream>

namespace ps2native {
namespace {
std::string hex(std::uint32_t value) { std::ostringstream out;out<<"0x"<<std::hex<<value;return out.str(); }
class Metadata {
public:
    explicit Metadata(std::span<const std::uint8_t> input) : data(input) {
        if (data.size()<52 || data[0]!=0x7f || data[1]!='E' || data[2]!='L' || data[3]!='F' ||
            data[4]!=1 || data[5]!=1 || data[6]!=1 || u16(18)!=8 || u32(20)!=1 || u16(40)!=52)
            throw std::runtime_error("metadata requires ELF32 little-endian MIPS v1");
        offset=u32(32);count=u16(48);names=u16(50);
        if(!offset && !count)return;
        if(!offset || !count || count>4096 || u16(46)!=40)throw std::runtime_error("invalid metadata section table");
        (void)range(offset,std::uint64_t{count}*40);
        if(names>=count)throw std::runtime_error("invalid section-name table index");
        if(names && field(names,4)!=3)throw std::runtime_error("section names require SHT_STRTAB");
        for(unsigned i=0;i<count;++i)if(field(i,4)!=8)(void)section(i);
    }
    std::span<const std::uint8_t> range(std::uint64_t begin,std::uint64_t size) const {
        if(begin>data.size() || size>data.size()-begin)throw std::runtime_error("ELF metadata outside file");
        return data.subspan(static_cast<std::size_t>(begin),static_cast<std::size_t>(size));
    }
    std::uint32_t u32(std::size_t p)const {
        auto b=range(p,4);return b[0]|(std::uint32_t{b[1]}<<8)|(std::uint32_t{b[2]}<<16)|(std::uint32_t{b[3]}<<24);
    }
    std::uint16_t u16(std::size_t p)const {auto b=range(p,2);return static_cast<std::uint16_t>(b[0]|(unsigned{b[1]}<<8));}
    std::uint32_t field(unsigned i,unsigned byte)const {
        if(i>=count || byte>36 || byte%4)throw std::runtime_error("invalid metadata section field");
        return u32(static_cast<std::size_t>(offset)+i*40u+byte);
    }
    std::span<const std::uint8_t> section(unsigned i)const {return range(field(i,16),field(i,20));}
    std::string string(unsigned table,std::uint32_t at)const {
        if(field(table,4)!=3)throw std::runtime_error("symbol names require string table");
        const auto bytes=section(table);
        if(at>=bytes.size())throw std::runtime_error("ELF string offset outside table");
        const auto end=std::find(bytes.begin()+at,bytes.end(),std::uint8_t{});
        if(end==bytes.end())throw std::runtime_error("unterminated ELF metadata string");
        return {bytes.begin()+at,end};
    }
    std::string name(unsigned i)const {return names ? string(names,field(i,0)) : std::string{};}
    std::span<const std::uint8_t> data;
    std::uint32_t offset{};unsigned count{},names{};
};
std::string symbols(const ElfImage& elf) {
    // ROM0 code images do not have ELF section headers.
    if(elf.bytes.size()<4 || elf.bytes[0]!=0x7f)return "[]";
    Metadata meta(elf.bytes);std::ostringstream out;out<<'[';bool comma=false;
    for(unsigned i=0;i<meta.count;++i) {
        const auto type=meta.field(i,4);if(type!=2 && type!=11)continue;
        const auto table=meta.section(i);const auto strings=meta.field(i,24);
        if(meta.field(i,36)!=16 || table.size()%16 || strings>=meta.count)throw std::runtime_error("invalid symbol metadata table");
        const auto base=meta.field(i,16);
        for(std::size_t n=0;n<table.size();n+=16) {
            const auto at=static_cast<std::size_t>(base)+n;
            if(!meta.u16(at+14) || !meta.u32(at))continue;
            const auto address=meta.u32(at+4),size=meta.u32(at+8);
            const auto kind=table[n+12]&15u;
            const LoadSegment* segment{};if(kind==2)try{segment=&elf.code_segment_at(address);}catch(const std::exception&){}
            const bool valid=segment && size && std::uint64_t{address}+size<=std::uint64_t{segment->vaddr}+segment->file_size;
            if(comma)out<<',';comma=true;
            out<<"{\"name\":"<<json_string(meta.string(strings,meta.u32(at)))<<",\"address\":"<<address
               <<",\"type\":"<<unsigned(kind)<<",\"binding\":"<<unsigned(table[n+12]>>4u)<<",\"section\":"<<meta.u16(at+14)
               <<",\"declared_size\":"<<size<<",\"verified_function_end\":"<<(valid ? std::to_string(std::uint64_t{address}+size) : "null")<<'}';
        }
    }
    return out.str()+']';
}
std::uint32_t number(std::string_view text,bool decimal=false) {
    if(text.empty())throw std::runtime_error("empty map number");
    unsigned base=decimal ? 10 : 16;
    if(text.starts_with("0x") || text.starts_with("0X")){text.remove_prefix(2);base=16;}
    std::uint32_t n{};const auto result=std::from_chars(text.data(),text.data()+text.size(),n,static_cast<int>(base));
    if(result.ec!=std::errc{} || result.ptr!=text.data()+text.size() || text.empty())throw std::runtime_error("invalid map number");
    return n;
}
}
std::vector<AnalysisHint> parse_ghidra_csv(std::string_view text) {
    std::istringstream input{std::string(text)};std::string line;
    if(!std::getline(input,line))throw std::runtime_error("empty Ghidra CSV");
    if(!line.empty() && line.back()=='\r')line.pop_back();
    if(line!="Name,Start,End,Size")throw std::runtime_error("Ghidra CSV header must be Name,Start,End,Size (End exclusive)");
    std::vector<AnalysisHint> hints;std::set<std::uint32_t> starts;
    while(std::getline(input,line)) {
        if(!line.empty() && line.back()=='\r')line.pop_back();if(line.empty())continue;
        if(line.size()>8192 || hints.size()>=65536)throw std::runtime_error("Ghidra map limit exceeded");
        // Split the numeric suffix, preserving commas in a user label. Exported
        // labels are metadata only; they are never executed or inserted in C++.
        auto c=line.rfind(','),b=c==std::string::npos ? c : line.rfind(',',c-1),a=b==std::string::npos ? b : line.rfind(',',b-1);
        if(a==std::string::npos || !a)throw std::runtime_error("invalid Ghidra CSV row");
        const auto start=number(std::string_view(line).substr(a+1,b-a-1)),end=number(std::string_view(line).substr(b+1,c-b-1));
        const auto size=number(std::string_view(line).substr(c+1),true);
        if((start&3u) || end<=start || std::uint64_t{start}+size!=end || !starts.insert(start).second)
            throw std::runtime_error("invalid or duplicate Ghidra range");
        hints.push_back({start,end,line.substr(0,a)});
    }
    return hints;
}
AnalysisReport analyze_ee_image(const ElfImage& elf,const AnalysisOptions& options) {
    auto roots=options.entries;
    for(const auto& hint:options.ghidra) {
        const auto& segment=elf.code_segment_at(hint.start);
        if(hint.end<=hint.start || std::uint64_t{hint.end}>std::uint64_t{segment.vaddr}+segment.file_size)
            throw std::runtime_error("Ghidra hint leaves its executable segment");
        roots.push_back(hint.start);
    }
    const auto graph=analyze_control_flow(elf,roots,options.tables,true);
    std::uint64_t executable_bytes=0;for(const auto& s:elf.segments)if(s.flags&1u)executable_bytes+=s.file_size;
    AnalysisReport report;std::ostringstream json,text,dot;
    json<<"{\"schema\":\"ps2n-analysis-v1\",\"architecture\":\"R5900\",\"sha256\":"<<json_string(sha256_hex(elf.bytes))
        <<",\"entry\":"<<elf.entry<<",\"executable_bytes\":"<<executable_bytes
        <<",\"aot_eligible_instructions\":"<<graph.instructions.size()
        <<",\"coverage_scope\":\"conservative CFG eligibility, not runtime coverage; unvisited executable bytes may be data; decode support does not prove runtime operand/device support\""
        <<",\"symbols\":"<<symbols(elf)<<",\"function_roots\":[";
    bool comma=false;
    for(auto entry:graph.function_entries) {
        if(comma)json<<',';comma=true;std::string evidence="direct control flow or relocation";
        if(entry==elf.entry)evidence="ELF entry";
        else if(std::find(elf.function_entries.begin(),elf.function_entries.end(),entry)!=elf.function_entries.end())evidence="ELF STT_FUNC";
        else if(std::find(roots.begin(),roots.end(),entry)!=roots.end())evidence="explicit developer hint";
        json<<"{\"entry\":"<<entry<<",\"end\":null,\"evidence\":"<<json_string(evidence)<<'}';
    }
    json<<"],\"ghidra_hints\":[";comma=false;
    for(const auto& hint:options.ghidra) {
        if(comma)json<<',';comma=true;
        json<<"{\"name\":"<<json_string(hint.name)<<",\"start\":"<<hint.start<<",\"reported_end\":"<<hint.end
            <<",\"trusted_extent\":false}";
    }
    json<<"],\"instructions\":[";comma=false;
    dot<<"digraph ps2n_cfg {\n  label=\"Conservative CFG; branch edges include delay-slot semantics\";\n";
    std::set<std::uint32_t> delay_slots;
    for(const auto& [pc,ins]:graph.instructions)if(ins.has_delay_slot())delay_slots.insert(pc+4u);
    struct Edge{std::uint32_t from,to;std::string kind;};std::vector<Edge> edges,calls;
    for(const auto& [pc,ins]:graph.instructions) {
        if(comma)json<<',';comma=true;
        json<<"{\"pc\":"<<pc<<",\"word\":"<<ins.word<<",\"mnemonic\":"<<json_string(mips_opcode_name(ins.opcode))
            <<",\"leader\":"<<(graph.leaders.contains(pc) ? "true" : "false")<<'}';
        dot<<"  n"<<pc<<" [label=\""<<hex(pc)<<" "<<mips_opcode_name(ins.opcode)<<"\"];\n";
        if(ins.has_delay_slot())edges.push_back({pc,pc+4,"delay-slot (annul on likely not-taken)"});
        if(ins.is_conditional_branch()) {
            edges.push_back({pc,mips_branch_target(pc,ins.word),"taken after delay-slot"});
            edges.push_back({pc,pc+8,"not-taken (delay/annul per opcode)"});
            if(ins.links())calls.push_back({pc,mips_branch_target(pc,ins.word),"conditional call"});
        } else if(ins.opcode==MipsOpcode::J || ins.opcode==MipsOpcode::Jal) {
            const auto target=mips_jump_target(pc,ins.word);edges.push_back({pc,target,"transfer after delay-slot"});
            if(ins.links()){calls.push_back({pc,target,"direct call"});edges.push_back({pc,pc+8,"call continuation"});}
        } else if(!ins.has_delay_slot()) {
            // A PC present as a delay slot may also have a normal entry. Its
            // sequential edge is conditional on that context, not an invented
            // branch-target successor for the preceding instruction.
            edges.push_back({pc,pc+4,delay_slots.contains(pc) ? "fallthrough only on normal entry" : "fallthrough"});
        }
    }
    for(const auto& target:graph.constant_jumps) {
        edges.push_back({target.jump_site,target.target,"proven constant indirect"});
        if(graph.instructions.at(target.jump_site).links())calls.push_back({target.jump_site,target.target,"proven indirect call"});
    }
    for(const auto& table:graph.jump_tables)
        for(std::uint32_t i=0;i<table.entries;++i)edges.push_back({table.jump_site,elf.data_word_at(table.table_address+i*4u),"proven bounded jump table"});
    auto output_edges=[&](std::string_view key,const std::vector<Edge>& items) {
        json<<"],\""<<key<<"\":[";bool separate=false;
        for(const auto& e:items) {
            if(separate)json<<',';separate=true;
            json<<"{\"from_pc\":"<<e.from<<",\"to\":"<<e.to<<",\"kind\":"<<json_string(e.kind)<<'}';
        }
    };
    output_edges("cfg_edges",edges);output_edges("call_edges",calls);
    for(const auto& e:edges)dot<<"  n"<<e.from<<" -> n"<<e.to<<" [label=\""<<e.kind<<"\"];\n";
    dot<<"}\n";
    json<<"],\"relocations\":[";comma=false;
    for(const auto& r:elf.relocated_code_pointers) {
        if(comma)json<<',';comma=true;json<<"{\"site\":"<<r.source<<",\"target\":"<<r.target<<",\"kind\":"<<unsigned(r.kind)<<",\"applied\":false}";
    }
    json<<"],\"unused_relocation_types\":[";comma=false;
    for(const auto& [type,count]:elf.unused_relocation_counts) {if(comma)json<<',';comma=true;json<<"{\"type\":"<<type<<",\"count\":"<<count<<'}';}
    json<<"],\"rejected_relocation_targets\":[";comma=false;
    for(const auto& r:elf.rejected_code_pointers) {if(comma)json<<',';comma=true;json<<"{\"site\":"<<r.source<<",\"value\":"<<r.value<<",\"reason\":"<<json_string(r.reason)<<'}';}
    json<<"],\"ignored_rela_sections\":"<<elf.ignored_rela_sections<<",\"jump_tables\":[";comma=false;
    for(const auto& t:graph.jump_tables) {
        if(comma)json<<',';comma=true;json<<"{\"site\":"<<t.jump_site<<",\"address\":"<<t.table_address<<",\"entries\":"<<t.entries<<",\"evidence\":"<<json_string(t.evidence)<<'}';
    }
    json<<"],\"unresolved_indirect\":[";comma=false;
    for(const auto& j:graph.unresolved_jumps) {if(comma)json<<',';comma=true;json<<"{\"pc\":"<<j.site<<",\"reason\":"<<json_string(j.reason)<<'}';}
    json<<"],\"interpreter_boundaries\":[";comma=false;
    for(const auto& b:graph.interpreter_boundaries) {
        if(comma)json<<',';comma=true;json<<"{\"pc\":"<<b.address<<",\"reason\":"<<json_string(b.reason);
        try{const auto word=elf.instruction_at(b.address);const auto ins=try_decode_r5900(b.address,word);
            json<<",\"word\":"<<word<<",\"interpreter_decodable\":"<<(ins ? "true" : "false");}catch(const std::exception&){}
        json<<'}';
    }
    json<<"],\"aot_gaps\":[";comma=false;
    for(const auto& segment:elf.segments)if(segment.flags&1u) {
        const auto end=std::uint64_t{segment.vaddr}+segment.file_size;std::uint64_t first=segment.vaddr;
        for(const auto& [pc,ins]:graph.instructions) {
            (void)ins;if(pc<segment.vaddr || pc>=end)continue;
            if(first<pc){if(comma)json<<',';comma=true;json<<"{\"begin\":"<<first<<",\"end\":"<<pc<<'}';}
            first=std::uint64_t{pc}+4;
        }
        if(first<end){if(comma)json<<',';comma=true;json<<"{\"begin\":"<<first<<",\"end\":"<<end<<'}';}
    }
    json<<"],\"dynamic_code\":\"not provable from a static ELF; out-of-image targets and self-modified code require explicit interpreter execution\"}\n";
    text<<"R5900 "<<hex(elf.entry)<<": "<<graph.function_entries.size()<<" verified/hinted roots, "<<graph.instructions.size()
        <<" AOT-eligible instructions in "<<executable_bytes<<" executable bytes\n"
        <<graph.interpreter_boundaries.size()<<" interpreter boundaries, "<<graph.unresolved_jumps.size()<<" unresolved indirect jumps, "
        <<graph.jump_tables.size()<<" proven bounded tables\nFunction extents are not inferred; Ghidra ranges remain hints.\n";
    report.json=json.str();report.text=text.str();report.dot=dot.str();return report;
}
AnalysisReport analyze_irx_metadata(std::span<const std::uint8_t> bytes) {
    Metadata m(bytes);if(m.u16(16)!=0xff80)throw std::runtime_error("IRX metadata requires ET_IOP module (0xff80)");
    std::ostringstream out;out<<"{\"schema\":\"ps2n-irx-metadata-v1\",\"architecture\":\"IOP\",\"sha256\":"<<json_string(sha256_hex(bytes))
        <<",\"entry\":"<<m.u32(24)<<",\"module_sections\":[";bool comma=false;
    for(unsigned i=0;i<m.count;++i)if(m.field(i,4)==0x70000080u || m.name(i)==".iopmod") {
        if(comma)out<<',';comma=true;
        out<<"{\"name\":"<<json_string(m.name(i))<<",\"offset\":"<<m.field(i,16)<<",\"bytes\":"<<m.field(i,20)
           <<",\"sha256\":"<<json_string(sha256_hex(m.section(i)))<<'}';
    }
    if(!comma)throw std::runtime_error("IRX has no verified .iopmod/type 0x70000080 metadata section");
    out<<"],\"scope\":\"container/section metadata only; module identity layout not decoded, no IOP CFG, relocation application or IRX linker\"}\n";
    return {out.str(),"IOP IRX: verified module metadata sections; no R5900 decoding or execution.\n", "digraph ps2n_irx { label=\"IOP metadata only; no CFG proof\"; }\n"};
}
} // namespace ps2native
