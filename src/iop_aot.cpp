#include "ps2native/iop_aot.hpp"
#include "ps2native/iop.hpp"
#include "ps2native/sha256.hpp"
#include <algorithm>
#include <deque>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace ps2native {
namespace {
using Key=std::pair<std::uint32_t,std::uint32_t>;
constexpr std::array<std::uint8_t,8> magic{'P','S','2','N','I','O','P',1};
void append(std::vector<std::uint8_t>& bytes,std::uint32_t value) {
    for(unsigned b=0;b<4;++b)bytes.push_back(static_cast<std::uint8_t>(value>>(b*8u)));
}
std::uint32_t read_word(std::span<const std::uint8_t> bytes,std::size_t at) {
    std::uint32_t result=0;for(unsigned b=0;b<4;++b)result|=std::uint32_t{bytes[at+b]}<<(b*8u);return result;
}
auto& registry() {static std::map<Key,void(*)(Iop&)> entries;return entries;}
std::uint32_t physical(std::uint32_t pc) {return pc<0xC0000000u ? pc&0x1FFFFFFFu : pc;}
std::string hex(std::uint32_t value) {
    std::ostringstream out;out<<"0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<value;return out.str();
}
std::string digest(std::string_view value) {
    return sha256_hex({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});
}
bool supported(std::uint32_t word) {
    const auto op=word>>26u,rs=(word>>21u)&31u,rt=(word>>16u)&31u,fn=word&63u;
    if(!op) {
        switch(fn) {
            case 0:case 2:case 3:case 4:case 6:case 7:case 8:case 9:case 12:case 13:
            case 16:case 17:case 18:case 19:case 24:case 25:case 26:case 27:
            case 32:case 33:case 34:case 35:case 36:case 37:case 38:case 39:case 42:case 43:return true;
            default:return false;
        }
    }
    if(op==1)return rt==0 || rt==1 || rt==16 || rt==17;
    if(op==16)return rs==0 || rs==4 || (rs==16 && fn==16);
    return (op>=2 && op<=15) || (op>=32 && op<=38) || op==40 || op==41 || op==42 || op==43 || op==46;
}
}
std::string iop_aot_identity(std::string_view source,std::span<const IopAotEntry> entries) {
    std::string text="ps2n-iop-aot:"+std::to_string(kIopAotAbi)+":"+std::string(source);
    for(const auto& e:entries)text+=":"+hex(e.address)+":"+hex(e.word);
    return digest(text);
}
IopAotRegistration::IopAotRegistration(const IopAotModule& module) {
    if(module.source_sha256.size()!=64 || module.identity!=iop_aot_identity(module.source_sha256,module.entries))
        throw std::runtime_error("IOP AOT registration identity mismatch");
    for(const auto& entry:module.entries) {
        if(!entry.execute || (entry.address&3u))throw std::runtime_error("Invalid IOP AOT entry");
        registry().try_emplace(Key{physical(entry.address),entry.word},entry.execute);
    }
}
bool execute_iop_aot(Iop& iop,std::uint32_t word) {
    const auto found=registry().find({physical(iop.pc()),word});
    if(found==registry().end())return false;
    found->second(iop);return true;
}
std::string missing_iop_aot(std::uint32_t pc,std::uint32_t word) {
    return "missing IOP AOT: loaded image at physical="+hex(physical(pc))+" pc="+hex(pc)+
        " instruction="+hex(word)+"; prepare the relocated image offline with ps2n-aot <loaded-image.bin> <output.cpp> --iop --base=<image-base> --entry="+hex(pc)+
        "; link PS2N_IOP_AOT_SOURCES (image SHA-256 is recorded in the artifact)";
}
std::vector<std::uint8_t> encode_iop_aot_image(std::uint32_t base,std::span<const ObservedWord> words) {
    if((base&3u) || words.empty() || words.size()>1048576u || std::uint64_t{base}+words.size()*4u>0x100000000ull)
        throw std::runtime_error("Invalid IOP captured image bounds");
    std::vector<std::uint8_t> bytes(magic.begin(),magic.end());append(bytes,base);append(bytes,static_cast<std::uint32_t>(words.size()));
    for(const auto word:words){append(bytes,word.value&~word.unknown);append(bytes,word.unknown);}
    return bytes;
}
AotSources generate_iop_aot_sources(std::span<const std::uint8_t> bytes,std::uint32_t base,
    std::span<const std::uint32_t> roots,const std::string& module_key,std::uint32_t partition_bytes) {
    const bool captured=bytes.size()>=8u && std::equal(magic.begin(),magic.end(),bytes.begin());
    std::size_t word_count=bytes.size()/4u;
    if(captured) {
        if(bytes.size()<16u || read_word(bytes,8)!=base || read_word(bytes,12)>1048576u ||
            bytes.size()!=16ull+std::uint64_t{read_word(bytes,12)}*8u)
            throw std::runtime_error("IOP capture base, version or length mismatch");
        word_count=read_word(bytes,12);
        for(std::size_t n=0;n<word_count;++n)
            if(read_word(bytes,16u+n*8u)&read_word(bytes,20u+n*8u))throw std::runtime_error("IOP capture contains values in unknown bits");
    }
    if(!word_count || (base&3u) || (bytes.size()&3u) || word_count*4ull>0x100000000ull-base ||
       partition_bytes<4u || (partition_bytes&(partition_bytes-1u)))
        throw std::runtime_error("IOP AOT needs an aligned loaded image and power-of-two partition size >= 4");
    if(roots.empty())throw std::runtime_error("IOP AOT requires explicit verified entry roots");
    const auto source=sha256_hex(bytes);
    std::map<std::uint32_t,IopAotEntry> graph;
    std::set<std::uint32_t> expanded;
    std::deque<std::uint32_t> work(roots.begin(),roots.end());
    const auto contains=[&](std::uint32_t pc) {return !(pc&3u) && physical(pc)>=physical(base) &&
        std::uint64_t{physical(pc)-physical(base)}+4u<=word_count*4u;};
    for(const auto root:roots)if(!contains(root))throw std::runtime_error("IOP entry outside loaded image: "+hex(root));
    const auto read=[&](std::uint32_t pc) {
        const auto at=physical(pc)-physical(base);
        return read_word(bytes,captured ? 16u+at*2ull : at);
    };
    const auto known=[&](std::uint32_t pc) {return !captured || !read_word(bytes,20ull+(physical(pc)-physical(base))*2ull);};
    while(!work.empty()) {
        const auto pc=work.front();work.pop_front();
        if(!contains(pc) || !expanded.insert(physical(pc)).second)continue;
        const auto word=read(pc);
        if(!known(pc) || !supported(word))continue;
        graph.emplace(physical(pc),IopAotEntry{physical(pc),word,nullptr});
        const auto op=word>>26u,fn=word&63u;
        const bool indirect=op==0 && (fn==8 || fn==9);
        const bool trap=op==0 && (fn==12 || fn==13);
        if(trap)continue;
        if(op==2 || op==3 || indirect || op==1 || (op>=4 && op<=7)) {
            // Delay word belongs to this transfer, never infer its fallthrough.
            if(contains(pc+4u) && known(pc+4u) && supported(read(pc+4u)))graph.try_emplace(physical(pc+4u),IopAotEntry{physical(pc+4u),read(pc+4u),nullptr});
            if(op==2 || op==3)work.push_back(((pc+4u)&0xF0000000u)|((word&0x03FFFFFFu)<<2u));
            else if(!indirect)work.push_back(pc+4u+(static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(word)))<<2u));
            if(op==3 || (indirect && fn==9) || op==1 || (op>=4 && op<=7))work.push_back(pc+8u);
        } else work.push_back(pc+4u);
    }
    std::vector<IopAotEntry> entries;
    for(const auto& [pc,e]:graph){(void)pc;entries.push_back(e);}
    const auto identity=iop_aot_identity(source,entries);
    const auto scope="iop_aot_"+digest(module_key+source+":"+std::to_string(kIopAotAbi));
    const std::string includes="#include <ps2native/iop_aot.hpp>\n#include <ps2native/iop_ops.hpp>\n";
    AotSources result;std::ostringstream out;
    out<<"// Offline IOP translation of already-relocated bytes. No IRX relocation is reapplied.\n"<<includes<<"namespace ps2native::"<<scope<<" {\n";
    std::map<std::uint32_t,std::vector<IopAotEntry>> buckets;
    for(const auto& e:entries)buckets[e.address/partition_bytes].push_back(e);
    for(const auto& [bucket,part]:buckets) {
        std::ostringstream unit;unit<<includes<<"namespace ps2native::"<<scope<<" {\n";
        for(const auto& e:part) {
            const auto name="at_"+std::to_string(e.address);
            out<<"void "<<name<<"(Iop&);\n";
            unit<<"void "<<name<<"(Iop& iop){iop.execute_native<"<<hex(e.word)<<"u>();}\n";
        }
        unit<<"}\n";auto text=unit.str();result.partitions.push_back({"ps2n_aot_"+digest(text)+".cpp",std::move(text)});
    }
    out<<"static const IopAotEntry entries[]{\n";
    for(const auto& e:entries)out<<"{"<<hex(e.address)<<"u,"<<hex(e.word)<<"u,&at_"<<e.address<<"},\n";
    if(entries.empty())throw std::runtime_error("IOP AOT found no supported instructions");
    out<<"};\nstatic const IopAotModule module{\""<<source<<"\",\""<<identity<<"\",entries};\n"
       <<"static const IopAotRegistration registration{module};\n}\n";
    result.dispatcher=out.str();return result;
}
} // namespace ps2native
