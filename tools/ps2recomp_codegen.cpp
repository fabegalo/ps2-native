#include "ps2recomp_codegen.hpp"
#include "ps2native/aot.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ps2native {
namespace {
MipsInstruction verified_instruction(const ps2recomp::Instruction& instruction) {
    const auto decoded=try_decode_r5900(instruction.address,instruction.raw);
    if(!decoded || !mips_aot_supported(decoded->opcode))
        throw std::runtime_error("SDK instruction has no integrated native translation at "+std::to_string(instruction.address));
    return *decoded;
}
std::string native_source(const std::string& native,const std::string& standalone) {
    return "#ifdef PS2N_SDK_NATIVE_BACKEND\n"+native+"\n#else\n"+standalone+"\n#endif\n";
}
}
std::string sdk_native_function(ps2recomp::CodeGenerator& generator,const ps2recomp::Function& function,
    const std::vector<ps2recomp::Instruction>& instructions,const std::string& standalone) {
    const auto name=generator.getFunctionName(function.start);
    std::ostringstream out;
    out<<"#include <ps2native/ee_system.hpp>\n#include <ps2_runtime.h>\n"
       <<"void "<<name<<"(uint8_t*, R5900Context* native, PS2Runtime*) {\n"
       <<"    using namespace ps2native;\n"
       <<"    auto& ctx=*native; auto& state=ctx.state; auto& memory=ctx.memory;\n"
       <<"    switch(state.pc) {\n";
    std::map<std::uint32_t,MipsInstruction> decoded;
    for(const auto& instruction:instructions) {
        const auto value=verified_instruction(instruction);
        const auto [it,inserted]=decoded.emplace(value.pc,value);
        if(!inserted && it->second.word!=value.word)throw std::runtime_error("SDK native function has conflicting instruction bytes");
    }
    for(const auto& [pc,instruction]:decoded) {
        (void)instruction;
        out<<"        case "<<pc<<"u: goto instruction_"<<std::uppercase<<std::hex;
        out.width(8);out.fill('0');out<<pc<<std::dec<<";\n";
    }
    out<<"        default: return;\n    }\n";
    for(const auto& [pc,instruction]:decoded) {(void)pc;emit_aot_instruction(out,instruction);}
    out<<"}\n";
    return native_source(out.str(),standalone);
}

std::string sdk_native_registration(ps2recomp::CodeGenerator& generator,const std::vector<ps2recomp::Function>& functions,
    const std::unordered_map<std::uint32_t,std::vector<ps2recomp::Instruction>>& decoded,
    const std::unordered_map<std::uint32_t,std::vector<std::uint32_t>>& resume_entries,
    const std::string& input_path,const std::string& standalone) {
    struct Entry {std::uint32_t word,owner;};
    std::map<std::uint32_t,Entry> entries;
    std::map<std::uint32_t,std::string> wrappers;
    std::set<std::uint32_t> indirect;
    for(const auto& function:functions) {
        if(function.isStub || function.isSkipped)throw std::runtime_error("Integrated SDK native output refuses skipped functions and stubs");
        if(!function.isRecompiled)continue;
        indirect.insert(function.start);
        const auto name=generator.getFunctionName(function.start);
        wrappers.emplace(function.start,name);
        for(const auto& instruction:decoded.at(function.start)) {
            const auto value=verified_instruction(instruction);
            const auto [it,inserted]=entries.emplace(value.pc,Entry{value.word,function.start});
            if(!inserted && it->second.word!=value.word)throw std::runtime_error("SDK native module has conflicting instruction bytes");
        }
    }
    if(entries.empty())throw std::runtime_error("SDK native output has no instructions");
    for(const auto& [owner,targets]:resume_entries) {
        (void)owner;
        for(const auto pc:targets) {
            if(!entries.contains(pc))throw std::runtime_error("SDK resume entry has no native instruction");
            indirect.insert(pc);
        }
    }
    std::ostringstream out;
    out<<"#include <ps2_runtime.h>\n#include <ps2_recompiled_functions.h>\n#include <array>\nnamespace ps2native {\n";
    for(const auto& [owner,name]:wrappers)
        out<<"static void sdk_block_"<<owner<<"(ExecutionContext& ctx) { ::"<<name<<"(nullptr,&ctx,nullptr); }\n";
    out<<"const AotModule& sdk_aot_module() {\n    static const std::array<AotBlock,"<<entries.size()<<"> blocks{{\n";
    for(const auto& [pc,entry]:entries)
        out<<"        {"<<pc<<"u, &sdk_block_"<<entry.owner<<"},\n";
    out<<"    }};\n    static constexpr std::array<std::uint32_t,"<<indirect.size()<<"> indirect{{";
    for(const auto pc:indirect)out<<pc<<"u,";
    out<<"}};\n    static const AotModule module{blocks.data(),blocks.size(),indirect.data(),indirect.size(),\"elf\",\""
       <<sha256_file(std::filesystem::u8path(input_path))<<"\"};\n    return module;\n}\n}\n";
    return native_source(out.str(),standalone);
}
}
