#include "ps2native/boot.hpp"
#include "ps2native/interpreter.hpp"
#include "ps2native/sha256.hpp"

#include <deque>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ps2native {
namespace {
std::string hex32(std::uint32_t value) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return stream.str();
}
std::string hex64(std::uint64_t value, unsigned digits) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setw(static_cast<int>(digits)) << std::setfill('0') << value;
    return stream.str();
}
const char* engine_name(ExecutionEngine engine) {
    return engine == ExecutionEngine::Aot ? "aot" : "int";
}

struct RetiredInstruction {
    std::uint64_t sequence;
    std::uint32_t pc;
    std::uint32_t word;
    ExecutionEngine engine;
};
std::string instruction_line(const RetiredInstruction& r) {
    std::ostringstream line;
    const auto decoded = try_decode_r5900(r.pc, r.word);
    line << "I " << std::setw(10) << std::setfill('0') << r.sequence << ' ' << engine_name(r.engine) << ' '
         << hex32(r.pc) << ' ' << hex32(r.word) << ' ' << (decoded ? mips_opcode_name(decoded->opcode) : "?");
    return line.str();
}

// Collects the reproducible boot trace; attached to the bus only while running.
class Tracer final : public InstructionObserver, public BusObserver, public SystemEventObserver {
public:
    Tracer(const BootOptions& options, BootReport& report, const EEState& state, const Memory& memory)
        : options_(options), report_(report), state_(state), memory_(memory) {}

    void on_instruction(std::uint32_t pc, std::uint32_t word, ExecutionEngine engine) override {
        if (options_.on_frame) present();
        const RetiredInstruction r{++sequence_, pc, word, engine};
        if (engine == ExecutionEngine::Aot) ++report_.aot_instructions;
        else ++report_.interpreted_instructions;
        if (!engine_ || *engine_ != engine) {
            if (engine_) ++report_.engine_switches;
            engine_ = engine;
            report_.trace.push_back(std::string("E ") + (engine == ExecutionEngine::Aot
                ? "aot (host-compiled block) at " : "interpreter (no AOT block) at ") + hex32(pc));
        }
        if (options_.trace == TraceLevel::Full) report_.trace.push_back(instruction_line(r));
        if (options_.tail_instructions != 0) {
            if (tail_.size() == options_.tail_instructions) tail_.pop_front();
            tail_.push_back(r);
        }
    }
    void on_bus_access(const BusAccess& access) override {
        if (options_.trace != TraceLevel::Full) return;
        std::ostringstream line;
        line << "  M " << (access.write ? 'W' : 'R') << static_cast<unsigned>(access.bytes) * 8u
             << " v=" << hex32(access.virtual_address) << " p=" << hex32(access.physical_address) << ' '
             << bus_region_name(access.region) << " = " << hex64(access.value, access.bytes * 2u);
        if (access.unknown_bits) line << " unknown-bits=" << hex64(access.unknown_bits, access.bytes * 2u);
        report_.trace.push_back(line.str());
    }
    void on_system_event(std::string_view event) override {
        report_.trace.push_back("  S " + std::string(event));
    }
    void finish() {
        // on_instruction observes an attempt. A fault after that callback has
        // no retirement; exclude that final attempt from coverage counters.
        if(sequence_>report_.retired && engine_) {
            if(sequence_-report_.retired!=1u)
                throw std::logic_error("instruction observation/retirement accounting mismatch");
            if(*engine_==ExecutionEngine::Aot)--report_.aot_instructions;
            else --report_.interpreted_instructions;
        }
        for (const auto& r : tail_) report_.tail.push_back(instruction_line(r));
    }

private:
    void present() {
        const auto& gs = memory_.gs();
        const auto cycle = state_.retired_instructions;
        if (generation_ != gs.timing_generation()) {
            generation_ = gs.timing_generation();
            const auto clock = gs.vertical_clock();
            next_frame_.reset();
            if (clock.state == GsRegisters::VerticalClock::State::Running)
                next_frame_ = clock.next_edge(cycle, false);
        }
        if (!next_frame_ || cycle < *next_frame_) return;
        // The observer runs before every instruction in both execution engines.
        const auto stamp = *next_frame_;
        next_frame_ = gs.vertical_clock().next_edge(cycle + 1u, false);
        std::optional<Framebuffer> frame;
        try { frame = gs.scanout(stamp); }
        catch (const DeviceRefusal& error) { report_.frame_unavailable = error.what(); return; }
        options_.on_frame(*frame);
        ++report_.frames_presented;
        report_.frame_unavailable.reset();
    }
    const BootOptions& options_;
    BootReport& report_;
    const EEState& state_;
    const Memory& memory_;
    std::uint64_t generation_{~0ull};
    std::optional<std::uint64_t> next_frame_;
    std::uint64_t sequence_{};
    std::optional<ExecutionEngine> engine_;
    std::deque<RetiredInstruction> tail_;
};

class ObserverScope {
public:
    ObserverScope(Memory& memory, BusObserver* observer) : memory_(memory) { memory_.set_observer(observer); }
    ~ObserverScope() { memory_.set_observer(nullptr); }
    ObserverScope(const ObserverScope&) = delete;
    ObserverScope& operator=(const ObserverScope&) = delete;

private:
    Memory& memory_;
};
} // namespace

const char* boot_outcome_name(BootOutcome outcome) noexcept {
    switch (outcome) {
        case BootOutcome::BlockPoint: return "block point";
        case BootOutcome::BudgetExhausted: return "instruction budget reached";
        case BootOutcome::StopAddress: return "stop address reached";
    }
    return "?";
}

void prepare_ee_boot(const BiosImage& bios, const AotModule* aot, EEState& state, Memory& memory,
                     const ConsoleHardware* hardware, const NvmImage* nvm,
                     const DiagnosticAssumptions* assumptions, const Rom1Image* rom1) {
    if (aot != nullptr && (std::string(aot->source_kind) != "bios-rom0" || bios.sha256 != aot->source_sha256))
        throw std::runtime_error(std::string("AOT module was generated from ") + aot->source_kind + " sha256 " +
                                 aot->source_sha256 + ", not from this ROM0 (sha256 " + bios.sha256 +
                                 "); firmware revisions are never mixed");
    if (hardware != nullptr && hardware->romver != bios.version.romver)
        throw std::runtime_error("console " + std::string(hardware->model) + ":" + std::string(hardware->mainboard) +
                                 " ships with ROMVER " + std::string(hardware->romver) + ", not " +
                                 bios.version.romver + "; firmware revisions are never mixed");
    if (nvm != nullptr && hardware == nullptr)
        throw std::runtime_error("an NVM image needs the console profile it was validated for");
    if(rom1) {
        if(!hardware)throw std::runtime_error("ROM1 requires its validated console profile");
        const auto checked=load_bios_rom1(rom1->bytes,bios,*hardware,rom1->identity);
        memory.reset_for_boot(bios.rom0,hardware,nvm,assumptions,checked.bytes);
    } else memory.reset_for_boot(bios.rom0, hardware, nvm, assumptions);
    state = ee_reset_state();
    memory.set_clock(&state.retired_instructions);
    memory.set_translation_context(0, (state.cp0(Cop0::Status) & kStatusERL) != 0u);
}

void prepare_direct_elf(const ElfImage& elf, const AotModule* aot, EEState& state, Memory& memory,
                        const DirectElfEnvironment& environment) {
    // Revalidate public ElfImage fields before making any runtime changes.
    const auto checked = parse_ee_elf(elf.bytes);
    if (aot && (std::string(aot->source_kind) != "elf" || sha256_hex(checked.bytes) != aot->source_sha256))
        throw std::runtime_error("Direct ELF AOT provenance mismatch (kind or SHA-256)");
    const auto sp = environment.stack_pointer;
    if ((sp & 15u) || sp < 16u || sp >= Memory::kMainRamBytes)
        throw std::runtime_error("Direct ELF stack must be 16-byte aligned in low EE RAM");
    for (const auto& segment : checked.segments) {
        const auto begin = segment.vaddr & 0x1FFFFFFFu;
        if (sp >= begin && sp - begin < segment.memory_size)
            throw std::runtime_error("Direct ELF stack overlaps a PT_LOAD segment");
    }
    memory.load_direct_elf(checked);
    state = ee_reset_state();
    state.pc = checked.entry;
    state.set_cp0(Cop0::Status, 0u);
    state.set_reg32(29u, sp);
    state.set_reg32(28u, environment.global_pointer);
    memory.set_clock(&state.retired_instructions);
    memory.set_translation_context(0, false);
}

BootReport run_ee_boot(EEState& state, Memory& memory, const AotModule* aot, const BootOptions& options) {
    BootReport report;
    Tracer tracer(options, report, state, memory);
    std::uint64_t executed = 0;
    // An unaligned stop address can never match an instruction PC.
    ExecutionContext ctx{state, memory, executed, options.max_instructions, 0xFFFFFFFFu, &tracer};
    const auto start_retired = state.retired_instructions;
    {
        ObserverScope scope(memory, &tracer);
        try {
            while (true) {
                if (options.stop_at && !state.delay_pending && state.pc == *options.stop_at) {
                    report.outcome = BootOutcome::StopAddress;
                    break;
                }
                if (executed >= options.max_instructions) {
                    report.outcome = BootOutcome::BudgetExhausted;
                    break;
                }
                state.instruction_pc=state.pc;
                state.instruction_in_delay_slot=state.delay_pending;
                (void)poll_ee_interrupts(state,memory);
                memory.set_cpu_context(state.cp0(Cop0::Status), state.cp0(Cop0::EntryHi));
                // Hybrid mode: every address may continue in the interpreter, so
                // the AOT-only verified-entry restriction does not apply here.
                state.indirect_entry_pending = false;
                const auto* block = aot != nullptr ? find_aot_block(*aot, state.pc) : nullptr;
                for (const auto& overlay : options.aot_overlays) {
                    const auto* candidate = find_aot_block(overlay.module_for(memory),state.pc);
                    if (candidate && block) throw std::runtime_error("overlapping AOT modules at " + hex32(state.pc));
                    if (candidate) block = candidate;
                }
                if (block != nullptr) {
                    const auto before = executed;
                    // A stop address inside a block ends it early, like the budget.
                    ctx.stop_pc = options.stop_at.value_or(0xFFFFFFFFu);
                    block->execute(ctx);
                    if (executed == before && executed < options.max_instructions &&
                        !(options.stop_at && state.pc == *options.stop_at))
                        throw std::runtime_error("AOT block at " + hex32(state.pc) + " made no progress");
                } else {
                    interpret_instruction(ctx, &tracer);
                }
            }
            report.pc = state.pc;
        } catch (const BusError& error) {
            report.outcome = BootOutcome::BlockPoint;
            report.category = std::string("bus: ") + bus_fault_name(error.fault);
            report.reason = error.what();
            report.pc = state.instruction_pc;
        } catch (const GuestStop& error) {
            report.outcome = BootOutcome::BlockPoint;
            report.category = stop_kind_name(error.kind);
            report.reason = error.what();
            report.pc = state.instruction_pc;
        } catch (const std::exception& error) {
            report.outcome = BootOutcome::BlockPoint;
            report.category = "unsupported";
            report.reason = error.what();
            // Raised before the instruction started (decode, delay-slot or code checks).
            report.pc = state.pc;
        }
    }
    if (report.outcome == BootOutcome::BlockPoint) {
        report.in_delay_slot = report.pc == state.instruction_pc ? state.instruction_in_delay_slot
                                                                 : state.delay_pending;
        try {
            report.word = memory.fetch32(report.pc);
        } catch (const std::exception&) {
            report.word.reset(); // The PC itself is not fetchable (reported in reason).
        }
    }
    report.retired = state.retired_instructions - start_retired;
    report.sio_output = memory.sio_output();
    if (memory.mapping() != Memory::Mapping::ElfProgram) {
        memory.sync_iop();
        const auto& iop = memory.iop();
        report.iop_instructions = iop.cycle();
        report.iop_pc = iop.pc();
        report.iop_stop = iop.stop_reason();
        for (const auto& event : iop.events())
            report.iop_events.push_back("IOP " + std::to_string(event.cycle) + " " + event.text);
        report.video_encoder_transactions = memory.video_encoder().transactions();
        report.gif_packets = memory.gif().packets();
    }
    tracer.finish();
    std::ostringstream stop;
    stop << "STOP " << boot_outcome_name(report.outcome);
    if (!report.category.empty()) stop << " [" << report.category << ']';
    stop << " pc=" << hex32(report.pc);
    if (report.word) stop << " word=" << hex32(*report.word);
    if (report.in_delay_slot) stop << " (delay slot)";
    stop << " retired=" << report.retired;
    if (!report.reason.empty()) stop << ": " << report.reason;
    report.trace.push_back(stop.str());
    return report;
}
} // namespace ps2native
