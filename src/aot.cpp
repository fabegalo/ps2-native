#include "ps2native/core.hpp"

#include <cstdint>
#include <iomanip>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace ps2native {
namespace {
enum class Opcode { Nop, Addiu, Addu, Ori, Lui, Lw, Sw, Beq, Bne, J, Jal, Jr };
struct Decoded { Opcode op; std::uint32_t word; };

std::string hex32(std::uint32_t value) {
    std::ostringstream os;
    os << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value << 'u';
    return os.str();
}
std::uint32_t signext_imm32(std::uint32_t word) {
    const auto imm = word & 0xffffu;
    return imm >= 0x8000u ? imm | 0xffff0000u : imm;
}
std::uint32_t branch_target(std::uint32_t pc, std::uint32_t word) {
    return pc + 4u + 4u * signext_imm32(word);
}
std::uint32_t jump_target(std::uint32_t pc, std::uint32_t word) {
    return ((pc + 4u) & 0xf0000000u) | ((word & 0x03ffffffu) << 2);
}
Decoded decode(std::uint32_t pc, std::uint32_t word) {
    const auto op = word >> 26;
    const auto funct = word & 0x3fu;
    const auto rs = (word >> 21) & 31u;
    const auto rt = (word >> 16) & 31u;
    const auto rd = (word >> 11) & 31u;
    const auto shamt = (word >> 6) & 31u;
    if (word == 0) return {Opcode::Nop, word};
    if (op == 0 && funct == 0x21 && shamt == 0) return {Opcode::Addu, word};
    if (op == 0 && funct == 0x08 && rt == 0 && rd == 0 && shamt == 0)
        return {Opcode::Jr, word};
    switch (op) {
        case 0x09: return {Opcode::Addiu, word};
        case 0x0d: return {Opcode::Ori, word};
        case 0x0f: if (rs == 0) return {Opcode::Lui, word}; break;
        case 0x23: return {Opcode::Lw, word};
        case 0x2b: return {Opcode::Sw, word};
        case 0x04: return {Opcode::Beq, word};
        case 0x05: return {Opcode::Bne, word};
        case 0x02: return {Opcode::J, word};
        case 0x03: return {Opcode::Jal, word};
        default: break;
    }
    throw std::runtime_error("AOT unsupported R5900 instruction " + hex32(word) +
                             " at " + hex32(pc) + " (no fake syscall/stub)");
}
bool control(Opcode op) {
    return op == Opcode::Beq || op == Opcode::Bne || op == Opcode::J ||
           op == Opcode::Jal || op == Opcode::Jr;
}
std::map<std::uint32_t, Decoded> discover(const ElfImage& image) {
    std::map<std::uint32_t, Decoded> found;
    std::set<std::uint32_t> expanded;
    std::queue<std::pair<std::uint32_t, bool>> work;
    work.emplace(image.entry, false);
    while (!work.empty()) {
        const auto [pc, is_delay_slot] = work.front();
        work.pop();
        if (!is_delay_slot && !expanded.insert(pc).second) continue;
        if (found.size() >= 131072) throw std::runtime_error("M0 AOT instruction limit reached");
        const auto decoded = decode(pc, image.instruction_at(pc));
        if (is_delay_slot && control(decoded.op))
            throw std::runtime_error("Branch instruction in delay slot is unsupported in M0 at " + hex32(pc));
        const auto [it, inserted] = found.emplace(pc, decoded);
        if (!inserted && it->second.word != decoded.word)
            throw std::runtime_error("Executable bytes changed during analysis");
        if (is_delay_slot) continue;
        if (control(decoded.op)) {
            work.emplace(pc + 4u, true);
            switch (decoded.op) {
                case Opcode::Beq:
                case Opcode::Bne:
                    work.emplace(branch_target(pc, decoded.word), false);
                    work.emplace(pc + 8u, false);
                    break;
                case Opcode::Jal:
                    work.emplace(pc + 8u, false); // Function may return to caller.
                    [[fallthrough]];
                case Opcode::J:
                    work.emplace(jump_target(pc, decoded.word), false);
                    break;
                case Opcode::Jr:
                    // Dynamic address: compile discovered targets only; runtime rejects unknown ones.
                    break;
                default: break;
            }
        } else {
            work.emplace(pc + 4u, false);
        }
    }
    return found;
}
std::string reg(unsigned n) { return "state.reg32(" + std::to_string(n) + "u)"; }
std::string emit(const std::uint32_t pc, const Decoded decoded) {
    const auto word = decoded.word;
    const unsigned rs = (word >> 21) & 31u;
    const unsigned rt = (word >> 16) & 31u;
    const unsigned rd = (word >> 11) & 31u;
    const auto imm = signext_imm32(word);
    std::ostringstream out;
    switch (decoded.op) {
        case Opcode::Nop: out << "            // NOP\n"; break;
        case Opcode::Addiu:
            out << "            state.set_reg32_sign_extended(" << rt << "u, "
                << reg(rs) << " + " << hex32(imm) << ");\n"; break;
        case Opcode::Addu:
            out << "            state.set_reg32_sign_extended(" << rd << "u, "
                << reg(rs) << " + " << reg(rt) << ");\n"; break;
        case Opcode::Ori:
            out << "            state.set_reg32(" << rt << "u, " << reg(rs) << " | "
                << hex32(word & 0xffffu) << ");\n"; break;
        case Opcode::Lui:
            out << "            state.set_reg32_sign_extended(" << rt << "u, "
                << hex32((word & 0xffffu) << 16) << ");\n"; break;
        case Opcode::Lw:
            out << "            state.set_reg32_sign_extended(" << rt
                << "u, memory.read32(" << reg(rs) << " + " << hex32(imm) << "));\n";
            break;
        case Opcode::Sw:
            out << "            memory.write32(" << reg(rs) << " + " << hex32(imm)
                << ", " << reg(rt) << ");\n"; break;
        case Opcode::Beq:
        case Opcode::Bne:
            out << "            state.delay_pending = true;\n"
                << "            state.delay_target = " << reg(rs)
                << (decoded.op == Opcode::Beq ? " == " : " != ") << reg(rt)
                << " ? " << hex32(branch_target(pc, word)) << " : " << hex32(pc + 8u) << ";\n";
            break;
        case Opcode::J:
            out << "            state.delay_pending = true;\n"
                << "            state.delay_target = " << hex32(jump_target(pc, word)) << ";\n";
            break;
        case Opcode::Jal:
            out << "            state.set_reg32_sign_extended(31u, " << hex32(pc + 8u) << ");\n"
                << "            state.delay_pending = true;\n"
                << "            state.delay_target = " << hex32(jump_target(pc, word)) << ";\n";
            break;
        case Opcode::Jr:
            out << "            state.delay_pending = true;\n"
                << "            state.delay_target = " << reg(rs) << ";\n";
            break;
    }
    return out.str();
}
} // namespace

std::string generate_aot_cpp(const ElfImage& image) {
    const auto instructions = discover(image);
    std::ostringstream out;
    out << "// AUTO-GENERATED by ps2n-aot. Contains translated guest instructions; do not distribute proprietary game code.\n"
        << "// Static reachable instructions: " << instructions.size() << "\n"
        << "#include <ps2native/core.hpp>\n"
        << "#include <ps2native/aot_runtime.hpp>\n"
        << "#include <stdexcept>\n\n"
        << "namespace ps2native {\n"
        << "std::uint64_t run_recompiled(EEState& state, Memory& memory,\n"
        << "    std::uint32_t stop_pc, std::uint64_t max_instructions) {\n"
        << "    std::uint64_t executed = 0;\n"
        << "    while (executed < max_instructions) {\n"
        << "        if (!state.delay_pending && state.pc == stop_pc) return executed;\n"
        << "        const bool was_delay_slot = state.delay_pending;\n"
        << "        const auto after_delay = state.delay_target;\n"
        << "        state.delay_pending = false;\n"
        << "        const auto current_pc = state.pc;\n"
        << "        state.pc += 4u;\n"
        << "        switch (current_pc) {\n";
    for (const auto& [pc, decoded] : instructions) {
        out << "        case " << hex32(pc) << ": {\n"
            << emit(pc, decoded) << "            break;\n        }\n";
    }
    out << "        default:\n"
        << "            throw std::runtime_error(\"AOT address not discovered (dynamic code/jump unsupported)\");\n"
        << "        }\n"
        << "        ++executed;\n"
        << "        ++state.retired_instructions;\n"
        << "        if (was_delay_slot) state.pc = after_delay;\n"
        << "    }\n"
        << "    if (!state.delay_pending && state.pc == stop_pc) return executed;\n"
        << "    throw std::runtime_error(\"Guest instruction budget exhausted\");\n"
        << "}\n} // namespace ps2native\n";
    return out.str();
}
} // namespace ps2native
