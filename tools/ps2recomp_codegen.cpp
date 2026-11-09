#include "ps2recomp_codegen.hpp"
#include "ps2native/aot.hpp"
#include "ps2native/aot_sources.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ps2native {
namespace {
std::vector<std::pair<std::uint32_t,std::uint32_t>> missing_translations;
std::size_t native_instruction_count{};
std::uint32_t execution_alias{};
std::map<std::uint32_t,std::string> native_partitions;
bool sdk_scalar_float(MipsOpcode opcode) {
    switch(opcode) {
        case MipsOpcode::AddS: case MipsOpcode::SubS: case MipsOpcode::MulS:
        case MipsOpcode::DivS: case MipsOpcode::SqrtS: case MipsOpcode::AddaS:
        case MipsOpcode::MulaS: case MipsOpcode::MaddS: case MipsOpcode::MsubS:
        case MipsOpcode::MaddaS: case MipsOpcode::MsubaS:return true;
        default:return false;
    }
}
void emit_sdk_instruction(std::ostream& out,ps2recomp::CodeGenerator& generator,
                          const ps2recomp::Instruction& instruction,const MipsInstruction& native) {
    if(!sdk_scalar_float(native.opcode)){emit_aot_instruction(out,native);return;}
    out<<"instruction_"<<std::uppercase<<std::hex;
    out.width(8);out.fill('0');out<<native.pc<<std::dec<<":\n"
       <<"    if (!aot_enter_checked(ctx, "<<native.pc<<"u, "<<native.word<<"u, false)) return;\n"
       <<"    {\n"
       <<"        if (!(state.cp0(Cop0::Status)&kStatusCU1)) throw GuestStop(StopKind::GuestException, \"SDK FPU with Status.CU1 = 0\");\n"
       <<"        const auto frame=ee::begin(state, "<<native.pc<<"u);\n"
       <<"        { SdkFloatContext sdk(state); auto* ctx=&sdk;\n"
       <<"          "<<generator.translateInstruction(instruction)<<"\n"
       <<"        }\n"
       <<"        if (ee::retire(ctx,frame)) return;\n"
       <<"    }\n";
}
std::optional<MipsInstruction> native_instruction(const ps2recomp::Instruction& instruction) {
    const auto decoded=try_decode_r5900(instruction.address,instruction.raw);
    if(!decoded || !mips_aot_supported(decoded->opcode))return {};
    return decoded;
}
std::string native_source(const std::string& native,const std::string& standalone) {
    return "#ifdef PS2N_SDK_NATIVE_BACKEND\n"+native+"\n#else\n"+standalone+"\n#endif\n";
}
}
const std::vector<std::pair<std::uint32_t,std::uint32_t>>& sdk_native_missing_translations(){return missing_translations;}
std::size_t sdk_native_instruction_count(){return native_instruction_count;}
void reset_sdk_native_codegen(std::uint32_t alias){native_partitions.clear();missing_translations.clear();native_instruction_count=0;execution_alias=alias;}
void write_sdk_native_sources(const std::filesystem::path& dispatcher) {
    const auto bytes=read_file(dispatcher);
    AotSources sources; sources.dispatcher.assign(bytes.begin(),bytes.end());
    for(const auto& [bucket,body]:native_partitions) {
        (void)bucket;
        auto text="#ifdef PS2N_SDK_NATIVE_BACKEND\n#include <ps2native/ee_system.hpp>\n#include <ps2native/sdk_float_context.hpp>\n"+body+"\n#endif\n";
        const auto hash=sha256_hex({reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});
        sources.partitions.push_back({"ps2n_aot_"+hash+".cpp",std::move(text)});
    }
    (void)write_aot_sources(dispatcher,sources);
}
std::string sdk_native_function(ps2recomp::CodeGenerator& generator,const ps2recomp::Function& function,
    const std::vector<ps2recomp::Instruction>& instructions,const std::string& standalone) {
    const auto name=generator.getFunctionName(function.start);
    std::ostringstream out;
    out<<"#include <ps2native/ee_system.hpp>\n#include <ps2_runtime.h>\n";
    std::map<std::uint32_t,std::map<std::uint32_t,ps2recomp::Instruction>> chunks;
    for(auto instruction:instructions) {
        instruction.address|=execution_alias;
        auto& decoded=chunks[instruction.address>>9u];
        const auto [it,inserted]=decoded.emplace(instruction.address,instruction);
        if(!inserted && it->second.raw!=instruction.raw)throw std::runtime_error("SDK native function has conflicting instruction bytes");
    }
    // Bound optimizer CFGs to 128 guest words. No guest instruction or clock is skipped.
    for(const auto& [chunk,decoded]:chunks) {
        out<<"void ps2n_chunk_"<<function.start<<"_"<<chunk<<"(ps2native::ExecutionContext&);\n";
        std::ostringstream body;
        body<<"#if defined(_MSC_VER)\n__declspec(noinline)\n#elif defined(__GNUC__)\n__attribute__((noinline))\n#endif\n"
           <<"void ps2n_chunk_"<<function.start<<"_"<<chunk<<"(ps2native::ExecutionContext& ctx) {\n"
           <<"    using namespace ps2native; auto& state=ctx.state; auto& memory=ctx.memory;\n"
           <<"    switch(state.pc) {\n";
        for(const auto& [pc,instruction]:decoded) {
            if(!native_instruction(instruction))continue;
            body<<"        case "<<pc<<"u: goto instruction_"<<std::uppercase<<std::hex;
            body.width(8);body.fill('0');body<<pc<<std::dec<<";\n";
        }
        body<<"        default: return;\n    }\n";
        for(const auto& [pc,instruction]:decoded) {
            if(const auto value=native_instruction(instruction))emit_sdk_instruction(body,generator,instruction,*value);
            else body<<"    return; // Stop before the missing offline translation at "<<pc<<".\n";
        }
        body<<"}\n";
        // Reuse the native AOT manifest's stable 64-KiB address buckets.
        native_partitions[chunk>>7u]+=body.str();
    }
    out<<"void "<<name<<"(uint8_t*, R5900Context* native, PS2Runtime*) {\n"
       <<"    switch(native->state.pc >> 9u) {\n";
    for(const auto& [chunk,decoded]:chunks) {
        (void)decoded;
        out<<"        case "<<chunk<<"u: ps2n_chunk_"<<function.start<<"_"<<chunk<<"(*native); return;\n";
    }
    out<<"        default: return;\n    }\n";
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
    std::map<std::uint32_t,std::uint32_t> missing,words;
    std::set<std::uint32_t> indirect;
    for(const auto& function:functions) {
        if(function.isStub || function.isSkipped)throw std::runtime_error("Integrated SDK native output refuses skipped functions and stubs");
        if(!function.isRecompiled)continue;
        const auto name=generator.getFunctionName(function.start);
        wrappers.emplace(function.start,name);
        for(auto instruction:decoded.at(function.start)) {
            instruction.address|=execution_alias;
            const auto [word,inserted]=words.emplace(instruction.address,instruction.raw);
            if(!inserted && word->second!=instruction.raw)throw std::runtime_error("SDK native module has conflicting instruction bytes");
            const auto value=native_instruction(instruction);
            if(!value){missing.emplace(instruction.address,instruction.raw);continue;}
            entries.emplace(value->pc,Entry{value->word,function.start});
        }
        if(entries.contains(function.start|execution_alias))indirect.insert(function.start|execution_alias);
    }
    if(entries.empty())throw std::runtime_error("SDK native output has no instructions");
    for(const auto& [owner,targets]:resume_entries) {
        (void)owner;
        for(auto pc:targets) {
            pc|=execution_alias;
            if(entries.contains(pc))indirect.insert(pc);
            else if(!missing.contains(pc))throw std::runtime_error("SDK resume entry has no decoded instruction");
        }
    }
    missing_translations.assign(missing.begin(),missing.end());native_instruction_count=entries.size();
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
