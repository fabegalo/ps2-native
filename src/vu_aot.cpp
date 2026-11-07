// Original BSD implementation. VU decode/semantics are shared with VuControl;
// this offline pass only establishes reachable static instruction pairs.
#include "ps2native/vu_aot.hpp"
#include "ps2native/sha256.hpp"
#include <algorithm>
#include <deque>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ps2native {
namespace {
constexpr std::array<std::uint8_t,8> capture_magic{'P','S','2','N','V','U','1',1};
void append_u32(std::vector<std::uint8_t>& bytes,std::uint32_t value) {
    for(unsigned shift=0;shift<32;shift+=8)bytes.push_back(static_cast<std::uint8_t>(value>>shift));
}
std::uint32_t read_u32(std::span<const std::uint8_t> bytes,std::size_t at) {
    std::uint32_t value=0;
    for(unsigned byte=0;byte<4;++byte)value|=std::uint32_t{bytes[at+byte]}<<(byte*8u);
    return value;
}
std::string hex(std::uint32_t value) {
    std::ostringstream out;out<<"0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<value<<"u";return out.str();
}
std::string digest(const std::string& value) {
    return sha256_hex({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});
}
bool upper_supported(std::uint32_t word) {
    if(word&0x38000000u)return false;
    const auto code=word&0x07FFFFFFu,function=code&63u,extended=code&0x7FFu;
    if(code==0x2FFu)return true;
    if(extended==0x1FFu)return !(word&0x06000000u) && ((word>>21u)&15u)==14u;
    if((extended&~3u)==0x17Cu || (extended&~3u)==0x1BCu || (extended&~3u)==0xBCu ||
       extended==0x2BEu || extended==0x2BFu)return true;
    if((function&~3u)==0u || (function&~3u)==4u || (function&~3u)==0x10u ||
       (function&~3u)==0x14u || (function&~3u)==0x18u || (function&~3u)==8u)return true;
    switch(function) {
    case 0x28u:case 0x2Cu:case 0x2Bu:case 0x2Fu:case 0x2Au:case 0x29u:return true;
    case 0x20u:case 0x24u:case 0x1Cu:case 0x21u:case 0x22u:case 0x26u:case 0x1Eu:case 0x23u:return !((code>>16u)&31u);
    default:return false;
    }
}
bool lower_supported(std::uint32_t word) {
    const auto is=(word>>11u)&31u,it=(word>>16u)&31u,id=(word>>6u)&31u;
    const auto mask=(word>>21u)&15u;
    switch(word>>25u) {
    case 0u:return is<16u;
    case 1u:return it<16u;
    case 5u:return is<16u && it<16u;
    case 4u:return is<16u && it<16u && mask && !(mask&(mask-1u));
    case 8u:case 9u:return is<16u && it<16u;
    case 0x11u:case 0x12u:case 0x13u:return !(word&0x01000000u);
    case 0x18u:case 0x1Au:case 0x1Bu:return is<16u && it<16u && !(word&0x01E007FFu);
    case 0x20u:return true;
    case 0x21u:return it<16u;
    case 0x28u:case 0x29u:return is<16u && it<16u && !(word&0x01E00000u);
    case 0x2Cu:case 0x2Du:case 0x2Eu:case 0x2Fu:return is<16u && !it && !(word&0x01E00000u);
    case 0x40u:
        switch(word&0x7FFu) {
        case 0x37Cu:return is<16u;
        case 0x37Du:return it<16u;
        case 0x3BCu:return true;
        case 0x3BFu:return word==0x800003BFu;
        case 0x3FFu:return is<16u && it<16u;
        case 0x3FEu:return is<16u && it<16u && mask && !(mask&(mask-1u));
        case 0x6BCu:case 0x6BDu:return it<16u && !is && !(word&0x01E00000u);
        case 0x3FCu:return it<16u && !(word&0x01800000u);
        case 0x3FDu:return is<16u;
        case 0x33Cu:case 0x33Du:return true;
        case 0x6FCu:return is<16u && !it && !(word&0x01E00000u);
        default:break;
        }
        switch(word&63u) {
        case 0x30u:case 0x31u:case 0x34u:case 0x35u:return is<16u && it<16u && id<16u;
        case 0x32u:return is<16u && it<16u;
        default:return false;
        }
    // JR/JALR are interpreter boundaries until a target is statically proved.
    default:return false;
    }
}
bool direct_branch(std::uint32_t low) {
    const auto op=low>>25u;
    return op==0x20u || op==0x21u || op==0x28u || op==0x29u || (op>=0x2Cu && op<=0x2Fu);
}
bool any_branch(std::uint32_t low) {
    return direct_branch(low) || (low>>25u)==0x24u || (low>>25u)==0x25u;
}
VuAotInstruction dependencies(std::uint32_t low,std::uint32_t upper) {
    VuAotInstruction result;result.lower=low;result.upper=upper;result.writes_i=(upper&0x80000000u)!=0u;
    const auto code=upper&0x07FFFFFFu,function=code&63u,extended=code&0x7FFu;
    const auto fs=(code>>11u)&31u,ft=(code>>16u)&31u,fd=(code>>6u)&31u;
    if(extended==0x1FFu) {
        result.vf_reads=(std::uint32_t{1}<<fs)|(std::uint32_t{1}<<ft);
        result.reads_flags=result.writes_flags=true;
    }else if(code!=0x2FFu && ((code>>21u)&15u)) {
        const bool fixed=(extended&~3u)==0x17Cu;
        const bool acc=(extended&~3u)==0x1BCu || (extended&~3u)==0xBCu || extended==0x2BEu || extended==0x2BFu;
        result.reads_acc=(extended&~3u)==0xBCu || extended==0x2BFu || (function&~3u)==8u || function==0x21u || function==0x23u || function==0x29u;
        result.reads_q=function==0x20u || function==0x24u || function==0x1Cu || function==0x21u;
        result.reads_i=function==0x22u || function==0x26u || function==0x1Eu || function==0x23u;
        result.vf_reads|=std::uint32_t{1}<<fs;
        if(!fixed && !result.reads_q && !result.reads_i)result.vf_reads|=std::uint32_t{1}<<ft;
        result.writes_acc=acc;
        if(!acc)result.vf_writes|=std::uint32_t{1}<<(fixed ? ft : fd);
        result.writes_flags=!fixed && (function&~3u)!=0x10u && (function&~3u)!=0x14u && function!=0x2Bu && function!=0x2Fu;
    }
    if(result.writes_i){result.vf_reads&=~1u;result.vf_writes&=~1u;return result;}
    const auto op=low>>25u,is=(low>>11u)&31u,it=(low>>16u)&31u,id=(low>>6u)&31u,ext=low&0x7FFu;
    const auto read_vi=[&](unsigned reg){if(reg<16u)result.vi_reads|=static_cast<std::uint16_t>(1u<<reg);};
    const auto write_vi=[&](unsigned reg){if(reg<16u)result.vi_writes|=static_cast<std::uint16_t>(1u<<reg);};
    if(op==8u || op==9u){read_vi(is);write_vi(it);}
    else if(op==0x11u)result.writes_flags=true;
    else if(op==0x12u || op==0x13u){write_vi(1);result.reads_flags=true;}
    else if(op==0x18u || op==0x1Au || op==0x1Bu){read_vi(is);write_vi(it);result.reads_flags=true;}
    else if(op==0x21u)write_vi(it);
    else if(op==0x28u || op==0x29u){read_vi(is);read_vi(it);}
    else if(op>=0x2Cu && op<=0x2Fu)read_vi(is);
    else if(op==0u || (op==0x40u && ext==0x37Cu)) {
        read_vi(is);result.vf_writes|=std::uint32_t{1}<<it;if(op)write_vi(is);
    } else if(op==1u || (op==0x40u && ext==0x37Du)) {
        read_vi(it);result.vf_reads|=std::uint32_t{1}<<is;if(op!=1u)write_vi(it);
    } else if(op==5u || (op==0x40u && ext==0x3FFu)){read_vi(is);read_vi(it);}
    else if(op==4u || (op==0x40u && ext==0x3FEu)){read_vi(is);write_vi(it);}
    else if(op==0x40u) {
        if(ext==0x3BCu){result.vf_reads|=(std::uint32_t{1}<<is)|(std::uint32_t{1}<<it);result.writes_q=true;}
        else if(ext==0x3BFu)result.reads_q=true;
        else if(ext==0x6BCu || ext==0x6BDu)write_vi(it);
        else if(ext==0x3FCu){result.vf_reads|=std::uint32_t{1}<<is;write_vi(it);}
        else if(ext==0x3FDu){read_vi(is);result.vf_writes|=std::uint32_t{1}<<it;}
        else if(ext==0x33Cu || ext==0x33Du){result.vf_reads|=std::uint32_t{1}<<is;result.vf_writes|=std::uint32_t{1}<<it;}
        else if(ext==0x6FCu)read_vi(is);
        else if((low&63u)==0x32u){read_vi(is);write_vi(it);}
        else {read_vi(is);read_vi(it);write_vi(id);}
    }
    result.vf_reads&=~1u;result.vf_writes&=~1u;result.vi_reads&=0xFFFEu;result.vi_writes&=0xFFFEu;
    return result;
}
std::string generate(const VuAotImage& image,const VuAotOptions& options,std::vector<AotSource>* partitions,
                     const std::string& module_key,std::uint32_t partition_bytes) {
    if(partitions && (partition_bytes<8u || (partition_bytes&(partition_bytes-1u))))
        throw std::runtime_error("VU1 AOT partition size must be a power of two >= 8");
    const auto graph=analyze_vu1_aot(image,options);
    const auto identity=vu1_aot_identity(image,options.diagnostic_power_two_carry);
    std::string key=module_key+identity;
    for(const auto pc:options.entries)key+=":"+std::to_string(pc);
    const auto scope="vu1_aot_"+digest(key);
    const auto includes=std::string{"#include <ps2native/vu_aot_runtime.hpp>\n#include <ps2native/vu_micro_ops.hpp>\n"};
    std::ostringstream out;
    out<<"// AUTO-GENERATED by ps2n-aot --vu1. Keep proprietary translations local.\n"
       <<"// VU1 identity "<<identity<<"; ABI "<<kVuAotAbi<<"; statically reachable pairs "<<graph.instructions.size()<<"\n"
       <<"// Dependencies are descriptive. Shared runtime retains all pipeline interlocks and writebacks.\n";
    for(const auto& boundary:graph.boundaries)out<<"// Interpreter boundary "<<hex(boundary.pc)<<": "<<boundary.reason<<"\n";
    out<<includes<<"namespace ps2native::"<<scope<<" {\n";
    const auto emit=[&](std::ostringstream& unit,const auto& pairs) {
        unit<<"    switch(vu.diagnostic_micro_pc(1)) {\n";
        for(const auto& [pc,instruction]:pairs) {
            unit<<"    case "<<hex(pc)<<": vu.execute_micro_native<"<<hex(instruction.lower)<<","<<hex(instruction.upper)<<">(gif,gs); return true;\n";
        }
        unit<<"    default:return false;\n    }\n";
    };
    if(!partitions) {
        out<<"static bool step(VuControl& vu,Gif* gif,GsRegisters* gs) {\n";emit(out,graph.instructions);out<<"}\n";
    } else {
        std::map<std::uint32_t,std::map<std::uint32_t,VuAotInstruction>> buckets;
        for(const auto& pair:graph.instructions)buckets[pair.first/partition_bytes].insert(pair);
        for(const auto& [bucket,pairs]:buckets) {
            out<<"bool part_"<<bucket<<"(VuControl&,Gif*,GsRegisters*);\n";
            std::ostringstream unit;
            unit<<"// Generated VU1 stable address partition.\n"<<includes<<"namespace ps2native::"<<scope<<" {\n"
                <<"bool part_"<<bucket<<"(VuControl& vu,Gif* gif,GsRegisters* gs) {\n";
            emit(unit,pairs);unit<<"}\n}\n";
            auto text=unit.str();partitions->push_back({"ps2n_aot_"+digest(text)+".cpp",std::move(text)});
        }
        out<<"static bool step(VuControl& vu,Gif* gif,GsRegisters* gs) {\n    switch(vu.diagnostic_micro_pc(1)/"<<partition_bytes<<"u) {\n";
        for(const auto& [bucket,pairs]:buckets){(void)pairs;out<<"    case "<<bucket<<"u:return part_"<<bucket<<"(vu,gif,gs);\n";}
        out<<"    default:return false;\n    }\n}\n";
    }
    out<<"static const VuAotImage image{{{\n";
    for(const auto& word:image.words)out<<"    {"<<hex(word.value)<<","<<hex(word.unknown)<<"},\n";
    out<<"}}};\nstatic const VuAotModule module{\""<<identity<<"\",&image,"
       <<(options.diagnostic_power_two_carry ? "true" : "false")<<",&step};\n"
       <<"static const VuAotRegistration registration{module};\n}\n";
    return out.str();
}
} // namespace
std::vector<std::uint8_t> encode_vu1_aot_image(const VuAotImage& image) {
    std::vector<std::uint8_t> bytes(capture_magic.begin(),capture_magic.end());bytes.reserve(32776);
    for(const auto& word:image.words) {
        if(word.value&word.unknown)throw std::runtime_error("VU1 capture contains values in unknown bits");
        append_u32(bytes,word.value);append_u32(bytes,word.unknown);
    }
    return bytes;
}
VuAotImage parse_vu1_aot_image(std::span<const std::uint8_t> bytes) {
    VuAotImage image;
    if(bytes.size()==16384u) {
        for(std::size_t i=0;i<image.words.size();++i)image.words[i]={read_u32(bytes,i*4u),0};
    } else {
        if(bytes.size()!=32776u || !std::equal(capture_magic.begin(),capture_magic.end(),bytes.begin()))
            throw std::runtime_error("VU1 input must be a 16 KiB raw image or a version-1 lossless capture");
        for(std::size_t i=0;i<image.words.size();++i) {
            image.words[i]={read_u32(bytes,8u+i*8u),read_u32(bytes,12u+i*8u)};
            if(image.words[i].value&image.words[i].unknown)throw std::runtime_error("VU1 capture contains values in unknown bits");
        }
    }
    return image;
}
std::string vu1_aot_identity(const VuAotImage& image,bool diagnostic_power_two_carry) {
    auto bytes=encode_vu1_aot_image(image);
    append_u32(bytes,kVuAotAbi);bytes.push_back(diagnostic_power_two_carry ? 1u : 0u);
    return sha256_hex(bytes);
}
VuAotAnalysis analyze_vu1_aot(const VuAotImage& image,const VuAotOptions& options) {
    VuAotAnalysis graph;
    // Delay contexts are distinct: following an E slot must not accidentally
    // discover the next memory contents as a continuation of that program.
    std::deque<std::pair<std::uint32_t,bool>> work;
    std::set<std::pair<std::uint32_t,bool>> visited;
    std::set<std::pair<std::uint32_t,std::string>> boundaries;
    for(const auto pc:options.entries) {
        if(pc>=16384u || (pc&7u))throw std::runtime_error("VU1 AOT entry must be an aligned byte PC within 16 KiB");
        work.emplace_back(pc,false);
    }
    if(work.empty())throw std::runtime_error("VU1 AOT requires at least one entry root");
    while(!work.empty()) {
        const auto [pc,delay]=work.front();work.pop_front();
        if(!visited.emplace(pc,delay).second)continue;
        const auto lower=image.words[pc/4u],upper=image.words[pc/4u+1u];
        std::string boundary;
        if(lower.unknown || upper.unknown)boundary="instruction pair contains unknown/unwritten bits";
        else if(!upper_supported(upper.value))boundary="upper instruction/control is outside the proved shared subset";
        else if(!(upper.value&0x80000000u) && !lower_supported(lower.value))boundary=any_branch(lower.value) ? "indirect target lacks static proof" : "lower instruction is outside the proved shared subset";
        else if(delay && ((!(upper.value&0x80000000u) && any_branch(lower.value)) || (upper.value&0x40000000u)))boundary="branch/E inside a delay slot lacks proof";
        if(!boundary.empty()){boundaries.emplace(pc,boundary);continue;}
        graph.instructions.emplace(pc,dependencies(lower.value,upper.value));
        if(delay)continue;
        const auto next=(pc+8u)&16383u;
        if(upper.value&0x40000000u)work.emplace_back(next,true);
        else if(!(upper.value&0x80000000u) && direct_branch(lower.value)) {
            work.emplace_back(next,true);
            const auto bits=lower.value&0x7FFu;
            const auto offset=static_cast<std::int32_t>(bits)-((bits&0x400u) ? 2048 : 0);
            const auto target=((pc/8u+1u+static_cast<std::uint32_t>(offset))&2047u)*8u;
            work.emplace_back(target,false);
            const auto op=lower.value>>25u;
            if(op!=0x20u && op!=0x21u)work.emplace_back((pc+16u)&16383u,false);
        } else work.emplace_back(next,false);
    }
    // A PC reachable in incompatible contexts remains an explicit boundary.
    for(const auto& [pc,reason]:boundaries){graph.instructions.erase(pc);graph.boundaries.push_back({pc,reason});}
    return graph;
}
std::string generate_vu1_aot_cpp(const VuAotImage& image,const VuAotOptions& options) {
    return generate(image,options,nullptr,{},1024);
}
AotSources generate_vu1_aot_sources(const VuAotImage& image,const VuAotOptions& options,
                                  const std::string& module_key,std::uint32_t partition_bytes) {
    AotSources sources;sources.dispatcher=generate(image,options,&sources.partitions,module_key,partition_bytes);return sources;
}
} // namespace ps2native
