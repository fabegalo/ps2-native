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
std::string hex64(std::uint64_t value) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(16)
           << std::setfill('0') << value << "ull";
    return stream.str();
}
std::string reg(unsigned index) {
    return "state.reg32(" + std::to_string(index) + "u)";
}
std::string reg64(unsigned index) {
    return "state.reg64(" + std::to_string(index) + "u)";
}
std::string set64(unsigned index, const std::string& value) {
    return "        state.set_reg64(" + std::to_string(index) + "u, " + value + ");\n";
}
std::string set32(unsigned index, const std::string& value) {
    return "        state.set_reg32_sign_extended(" + std::to_string(index) + "u, " + value + ");\n";
}
// Effective address: lower 32 bits of rs plus the sign-extended offset.
std::string address(const MipsInstruction& instruction) {
    return reg(instruction.rs) + " + " + hex32(mips_sign_extended_imm(instruction.word));
}
std::uint64_t imm64(std::uint32_t word) {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int16_t>(word & 0xffffu)));
}
std::string branch_condition(const MipsInstruction& instruction) {
    // R5900 branches compare the full lower 64 bits.
    const auto rs = reg64(instruction.rs);
    switch (instruction.opcode) {
        case MipsOpcode::Beq: case MipsOpcode::Beql:
            return rs + " == " + reg64(instruction.rt);
        case MipsOpcode::Bne: case MipsOpcode::Bnel:
            return rs + " != " + reg64(instruction.rt);
        case MipsOpcode::Blez: case MipsOpcode::Blezl:
            return "s64(" + rs + ") <= 0";
        case MipsOpcode::Bgtz: case MipsOpcode::Bgtzl:
            return "s64(" + rs + ") > 0";
        case MipsOpcode::Bltz: case MipsOpcode::Bltzl:
        case MipsOpcode::Bltzal: case MipsOpcode::Bltzall:
            return "s64(" + rs + ") < 0";
        default:
            return "s64(" + rs + ") >= 0";
    }
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
    const auto zimm = hex64(word & 0xffffu);
    const auto sa = std::to_string(instruction.sa) + "u";
    std::ostringstream out;
    switch (instruction.opcode) {
        case MipsOpcode::Nop:
            out << "        // NOP\n";
            break;
        case MipsOpcode::Sync:
            // Host execution is sequential: guest memory accesses are already ordered.
            out << "        // SYNC\n";
            break;
        // 32-bit shifts produce sign-extended 64-bit results.
        case MipsOpcode::Sll:
            out << set64(rd, "sx32(" + reg(rt) + " << " + sa + ")");
            break;
        case MipsOpcode::Srl:
            out << set64(rd, "sx32(" + reg(rt) + " >> " + sa + ")");
            break;
        case MipsOpcode::Sra:
            out << set64(rd, "sx32(static_cast<std::uint32_t>(static_cast<std::int32_t>(" +
                                 reg(rt) + ") >> " + sa + "))");
            break;
        case MipsOpcode::Sllv:
            out << set64(rd, "sx32(" + reg(rt) + " << (" + reg(rs) + " & 31u))");
            break;
        case MipsOpcode::Srlv:
            out << set64(rd, "sx32(" + reg(rt) + " >> (" + reg(rs) + " & 31u))");
            break;
        case MipsOpcode::Srav:
            out << set64(rd, "sx32(static_cast<std::uint32_t>(static_cast<std::int32_t>(" +
                                 reg(rt) + ") >> (" + reg(rs) + " & 31u)))");
            break;
        case MipsOpcode::Dsll:
            out << set64(rd, reg64(rt) + " << " + sa);
            break;
        case MipsOpcode::Dsrl:
            out << set64(rd, reg64(rt) + " >> " + sa);
            break;
        case MipsOpcode::Dsra:
            out << set64(rd, "static_cast<std::uint64_t>(s64(" + reg64(rt) + ") >> " + sa + ")");
            break;
        case MipsOpcode::Dsll32:
            out << set64(rd, reg64(rt) + " << " + std::to_string(instruction.sa + 32u) + "u");
            break;
        case MipsOpcode::Dsrl32:
            out << set64(rd, reg64(rt) + " >> " + std::to_string(instruction.sa + 32u) + "u");
            break;
        case MipsOpcode::Dsra32:
            out << set64(rd, "static_cast<std::uint64_t>(s64(" + reg64(rt) + ") >> " +
                                 std::to_string(instruction.sa + 32u) + "u)");
            break;
        case MipsOpcode::Dsllv:
            out << set64(rd, reg64(rt) + " << (" + reg(rs) + " & 63u)");
            break;
        case MipsOpcode::Dsrlv:
            out << set64(rd, reg64(rt) + " >> (" + reg(rs) + " & 63u)");
            break;
        case MipsOpcode::Dsrav:
            out << set64(rd, "static_cast<std::uint64_t>(s64(" + reg64(rt) + ") >> (" +
                                 reg(rs) + " & 63u))");
            break;
        case MipsOpcode::Addu:
            out << set32(rd, reg(rs) + " + " + reg(rt));
            break;
        case MipsOpcode::Subu:
            out << set32(rd, reg(rs) + " - " + reg(rt));
            break;
        case MipsOpcode::Daddu:
            out << set64(rd, reg64(rs) + " + " + reg64(rt));
            break;
        case MipsOpcode::Dsubu:
            out << set64(rd, reg64(rs) + " - " + reg64(rt));
            break;
        case MipsOpcode::And:
            out << set64(rd, reg64(rs) + " & " + reg64(rt));
            break;
        case MipsOpcode::Or:
            out << set64(rd, reg64(rs) + " | " + reg64(rt));
            break;
        case MipsOpcode::Xor:
            out << set64(rd, reg64(rs) + " ^ " + reg64(rt));
            break;
        case MipsOpcode::Nor:
            out << set64(rd, "~(" + reg64(rs) + " | " + reg64(rt) + ")");
            break;
        case MipsOpcode::Slt:
            out << set64(rd, "s64(" + reg64(rs) + ") < s64(" + reg64(rt) + ") ? 1u : 0u");
            break;
        case MipsOpcode::Sltu:
            out << set64(rd, reg64(rs) + " < " + reg64(rt) + " ? 1u : 0u");
            break;
        case MipsOpcode::Movz:
            out << "        if (" << reg64(rt) << " == 0u)\n    " << set64(rd, reg64(rs));
            break;
        case MipsOpcode::Movn:
            out << "        if (" << reg64(rt) << " != 0u)\n    " << set64(rd, reg64(rs));
            break;
        // R5900 MULT/MULTU: sign-extended 32-bit halves in LO/HI; LO also goes to rd.
        case MipsOpcode::Mult:
            out << "        {\n"
                << "            const auto product = static_cast<std::uint64_t>(\n"
                << "                static_cast<std::int64_t>(static_cast<std::int32_t>(" << reg(rs) << ")) *\n"
                << "                static_cast<std::int64_t>(static_cast<std::int32_t>(" << reg(rt) << ")));\n"
                << "            state.lo = sx32(static_cast<std::uint32_t>(product));\n"
                << "            state.hi = sx32(static_cast<std::uint32_t>(product >> 32));\n"
                << "        }\n" << set64(rd, "state.lo");
            break;
        case MipsOpcode::Multu:
            out << "        {\n"
                << "            const auto product = static_cast<std::uint64_t>(" << reg(rs)
                << ") * " << reg(rt) << ";\n"
                << "            state.lo = sx32(static_cast<std::uint32_t>(product));\n"
                << "            state.hi = sx32(static_cast<std::uint32_t>(product >> 32));\n"
                << "        }\n" << set64(rd, "state.lo");
            break;
        // R5900 DIV/DIVU never trap: overflow and division by zero have defined results.
        case MipsOpcode::Div:
            out << "        {\n"
                << "            const auto n = static_cast<std::int32_t>(" << reg(rs) << ");\n"
                << "            const auto d = static_cast<std::int32_t>(" << reg(rt) << ");\n"
                << "            if (n == INT32_MIN && d == -1) { state.lo = sx32(0x80000000u); state.hi = 0u; }\n"
                << "            else if (d != 0) {\n"
                << "                state.lo = sx32(static_cast<std::uint32_t>(n / d));\n"
                << "                state.hi = sx32(static_cast<std::uint32_t>(n % d));\n"
                << "            } else {\n"
                << "                state.lo = n < 0 ? 1u : ~0ull;\n"
                << "                state.hi = sx32(static_cast<std::uint32_t>(n));\n"
                << "            }\n"
                << "        }\n";
            break;
        case MipsOpcode::Divu:
            out << "        {\n"
                << "            const auto n = " << reg(rs) << ";\n"
                << "            const auto d = " << reg(rt) << ";\n"
                << "            if (d != 0u) { state.lo = sx32(n / d); state.hi = sx32(n % d); }\n"
                << "            else { state.lo = ~0ull; state.hi = sx32(n); }\n"
                << "        }\n";
            break;
        case MipsOpcode::Mfhi:
            out << set64(rd, "state.hi");
            break;
        case MipsOpcode::Mflo:
            out << set64(rd, "state.lo");
            break;
        case MipsOpcode::Mthi:
            out << "        state.hi = " << reg64(rs) << ";\n";
            break;
        case MipsOpcode::Mtlo:
            out << "        state.lo = " << reg64(rs) << ";\n";
            break;
        case MipsOpcode::Addiu:
            out << set32(rt, reg(rs) + " + " + hex32(imm));
            break;
        case MipsOpcode::Daddiu:
            out << set64(rt, reg64(rs) + " + " + hex64(imm64(word)));
            break;
        case MipsOpcode::Slti:
            out << set64(rt, "s64(" + reg64(rs) + ") < s64(" + hex64(imm64(word)) + ") ? 1u : 0u");
            break;
        case MipsOpcode::Sltiu:
            out << set64(rt, reg64(rs) + " < " + hex64(imm64(word)) + " ? 1u : 0u");
            break;
        // Logical immediates are zero-extended and act on all 64 bits.
        case MipsOpcode::Andi:
            out << set64(rt, reg64(rs) + " & " + zimm);
            break;
        case MipsOpcode::Ori:
            out << set64(rt, reg64(rs) + " | " + zimm);
            break;
        case MipsOpcode::Xori:
            out << set64(rt, reg64(rs) + " ^ " + zimm);
            break;
        case MipsOpcode::Lui:
            out << set32(rt, hex32((word & 0xffffu) << 16));
            break;
        case MipsOpcode::Lb:
            out << set64(rt, "static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int8_t>(memory.read8(" +
                                 address(instruction) + "))))");
            break;
        case MipsOpcode::Lbu:
            out << set64(rt, "memory.read8(" + address(instruction) + ")");
            break;
        case MipsOpcode::Lh:
            out << set64(rt, "static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int16_t>(memory.read16(" +
                                 address(instruction) + "))))");
            break;
        case MipsOpcode::Lhu:
            out << set64(rt, "memory.read16(" + address(instruction) + ")");
            break;
        case MipsOpcode::Lw:
            out << set32(rt, "memory.read32(" + address(instruction) + ")");
            break;
        case MipsOpcode::Lwu:
            out << set64(rt, "memory.read32(" + address(instruction) + ")");
            break;
        case MipsOpcode::Ld:
            out << set64(rt, "memory.read64(" + address(instruction) + ")");
            break;
        // LQ/SQ silently clear the low four address bits (no address error).
        case MipsOpcode::Lq:
            out << "        {\n"
                << "            const auto quad = (" << address(instruction) << ") & ~15u;\n"
                << "            const auto low = memory.read64(quad);\n"
                << "            state.set_reg128(" << static_cast<unsigned>(rt)
                << "u, low, memory.read64(quad + 8u));\n"
                << "        }\n";
            break;
        case MipsOpcode::Sb:
            out << "        memory.write8(" << address(instruction)
                << ", static_cast<std::uint8_t>(" << reg(rt) << "));\n";
            break;
        case MipsOpcode::Sh:
            out << "        memory.write16(" << address(instruction)
                << ", static_cast<std::uint16_t>(" << reg(rt) << "));\n";
            break;
        case MipsOpcode::Sw:
            out << "        memory.write32(" << address(instruction)
                << ", " << reg(rt) << ");\n";
            break;
        case MipsOpcode::Sd:
            out << "        memory.write64(" << address(instruction)
                << ", " << reg64(rt) << ");\n";
            break;
        case MipsOpcode::Sq:
            out << "        {\n"
                << "            const auto quad = (" << address(instruction) << ") & ~15u;\n"
                << "            const auto value = state.reg128(" << static_cast<unsigned>(rt) << "u);\n"
                << "            memory.write64(quad, value.low);\n"
                << "            memory.write64(quad + 8u, value.high);\n"
                << "        }\n";
            break;
        case MipsOpcode::Beq: case MipsOpcode::Bne: case MipsOpcode::Blez:
        case MipsOpcode::Bgtz: case MipsOpcode::Bltz: case MipsOpcode::Bgez:
        case MipsOpcode::Bltzal: case MipsOpcode::Bgezal:
        case MipsOpcode::Beql: case MipsOpcode::Bnel: case MipsOpcode::Blezl:
        case MipsOpcode::Bgtzl: case MipsOpcode::Bltzl: case MipsOpcode::Bgezl:
        case MipsOpcode::Bltzall: case MipsOpcode::Bgezall:
            // The condition reads rs before BxxZAL writes the link register.
            out << "        const bool taken = " << branch_condition(instruction) << ";\n";
            if (instruction.links())
                out << "        state.set_reg32_sign_extended(31u, " << hex32(pc + 8u) << ");\n";
            if (instruction.is_likely_branch()) {
                out << "        if (taken) {\n"
                    << "            state.delay_pending = true;\n"
                    << "            state.delay_target = " << hex32(mips_branch_target(pc, word)) << ";\n"
                    << "        } else {\n"
                    << "            state.pc = " << hex32(pc + 8u) << "; // likely: delay slot nullified\n"
                    << "        }\n";
            } else {
                out << "        state.delay_pending = true;\n"
                    << "        state.delay_target = taken ? " << hex32(mips_branch_target(pc, word))
                    << " : " << hex32(pc + 8u) << ";\n";
            }
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
                << "        state.delay_indirect = true;\n"
                << "        state.delay_target = " << reg(rs) << ";\n";
            break;
        case MipsOpcode::Jalr:
            // Deliberate order: JALR rs==rd must read its target BEFORE writing link.
            out << "        state.delay_target = " << reg(rs) << ";\n"
                << "        state.set_reg32_sign_extended(" << static_cast<unsigned>(rd)
                << "u, " << hex32(pc + 8u) << ");\n"
                << "        state.delay_pending = true;\n"
                << "        state.delay_indirect = true;\n";
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
            << "        const bool pending_indirect = state.delay_indirect;\n"
            << "        state.delay_pending = false;\n"
            << "        state.delay_indirect = false;\n"
            << "        state.pc = " << hex32(pc + 4u) << ";\n"
            << emit_opcode(instruction)
            << "        ++ctx.executed;\n"
            << "        ++state.retired_instructions;\n"
            << "        if (in_delay_slot) {\n"
            << "            state.pc = pending_target;\n"
            << "            // JR/JALR targets are always checked by the dispatcher.\n"
            << "            if (pending_indirect) { state.indirect_entry_pending = true; return; }\n"
            << "        }\n"
            << "    }\n";
    }
    out << "}\n\n";
}
} // namespace

std::string generate_aot_cpp(const ElfImage& image, const AotOptions& options) {
    const auto graph = analyze_control_flow(image, options.additional_function_entries,
                                           options.jump_tables);
    const auto blocks = make_blocks(graph);
    std::ostringstream out;
    out << "// AUTO-GENERATED by ps2n-aot. Do not distribute translated proprietary guest code.\n"
        << "// Static instructions: " << graph.instructions.size()
        << "; block entries: " << blocks.size()
        << "; verified function roots: " << graph.function_entries.size()
        << "; evidence-backed indirect targets: " << graph.resolved_indirect_targets.size()
        << "; rejected relocation candidates: " << graph.rejected_indirect_targets.size() << "\n"
        << "// Verified indirect entries: " << graph.indirect_entries.size()
        << "; proven jump tables: " << graph.jump_tables.size()
        << "; unresolved JR sites (runtime-checked): " << graph.unresolved_jumps.size()
        << "; unpaired R_MIPS_LO16: " << image.unpaired_lo16_relocations << "\n";
    for (const auto& [type, count] : image.unused_relocation_counts)
        out << "// Relocation type " << type << " not used as evidence: " << count << " record(s)\n";
    if (image.ignored_rela_sections != 0)
        out << "// SHT_RELA sections ignored: " << image.ignored_rela_sections << "\n";
    for (const auto& table : graph.jump_tables)
        out << "// Jump table: JR " << hex32(table.jump_site) << ", table " << hex32(table.table_address)
            << ", " << table.entries << " entries, bound at " << hex32(table.bound_site)
            << " (" << table.evidence << ")\n";
    for (const auto& jump : graph.unresolved_jumps)
        out << "// Unresolved JR " << hex32(jump.site) << ": " << jump.reason << "\n";
    for (const auto& rejected : graph.rejected_indirect_targets)
        out << "// Rejected code reference " << hex32(rejected.source) << " -> "
            << hex32(rejected.value) << ": " << rejected.reason << "\n";
    out << "#include <ps2native/core.hpp>\n"
        << "#include <ps2native/aot_runtime.hpp>\n"
        << "#include <algorithm>\n"
        << "#include <array>\n"
        << "#include <cstdint>\n"
        << "#include <iomanip>\n"
        << "#include <sstream>\n"
        << "#include <stdexcept>\n\n"
        << "namespace ps2native {\n"
        << "[[maybe_unused]] static inline std::uint64_t sx32(std::uint32_t value) {\n"
        << "    return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(value)));\n"
        << "}\n"
        << "[[maybe_unused]] static inline std::int64_t s64(std::uint64_t value) {\n"
        << "    return static_cast<std::int64_t>(value);\n"
        << "}\n"
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
        << "static constexpr std::array<std::uint32_t, " << graph.indirect_entries.size()
        << "> kIndirectEntries{{\n";
    for (const auto entry : graph.indirect_entries) out << "    " << hex32(entry) << ",\n";
    out << "}};\n\n"
        << "std::uint64_t run_recompiled(EEState& state, Memory& memory,\n"
        << "    std::uint32_t stop_pc, std::uint64_t max_instructions) {\n"
        << "    std::uint64_t executed = 0;\n"
        << "    BlockContext ctx{state, memory, executed, max_instructions, stop_pc};\n"
        << "    while (true) {\n"
        << "        if (!state.delay_pending && state.pc == stop_pc) {\n"
        << "            state.indirect_entry_pending = false; // Host return address.\n"
        << "            return executed;\n"
        << "        }\n"
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
        << "        if (state.indirect_entry_pending) {\n"
        << "            if (!std::binary_search(kIndirectEntries.begin(), kIndirectEntries.end(), state.pc)) {\n"
        << "                std::ostringstream error;\n"
        << "                error << \"AOT indirect transfer to 0x\" << std::uppercase << std::hex\n"
        << "                      << std::setw(8) << std::setfill('0') << state.pc\n"
        << "                      << \" is not a verified entry (function root, call continuation or proven jump-table target)\";\n"
        << "                throw std::runtime_error(error.str());\n"
        << "            }\n"
        << "            state.indirect_entry_pending = false;\n"
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
