#include "ps2native/aot_sources.hpp"
#include "ps2native/control_flow.hpp"
#include "ps2native/sha256.hpp"
#include "ps2native/io.hpp"

#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <set>

namespace ps2native {
namespace {
struct BlockSource {
    std::uint32_t start{};
    std::vector<MipsInstruction> instructions;
};

std::string hex32(std::uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(8)
           << std::setfill('0') << value << 'u';
    return stream.str();
}
std::string address_label(std::uint32_t address) {
    const auto hex = hex32(address);
    return hex.substr(2, 8); // Valid C++ identifier suffix, deterministic.
}

std::map<std::uint32_t, BlockSource> make_blocks(const AotControlFlow& graph) {
    std::map<std::uint32_t, BlockSource> blocks;
    for (const auto start : graph.leaders) {
        auto current = start;
        BlockSource block;
        block.start = start;
        while (true) {
            const auto instruction = graph.instructions.find(current);
            if (instruction == graph.instructions.end()) {
                throw std::runtime_error("Discovered block leader missing from instruction map");
            }
            block.instructions.push_back(instruction->second);
            const auto next = current + 4u;
            if (instruction->second.has_delay_slot() ||
                graph.leaders.contains(next) || !graph.instructions.contains(next))
                break;
            current = next;
        }
        blocks.emplace(start, std::move(block));
    }
    return blocks;
}

// Every instruction is a call into the shared semantics (ee_semantics.hpp)
// with its constant pc and word; the host compiler inlines and folds it.
void emit_block(std::ostringstream& out, const BlockSource& block, bool compiled_delay_slot, bool local = true) {
    out << (local ? "static " : "") << "void block_" << address_label(block.start) << "(ExecutionContext& ctx) {\n"
        << "    auto& state = ctx.state;\n"
        << "    auto& memory = ctx.memory;\n";
    // Every proven instruction can resume natively. All entry labels share
    // this one body; no suffix function or duplicate translation is emitted.
    out << "    switch (state.pc) {\n";
    for (const auto& instruction : block.instructions)
        out << "        case " << hex32(instruction.pc) << ": goto instruction_"
            << address_label(instruction.pc) << ";\n";
    out << "        default: return;\n    }\n";
    for (const auto& instruction : block.instructions) {
        const auto pc = hex32(instruction.pc);
        const auto word = hex32(instruction.word);
        out << "instruction_" << address_label(instruction.pc) << ":\n"
            << "    if (!aot_enter_checked(ctx, " << pc << ", " << word << ", "
            << (instruction.has_delay_slot() ? "true" : "false") << ")) return;\n"
            << "    {\n"
            << "        const auto frame = ee::begin(state, " << pc << ");\n"
            << "        ee::exec<MipsOpcode::" << mips_opcode_name(instruction.opcode)
            << ", " << word << ">(state, memory, " << pc << ", " << word << ");\n"
            << "        if (ee::retire(ctx, frame)) return;\n"
            << "    }\n";
    }
    if (compiled_delay_slot) {
        const auto slot=block.instructions.back().pc+4u;
        // A branch changes neither the translation/cache context nor the
        // module. Enter its already-compiled delay-slot function directly;
        // that function still checks budget, bytes, IRQs and observation.
        // False likely branches return to dispatch with the slot annulled.
        out << "    if (state.delay_pending && state.pc == " << hex32(slot) << ")\n"
            << "        block_" << address_label(slot) << "(ctx);\n";
    }
    out << "}\n\n";
}
} // namespace

namespace {
std::string digest(const std::string& text) {
    return sha256_hex({reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});
}
std::string generate(const ElfImage& image, const AotOptions& options,
    std::vector<AotSource>* partitions = nullptr, const std::string& module_key = {},
    std::uint32_t partition_bytes = 65536) {
    if (partitions && (partition_bytes < 4 || (partition_bytes & (partition_bytes-1u))))
        throw std::runtime_error("AOT partition size must be a power of two >= 4");
    const auto graph = analyze_control_flow(image, options.additional_function_entries,
                                           options.jump_tables, options.interpreter_boundaries);
    const auto blocks = make_blocks(graph);
    std::map<std::uint32_t,std::uint32_t> dispatch_entries;
    for (const auto& [start,block] : blocks)
        for (const auto& instruction : block.instructions) dispatch_entries.emplace(instruction.pc,start);
    const auto source_sha256 = options.source_sha256.empty() ? sha256_hex(image.bytes) : options.source_sha256;
    std::ostringstream out;
    out << "// AUTO-GENERATED by ps2n-aot. Do not distribute translated proprietary guest code.\n"
        << "// Source: " << options.source_kind << " sha256 " << source_sha256 << "\n"
        << "// Generator contract: ps2n-ee-aot-v3 (constant operands, shared semantics, checked instruction bytes)\n"
        << "// Static instructions: " << graph.instructions.size()
        << "; block functions: " << blocks.size() << "; dispatch entries: " << dispatch_entries.size()
        << "; verified function roots: " << graph.function_entries.size()
        << "; evidence-backed indirect targets: " << graph.resolved_indirect_targets.size()
        << "; rejected relocation candidates: " << graph.rejected_indirect_targets.size() << "\n"
        << "// Verified indirect entries: " << graph.indirect_entries.size()
        << "; proven jump tables: " << graph.jump_tables.size()
        << "; constant JR targets: " << graph.constant_jumps.size()
        << "; unresolved JR sites (runtime-checked): " << graph.unresolved_jumps.size()
        << "; unpaired R_MIPS_LO16: " << image.unpaired_lo16_relocations << "\n";
    if (options.interpreter_boundaries)
        out << "// Hybrid translation: interpreter boundaries: " << graph.interpreter_boundaries.size() << "\n";
    for (const auto& [type, count] : image.unused_relocation_counts)
        out << "// Relocation type " << type << " not used as evidence: " << count << " record(s)\n";
    if (image.ignored_rela_sections != 0)
        out << "// SHT_RELA sections ignored: " << image.ignored_rela_sections << "\n";
    for (const auto& table : graph.jump_tables)
        out << "// Jump table: JR " << hex32(table.jump_site) << ", table " << hex32(table.table_address)
            << ", " << table.entries << " entries, bound at " << hex32(table.bound_site)
            << " (" << table.evidence << ")\n";
    for (const auto& jump : graph.constant_jumps)
        out << "// Constant JR " << hex32(jump.jump_site) << " -> " << hex32(jump.target) << " (LUI at "
            << hex32(jump.lui_site) << ")\n";
    for (const auto& jump : graph.unresolved_jumps)
        out << "// Unresolved JR " << hex32(jump.site) << ": " << jump.reason << "\n";
    for (const auto& rejected : graph.rejected_indirect_targets)
        out << "// Rejected code reference " << hex32(rejected.source) << " -> "
            << hex32(rejected.value) << ": " << rejected.reason << "\n";
    for (const auto& boundary : graph.interpreter_boundaries)
        out << "// Interpreter boundary " << hex32(boundary.address) << ": " << boundary.reason << "\n";
    out << "#include <ps2native/aot_runtime.hpp>\n"
        << "#include <ps2native/ee_semantics.hpp>\n"
        << "#include <array>\n"
        << "#include <cstdint>\n\n"
        << "namespace ps2native {\n";
    const auto scope = "aot_" + digest(module_key);
    if (partitions) out << "namespace " << scope << " {\n";
    for (const auto& [start, block] : blocks) {
        (void)block;
        out << (partitions ? "" : "static ") << "void block_" << address_label(start) << "(ExecutionContext&);\n";
    }
    if (partitions) out << "}\nusing namespace " << scope << ";\n";
    out << '\n';
    if (!partitions) {
        for (const auto& [start, block] : blocks) {
            (void)start;
            const auto& last=block.instructions.back();
            emit_block(out,block,last.has_delay_slot() && blocks.contains(last.pc+4u));
        }
    } else {
        std::map<std::uint32_t,std::vector<const BlockSource*>> buckets;
        for (const auto& [start,block] : blocks) buckets[start / partition_bytes].push_back(&block);
        for (const auto& [bucket,group] : buckets) {
            (void)bucket;
            std::ostringstream unit;
            unit << "// AUTO-GENERATED by ps2n-aot; local translated code, do not distribute.\n"
                 << "// Stable address partition; checked instruction bytes, shared EE semantics.\n"
                 << "#include <ps2native/aot_runtime.hpp>\n"
                 << "namespace ps2native::" << scope << " {\n";
            std::set<std::uint32_t> declarations;
            for (const auto* block : group) {
                declarations.insert(block->start);
                const auto& last=block->instructions.back();
                if (last.has_delay_slot() && blocks.contains(last.pc+4u)) declarations.insert(last.pc+4u);
            }
            for (const auto start : declarations)
                unit << "void block_" << address_label(start) << "(ExecutionContext&);\n";
            for (const auto* block : group) {
                const auto& last=block->instructions.back();
                emit_block(unit,*block,last.has_delay_slot() && blocks.contains(last.pc+4u),false);
            }
            unit << "}\n";
            auto text=unit.str();
            partitions->push_back({"ps2n_aot_"+digest(text)+".cpp",std::move(text)});
        }
    }
    out << "static constexpr std::array<AotBlock, " << dispatch_entries.size() << "> kBlocks{{\n";
    for (const auto& [pc,start] : dispatch_entries) {
        out << "    {" << hex32(pc) << ", &block_" << address_label(start) << "},\n";
    }
    out << "}};\n\n"
        << "static constexpr std::array<std::uint32_t, " << graph.indirect_entries.size()
        << "> kIndirectEntries{{\n";
    for (const auto entry : graph.indirect_entries) out << "    " << hex32(entry) << ",\n";
    out << "}};\n\n"
        << "const AotModule& aot_module() {\n"
        << "    static const AotModule module{kBlocks.data(), kBlocks.size(),\n"
        << "        kIndirectEntries.data(), kIndirectEntries.size(),\n"
        << "        \"" << options.source_kind << "\", \"" << source_sha256 << "\"};\n"
        << "    return module;\n"
        << "}\n\n"
        << "std::uint64_t run_recompiled(EEState& state, Memory& memory,\n"
        << "    std::uint32_t stop_pc, std::uint64_t max_instructions) {\n"
        << "    return run_aot_module(aot_module(), state, memory, stop_pc, max_instructions);\n"
        << "}\n} // namespace ps2native\n";
    return out.str();
}
} // namespace

std::string generate_aot_cpp(const ElfImage& image, const AotOptions& options) {
    return generate(image,options);
}
AotSources generate_aot_sources(const ElfImage& image, const AotOptions& options,
    const std::string& module_key, std::uint32_t partition_bytes) {
    AotSources sources;
    sources.dispatcher=generate(image,options,&sources.partitions,module_key,partition_bytes);
    return sources;
}
bool write_aot_sources(const std::filesystem::path& dispatcher, const AotSources& sources) {
    bool changed=false;
    std::string manifest="ps2n-aot-sources-v1 "+digest(sources.dispatcher)+"\n";
    for (const auto& unit : sources.partitions) {
        const auto hash=digest(unit.text);
        if (unit.name!="ps2n_aot_"+hash+".cpp") throw std::runtime_error("Invalid AOT unit identity");
        if (dispatcher.filename()==unit.name) throw std::runtime_error("AOT dispatcher collides with partition");
        changed=write_text_if_changed(dispatcher.parent_path()/unit.name,unit.text) || changed;
        manifest+=unit.name+" "+hash+"\n";
    }
    changed=write_text_if_changed(dispatcher,sources.dispatcher) || changed;
    auto sidecar=dispatcher;sidecar+=".sources";
    return write_text_if_changed(sidecar,manifest) || changed;
}
std::string aot_sources_sha256(const std::filesystem::path& dispatcher) {
    auto sidecar=dispatcher;sidecar+=".sources";
    const auto bytes=read_file(sidecar);
    const std::string manifest(bytes.begin(),bytes.end());
    std::istringstream input(manifest);
    std::string version,hash,name;
    const auto valid_hash=[](const std::string& h) {
        return h.size()==64 && h.find_first_not_of("0123456789abcdef")==std::string::npos;
    };
    if (!(input>>version>>hash) || version!="ps2n-aot-sources-v1" || !valid_hash(hash) || sha256_file(dispatcher)!=hash)
        throw std::runtime_error("AOT dispatcher identity mismatch");
    std::set<std::string> names;
    while (input>>name) {
        if (!(input>>hash) || !valid_hash(hash) || name!="ps2n_aot_"+hash+".cpp" ||
            !names.insert(name).second || sha256_file(dispatcher.parent_path()/name)!=hash)
            throw std::runtime_error("AOT partition identity mismatch");
    }
    return digest(manifest);
}
} // namespace ps2native
