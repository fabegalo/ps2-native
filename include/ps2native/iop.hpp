#pragma once
// The IOP (I/O processor): an R3000A-compatible core that starts at the same
// reset vector as the EE (ps2tek: "Upon reset, both the EE and IOP begin
// executing at BFC00000h") and takes the PRId < 0x59 branch of the shared ROM0
// reset code. Modeled for the boot path only: the MIPS I integer instruction
// set with branch and load delay slots, the COP0 registers the reset code
// writes, cache isolation (Status.IsC), 2 MiB of RAM, ROM0, and the bus
// configuration registers of the reset code as latches. Everything else stops
// the IOP with a named reason; the EE is refused when it depends on IOP state
// that the IOP did not produce.
//
// Time base: the IOP clock is 36.864 MHz, exactly one eighth of the EE clock
// (fact); one IOP cycle per instruction is a hypothesis, like the EE's. The
// IOP never touches EE state here, so it runs lazily: whenever the EE reads
// state the IOP owns, the IOP first runs up to the EE's current time. Both EE
// engines (interpreter and AOT) see the same IOP state at the same EE cycle.
#include "ps2native/console.hpp"
#include "ps2native/device_timing.hpp"
#include "ps2native/gs_registers.hpp"
#include "ps2native/nvm.hpp"
#include "ps2native/disc.hpp"
#include "ps2native/gif.hpp"
#include <memory>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace ps2native {
inline constexpr std::uint64_t kEeCyclesPerIopCycle = 8u; // 294.912 MHz / 36.864 MHz
inline constexpr std::uint32_t kIopRamBytes = 2u * 1024u * 1024u; // PS2Ident: 2097152 on every model
// EE physical window onto IOP RAM.
inline constexpr std::uint32_t kIopRamEePhysical = 0x1C000000u;
// 0x1F803204 (same register for the EE and the IOP): tool (DTL-T) board id, read as a byte by the EE RDRAM module and
// the IOP EECONF module. Retail consoles have no tool board; 0xFF (bus pulled
// high) is a hypothesis backed by 0xBF803800 reading 0xFFFF on every retail
// console in the PS2Ident database. The firmware only checks for tool board
// classes (high nibble 1-3, or 0x60/0x61), which 0xFF is not.
inline constexpr std::uint32_t kToolBoardIdPhysical = 0x1F803204u;
inline constexpr std::uint8_t kRetailToolBoardId = 0xFFu;
// DEV9_R_REV (ps2sdk dev9regs.h), the same register for the EE and the IOP:
// its value comes from the console profile (ConsoleHardware::dev9_revision).
inline constexpr std::uint32_t kDev9Revision = 0x1F80146Eu;

class Iop {
public:
    struct Event {
        std::uint64_t cycle; // IOP cycle
        std::string text;
    };
    Iop();
    // Power-on with `rom0` (empty: no ROM, the IOP stays stopped) on console
    // `hardware` (nullptr: none, so its PRId read stops the IOP), whose
    // MECHACON holds `nvm` (nullptr: none, so NVM reads stop the IOP). `nvm`
    // must have been validated for `hardware` and outlive the IOP.
    void reset(std::span<const std::uint8_t> rom0, const ConsoleHardware* hardware, const NvmImage* nvm = nullptr,
               bool diagnostic_sbus_latches = false, std::optional<std::uint32_t> diagnostic_dmacinten = std::nullopt,
               std::span<const std::uint8_t> rom1 = {}, bool diagnostic_sio2_irq_clear = false,
               bool diagnostic_spu2_quiescent = false);
    // Runs until `cycle` IOP cycles since reset, or until the IOP stops.
    // `video` (nullptr: none) is the GS whose vertical clock raises VBLANK
    // start/end (I_STAT bits 0 and 11); its timing must not change while the
    // IOP runs (the EE syncs the IOP before every GS write).
    void run_until(std::uint64_t cycle, const GsRegisters* video = nullptr);
    // EE read of IOP RAM at `offset` (the IOP must have been run up to now).
    // Throws DeviceRefusal when the bytes were never written by the IOP, or
    // when the IOP stopped before this time (its later state is unknown).
    [[nodiscard]] std::uint64_t ee_read(std::uint32_t offset, std::size_t bytes) const;
    // Diagnostic snapshot at the stopped/current IOP time, never an EE bus
    // response. Unwritten bytes remain unknown; this does not resume the IOP.
    [[nodiscard]] std::optional<std::uint32_t> diagnostic_ram32(std::uint32_t offset) const {
        if(offset>ram_.size()-4u)return std::nullopt;
        std::uint32_t value=0;
        for(unsigned byte=0;byte<4;++byte) {
            if(!ram_written_[offset+byte])return std::nullopt;
            value|=std::uint32_t{ram_[offset+byte]}<<(8u*byte);
        }
        return value;
    }
    void ee_write(std::uint32_t offset, std::size_t bytes, std::uint64_t value);
    // The EE sees the same physical CDVD registers through the SBUS.
    // Call after synchronizing the IOP; no second command/result queue.
    [[nodiscard]] std::uint32_t ee_cdvd_read(std::uint32_t physical,std::size_t bytes);
    void ee_cdvd_write(std::uint32_t physical,std::size_t bytes,std::uint32_t value);
    enum class DiscMedium { Ps2Cd, Ps2Dvd };
    // Explicit host insertion before execution, already detected and paused.
    // No filesystem service or IRX loader is installed in the guest.
    void mount_disc(std::shared_ptr<DiscImage> disc, DiscMedium medium, bool diagnostic_cav_nominal_timing = false);
    // SIF register `index` (0 MSCOM, 1 SMCOM, 2 MSFLG, 3 SMFLG, 4 CTRL, 6 BD6;
    // EE 0x1000F200 + index * 0x10) as the EE reads or writes it; the IOP must
    // have been run up to now. Throws DeviceRefusal for what is not modeled.
    [[nodiscard]] std::uint32_t ee_sif_read(unsigned index) const;
    void ee_sif_write(unsigned index, std::uint32_t value);
    // Bare-metal development image, not IRX relocation/import linking.
    void load_program(std::span<const std::uint8_t> bytes, std::uint32_t address, std::uint32_t entry);
    void attach_ee_ram(std::span<std::uint8_t> ram,
                       std::unordered_map<std::uint32_t,std::uint32_t>* unknown = nullptr) {
        ee_ram_ = ram; ee_ram_unknown_ = unknown;
    }
    [[nodiscard]] std::uint32_t ee_dma_read(std::uint32_t address) const;
    void ee_dma_write(std::uint32_t address, std::uint32_t value);
    [[nodiscard]] static bool ee_dma_address(std::uint32_t address);
    [[nodiscard]] ObservedWord ee_dma_observe(std::uint32_t address) const;
    void ee_dma_write_observed(std::uint32_t address, ObservedWord word);
    [[nodiscard]] bool reads_ee_ram() const noexcept { return (ee_sif1_chcr_ & 0x100u) != 0u || (ee_dma_setup_[2][0].value_or(0u)&0x100u)!=0u; }
    void advance_gif_dma(std::uint64_t ee_cycle, Gif& gif, GsRegisters& gs);
    [[nodiscard]] bool writes_ee_ram() const noexcept { return (ee_dma_setup_[5][0].value_or(0u)&0x100u)!=0; }
    struct SifDmaMetrics {
        std::uint64_t bytes{}, source_full_cycles{}, destination_empty_cycles{};
        std::size_t peak_fifo_words{};
    };
    [[nodiscard]] const SifDmaMetrics& sif1_metrics() const noexcept { return sif1_metrics_; }
    [[nodiscard]] const SifDmaMetrics& sif0_metrics() const noexcept { return sif0_metrics_; }
    [[nodiscard]] bool ee_dma_interrupt_pending() const noexcept {
        return (ee_dstat_ & (ee_dstat_>>16u) & 0x63FFu)!=0u;
    }
    void set_ee_dma_suspended(bool suspended) noexcept { ee_dma_suspended_=suspended; }
    [[nodiscard]] bool ee_dma_suspended() const noexcept { return ee_dma_suspended_; }

    // Explicit experiment only: no external IR dongle, command timeout in
    // IOP cycles, self-clearing CTRL command bits, retained configuration and
    // completion flag bit 0. Timing and
    // these control/status details are not physical measurements. Reset
    // disables it. Other peripherals and SIO2 DMA remain unsupported.
    void diagnostic_sio2_infrared_timeout(std::uint32_t cycles);
    // Unmeasured RECV2 bits 4/5 used by PADMAN to choose port timing.
    // Defines only these two bits; no connection status or serial reply.
    void diagnostic_sio2_port_timing(unsigned bits);
    void diagnostic_sio2_dma_word_cycles(std::uint32_t cycles);
    [[nodiscard]] std::uint64_t sio2_dma_words(unsigned channel) const { return sio2_dma_.at(channel).words; }
    [[nodiscard]] std::optional<std::uint8_t> sio2_input_byte(std::size_t n) const {
        return n<sio2_input_.size() ? std::optional<std::uint8_t>{sio2_input_[n]} : std::nullopt;
    }
    // Unknown SPDIF 0x7CA write side effects: explicit diagnostic hypothesis
    // of a configuration latch only. Reads remain unsupported; reset disables it.
    void diagnostic_spu2_7ca_latch() { diagnostic_spu2_7ca_latch_=true; }
    // Separate hypothesis for the unassigned 0x346..0x3FF aperture and its
    // 0x800 mirror. Does not turn reads or functional registers into stubs.
    void diagnostic_spu2_reserved_latches() { diagnostic_spu2_reserved_latches_=true; }
    // Explicit initial EEA readbacks, including zero reserved bits; not a
    // measured reset value. Guest writes supersede it and reset removes it.
    void diagnostic_spu2_effect_end_reset(std::array<std::uint16_t,2> values);
    // Unmeasured initial internal ESA; only an explicit experiment may supply it.
    // Guest address writes supersede each half. This does not invent readback.
    void diagnostic_spu2_effect_start_reset(std::array<std::uint32_t,2> values);
    // Explicit hypothesis for the twelve unused ESA-high/EEA readback bits.
    // Does not supply an initial address, IRQ status or sound data.
    void diagnostic_spu2_effect_address_reserved_zero();
    // Behavioral-reference hypothesis, not a measured hardware rule: an
    // inverted effect workspace performs no DSP memory accesses.
    void diagnostic_spu2_inverted_effect_area_inactive();
    // Functional FIFO-to-RAM transfer with an explicit, unmeasured rate.
    // Select before guest execution. Reset removes this timing experiment.
    void diagnostic_spu2_pio_word_cycles(std::uint32_t cycles);
    [[nodiscard]] std::optional<std::uint16_t> spu2_ram_halfword(std::uint32_t word_address) const;
    [[nodiscard]] std::uint64_t spu2_pio_words(unsigned core) const { return spu2_transfer_.at(core).words; }
    // Functional ADMA double buffering; explicit throughput/priming model.
    void diagnostic_spu2_adma_word_cycles(std::uint32_t cycles);
    // Separate unmeasured readback hypothesis, never an assumed cold value.
    void diagnostic_spu2_admas_control_latch() {
        if(cycle_)throw DeviceRefusal("ADMAS readback experiment must be selected before guest execution");
        diagnostic_spu2_admas_control_latch_=true;
    }
    [[nodiscard]] std::uint64_t spu2_adma_words(unsigned core) const { return spu2_adma_.at(core).words; }
    [[nodiscard]] std::uint64_t spu2_dma_words(unsigned core) const { return spu2_adma_.at(core).normal_words; }
    [[nodiscard]] std::uint64_t spu2_input_frames(unsigned core) const { return spu2_adma_.at(core).frames; }
    struct Spu2Sample { std::uint64_t cycle; std::int16_t left,right; };
    [[nodiscard]] const std::optional<Spu2Sample>& spu2_last_sample(unsigned core) const { return spu2_last_sample_.at(core); }
    [[nodiscard]] std::uint64_t spu2_sample_count(unsigned core) const { return spu2_sample_count_.at(core); }
    [[nodiscard]] const std::optional<Spu2Sample>& spu2_last_input(unsigned core) const { return spu2_adma_.at(core).last_input; }
    [[nodiscard]] std::uint64_t cycle() const noexcept { return cycle_; }
    [[nodiscard]] std::uint32_t pc() const noexcept { return pc_; }
    [[nodiscard]] bool stopped() const noexcept { return stop_.has_value(); }
    [[nodiscard]] const std::optional<std::string>& stop_reason() const noexcept { return stop_; }
    [[nodiscard]] const std::vector<Event>& events() const noexcept { return events_; }
    [[nodiscard]] std::uint32_t gpr(unsigned index) const noexcept { return gpr_[index & 31u]; }

private:
    // SIF1: EE channel 6 normal source -> bounded FIFO -> IOP channel 10
    // destination chain. No RPC/HLE or direct RAM mirroring.
    void pump_sif1();
    void pump_sif0();
    void finish_iop_sif0();
    void update_dma_irq();
    void pump_cdvd();
    std::uint32_t iop_sif0_read(std::uint32_t address) const;
    void iop_sif0_write(std::uint32_t address, std::uint32_t value);
    std::uint32_t iop_sif1_read(std::uint32_t address) const;
    void iop_sif1_write(std::uint32_t address, std::uint32_t value);
    std::span<std::uint8_t> ee_ram_;
    std::unordered_map<std::uint32_t,std::uint32_t>* ee_ram_unknown_{};
    std::uint32_t ee_dctrl_{}, ee_dstat_{}, ee_sif1_chcr_{}, ee_sif1_madr_{}, ee_sif1_qwc_{};
    std::array<std::array<std::optional<std::uint32_t>, 9>, 10> ee_dma_setup_{};
    std::array<std::optional<std::uint32_t>, 4> ee_dma_global_{};
    std::array<std::uint32_t, 4> ee_dma_global_unknown_{};
    std::array<std::uint32_t, 4> iop_sif1_{};
    std::deque<std::uint32_t> sif1_fifo_;
    bool sif1_have_tag_{}, sif1_end_{}, sif1_tag_irq_{};
    bool ee_sif1_chain_end_{};
    std::uint32_t sif1_words_{};
    SifDmaMetrics sif1_metrics_;
    std::array<std::uint32_t,4> iop_sif0_{};
    std::uint8_t iop_sif0_written_{};
    std::deque<ObservedWord> sif0_fifo_;
    std::uint32_t sif0_source_words_{};
    bool sif0_source_tag_{},sif0_source_end_{},sif0_source_irq_{},sif0_dest_end_{};
    // Last complete SIF bus qword. Partial packets retain the lanes the IOP
    // did not supply; unknown tag padding stays unknown until overwritten.
    std::array<std::optional<ObservedWord>,4> sif0_last_qword_{};
    SifDmaMetrics sif0_metrics_;
    struct PendingLoad {
        unsigned reg;
        std::uint32_t value;
        bool defined{true};
        std::string origin; // why the value is unknown (when !defined)
        std::uint32_t known; // bits known even when !defined
        std::uint64_t sio2_epoch{}; // unmodified opaque CTRL sample, not a numeric guess
    };
    struct Access {
        std::uint32_t physical;
        bool cached;
        bool kseg2;
    };
    // Hardware timer (ps2tek "IOP Timers"): 0-2 are 16-bit, 3-5 32-bit.
    struct Timer {
        bool configured{};                  // mode written since power-on
        std::optional<std::uint32_t> count; // empty: never written
        std::uint32_t mode{};
        std::optional<std::uint32_t> target;
        std::uint32_t prescale{1}, phase{};
    };
    void step();
    void commit(const PendingLoad& load);
    // R3000A exception entry: EPC, Cause.ExcCode/BD, SR mode stack, vector.
    void enter_exception(std::uint32_t code, std::uint32_t pc, bool delay_slot);
    // IOP interrupt line to COP0 (Cause.IP2): I_CTRL bit 0 and I_STAT & I_MASK.
    // Empty: it depends on a source that is not modeled.
    [[nodiscard]] std::optional<bool> interrupt_line() const;
    [[nodiscard]] const std::optional<std::uint32_t>* latch(std::uint32_t physical) const;
    void tick_timers();
    void refresh_video(const GsRegisters& video);
    void tick_video();
    // I_STAT bits whose state is modeled: the timers, and VBLANK once the GS
    // vertical clock is known.
    [[nodiscard]] std::uint32_t modeled_irqs() const noexcept;
    [[nodiscard]] std::uint32_t timer_read(unsigned index, std::uint32_t reg, const std::string& name);
    void timer_write(unsigned index, std::uint32_t reg, std::optional<std::uint32_t> value, const std::string& name);
    [[nodiscard]] std::uint32_t reg(unsigned index) const;
    // Register value of which only the bits in `mask` are used: known when
    // those bits are, even if others are not.
    [[nodiscard]] std::uint32_t masked_reg(unsigned index, std::uint32_t mask) const;
    // AND of two registers, either possibly partly unknown: a bit is known
    // when known in both, or known 0 in one of them.
    void and_regs(unsigned rd, unsigned rs, unsigned rt);
    void and_immediate(unsigned destination, unsigned source, std::uint32_t mask);
    void or_value(unsigned rd,unsigned rs,std::uint32_t value,std::uint32_t known,const std::string& origin);
    // rs == rt, decided when the known bits already differ even if some
    // bits are unknown; otherwise both must be known.
    [[nodiscard]] bool regs_equal(unsigned rs, unsigned rt) const;
    // Value to store: may be unknown (copied as unknown into RAM or a latch).
    [[nodiscard]] std::optional<std::uint32_t> stored_reg(unsigned index) const;
    void set_reg(unsigned index, std::uint32_t value);
    void copy_reg(unsigned destination, unsigned source);
    void shift_right(unsigned destination, unsigned source, unsigned amount);
    void shift_left(unsigned destination, unsigned source, unsigned amount);
    void add_sub_unsigned(unsigned destination,unsigned left,unsigned right,bool subtract);
    void load_reg(unsigned index, std::uint32_t value);
    [[nodiscard]] Access translate(std::uint32_t address, std::size_t bytes, bool write, const char* what) const;
    [[nodiscard]] std::uint32_t fetch(std::uint32_t address) const;
    [[nodiscard]] std::uint32_t load(std::uint32_t address, std::size_t bytes, const char* what);
    void store(std::uint32_t address, std::size_t bytes, std::optional<std::uint32_t> value, const char* what,
               const ObservedWord* observed=nullptr,std::uint64_t sio2_epoch=0);
    void reset_sio2_observed(ObservedWord word,std::uint64_t epoch);
    void write_sio2_dma(unsigned channel,unsigned field,std::size_t bytes,std::uint32_t value);
    [[nodiscard]] std::uint32_t read_sio2_dma(unsigned channel,unsigned field,std::size_t bytes) const;
    void pump_sio2_dma();
    struct Sio2Dma {
        std::array<std::uint32_t,3> registers{};
        std::uint32_t block_words{},words_left{};
        std::uint8_t written{};
        std::uint64_t words{};
    };
    std::array<Sio2Dma,2> sio2_dma_{};
    std::uint32_t diagnostic_sio2_dma_cycles_{};
    std::uint64_t sio2_dma_next_{};
    unsigned sio2_dma_turn_{};
    void write_spu2_control(std::uint32_t physical,ObservedWord word);
    [[nodiscard]] bool spu2_effects_active(unsigned core) const;
    [[nodiscard]] std::pair<std::uint32_t,std::uint32_t> spu2_effect_area(unsigned core) const;
    void tick_spu2_quiet();
    void validate_spu2_pio(unsigned core,const ObservedWord& control) const;
    void validate_spu2_adma(unsigned core) const;
    void validate_spu2_normal_dma(unsigned core,std::uint32_t words) const;
    [[nodiscard]] bool spu2_input_quiet(unsigned core,std::uint32_t mixer) const;
    [[nodiscard]] bool tick_spu2_adma(unsigned core);
    void consume_spu2_input(unsigned core,std::uint64_t sample_cycle);
    void write_spu2_dma(unsigned core,std::uint32_t field,std::size_t bytes,std::uint32_t value);
    [[nodiscard]] std::uint32_t read_spu2_dma(unsigned core,std::uint32_t field,std::size_t bytes) const;
    void start_sio2_infrared(std::uint32_t interrupt_enable);
    [[nodiscard]] std::uint32_t io_read(std::uint32_t physical, std::size_t bytes, const char* what);
    void io_write(std::uint32_t physical, std::size_t bytes, std::optional<std::uint32_t> value, const char* what);
    void mechacon_command(std::uint8_t command, const char* what);
    void cop0_write(unsigned index, std::uint32_t value);
    [[nodiscard]] std::uint32_t cop0_read(unsigned index);

    std::vector<std::uint8_t> rom_;
    std::vector<std::uint8_t> rom1_;
    std::vector<std::uint8_t> ram_;
    std::vector<std::uint8_t> ram_written_; // 1 per byte written since power-on
    std::array<std::uint32_t, 32> gpr_{};
    std::uint32_t gpr_defined_{1u};         // bit n: GPR n holds a known value ($zero always)
    std::array<std::string, 32> gpr_origin_; // why an unknown GPR is unknown
    std::array<std::uint32_t, 32> gpr_known_{}; // bits of an unknown GPR that are known
    std::optional<std::uint32_t> hi_, lo_;
    std::uint32_t pc_{};
    std::optional<std::uint32_t> branch_target_; // set by a jump/branch: taken after the delay slot
    bool in_delay_slot_{};
    std::optional<PendingLoad> pending_load_;    // load result, visible one instruction later
    std::optional<PendingLoad> write_;           // this instruction's ALU result
    std::optional<std::string> undefined_load_;  // this instruction loaded an unknown value: why
    std::uint32_t undefined_known_{};            // bits of that value that are known
    std::array<std::uint64_t,32> gpr_sio2_epoch_{};
    std::uint64_t sio2_control_epoch_{},undefined_sio2_epoch_{};
    std::optional<unsigned> diagnostic_sio2_port_timing_;
    std::vector<std::uint8_t> sio2_input_;
    std::deque<std::uint8_t> sio2_output_;
    std::optional<std::uint64_t> sio2_complete_at_;
    std::uint32_t diagnostic_sio2_timeout_cycles_{};
    std::uint32_t sio2_reply_bytes_{},sio2_interrupt_enable_{};
    std::optional<std::uint32_t> sio2_diagnostic_control_;
    bool sio2_completion_flag_{},sio2_response_valid_{};
    std::array<std::optional<std::uint32_t>, 16> cop0_{};
    const ConsoleHardware* hardware_{};
    // Bus configuration as written; empty value: unknown (reset value never
    // established, or an unknown value written back).
    std::vector<std::pair<std::uint32_t, std::optional<std::uint32_t>>> latches_;
    // MECHACON S command interface: parameters written so far, the last
    // command and its pending result bytes.
    std::vector<std::uint8_t> s_params_;
    std::optional<std::uint8_t> s_command_;
    struct MechaReplyByte {
        std::uint8_t value,known{0xFFu};
        const char* origin{};
        MechaReplyByte(std::uint8_t byte):value(byte){}
        MechaReplyByte(std::uint8_t byte,std::uint8_t mask,const char* why):value(byte),known(mask),origin(why){}
    };
    std::deque<MechaReplyByte> s_results_;
    // Configuration area opened by OpenConfig (read mode only).
    struct OpenConfig {
        unsigned area;
        unsigned blocks;
        unsigned next;
    };
    std::optional<OpenConfig> config_;
    std::array<Timer, 6> timers_{};
    bool timers_running_{};
    std::uint32_t istat_{}; // I_STAT bits of the modeled sources
    bool sio2_irq_acknowledged_{};
    bool dma_irq_output_{};
    std::uint8_t cdvd_irq_{};
    bool ee_dma_suspended_{};
    std::optional<std::uint8_t> cdvd_ncommand_;
    std::optional<std::uint64_t> cdvd_complete_at_;
    std::shared_ptr<DiscImage> disc_;
    std::uint8_t cdvd_type_{}, cdvd_status_{}, cdvd_sticky_{};
    bool diagnostic_cav_nominal_timing_{};
    bool diagnostic_spu2_quiescent_{};
    bool diagnostic_spu2_7ca_latch_{};
    bool diagnostic_spu2_reserved_latches_{};
    std::array<std::optional<ObservedWord>,2> spu2_control_;
    // Abstract voice state: KON makes decoder/envelope state unknown. A
    // constant zero gain on both channels still proves its mixer output.
    // KOFF does not prove that an unknown release envelope has finished.
    std::array<std::uint32_t,2> spu2_symbolic_voices_{};
    std::array<std::array<std::optional<std::uint16_t>,4>,2> spu2_keys_;
    std::array<ObservedWord,2> spu2_endx_;
    // Upper bounds, not invented envelope levels. Zero proves completion
    // of a supported linear release; other values cannot be read as ENVX.
    std::array<std::array<std::optional<std::uint16_t>,24>,2> spu2_release_upper_;
    std::array<std::uint32_t,2> spu2_release_pending_{};
    std::optional<std::array<std::uint16_t,2>> diagnostic_spu2_effect_end_;
    std::optional<std::array<std::uint32_t,2>> diagnostic_spu2_effect_start_;
    bool diagnostic_spu2_effect_address_reserved_zero_{};
    bool diagnostic_spu2_inverted_effect_area_inactive_{};
    std::array<bool,2> spu2_effect_area_dirty_{};
    // Quiet clocks are available only under the explicit cold-state
    // experiment: no active voices, inputs, effects or transfers.
    std::array<std::optional<std::uint64_t>,2> spu2_next_sample_;
    std::array<std::optional<Spu2Sample>,2> spu2_last_sample_;
    std::array<std::uint64_t,2> spu2_sample_count_{};
    bool spu2_clock_active_{};
    struct Spu2Transfer {
        std::optional<std::uint16_t> high,low;
        std::optional<std::uint32_t> address;
        std::deque<std::uint16_t> fifo;
        std::optional<std::uint64_t> next;
        std::uint64_t words{};
    };
    std::array<Spu2Transfer,2> spu2_transfer_;
    std::vector<std::uint16_t> spu2_ram_;
    std::vector<std::uint8_t> spu2_ram_written_;
    std::uint32_t diagnostic_spu2_pio_cycles_{};
    struct Spu2Adma {
        bool enabled{},primed{},control_written{};
        bool normal{};
        std::array<bool,2> ready{};
        unsigned fill_bank{},fill_word{},read_bank{},read_position{};
        std::array<std::uint32_t,3> dma{}; // MADR, BCR, CHCR
        std::uint8_t written{}; // address, low/high BCR, CHCR
        std::uint32_t remaining{},block_remaining{};
        std::optional<std::uint64_t> next;
        std::uint64_t words{},frames{};
        std::uint64_t normal_words{};
        std::optional<Spu2Sample> last_input;
    };
    std::array<Spu2Adma,2> spu2_adma_;
    std::uint32_t diagnostic_spu2_adma_cycles_{};
    bool diagnostic_spu2_admas_control_latch_{};
    std::uint64_t spu2_adma_bus_available_{};
    unsigned spu2_adma_last_grant_{1u};
    std::uint64_t gif_dma_cycle_{};
    std::optional<std::string> gif_dma_stop_;
    std::optional<std::uint8_t> cdvd_howto_;
    std::vector<std::uint8_t> cdvd_params_;
    std::uint32_t cdvd_lba_{}, cdvd_sectors_{}, cdvd_sector_cycles_{};
    std::uint64_t cdvd_sector_at_{};
    std::array<std::uint8_t,2048> cdvd_sector_{};
    unsigned cdvd_sector_offset_{2048};
    // SIF registers shared with the EE (ps2tek "SIF Registers"): MSCOM is
    // written by the EE, SMCOM by the IOP (empty: never written); MSFLG bits
    // are set by the EE and cleared by the IOP, SMFLG the other way round.
    std::optional<std::uint32_t> sif_mscom_, sif_smcom_;
    std::uint32_t sif_msflg_{}, sif_smflg_{};
    // Explicit experiment: retain the SDK's SBUS configuration writes;
    // readback and SIF2 electrical/clock behavior are not established.
    bool diagnostic_sbus_latches_{};
    std::uint32_t diagnostic_sif_ready_{};
    std::optional<std::uint32_t> sbus_control_write_, sbus_bd6_write_;
    // GS vertical clock as last seen (generation), and the next edges.
    std::optional<std::uint64_t> video_generation_;
    GsRegisters::VerticalClock video_{};
    std::uint64_t next_vblank_start_{}, next_vblank_end_{}; // EE cycles
    // i.LINK link controller: a bus reset was started through the PHY.
    bool ilink_bus_reset_{};
    const NvmImage* nvm_{};
    std::uint64_t cycle_{};
    std::optional<std::string> stop_;
    std::uint64_t stop_cycle_{};
    std::vector<Event> events_;
};
} // namespace ps2native
