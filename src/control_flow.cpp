#include "ps2native/control_flow.hpp"

#include <array>
#include <iomanip>
#include <limits>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <variant>

namespace ps2native {
namespace {
constexpr std::uint32_t kMaxBoundDistance = 16;   // instructions scanned back from JR
constexpr std::uint32_t kMaxTableEntries = 4096;

std::string hex(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

// Every address execution can reach other than by falling through from the
// previous instruction: direct branch/jump targets plus verified indirect entries.
std::set<std::uint32_t> entry_points(const AotControlFlow& graph) {
    auto entries = graph.indirect_entries;
    for (const auto& [pc, instruction] : graph.instructions) {
        if (instruction.is_conditional_branch())
            entries.insert(mips_branch_target(pc, instruction.word));
        else if (instruction.opcode == MipsOpcode::J || instruction.opcode == MipsOpcode::Jal)
            entries.insert(mips_jump_target(pc, instruction.word));
    }
    return entries;
}

// Abstract register contents along the straight-line path from SLTIU to JR.
struct Symbolic {
    enum class Kind : std::uint8_t { Unknown, Constant, Index, Bound, Address, TableEntry };
    Kind kind{Kind::Unknown};
    std::uint32_t value{};   // Constant value, Address base, or TableEntry table address
    unsigned shift{};        // Index/Address: index scaled by 1 << shift
    std::uint32_t low_site{}; // instruction that added the low 16 bits of the table address
};
struct TableProof {
    std::uint32_t bound_site{};
    std::uint32_t table{};
    std::uint32_t entries{};
    std::uint32_t low_site{};
};

// Proves `idx < N` guards a JR whose register was loaded from table[idx]:
//   sltiu t, idx, N ; beq t, $zero, out_of_range ; [sll s, idx, 2] ;
//   lui b, %hi(T) ; [addiu b, b, %lo(T)] ; addu a, s, b ; lw r, %lo(T)|0(a) ; jr r
// The instructions must be straight-line (the BEQ fall-through is the in-range
// path). Any other shape is reported, never guessed.
std::variant<TableProof, std::string> evaluate_bounded_table(
    const AotControlFlow& graph, std::uint32_t bound_site, std::uint32_t jump_site) {
    using Kind = Symbolic::Kind;
    const auto& bound = graph.instructions.at(bound_site);
    const auto raw_limit = bound.word & 0xffffu;
    if (bound.rs == 0) return std::string("SLTIU bound tests $zero");
    if (raw_limit == 0 || raw_limit > kMaxTableEntries)
        return "SLTIU bound " + std::to_string(raw_limit) + " is not a table size in 1..4096";

    std::array<Symbolic, 32> regs{};
    regs[0] = {Kind::Constant, 0u, 0u, 0u};
    regs[bound.rs] = {Kind::Index, 0u, 0u, 0u};
    if (bound.rt != 0) regs[bound.rt] = {Kind::Bound, 0u, 0u, 0u};
    bool guarded = false;
    for (std::uint32_t pc = bound_site + 4u; pc <= jump_site; pc += 4u) {
        const auto found = graph.instructions.find(pc);
        if (found == graph.instructions.end())
            return "instruction " + hex(pc) + " between bound and JR was not discovered";
        const auto& instruction = found->second;
        if (pc == jump_site) {
            const auto& target = regs[instruction.rs];
            if (!guarded) return std::string("SLTIU result does not guard the JR with BEQ $zero");
            if (target.kind != Kind::TableEntry)
                return std::string("JR register is not loaded from a bounded, word-scaled table");
            return TableProof{bound_site, target.value, raw_limit, target.low_site};
        }
        if (instruction.has_delay_slot()) {
            const bool tests_bound =
                (regs[instruction.rs].kind == Kind::Bound && instruction.rt == 0) ||
                (instruction.rs == 0 && regs[instruction.rt].kind == Kind::Bound);
            if (instruction.opcode == MipsOpcode::Beq && !guarded && tests_bound) {
                guarded = true; // Its delay slot executes on both paths and is evaluated next.
                continue;
            }
            return "control transfer at " + hex(pc) + " between bound check and JR";
        }
        const auto imm = mips_sign_extended_imm(instruction.word);
        const auto& a = regs[instruction.rs];
        const auto& b = regs[instruction.rt];
        Symbolic result{};
        switch (instruction.opcode) {
            case MipsOpcode::Nop:
            case MipsOpcode::Sync:
                continue;
            case MipsOpcode::Lui:
                result = {Kind::Constant, (instruction.word & 0xffffu) << 16, 0u, 0u};
                break;
            case MipsOpcode::Ori:
                if (a.kind == Kind::Constant)
                    result = {Kind::Constant, a.value | (instruction.word & 0xffffu), 0u, 0u};
                break;
            case MipsOpcode::Addiu:
            case MipsOpcode::Daddiu:
                if (a.kind == Kind::Constant) result = {Kind::Constant, a.value + imm, 0u, pc};
                else if (a.kind == Kind::Address) result = {Kind::Address, a.value + imm, a.shift, pc};
                break;
            case MipsOpcode::Sll:
            case MipsOpcode::Dsll:
                if (b.kind == Kind::Index && b.shift + instruction.sa <= 3u)
                    result = {Kind::Index, 0u, b.shift + instruction.sa, 0u};
                break;
            case MipsOpcode::Addu:
            case MipsOpcode::Daddu:
                if (a.kind == Kind::Index && b.kind == Kind::Constant)
                    result = {Kind::Address, b.value, a.shift, b.low_site};
                else if (a.kind == Kind::Constant && b.kind == Kind::Index)
                    result = {Kind::Address, a.value, b.shift, a.low_site};
                else if (a.kind == Kind::Constant && b.kind == Kind::Constant)
                    result = {Kind::Constant, a.value + b.value, 0u, 0u};
                break;
            case MipsOpcode::Lw:
                // The LW carries %lo(T) unless an ADDIU already added the low half.
                if (a.kind == Kind::Address && a.shift == 2u)
                    result = {Kind::TableEntry, a.value + imm, 0u, a.low_site != 0u ? a.low_site : pc};
                break;
            default:
                break; // Anything else makes its destination unknown.
        }
        if (const auto written = instruction.written_gpr()) regs[*written] = result;
    }
    return std::string("JR not reached from SLTIU bound");
}

std::variant<TableProof, std::string> prove_bounded_table(const AotControlFlow& graph,
                                                          std::uint32_t jump_site) {
    for (std::uint32_t distance = 1; distance <= kMaxBoundDistance; ++distance) {
        if (jump_site < 4u * distance) break;
        const auto pc = jump_site - 4u * distance;
        const auto found = graph.instructions.find(pc);
        if (found == graph.instructions.end()) break;
        if (found->second.opcode == MipsOpcode::Sltiu)
            return evaluate_bounded_table(graph, pc, jump_site);
        // Only the BEQ guard may sit between the bound and the JR; never scan
        // back across another control transfer (e.g. a previous function's return).
        if (found->second.has_delay_slot() && found->second.opcode != MipsOpcode::Beq) break;
    }
    return std::string("no SLTIU bound check within 16 instructions before JR");
}

std::optional<std::uint32_t> window_entry(const std::set<std::uint32_t>& entries,
                                          std::uint32_t bound_site, std::uint32_t jump_site) {
    const auto inside = entries.upper_bound(bound_site);
    if (inside != entries.end() && *inside <= jump_site) return *inside;
    return std::nullopt;
}

// Propagate constants through a discovered straight-line suffix. Copies and
// address arithmetic may use different GPRs; no loads, caller values, delay-slot
// definitions or incoming paths bypassing a contributing LUI become evidence.
std::variant<AotConstantJump, std::string> prove_constant_target(const AotControlFlow& graph,
                                                                 std::uint32_t jump_site) {
    auto first=jump_site;
    for (unsigned distance=1;distance<=kMaxBoundDistance && jump_site>=4u*distance;++distance) {
        const auto pc=jump_site-4u*distance;
        const auto found=graph.instructions.find(pc);
        if (found==graph.instructions.end() || found->second.has_delay_slot()) break;
        first=pc;
    }
    if (first>=4u) {
        const auto previous=graph.instructions.find(first-4u);
        if (previous!=graph.instructions.end() && previous->second.has_delay_slot()) first+=4u;
    }
    struct Constant { std::uint32_t value{},lui{}; };
    std::array<std::optional<Constant>,32> registers{};
    registers[0]=Constant{};
    for (auto pc=first;pc<jump_site;pc+=4u) {
        const auto& instruction=graph.instructions.at(pc);
        const auto written=instruction.written_gpr();
        if (!written) continue;
        const auto a=registers[instruction.rs],b=registers[instruction.rt];
        std::optional<Constant> result;
        using O=MipsOpcode;
        if (instruction.opcode==O::Lui) result=Constant{(instruction.word&0xFFFFu)<<16u,pc};
        else if (a && a->lui && (instruction.opcode==O::Ori || instruction.opcode==O::Addiu || instruction.opcode==O::Daddiu)) {
            const auto value=instruction.opcode==O::Ori ? a->value|(instruction.word&0xFFFFu)
                : a->value+mips_sign_extended_imm(instruction.word);
            result=Constant{value,a->lui};
        } else if (a && b && (instruction.opcode==O::Addu || instruction.opcode==O::Daddu ||
                             instruction.opcode==O::Or)) {
            const auto origin=!a->lui ? b->lui : !b->lui ? a->lui : std::min(a->lui,b->lui);
            const auto value=instruction.opcode==O::Or ? a->value|b->value : a->value+b->value;
            result=Constant{value,origin};
        }
        registers[*written]=result;
    }
    const auto target=registers[graph.instructions.at(jump_site).rs];
    if (!target || !target->lui)
        return std::string("no constant LUI-derived target in the straight-line code before JR");
    return AotConstantJump{jump_site,target->lui,target->value};
}
} // namespace

AotControlFlow analyze_control_flow(
    const ElfImage& image, std::span<const std::uint32_t> additional_function_entries,
    std::span<const AotJumpTable> additional_jump_tables, bool interpreter_boundaries) {
    AotControlFlow graph;
    std::set<std::uint32_t> expanded_as_normal;
    std::set<std::uint32_t> boundary_addresses;
    std::queue<std::pair<std::uint32_t, bool>> pending;

    auto boundary = [&](std::uint32_t address, const std::string& reason) {
        graph.leaders.erase(address);
        if (boundary_addresses.insert(address).second)
            graph.interpreter_boundaries.push_back({address, reason});
    };
    // Instruction word, or nullopt at an interpreter boundary (hybrid mode only).
    auto fetch = [&](std::uint32_t address) -> std::optional<std::uint32_t> {
        if (!interpreter_boundaries) return image.instruction_at(address);
        try {
            return image.instruction_at(address);
        } catch (const std::exception& error) {
            boundary(address, std::string("outside the static image (") + error.what() + ")");
            return std::nullopt;
        }
    };

    auto seed = [&](std::uint32_t address, bool function) {
        if ((address & 3u) != 0)
            throw std::runtime_error("Unaligned AOT function entry address");
        if (!fetch(address)) return; // Validate executable file-backed code.
        if (boundary_addresses.contains(address)) return;
        graph.leaders.insert(address);
        if (function) {
            graph.function_entries.insert(address);
            graph.indirect_entries.insert(address);
        }
        pending.emplace(address, false);
    };

    seed(image.entry, true);
    for (const auto address : image.function_entries) seed(address, true);
    for (const auto address : additional_function_entries) seed(address, true);
    // Relocation evidence validated by the ELF parser (source section, symbol,
    // target section and the linked bytes themselves).
    for (const auto& rejected : image.rejected_code_pointers) {
        graph.rejected_indirect_targets.push_back(
            {rejected.source, rejected.value, rejected.reason});
    }
    for (const auto& target : image.relocated_code_pointers) {
        switch (target.kind) {
            case CodeReferenceKind::DataWord32:
                seed(target.target, true);
                graph.resolved_indirect_targets.push_back({target.source, target.target, "ELF R_MIPS_32"});
                break;
            case CodeReferenceKind::Call26:
                seed(target.target, true);
                graph.resolved_indirect_targets.push_back({target.source, target.target, "ELF R_MIPS_26 JAL"});
                break;
            case CodeReferenceKind::Jump26:
                seed(target.target, false); // A J target is a label, not a callable entry.
                graph.resolved_indirect_targets.push_back({target.source, target.target, "ELF R_MIPS_26 J"});
                break;
            case CodeReferenceKind::AddressHi16Lo16:
                seed(target.target, true);
                graph.resolved_indirect_targets.push_back(
                    {target.source, target.target, "ELF R_MIPS_HI16/LO16 code address"});
                break;
        }
    }
    // For stripped ELFs, a caller may identify the exact table address/count.
    // Never scan unrelated data; an invalid table is rejected in full.
    if (additional_jump_tables.size() > 256u)
        throw std::runtime_error("Too many explicitly declared jump tables");
    std::size_t pointer_count = 0;
    for (const auto& table : additional_jump_tables) {
        if ((table.address & 3u) != 0u || table.entries == 0u ||
            table.entries > kMaxTableEntries || pointer_count + table.entries > kMaxTableEntries)
            throw std::runtime_error("Invalid jump table alignment/count/limit");
        pointer_count += table.entries;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> verified;
        for (std::uint32_t index = 0; index < table.entries; ++index) {
            const std::uint64_t position = static_cast<std::uint64_t>(table.address) +
                                           static_cast<std::uint64_t>(index) * 4u;
            if (position > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("Jump table address overflow");
            const auto source = static_cast<std::uint32_t>(position);
            std::uint32_t target{};
            try {
                target = image.data_word_at(source);
                (void)image.instruction_at(target);
            } catch (const std::exception& error) {
                std::ostringstream message;
                message << "Rejected explicit jump table entry " << index
                        << " at 0x" << std::hex << std::uppercase << source
                        << ": " << error.what();
                throw std::runtime_error(message.str());
            }
            verified.emplace_back(source, target);
        }
        // Commit only when every entry is backed by valid executable data.
        for (const auto& [source, target] : verified) {
            seed(target, true);
            graph.resolved_indirect_targets.push_back(
                {source, target, "explicit jump table"});
        }
    }
    if (graph.function_entries.size() > 65536u)
        throw std::runtime_error("Too many AOT entry hints/symbols/tables");

    auto drain = [&] {
        while (!pending.empty()) {
            const auto [pc, is_delay_slot] = pending.front();
            pending.pop();
            if (!is_delay_slot && !expanded_as_normal.insert(pc).second) continue;
            if (graph.instructions.size() >= 131072)
                throw std::runtime_error("M1 AOT instruction discovery limit reached");
            if (boundary_addresses.contains(pc)) continue;
            const auto word = fetch(pc);
            if (!word) continue;
            if (interpreter_boundaries) {
                const auto candidate = try_decode_r5900(pc, *word);
                if (!candidate || !mips_aot_supported(candidate->opcode)) {
                    boundary(pc, candidate ? std::string("system instruction ") +
                                                 mips_opcode_name(candidate->opcode)
                                           : std::string("instruction not implemented by the AOT"));
                    // A system instruction that does not transfer control falls
                    // through: translation resumes after it. ERET/SYSCALL/BREAK
                    // and unknown encodings end static discovery on this path.
                    const bool falls_through = candidate && candidate->opcode != MipsOpcode::Eret &&
                                               candidate->opcode != MipsOpcode::Syscall &&
                                               candidate->opcode != MipsOpcode::Break;
                    if (falls_through && !is_delay_slot) seed(pc + 4u, false);
                    continue;
                }
            }
            const auto decoded = decode_mips(pc, *word);
            if (is_delay_slot && decoded.has_delay_slot())
                throw std::runtime_error("Control transfer in branch delay slot is unsupported by M1");
            graph.instructions.emplace(pc, decoded);
            if (is_delay_slot) continue;
            if (!decoded.has_delay_slot()) {
                pending.emplace(pc + 4u, false);
                continue;
            }
            // Delay slot is a dispatchable entry to preserve exact budget, fault
            // positions and resume behavior, even after indirect transfers.
            graph.leaders.insert(pc + 4u);
            pending.emplace(pc + 4u, true);
            // A linking instruction's continuation is a legitimate JR $ra target.
            if (decoded.links()) graph.indirect_entries.insert(pc + 8u);
            if (decoded.is_conditional_branch()) {
                seed(mips_branch_target(pc, decoded.word), false);
                seed(pc + 8u, false); // Not-taken path (also after a nullified likely slot).
                continue;
            }
            switch (decoded.opcode) {
                case MipsOpcode::Jal:
                    seed(pc + 8u, false);
                    seed(mips_jump_target(pc, decoded.word), true); // A JAL target is a function.
                    break;
                case MipsOpcode::J:
                    seed(mips_jump_target(pc, decoded.word), false);
                    break;
                case MipsOpcode::Jalr:
                    seed(pc + 8u, false); // Target resolved at runtime against indirect_entries.
                    break;
                default:
                    break; // JR: returns, proven tables, or runtime-checked targets.
            }
        }
    };

    // Discovery and jump-table proofs feed each other until nothing new appears.
    std::set<std::uint32_t> attempted;
    while (true) {
        drain();
        std::vector<std::uint32_t> jumps;
        for (const auto& [pc, instruction] : graph.instructions) {
            if (((instruction.opcode == MipsOpcode::Jr && instruction.rs != 31) ||
                 instruction.opcode == MipsOpcode::Jalr) && !attempted.contains(pc))
                jumps.push_back(pc);
        }
        bool grew = false;
        for (const auto jump_site : jumps) {
            attempted.insert(jump_site);
            const auto proof = prove_bounded_table(graph, jump_site);
            if (const auto* reason = std::get_if<std::string>(&proof)) {
                const auto constant = prove_constant_target(graph, jump_site);
                if (const auto* why = std::get_if<std::string>(&constant)) {
                    graph.unresolved_jumps.push_back({jump_site, *reason + "; not a constant target: " + *why});
                    continue;
                }
                const auto& jump = std::get<AotConstantJump>(constant);
                if (const auto entry = window_entry(entry_points(graph), jump.lui_site, jump_site)) {
                    graph.unresolved_jumps.push_back(
                        {jump_site, "constant target window is entered at " + hex(*entry) + ", bypassing the LUI"});
                    continue;
                }
                try {
                    (void)image.instruction_at(jump.target);
                } catch (const std::exception& error) {
                    graph.unresolved_jumps.push_back({jump_site, "constant target " + hex(jump.target) +
                                                                     " is not code in the image: " + error.what()});
                    continue;
                }
                graph.constant_jumps.push_back(jump);
                seed(jump.target, graph.instructions.at(jump_site).opcode == MipsOpcode::Jalr);
                graph.indirect_entries.insert(jump.target);
                graph.resolved_indirect_targets.push_back({jump_site, jump.target, "constant LUI/ORI JR"});
                grew = true;
                continue;
            }
            const auto& table = std::get<TableProof>(proof);
            if (const auto entry = window_entry(entry_points(graph), table.bound_site, jump_site)) {
                graph.unresolved_jumps.push_back(
                    {jump_site, "guarded window is entered at " + hex(*entry) + ", bypassing the SLTIU bound"});
                continue;
            }
            std::string evidence = "SLTIU bound + BEQ guard";
            std::string problem;
            for (const auto& pair : image.address_pairs) {
                if (pair.low_site != table.low_site) continue;
                if (pair.value != table.table)
                    problem = "R_MIPS_HI16/LO16 evidence " + hex(pair.value) +
                              " disagrees with table address " + hex(table.table);
                else
                    evidence += " + R_MIPS_HI16/LO16";
            }
            const auto& jump_segment = image.code_segment_at(jump_site);
            std::vector<std::pair<std::uint32_t, std::uint32_t>> targets;
            for (std::uint32_t index = 0; problem.empty() && index < table.entries; ++index) {
                const auto position = static_cast<std::uint64_t>(table.table) + 4u * index;
                if (position > std::numeric_limits<std::uint32_t>::max()) {
                    problem = "table address overflow";
                    break;
                }
                const auto source = static_cast<std::uint32_t>(position);
                try {
                    // Linked PS2 images can place guarded tables in a segment
                    // that also permits execution. The SLTIU/BEQ/LW proof,
                    // not PF_X alone, identifies these words as table data.
                    // Keep explicit/relocation-only hints on their stricter
                    // data accessor and never reinterpret discovered code.
                    if(graph.instructions.contains(source))
                        throw std::runtime_error("bounded table overlaps discovered instructions");
                    std::uint32_t target;
                    try {target=image.data_word_at(source);}
                    catch(const std::runtime_error&) {target=image.instruction_at(source);}
                    if (&image.code_segment_at(target) != &jump_segment)
                        problem = "entry " + std::to_string(index) + " leaves the JR's code segment";
                    else
                        targets.emplace_back(source, target);
                } catch (const std::exception& error) {
                    problem = "entry " + std::to_string(index) + " at " + hex(source) + ": " + error.what();
                }
            }
            if (!problem.empty()) {
                graph.unresolved_jumps.push_back({jump_site, "bounded table at " + hex(table.table) +
                                                             " rejected: " + problem});
                continue;
            }
            graph.jump_tables.push_back({jump_site, table.bound_site, table.table, table.entries, evidence});
            for (const auto& [source, target] : targets) {
                seed(target, false);
                graph.indirect_entries.insert(target);
                graph.resolved_indirect_targets.push_back({source, target, "SLTIU-bounded jump table"});
            }
            grew = true;
        }
        if (!grew) break;
    }

    // Code discovered after a proof must not have opened a path around its bound.
    const auto entries = entry_points(graph);
    for (const auto& table : graph.jump_tables) {
        if (const auto entry = window_entry(entries, table.bound_site, table.jump_site))
            throw std::runtime_error("Jump table at JR " + hex(table.jump_site) +
                                     ": bound proof invalidated by entry " + hex(*entry) +
                                     " discovered later");
        for(std::uint32_t index=0;index<table.entries;++index)
            if(graph.instructions.contains(table.table_address+4u*index))
                throw std::runtime_error("Jump table at JR "+hex(table.jump_site)+
                    ": table data overlaps instructions discovered later");
    }
    for (const auto& jump : graph.constant_jumps) {
        if (const auto entry = window_entry(entries, jump.lui_site, jump.jump_site))
            throw std::runtime_error("Constant JR " + hex(jump.jump_site) + ": proof invalidated by entry " +
                                     hex(*entry) + " discovered later");
    }
    return graph;
}
} // namespace ps2native
