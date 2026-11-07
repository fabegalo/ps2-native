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
#include "ps2native/microwire.hpp"
#include "ps2native/disc.hpp"
#include "ps2native/gif.hpp"
#include "ps2native/vif.hpp"
    // RUNTIME_HELPERS_BEGIN
#include "ps2native/vu_control.hpp"
    // RUNTIME_HELPERS_END
#include "ps2native/diagnostic_history.hpp"
#include <memory>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
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
    friend struct CheckpointAccess;
public:
    // External machine event, not a ROM/NVM value (ps2sdk wakeup reasons).
    enum class PowerEvent : std::uint8_t { FrontPanelPowerOn=0, MainReset=1, Alarm=2, ExternalDevice=3 };
    struct Event {
        std::uint64_t cycle; // IOP cycle
        std::string text;
    };
    Iop();
    // User/front-panel power-on with `rom0` (empty: no ROM, the IOP stays stopped) on console
    // `hardware` (nullptr: none, so its PRId read stops the IOP), whose
    // MECHACON holds `nvm` (nullptr: none, so NVM reads stop the IOP). `nvm`
    // must have been validated for `hardware` and outlive the IOP.
    void reset(std::span<const std::uint8_t> rom0, const ConsoleHardware* hardware, const NvmImage* nvm = nullptr,
               bool diagnostic_sbus_latches = false, std::optional<std::uint32_t> diagnostic_dmacinten = std::nullopt,
               std::span<const std::uint8_t> rom1 = {}, bool diagnostic_sio2_irq_clear = false,
               bool diagnostic_spu2_quiescent = false, PowerEvent power_event = PowerEvent::FrontPanelPowerOn);
    // Runs until `cycle` IOP cycles since reset, or until the IOP stops.
    // `video` (nullptr: none) is the GS whose vertical clock raises VBLANK
    // start/end (I_STAT bits 0 and 11); its timing must not change while the
    // IOP runs (the EE syncs the IOP before every GS write).
    void run_until(std::uint64_t cycle, const GsRegisters* video = nullptr);
    // RUNTIME_HELPERS_BEGIN
    [[nodiscard]] bool synchronization_due(std::uint64_t target,const GsRegisters* video=nullptr) const noexcept {
        return (!stop_ && cycle_<target) ||
            (video ? video_generation_!=video->timing_generation() : video_generation_.has_value());
    }
    // RUNTIME_HELPERS_END
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
            if(ram_known_[offset+byte]!=0xFFu)return std::nullopt;
            value|=std::uint32_t{ram_[offset+byte]}<<(8u*byte);
        }
        return value;
    }
    void ee_write(std::uint32_t offset, std::size_t bytes, std::uint64_t value);
    // Records only the request; neither acknowledgement nor DVD permissions
    // are inferred from an unmeasured MECHACON reply.
    [[nodiscard]] bool dvd_playback_forbid_requested() const noexcept {return dvd_playback_forbid_requested_;}
    [[nodiscard]] std::optional<bool> remote_bypass_enabled() const noexcept {return sio2_remote_bypass_;}
    // Explicit behavioral-reference experiment only: SCMD 15 reply bit 7=0.
    // The other seven bits and resulting DVD permission state stay unknown.
    void diagnostic_forbid_dvdp_error_bit_clear() {
        if(cycle_)throw DeviceRefusal("ForbidDVDP error-bit experiment requires reset state before execution");
        diagnostic_forbid_dvdp_error_clear_=true;
    }
    // Explicit initial alarm-control experiment: seconds=FF (disabled),
    // minute bit 7=0. Status, time and user word remain opaque. Power cause
    // comes independently from the external event supplied to reset().
    void diagnostic_wakeup_alarm_disabled() {
        if(cycle_)throw DeviceRefusal("Wakeup alarm experiment requires reset state before execution");
        diagnostic_wakeup_alarm_disabled_=true;
    }
    // Explicit healthy, running RTC input at reset, expressed as UTC Unix
    // seconds. SCMD 08 returns JST/BCD and advances only with IOP cycles.
    // This immutable host input is pinned by complete-checkpoint bindings;
    // an absent input remains unknown and is never seeded from ROM/NVM.
    void configure_rtc(std::uint64_t utc_epoch_seconds);
    // Last recognized BootCertify input, never an authentication-success flag.
    [[nodiscard]] const std::optional<std::array<std::uint8_t,4>>& boot_certify_request() const noexcept {
        return boot_certify_request_;
    }
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
                       std::unordered_map<std::uint32_t,std::uint32_t>* unknown = nullptr,
                       std::span<std::uint8_t> scratchpad = {}) {
        ee_ram_ = ram; ee_ram_unknown_ = unknown; ee_scratchpad_ = scratchpad;
    }
    [[nodiscard]] std::uint32_t ee_dma_read(std::uint32_t address) const;
    void ee_dma_write(std::uint32_t address, std::uint32_t value);
    [[nodiscard]] static bool ee_dma_address(std::uint32_t address);
    [[nodiscard]] ObservedWord ee_dma_observe(std::uint32_t address) const;
    void ee_dma_write_observed(std::uint32_t address, ObservedWord word);
    [[nodiscard]] bool reads_ee_ram() const noexcept { return (ee_sif1_chcr_ & 0x100u) != 0u ||
        ((ee_dma_setup_[0][0].value_or(0u)|ee_dma_setup_[1][0].value_or(0u)|ee_dma_setup_[2][0].value_or(0u))&0x100u)!=0u; }
    void advance_gif_dma(std::uint64_t ee_cycle, Gif& gif, GsRegisters& gs);
    // RUNTIME_HELPERS_BEGIN
    [[nodiscard]] bool graphics_sync_needed(std::uint64_t cycle,const Vif& vif0,const Vif& vif1,const VuControl& vu) const noexcept {
        // An armed channel retains priority/arbitration checks even before its
        // next bus beat; stored refusals always take their original priority.
        const bool enabled=!ee_dma_suspended_ && (ee_dctrl_&1u);
        if(enabled && (ee_dma_setup_[8][0].value_or(0u)&0x100u))return true;
        for(unsigned channel=0;channel<3u;++channel)
            if(graphics_dma_[channel].stop || (enabled && (ee_dma_setup_[channel][0].value_or(0u)&0x100u)))return true;
        return vu.micro_event_due(cycle) || vif0.drain_pending() || vif1.drain_pending();
    }
    // Valid only after graphics_sync_needed() returned false at this boundary.
    // These anchors are modeled checkpoint state, even while the bus is idle.
    void stamp_idle_graphics(std::uint64_t cycle) noexcept {
        graphics_dma_[0].cycle=graphics_dma_[1].cycle=graphics_dma_[2].cycle=cycle;
        spr_dma_cycle_=cycle;
    }
    // RUNTIME_HELPERS_END
    void advance_graphics(std::uint64_t ee_cycle,Vif& vif0,Vif& vif1,Gif& gif,GsRegisters& gs,VuControl& vu);
    void advance_vif_dma(std::uint64_t ee_cycle,Vif& vif0,Vif& vif1,Gif& gif,GsRegisters& gs,VuControl* vu=nullptr);
    [[nodiscard]] bool writes_ee_ram() const noexcept { return ((ee_dma_setup_[5][0].value_or(0u)|ee_dma_setup_[8][0].value_or(0u))&0x100u)!=0; }
    [[nodiscard]] std::uint64_t ee_ram_write_generation() const noexcept {return ee_ram_write_generation_;}
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
    // Unmeasured DEV9 expansion-bay reset/power bits 0/2 only.
    // All other POWER bits remain unknown; reset removes this hypothesis.
    void diagnostic_dev9_power_reset(std::uint16_t value);
    // Explicit cold mask=0 and clear IOP pending input, never a measured reset default.
    void diagnostic_dev9_irq_reset_clear();
    // Explicit disconnected IRQ wire fault; device status/data remain unknown.
    // Does not clear an earlier pending INTC event. Reset removes the fault.
    void diagnostic_dev9_irq_disconnected();
    // Logical EEPROM words, explicitly bound to this console's NVM hash.
    // Never substitute the MECHACON MAC record or another console's data.
    void attach_speed_eeprom_words(std::span<const std::uint16_t> words,std::string_view nvm_sha256);
    void diagnostic_speed_eeprom_presence(bool present);
    [[nodiscard]] std::uint64_t speed_eeprom_words_read() const {return speed_eeprom_.words_read();}
    // Unmeasured 1460/1464/1466 side effects: explicit passive-latch
    // experiment only. No active peripheral or IRQ response is supplied.
    void diagnostic_dev9_control_latches();
    void diagnostic_dev9_power_settle_cycles(std::uint32_t cycles);
    void diagnostic_sio2_infrared_timeout(std::uint32_t cycles);
    void diagnostic_sio2_reset_clears_completion() {
        if(cycle_)throw DeviceRefusal("SIO2 reset-latch experiment requires reset state before execution");
        diagnostic_sio2_reset_completion_=true;
    }
    // Unmeasured RECV2 bits 4/5 used by PADMAN to choose port timing.
    // Defines only these two bits; no connection status or serial reply.
    void diagnostic_sio2_port_timing(unsigned bits);
    // Standard digital pad with explicit active-low buttons and unmeasured
    // packet duration. No analog/configuration commands are inferred.
    void diagnostic_sio2_digital_pad(unsigned port,std::uint16_t buttons,std::uint32_t packet_cycles);
    // Host input for an explicitly attached digital pad. Active-low bits;
    // a packet already started retains its sampled reply.
    void set_sio2_digital_pad_buttons(unsigned port,std::uint16_t buttons);
    // HOST_INPUT_BEGIN (not serialized)
    // Sampled only by a real 01/42 digital-pad poll. The returned active-low
    // value is latched into that packet; this never creates a guest poll.
    using PadPollSource=std::function<std::uint16_t(unsigned,std::uint64_t)>;
    void set_pad_poll_source(PadPollSource source) { pad_poll_source_=std::move(source); }
    // HOST_INPUT_END
    void diagnostic_sio2_unplugged_ports(unsigned mask,std::uint32_t timeout_cycles);
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
    // Separate unmeasured end-of-RAM policy: normal DMA wraps to word zero.
    // Writes into dynamic capture/input buffers retain unknown visibility.
    void diagnostic_spu2_dma_wrap_to_zero();
    // Separate unmeasured readback hypothesis, never an assumed cold value.
    void diagnostic_spu2_admas_control_latch() {
        if(cycle_)throw DeviceRefusal("ADMAS readback experiment must be selected before guest execution");
        diagnostic_spu2_admas_control_latch_=true;
    }
    [[nodiscard]] std::uint64_t spu2_adma_words(unsigned core) const { return spu2_adma_.at(core).words; }
    [[nodiscard]] std::uint64_t spu2_dma_words(unsigned core) const { return spu2_adma_.at(core).normal_words; }
    [[nodiscard]] std::uint64_t spu2_input_frames(unsigned core) const { return spu2_adma_.at(core).frames; }
    struct Spu2Sample { std::uint64_t cycle; std::int16_t left,right; };
    // Decoder/envelope state is hardware state; the optional host sink is not.
    struct Spu2VoiceState {
        bool modeled{},manual_loop{};
        std::uint8_t phase{}; // 0 stopped, 1 attack, 2 decay, 3 sustain, 4 release
        std::int32_t envelope{},previous{},older{},output{};
        std::uint32_t envelope_counter{},pitch_counter{},next{};
        std::optional<std::uint32_t> loop;
        std::array<std::int16_t,32> decoded{};
        std::uint64_t read{},write{},key_ons{},decoded_samples{},nonzero_samples{};
    };
    [[nodiscard]] const Spu2VoiceState& spu2_voice(unsigned core,unsigned voice) const { return spu2_voices_.at(core).at(voice); }
    using Spu2SampleSink=std::function<void(unsigned,const Spu2Sample&)>;
    void set_spu2_sample_sink(Spu2SampleSink sink) { spu2_sample_sink_=std::move(sink); }
    [[nodiscard]] const std::optional<Spu2Sample>& spu2_last_sample(unsigned core) const { return spu2_last_sample_.at(core); }
    [[nodiscard]] std::uint64_t spu2_sample_count(unsigned core) const { return spu2_sample_count_.at(core); }
    [[nodiscard]] const std::optional<Spu2Sample>& spu2_last_input(unsigned core) const { return spu2_adma_.at(core).last_input; }
    [[nodiscard]] std::uint64_t cycle() const noexcept { return cycle_; }
    [[nodiscard]] std::uint32_t pc() const noexcept { return pc_; }
    [[nodiscard]] bool stopped() const noexcept { return stop_.has_value(); }
    [[nodiscard]] const std::optional<std::string>& stop_reason() const noexcept { return stop_; }
    [[nodiscard]] const std::vector<Event>& events() const { return events_.items(); }
    void diagnostic_events(std::size_t limit, DeviceEventSink sink) {
        events_.configure(limit,sink ? std::function<void(const Event&)>{[sink](const Event& e){sink("IOP",e.cycle,e.text);}} : nullptr);
    }
    [[nodiscard]] ObservedWord diagnostic_gpr(unsigned index) const noexcept {
        index &= 31u; return {gpr_[index], (gpr_defined_ & (1u<<index)) ? 0u : ~gpr_known_[index]};
    }
    [[nodiscard]] const auto& diagnostic_cop0() const noexcept { return cop0_; }
    [[nodiscard]] const auto& diagnostic_ee_dma() const noexcept { return ee_dma_setup_; }
    [[nodiscard]] const auto& diagnostic_ee_dma_global() const noexcept { return ee_dma_global_; }
    [[nodiscard]] std::uint32_t diagnostic_istat() const noexcept { return istat_; }
    [[nodiscard]] std::size_t diagnostic_sif0_fifo_words() const noexcept { return sif0_fifo_.size(); }
    [[nodiscard]] std::size_t diagnostic_sif1_fifo_words() const noexcept { return sif1_fifo_.size(); }
    [[nodiscard]] std::optional<std::uint32_t> diagnostic_latch(std::uint32_t address) const {
        if(address==0x1F801070u)return istat_;
        const auto* value=latch(address);return value ? *value : std::nullopt;
    }
    [[nodiscard]] std::optional<std::uint32_t> diagnostic_cop0(unsigned n) const {return cop0_.at(n);}
    [[nodiscard]] std::pair<std::uint32_t,bool> diagnostic_sio2_irq() const {return {sio2_interrupt_enable_,sio2_completion_flag_};}
    [[nodiscard]] ObservedWord diagnostic_ram_observed(std::uint32_t at) const {
        if(at>ram_.size()-4u)return {0,~0u};
        ObservedWord word{0,0};for(unsigned b=0;b<4;++b) {
            word.value|=std::uint32_t{ram_[at+b]}<<(b*8u);
            word.unknown|=(std::uint32_t{static_cast<std::uint8_t>(~ram_known_[at+b])})<<(b*8u);
        }return word;
    }
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
    std::uint64_t ee_ram_write_generation_{};
    std::span<std::uint8_t> ee_scratchpad_;
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
        bool hblank{}; // Timer 1/3 external source, never the CPU prescaler
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
    void schedule_iop_hblank() noexcept;
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
    void or_value(unsigned rd,unsigned rs,std::uint32_t value,std::uint32_t known,const std::string& origin,std::uint64_t epoch=0);
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
    void write_sio2_remote_bypass(ObservedWord word,std::uint64_t epoch);
    void require_sio2_remote_bypass() const;
    void write_sio2_dma(unsigned channel,unsigned field,std::size_t bytes,std::uint32_t value);
    [[nodiscard]] std::uint32_t read_sio2_dma(unsigned channel,unsigned field,std::size_t bytes) const;
    void pump_sio2_dma();
    void update_sio2_irq();
    void reset_sio2_completion();
    bool diagnostic_sio2_reset_completion_{};
    bool sio2_irq_output_{};
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
    void tick_spu2();
    [[nodiscard]] std::uint16_t spu2_known_register(std::uint32_t address) const;
    [[nodiscard]] bool spu2_voice_configured(unsigned core,unsigned voice) const;
    void start_spu2_voice(unsigned core,unsigned voice);
    [[nodiscard]] Spu2Sample synthesize_spu2(unsigned core,std::uint64_t cycle);
    void spu2_ram_irq(std::uint32_t address);
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
    std::vector<std::uint8_t> ram_known_; // bit mask of established bits in each RAM byte
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
    std::uint64_t sio2_remote_epoch_{}; // high token bit distinguishes the remote register from CTRL
    std::optional<bool> sio2_remote_bypass_; // bit 0 only; no cold value or upper bits inferred
    std::optional<unsigned> diagnostic_sio2_port_timing_;
    std::array<std::optional<std::uint16_t>,2> sio2_digital_pads_;
    // HOST_INPUT_BEGIN (not serialized)
    PadPollSource pad_poll_source_;
    // HOST_INPUT_END
    std::uint32_t sio2_response_status_{};
    std::vector<std::uint8_t> sio2_input_;
    std::deque<ObservedWord> sio2_output_,sio2_pending_reply_;
    unsigned diagnostic_sio2_unplugged_ports_{};
    bool diagnostic_sio2_infrared_absent_{};
    std::optional<std::uint64_t> sio2_complete_at_;
    std::uint32_t diagnostic_sio2_timeout_cycles_{};
    std::uint32_t sio2_reply_bytes_{},sio2_interrupt_enable_{};
    std::optional<std::uint32_t> sio2_diagnostic_control_;
    bool sio2_completion_flag_{},sio2_response_valid_{};
    std::array<std::optional<std::uint32_t>, 16> cop0_{};
    const ConsoleHardware* hardware_{};
    std::optional<std::uint64_t> rtc_epoch_; // immutable external input, like ROM/NVM/disc bindings
    // Bus configuration as written; empty value: unknown (reset value never
    // established, or an unknown value written back).
    std::vector<std::pair<std::uint32_t, std::optional<std::uint32_t>>> latches_;
    // MECHACON S command interface: parameters written so far, the last
    // command and its pending result bytes.
    std::vector<std::uint8_t> s_params_;
    std::optional<std::uint8_t> s_command_;
    std::optional<std::array<std::uint8_t,4>> boot_certify_request_;
    bool dvd_playback_forbid_requested_{};
    bool diagnostic_forbid_dvdp_error_clear_{};
    bool diagnostic_wakeup_alarm_disabled_{};
    PowerEvent power_event_{PowerEvent::FrontPanelPowerOn};
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
    std::uint32_t hblank_timer_mask_{};
    std::optional<GsRegisters::LineClock> hblank_clock_;
    bool hblank_held_{};
    std::string hblank_reason_{"no GS attached"};
    std::uint64_t next_hblank_ee_{},next_hblank_iop_{};
    void write_dev9_power(ObservedWord word);
    [[nodiscard]] bool dev9_power_ready() const;
    bool diagnostic_dev9_controls_{},dev9_initial_power_ready_{};
    std::array<std::optional<std::uint16_t>,3> dev9_controls_{};
    std::uint32_t diagnostic_dev9_settle_cycles_{};
    std::optional<std::uint64_t> dev9_power_ready_at_;
    ObservedWord dev9_power_{0,0xFFFFu};
    ObservedWord speed_irq_mask_{0,0xFFFFu};
    bool dev9_irq_acknowledged_{};
    bool diagnostic_dev9_irq_disconnected_{};
    [[nodiscard]] bool dev9_irq_masked() const noexcept;
    Microwire64x16 speed_eeprom_;
    ObservedWord speed_gpio_dir_{0,0xFFu},speed_gpio_data_{0,0xFFu};
    std::optional<std::uint64_t> speed_gpio_clock_at_;
    void write_speed_gpio(std::uint32_t address,std::size_t bytes,std::uint32_t value);
    [[nodiscard]] ObservedWord read_speed_gpio(std::uint32_t address,std::size_t bytes) const;
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
    // Abstract fallback for insufficient decoder state with both gains
    // proved zero. Numerical voices below retain real samples and ADSR.
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
    // Reset-aligned clocks use the explicit cold-state experiment; subsequent
    // guest KON and transfers advance the same device, RAM and sample edges.
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
    bool diagnostic_spu2_dma_wrap_zero_{};
    bool diagnostic_spu2_admas_control_latch_{};
    std::uint64_t spu2_adma_bus_available_{};
    unsigned spu2_adma_last_grant_{1u};
    std::array<std::array<Spu2VoiceState,24>,2> spu2_voices_{};
    std::array<bool,2> spu2_irq_pending_{};
    Spu2SampleSink spu2_sample_sink_; // excluded from checkpoints; detached on restore
    void advance_graphics_dma(unsigned channel, std::uint64_t ee_cycle,Vif* vif0,Vif* vif1,Gif& gif,GsRegisters& gs,VuControl* vu=nullptr);
    // RUNTIME_HELPERS_BEGIN
    void advance_graphics_due(std::uint64_t ee_cycle,Vif& vif0,Vif& vif1,Gif& gif,GsRegisters& gs,VuControl& vu);
    // RUNTIME_HELPERS_END
    struct GraphicsDma {
        std::uint64_t cycle{};
        bool end{}, inline_data{};
        std::optional<std::string> stop;
    };
    std::array<GraphicsDma,3> graphics_dma_{};
    std::uint64_t spr_dma_cycle_{};
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
    DiagnosticHistory<Event> events_;
};
    // RUNTIME_HELPERS_BEGIN
inline void Iop::advance_graphics(std::uint64_t ee_cycle,Vif& vif0,Vif& vif1,Gif& gif,GsRegisters& gs,VuControl& vu) {
    if(!graphics_sync_needed(ee_cycle,vif0,vif1,vu)) {stamp_idle_graphics(ee_cycle);return;}
    advance_graphics_due(ee_cycle,vif0,vif1,gif,gs,vu);
}
    // RUNTIME_HELPERS_END
} // namespace ps2native
