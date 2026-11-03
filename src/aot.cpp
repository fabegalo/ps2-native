#include "ps2native/aot.hpp"
#include "ps2native/control_flow.hpp"

#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ps2native {
namespace {
struct AotBlock {
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
std::string reg(unsigned index) {
    return "state.reg32(" + std::to_string(index) + "u)";
}

std::map<std::uint32_t, AotBlock> make_blocks(const AotControlFlow& graph) {
    std::map<std::uint32_t, AotBlock> blocks;
    for (const auto start : graph.leaders) {
        auto current = start;
        AotBlock block;
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

std::string emit_opcode(const MipsInstruction& instruction) {
    const auto word = instruction.word;
    const auto pc = instruction.pc;
    const auto rs = instruction.rs;
    const auto rt = instruction.rt;
    const auto rd = instruction.rd;
    const auto imm = mips_sign_extended_imm(word);
    std::ostringstream out;
    switch (instruction.opcode) {
        case MipsOpcode::Nop:
            out << "        // NOP\n";
            break;
        case MipsOpcode::Addiu:
            out << "        state.set_reg32_sign_extended(" << static_cast<unsigned>(rt)
                << "u, " << reg(rs) << " + " << hex32(imm) << ");\n";
            break;
        case MipsOpcode::Addu:
            out << "        state.set_reg32_sign_extended(" << static_cast<unsigned>(rd)
                << "u, " << reg(rs) << " + " << reg(rt) << ");\n";
            break;
        case MipsOpcode::Ori:
            out << "        state.set_reg32(" << static_cast<unsigned>(rt)
                << "u, " << reg(rs) << " | " << hex32(word & 0xffffu) << ");\n";
            break;
        case MipsOpcode::Lui:
            out << "        state.set_reg32_sign_extended(" << static_cast<unsigned>(rt)
                << "u, " << hex32((word & 0xffffu) << 16) << ");\n";
            break;
        case MipsOpcode::Lw:
            out << "        state.set_reg32_sign_extended(" << static_cast<unsigned>(rt)
                << "u, memory.read32(" << reg(rs) << " + " << hex32(imm) << "));\n";
            break;
        case MipsOpcode::Sw:
            out << "        memory.write32(" << reg(rs) << " + " << hex32(imm)
                << ", " << reg(rt) << ");\n";
            break;
        case MipsOpcode::Beq:
        case MipsOpcode::Bne:
            out << "        state.delay_pending = true;\n"
                << "        state.delay_target = " << reg(rs)
                << (instruction.opcode == MipsOpcode::Beq ? " == " : " != ") << reg(rt)
                << " ? " << hex32(mips_branch_target(pc, word))
                << " : " << hex32(pc + 8u) << ";\n";
            break;
        case MipsOpcode::J:
            out << "        state.delay_pending = true;\n"
                << "        state.delay_target = " << hex32(mips_jump_target(pc, word)) << ";\n";
            break;
        case MipsOpcode::Jal:
            out << "        state.set_reg32_sign_extended(31u, " << hex32(pc + 8u) << ");\n"
                << "        state.delay_pending = true;\n"
                << "        state.delay_target = " << hex32(mips_jump_target(pc, word)) << ";\n";
            break;
        case MipsOpcode::Jr:
            out << "        state.delay_pending = true;\n"
                << "        state.delay_target = " << reg(rs) << ";\n";
            break;
        case MipsOpcode::Jalr:
            // Deliberate order: JALR rs==rd must read its target BEFORE writing link.
            out << "        state.delay_target = " << reg(rs) << ";\n"
                << "        state.set_reg32_sign_extended(" << static_cast<unsigned>(rd)
                << "u, " << hex32(pc + 8u) << ");\n"
                << "        state.delay_pending = true;\n";
            break;
    }
    return out.str();
}

void emit_block(std::ostringstream& out, const AotBlock& block) {
    out << "static void block_" << address_label(block.start) << "(BlockContext& ctx) {\n"
        << "    auto& state = ctx.state;\n"
        << "    auto& memory = ctx.memory;\n";
    for (const auto& instruction : block.instructions) {
        const auto pc = instruction.pc;
        out << "    if (state.pc != " << hex32(pc) << ") return;\n"
            << "    if (ctx.executed >= ctx.budget || (!state.delay_pending && state.pc == ctx.stop_pc)) return;\n"
            << (instruction.has_delay_slot()
                ? "    if (state.delay_pending) throw std::runtime_error(\"Control transfer in an active delay slot is unsupported\");\n"
                : "")
            << "    if (memory.read32(" << hex32(pc) << ") != " << hex32(instruction.word) << ")\n"
            << "        throw std::runtime_error(\"AOT executable code modified at " << hex32(pc) << "\");\n"
            << "    {\n"
            << "        const bool in_delay_slot = state.delay_pending;\n"
            << "        const auto pending_target = state.delay_target;\n"
            << "        state.delay_pending = false;\n"
            << "        state.pc = " << hex32(pc + 4u) << ";\n"
            << emit_opcode(instruction)
            << "        ++ctx.executed;\n"
            << "        ++state.retired_instructions;\n"
            << "        if (in_delay_slot) state.pc = pending_target;\n"
            << "    }\n";
    }
    out << "}\n\n";
}
} // namespace

std::string generate_aot_cpp(const ElfImage& image, const AotOptions& options) {
    const auto graph = analyze_control_flow(image, options.additional_function_entries);
    const auto blocks = make_blocks(graph);
    std::ostringstream out;
    out << "// AUTO-GENERATED by ps2n-aot. Do not distribute translated proprietary guest code.\n"
        << "// Static instructions: " << graph.instructions.size()
        << "; block entries: " << blocks.size()
        << "; verified function roots: " << graph.function_entries.size() << "\n"
        << "#include <ps2native/core.hpp>\n"
        << "#include <ps2native/aot_runtime.hpp>\n"
        << "#include <algorithm>\n"
        << "#include <array>\n"
        << "#include <cstdint>\n"
        << "#include <iomanip>\n"
        << "#include <sstream>\n"
        << "#include <stdexcept>\n\n"
        << "namespace ps2native {\n"
        << "struct BlockContext {\n"
        << "    EEState& state;\n"
        << "    Memory& memory;\n"
        << "    std::uint64_t& executed;\n"
        << "    std::uint64_t budget;\n"
        << "    std::uint32_t stop_pc;\n"
        << "};\n\n";
    for (const auto& [start, block] : blocks) {
        (void)start;
        emit_block(out, block);
    }
    out << "struct DispatchEntry { std::uint32_t address; void (*execute)(BlockContext&); };\n"
        << "static constexpr std::array<DispatchEntry, " << blocks.size() << "> kBlocks{{\n";
    for (const auto& [start, block] : blocks) {
        (void)block;
        out << "    {" << hex32(start) << ", &block_" << address_label(start) << "},\n";
    }
    out << "}};\n\n"
        << "std::uint64_t run_recompiled(EEState& state, Memory& memory,\n"
        << "    std::uint32_t stop_pc, std::uint64_t max_instructions) {\n"
        << "    std::uint64_t executed = 0;\n"
        << "    BlockContext ctx{state, memory, executed, max_instructions, stop_pc};\n"
        << "    while (true) {\n"
        << "        if (!state.delay_pending && state.pc == stop_pc) return executed;\n"
        << "        if (executed >= max_instructions)\n"
        << "            throw std::runtime_error(\"Guest instruction budget exhausted\");\n"
        << "        const auto it = std::lower_bound(kBlocks.begin(), kBlocks.end(), state.pc,\n"
        << "            [](const DispatchEntry& entry, std::uint32_t pc) { return entry.address < pc; });\n"
        << "        if (it == kBlocks.end() || it->address != state.pc) {\n"
        << "            std::ostringstream error;\n"
        << "            error << \"AOT target not compiled (indirect jump/dynamic code) at 0x\"\n"
        << "                  << std::uppercase << std::hex << std::setw(8)\n"
        << "                  << std::setfill('0') << state.pc;\n"
        << "            throw std::runtime_error(error.str());\n"
        << "        }\n"
        << "        const auto before = executed;\n"
        << "        it->execute(ctx); // Host-compiled basic-block function, no interpreter.\n"
        << "        if (before == executed)\n"
        << "            throw std::runtime_error(\"AOT dispatcher did not make progress\");\n"
        << "    }\n"
        << "}\n} // namespace ps2native\n";
    return out.str();
}
} // namespace ps2native
