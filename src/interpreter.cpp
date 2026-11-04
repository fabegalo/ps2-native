#include "ps2native/interpreter.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ps2native {
namespace {
std::string hex32(std::uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return stream.str();
}

const char* cop0_name(unsigned reg) {
    static constexpr const char* kNames[32] = {
        "Index", "Random", "EntryLo0", "EntryLo1", "Context", "PageMask", "Wired", "reserved7",
        "BadVAddr", "Count", "EntryHi", "Compare", "Status", "Cause", "EPC", "PRid",
        "Config", "reserved17", "reserved18", "reserved19", "reserved20", "reserved21", "reserved22", "BadPAddr",
        "Debug", "Perf", "reserved26", "reserved27", "TagLo", "TagHi", "ErrorEPC", "reserved31"};
    return kNames[reg & 31u];
}

class Interpreter {
public:
    Interpreter(ExecutionContext& ctx, SystemEventObserver* events)
        : s_(ctx.state), m_(ctx.memory), events_(events) {}

    void event(const std::string& text) {
        if (events_ != nullptr) events_->on_system_event(text);
    }
    [[noreturn]] void stop(StopKind kind, const std::string& what) {
        throw GuestStop(kind, what + " at " + hex32(s_.instruction_pc));
    }

    void apply_status() {
        const auto status = s_.cp0(Cop0::Status);
        const bool exception_level = (status & (kStatusEXL | kStatusERL)) != 0u;
        m_.set_cpu_context(status, s_.cp0(Cop0::EntryHi));
        if (!exception_level && (status & kStatusKSU) == kStatusKSU)
            stop(StopKind::Undefined, "Status.KSU selects reserved privilege mode");
        if (!exception_level && (status & kStatusIE) && (status & kStatusEIE) &&
            (status & kStatusIM7) && !s_.compare_written)
            stop(StopKind::NotModeled, "COP0 timer interrupt enabled with undefined Compare");
    }

    void mfc0(unsigned rt, unsigned rd) {
        switch (static_cast<Cop0>(rd)) {
            case Cop0::Random:
                stop(StopKind::NotModeled, "MFC0 Random: its per-cycle decrement is not modeled");
            case Cop0::Count:
                s_.set_reg32_sign_extended(rt, s_.count());
                return;
            case Cop0::Debug: case Cop0::Perf:
                stop(StopKind::NotModeled, std::string("MFC0 ") + cop0_name(rd) + " (breakpoint/performance counters)");
            case Cop0::Index: case Cop0::EntryLo0: case Cop0::EntryLo1: case Cop0::Context:
            case Cop0::PageMask: case Cop0::Wired: case Cop0::BadVAddr: case Cop0::EntryHi:
            case Cop0::Compare: case Cop0::Status: case Cop0::Cause: case Cop0::Epc: case Cop0::PRid:
            case Cop0::Config: case Cop0::BadPAddr: case Cop0::TagLo: case Cop0::TagHi: case Cop0::ErrorEpc:
                s_.set_reg32_sign_extended(rt, s_.cop0[rd]);
                return;
        }
        stop(StopKind::Undefined, std::string("MFC0 from reserved COP0 register ") + cop0_name(rd));
    }

    void mtc0(unsigned rt, unsigned rd) {
        const auto value = s_.reg32(rt);
        const auto old = s_.cop0[rd];
        std::uint32_t stored = value;
        switch (static_cast<Cop0>(rd)) {
            case Cop0::Index: stored = (old & 0x80000000u) | (value & 0x3Fu); break;
            case Cop0::EntryLo0: stored = value & 0x83FFFFFFu; break;
            case Cop0::EntryLo1: stored = value & 0x03FFFFFFu; break;
            case Cop0::Context: stored = (value & 0xFF800000u) | (old & 0x007FFFFFu); break;
            case Cop0::PageMask: stored = value & 0x01FFE000u; break;
            case Cop0::Wired:
                stored = value & 0x3Fu;
                s_.set_cp0(Cop0::Random, 47u); // Writing Wired resets Random to its upper bound.
                break;
            case Cop0::EntryHi: stored = value & 0xFFFFE0FFu; break;
            case Cop0::Count:
                s_.count_anchor = s_.retired_instructions;
                break;
            case Cop0::Compare:
                s_.compare_written = true;
                s_.set_cp0(Cop0::Cause, s_.cp0(Cop0::Cause) & ~kStatusIM7);
                break;
            case Cop0::Epc: case Cop0::ErrorEpc:
            case Cop0::BadPAddr: case Cop0::TagLo: case Cop0::TagHi:
                break;
            case Cop0::Status: stored = value & kStatusWritable; break;
            case Cop0::Config: stored = (value & ~0x00000FC0u) | (old & 0x00000FC0u); break; // IC/DC sizes are fixed.
            case Cop0::Cause:
                if (value != 0u)
                    stop(StopKind::NotModeled, "MTC0 Cause " + hex32(value) + " (software-set cause bits)");
                stored = old;
                break;
            case Cop0::Random: case Cop0::BadVAddr: case Cop0::PRid:
                stored = old; // Read-only: the write has no effect.
                break;
            case Cop0::Debug: case Cop0::Perf:
                stop(StopKind::NotModeled, std::string("MTC0 ") + cop0_name(rd) + " (breakpoint/performance counters)");
            default:
                stop(StopKind::Undefined, std::string("MTC0 to reserved COP0 register ") + cop0_name(rd));
        }
        s_.cop0[rd] = stored;
        event(std::string("MTC0 ") + cop0_name(rd) + " <- " + hex32(value) +
              (stored != value ? " (stored " + hex32(stored) + ")" : std::string()));
        if (rd == static_cast<unsigned>(Cop0::Status) || rd == static_cast<unsigned>(Cop0::EntryHi)) apply_status();
    }

    void tlb_write_indexed() {
        const auto index = s_.cp0(Cop0::Index) & 0x3Fu;
        if (index >= Memory::kTlbEntries)
            stop(StopKind::Undefined, "TLBWI with Index " + std::to_string(index) + " (only 48 entries)");
        const TlbEntry entry{s_.cp0(Cop0::PageMask), s_.cp0(Cop0::EntryHi), s_.cp0(Cop0::EntryLo0),
                             s_.cp0(Cop0::EntryLo1)};
        m_.tlb_write(index, entry);
        event("TLBWI " + std::to_string(index) + ": PageMask " + hex32(entry.page_mask) + " EntryHi " +
              hex32(entry.entry_hi) + " EntryLo0 " + hex32(entry.entry_lo0) + " EntryLo1 " + hex32(entry.entry_lo1));
    }

    void tlb_read() {
        const auto index = s_.cp0(Cop0::Index) & 0x3Fu;
        if (index >= Memory::kTlbEntries)
            stop(StopKind::Undefined, "TLBR with Index " + std::to_string(index) + " (only 48 entries)");
        const auto& entry = m_.tlb_entry(index);
        // G reads back as set only when both halves are global.
        const auto global = entry.entry_lo0 & entry.entry_lo1 & 1u;
        s_.set_cp0(Cop0::PageMask, entry.page_mask);
        s_.set_cp0(Cop0::EntryHi, entry.entry_hi & ~entry.page_mask);
        s_.set_cp0(Cop0::EntryLo0, (entry.entry_lo0 & ~1u) | global);
        s_.set_cp0(Cop0::EntryLo1, (entry.entry_lo1 & ~1u) | global);
        event("TLBR " + std::to_string(index));
        apply_status();
    }

    void tlb_probe() {
        const auto match = m_.tlb_probe(s_.cp0(Cop0::EntryHi));
        s_.set_cp0(Cop0::Index, match ? *match : 0x80000000u);
        event("TLBP EntryHi " + hex32(s_.cp0(Cop0::EntryHi)) + " -> " +
              (match ? "entry " + std::to_string(*match) : std::string("no match")));
    }

    void eret() {
        auto status = s_.cp0(Cop0::Status);
        if (status & kStatusERL) {
            s_.pc = s_.cp0(Cop0::ErrorEpc);
            status &= ~kStatusERL;
        } else {
            s_.pc = s_.cp0(Cop0::Epc);
            status &= ~kStatusEXL;
        }
        s_.set_cp0(Cop0::Status, status);
        event("ERET -> " + hex32(s_.pc) + ", Status " + hex32(status));
        apply_status();
    }

    void interrupt_enable(bool enable) {
        auto status = s_.cp0(Cop0::Status);
        // Effective in kernel mode, at exception/error level, or with Status.EDI.
        if ((status & (kStatusEDI | kStatusEXL | kStatusERL)) || (status & kStatusKSU) == 0u)
            status = enable ? status | kStatusEIE : status & ~kStatusEIE;
        s_.set_cp0(Cop0::Status, status);
        event(std::string(enable ? "EI" : "DI") + ", Status " + hex32(status));
        apply_status();
    }

    // No cache is modeled: guest memory is always coherent with what the cache
    // would hold, so invalidation and write-back have no observable effect.
    // Operations that read or write cache contents/tags are refused.
    void cache(std::uint32_t word) {
        const auto op = ee::rt(word);
        const auto address = ee::effective_address(s_, word);
        const char* name = nullptr;
        switch (op) {
            case 0x07: name = "IXIN"; break;
            case 0x0C: name = "BFH"; break;
            case 0x14: name = "DXWBIN"; break;
            case 0x16: name = "DXIN"; break;
            case 0x18: name = "DHWBIN"; break;
            case 0x1A: name = "DHIN"; break;
            case 0x1C: name = "DHWOIN"; break;
            case 0x04: case 0x12:
                // Index Store Tag with a zero TagLo writes an invalid line: an invalidation.
                if (s_.cp0(Cop0::TagLo) != 0u)
                    stop(StopKind::NotModeled, "CACHE Index Store Tag with TagLo " + hex32(s_.cp0(Cop0::TagLo)) +
                         " (cache tags are not modeled)");
                name = op == 0x04 ? "IXSTG (TagLo 0)" : "DXSTG (TagLo 0)";
                break;
            default:
                stop(StopKind::NotModeled, "CACHE operation " + hex32(op) + " (cache contents are not modeled)");
        }
        // Only hit operations use the address's translation/tag. Index
        // operations select a cache set/way directly, including unmapped VA 0.
        if (op == 0x18 || op == 0x1A || op == 0x1C) (void)m_.translate(address, false);
        event(std::string("CACHE ") + name + " " + hex32(address) + " (no cache model: memory coherent)");
    }

    // LWC1/SWC1 move a raw 32-bit word between memory and an FPU register.
    void cop1_transfer(bool store, std::uint32_t word) {
        if ((s_.cp0(Cop0::Status) & kStatusCU1) == 0u)
            stop(StopKind::GuestException, std::string(store ? "SWC1" : "LWC1") +
                 " with Status.CU1 = 0: coprocessor unusable exception; vectoring is not modeled");
        const auto address = ee::effective_address(s_, word);
        auto& fpr = s_.fpr[ee::rt(word)];
        if (store) m_.write32(address, fpr);
        else fpr = m_.read32(address);
    }

    EEState& s_;
    Memory& m_;
    SystemEventObserver* events_;
};
} // namespace

const char* stop_kind_name(StopKind kind) noexcept {
    switch (kind) {
        case StopKind::GuestException: return "guest exception";
        case StopKind::NotModeled: return "not modeled";
        case StopKind::Undefined: return "architecturally undefined";
    }
    return "?";
}

void interpret_instruction(ExecutionContext& ctx, SystemEventObserver* events) {
    auto& state = ctx.state;
    auto& memory = ctx.memory;
    state.instruction_pc=state.pc;
    state.instruction_in_delay_slot=state.delay_pending;
    (void)poll_ee_interrupts(state,memory);
    memory.set_cpu_context(state.cp0(Cop0::Status), state.cp0(Cop0::EntryHi));
    const auto pc = state.pc;
    state.instruction_pc = pc;
    state.instruction_in_delay_slot = state.delay_pending;
    const auto word = memory.fetch32(pc);
    const auto instruction = decode_r5900(pc, word);
    if (instruction.has_delay_slot() && state.delay_pending)
        throw std::runtime_error("Control transfer in an active delay slot is unsupported");
    Interpreter cpu(ctx, events);
    const auto status = state.cp0(Cop0::Status);
    if (((word >> 26u) == 0x10u || instruction.opcode == MipsOpcode::Cache) &&
        instruction.opcode != MipsOpcode::Ei && instruction.opcode != MipsOpcode::Di &&
        !(status & (kStatusEXL | kStatusERL | (1u << 28u))) && (status & kStatusKSU))
        cpu.stop(StopKind::GuestException, "COP0 unusable in supervisor/user mode with Status.CU0 = 0; vectoring is not modeled");
    switch (instruction.opcode) {
        case MipsOpcode::Syscall: case MipsOpcode::Break: {
            if (ctx.observer != nullptr) ctx.observer->on_instruction(pc, word, ExecutionEngine::Interpreter);
            enter_ee_exception(state, instruction.opcode == MipsOpcode::Syscall ? 8u : 9u, pc, state.delay_pending);
            memory.set_cpu_context(state.cp0(Cop0::Status), state.cp0(Cop0::EntryHi));
            cpu.event(std::string(instruction.opcode == MipsOpcode::Syscall ? "SYSCALL" : "BREAK") +
                      " exception -> " + hex32(state.pc) + " EPC=" + hex32(state.cp0(Cop0::Epc)) +
                      " Cause=" + hex32(state.cp0(Cop0::Cause)));
            // Exception entry replaces normal retirement: retire() would apply
            // the cancelled branch target when the trap was in its delay slot.
            ++ctx.executed;
            ++state.retired_instructions;
            return;
        }
        case MipsOpcode::Eret:
            if (state.delay_pending) cpu.stop(StopKind::Undefined, "ERET in a branch delay slot");
            break;
        default:
            break;
    }
    if (ctx.observer != nullptr) ctx.observer->on_instruction(pc, word, ExecutionEngine::Interpreter);
    const auto frame = ee::begin(state, pc);
    switch (instruction.opcode) {
#define PS2N_INTERPRET(name) case MipsOpcode::name: ee::exec<MipsOpcode::name>(state, memory, pc, word); break;
        PS2N_R5900_AOT_OPCODES(PS2N_INTERPRET)
#undef PS2N_INTERPRET
        case MipsOpcode::Mfc0: cpu.mfc0(instruction.rt, instruction.rd); break;
        case MipsOpcode::Mtc0: cpu.mtc0(instruction.rt, instruction.rd); break;
        case MipsOpcode::Tlbwi: cpu.tlb_write_indexed(); break;
        case MipsOpcode::Tlbwr: cpu.stop(StopKind::NotModeled, "TLBWR (Random register timing is not modeled)");
        case MipsOpcode::Tlbr: cpu.tlb_read(); break;
        case MipsOpcode::Tlbp: cpu.tlb_probe(); break;
        case MipsOpcode::Eret: cpu.eret(); break;
        case MipsOpcode::Ei: cpu.interrupt_enable(true); break;
        case MipsOpcode::Di: cpu.interrupt_enable(false); break;
        case MipsOpcode::Cache: cpu.cache(word); break;
        case MipsOpcode::Pref: break; // A hint: no architectural effect.
        case MipsOpcode::Lwc1: cpu.cop1_transfer(false, word); break;
        case MipsOpcode::Swc1: cpu.cop1_transfer(true, word); break;
        case MipsOpcode::Syscall: case MipsOpcode::Break: break; // Stopped above.
    }
    (void)ee::retire(ctx, frame);
}
} // namespace ps2native
