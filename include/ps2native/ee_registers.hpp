#pragma once
// Device registers the EE boot path models: the EE registers at physical
// 0x10000000..0x1000FFFF, the GS privileged registers (GsRegisters, which also
// clock HBLANK for the EE timers), the MCH serial interface to the RDRAM chain
// (MchRegisters), IOP RAM as the EE sees it at 0x1C000000 (Iop, run up to the
// EE time on each access) and, in the IOP register window
// 0x1F800000..0x1F9FFFFF, only the tool board id byte. Each register documents what is known and what
// is a hypothesis; every other address, access size or mode still fails with a
// reason.
#include "ps2native/console.hpp"
#include "ps2native/device_timing.hpp"
#include "ps2native/gif.hpp"
#include "ps2native/gs_registers.hpp"
#include "ps2native/iop.hpp"
#include "ps2native/ipu.hpp"
#include "ps2native/mch.hpp"
#include "ps2native/video_encoder.hpp"
#include "ps2native/vif.hpp"
#include "ps2native/vu_control.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace ps2native {
// DEV9 interface registers 0x1F801470/0x1F801472 (ps2sdk dev9regs.h names
// them only by address). The RDRAM module pulses them (1472 = 1, 1470 = 0,
// 1472 = 0, ~555-iteration delay, 1472 = 1); their function is undocumented.
inline constexpr std::uint32_t kDev9Reg1470 = 0x1F801470u;
inline constexpr std::uint32_t kDev9Reg1472 = 0x1F801472u;
// EE SIO (debug UART), register names from ps2sdk sio.h: LCR 0x1000F100,
// LSR F110, IER F120, ISR F130, FCR F140, BGR F150, TXFIFO F180, RXFIFO F1C0.
inline constexpr std::uint32_t kSioLcr = 0x1000F100u;
inline constexpr std::uint32_t kSioLsr = 0x1000F110u;
inline constexpr std::uint32_t kSioIer = 0x1000F120u;
inline constexpr std::uint32_t kSioIsr = 0x1000F130u;
inline constexpr std::uint32_t kSioFcr = 0x1000F140u;
inline constexpr std::uint32_t kSioBgr = 0x1000F150u;
inline constexpr std::uint32_t kSioTxFifo = 0x1000F180u;
// DMAC D_ENABLER (EE User's Manual): read-only view of D_ENABLEW; its low half
// identifies the board (see console.hpp).
inline constexpr std::uint32_t kDmacEnabler = 0x1000F520u;
// EE interrupt controller (ps2tek): I_STAT 0x1000F000 (pending sources 0-14,
// write 1 to clear) and I_MASK 0x1000F010 (write 1 to reverse a bit).
// 0x1F801900..0x1F80191F and 0x1A000000..0x1A00001F: the two bus masters to
// the video encoder (video_encoder.hpp).
inline constexpr std::uint32_t kUndocumented190x = kVideoEncoderMaster190x;
inline constexpr std::uint32_t kUndocumented1A = kVideoEncoderMaster1A;
inline constexpr std::uint32_t kUndocumentedBlockBytes = kVideoEncoderMasterBytes;
inline constexpr std::uint32_t kIntcStat = 0x1000F000u;
inline constexpr std::uint32_t kIntcMask = 0x1000F010u;
inline constexpr std::uint32_t kIntcSources = 0x7FFFu;

class EeRegisters {
    friend struct CheckpointAccess;
public:
    // State after a hardware reset of the console `hardware` (nullptr: none
    // selected, so board-specific registers refuse reads), with the IOP at its
    // reset vector in `rom0` (empty: no IOP execution); the clock is attached
    // separately.
    void reset(const ConsoleHardware* hardware = nullptr, std::span<const std::uint8_t> rom0 = {},
               const NvmImage* nvm = nullptr, const DiagnosticAssumptions* assumptions = nullptr,
               std::span<const std::uint8_t> rom1 = {});
    // EE clock: base retired-instruction count plus optional modeled stalls.
    void set_clock(const std::uint64_t* ee_cycles,const std::uint64_t* stalls=nullptr) noexcept { clock_=ee_cycles;stall_clock_=stalls; }
    [[nodiscard]] std::uint64_t read(std::uint32_t physical, std::size_t bytes);
    void write(std::uint32_t physical, std::size_t bytes, std::uint64_t value);
    // 128-bit store (SQ): the GIF FIFO takes whole quadwords.
    void write128(std::uint32_t physical, std::uint64_t low, std::uint64_t high);
    [[nodiscard]] const Gif& gif() const noexcept { return gif_; }
    void diagnostic_events(std::size_t limit, DeviceEventSink sink) {
        iop_.diagnostic_events(limit,sink); gif_.diagnostic_events(limit,sink); video_encoder_.diagnostic_events(limit,std::move(sink));
    }
    [[nodiscard]] ObservedWord diagnostic_vif_status(bool unit1) const noexcept { return (unit1 ? vif1_ : vif0_).observe_status(); }
    [[nodiscard]] const GsRegisters& gs() const noexcept { return gs_; }
    // HOST_INPUT_BEGIN
    void set_gs_gpu_backend(std::shared_ptr<GsGpu> backend) { gs_.set_gpu_backend(std::move(backend)); }
    // HOST_INPUT_END
    void diagnostic_gs_scanmask_reset(unsigned mask) { gs_.diagnostic_scanmask_reset(mask); }
    void diagnostic_gs_texture_cache_reset_invalid() { gs_.diagnostic_texture_cache_reset_invalid(); }
    void diagnostic_gs_texture_precision(unsigned modes) { gs_.diagnostic_texture_precision(modes); }
    [[nodiscard]] const MchRegisters& mch() const noexcept { return mch_; }
    void diagnostic_dev9_power_reset(std::uint16_t value) { iop_.diagnostic_dev9_power_reset(value); }
    void diagnostic_dev9_irq_reset_clear() { iop_.diagnostic_dev9_irq_reset_clear(); }
    void diagnostic_dev9_irq_disconnected() { iop_.diagnostic_dev9_irq_disconnected(); }
    void diagnostic_spu2_dma_wrap_to_zero() { iop_.diagnostic_spu2_dma_wrap_to_zero(); }
    void attach_speed_eeprom_words(std::span<const std::uint16_t> words,std::string_view hash) { iop_.attach_speed_eeprom_words(words,hash); }
    void diagnostic_speed_eeprom_presence(bool present) { iop_.diagnostic_speed_eeprom_presence(present); }
    void diagnostic_dev9_control_latches() { iop_.diagnostic_dev9_control_latches(); }
    void diagnostic_dev9_power_settle_cycles(std::uint32_t cycles) { iop_.diagnostic_dev9_power_settle_cycles(cycles); }
    void diagnostic_smap_reset_cycles(std::uint32_t cycles) { iop_.diagnostic_smap_reset_cycles(cycles); }
    void diagnostic_sio2_infrared_timeout(std::uint32_t cycles) { iop_.diagnostic_sio2_infrared_timeout(cycles); }
    void diagnostic_sio2_port_timing(unsigned bits) { iop_.diagnostic_sio2_port_timing(bits); }
    void diagnostic_sio2_digital_pad(unsigned port,std::uint16_t buttons,std::uint32_t cycles) { iop_.diagnostic_sio2_digital_pad(port,buttons,cycles); }
    void set_sio2_digital_pad_buttons(unsigned port,std::uint16_t buttons) { iop_.set_sio2_digital_pad_buttons(port,buttons); }
    void diagnostic_sio2_dma_word_cycles(std::uint32_t cycles) { iop_.diagnostic_sio2_dma_word_cycles(cycles); }
    void diagnostic_sio2_reset_clears_completion() { iop_.diagnostic_sio2_reset_clears_completion(); }
    void diagnostic_sio2_unplugged_ports(unsigned mask,std::uint32_t cycles) { iop_.diagnostic_sio2_unplugged_ports(mask,cycles); }
    [[nodiscard]] const Iop& iop() const noexcept { return iop_; }
    void configure_rtc(std::uint64_t utc_epoch_seconds) { iop_.configure_rtc(utc_epoch_seconds); }
    void diagnostic_spu2_7ca_latch() { iop_.diagnostic_spu2_7ca_latch(); }
    void diagnostic_spu2_reserved_latches() { iop_.diagnostic_spu2_reserved_latches(); }
    // RUNTIME_HELPERS_BEGIN
    void diagnostic_spu2_voice_address_reserved_zero() { iop_.diagnostic_spu2_voice_address_reserved_zero(); }
    // RUNTIME_HELPERS_END
    void mount_disc(std::shared_ptr<DiscImage> disc,Iop::DiscMedium medium,bool diagnostic_cav_nominal_timing=false) {
        iop_.mount_disc(std::move(disc),medium,diagnostic_cav_nominal_timing);
    }
    [[nodiscard]] const VideoEncoderBus& video_encoder() const noexcept { return video_encoder_; }
    [[nodiscard]] ObservedWord observe_ipu_control() const noexcept {return ipu_.observe_control();}
    [[nodiscard]] ObservedWord observe_vif_status(bool unit1) {
        if(clock_)sync_iop();
        return (unit1 ? vif1_ : vif0_).observe_status();
    }
    [[nodiscard]] std::uint32_t read_vu_control(unsigned index) const {
        if(clock_)const_cast<EeRegisters*>(this)->sync_iop();
        if (index==29u && (vif0_.processing() || vif1_.processing()))
            throw DeviceRefusal("VU status with active VIF processing is not modeled");
        vu_control_.advance_q(clock_ ? now() : 0);
        return vu_control_.read(index);
    }
    void write_vu_control(unsigned index, std::uint32_t value) { vu_control_.write(index,value); }
    [[nodiscard]] ObservedWord observe_vu_control(unsigned index) const {
        if(clock_)const_cast<EeRegisters*>(this)->sync_iop();
        if (index==29u && (vif0_.processing() || vif1_.processing()))
            throw DeviceRefusal("VU status with active VIF processing is not modeled");
        vu_control_.advance_q(clock_ ? now() : 0);
        return vu_control_.observe(index);
    }
    void write_vu_control_observed(unsigned index, ObservedWord word) {vu_control_.write_observed(index,word);}
    // RUNTIME_HELPERS_BEGIN
    void vu_sync_micro(std::uint64_t cycle) {
        if(clock_)sync_iop();else advance_vpus(cycle,&vif0_,&vif1_,&vu_control_,gif_,gs_);
    }
    [[nodiscard]] bool vu0_micro_running() const {return vu_control_.micro_running(0);}
    [[nodiscard]] std::uint64_t vu_next_micro_cycle() const {return vu_control_.next_micro_cycle().value();}
    void vu0_call_micro(unsigned address,std::uint64_t cycle) {vu_control_.start_micro(0,address,cycle);}
    // RUNTIME_HELPERS_END
    void vu_advance_q(std::uint64_t cycle) { vu_control_.advance_q(cycle); }
    [[nodiscard]] std::uint64_t vu_q_ready_cycle() const noexcept { return vu_control_.q_ready_cycle(); }
    void vu_issue_q(std::uint64_t cycle,unsigned left,unsigned right,unsigned left_lane,unsigned right_lane,unsigned q_op) {
        vu_control_.issue_q(cycle,left,right,left_lane,right_lane,q_op);
    }
    void vu_integer_store(unsigned source,unsigned base,unsigned mask) {vu_control_.integer_store(source,base,mask);}
    [[nodiscard]] ObservedWord observe_vu_data(std::uint32_t address) const {return vu_control_.observe_data(address);}
    // HOST_INPUT_BEGIN
    void enable_vu1_aot(bool enabled) {vu_control_.enable_vu1_aot(enabled);}
    [[nodiscard]] const VuAotStats& vu0_aot_stats() const {return vu_control_.vu0_aot_stats();}
    [[nodiscard]] VuAotImage capture_vu0_micro() const {return vu_control_.capture_micro(0);}
    [[nodiscard]] const VuAotStats& vu1_aot_stats() const {return vu_control_.vu1_aot_stats();}
    [[nodiscard]] VuAotImage capture_vu1_micro() const {return vu_control_.capture_vu1_micro();}
    void set_vu1_capture(VuControl::Vu1Capture capture) {vu_control_.set_vu1_capture(std::move(capture));}
    // HOST_INPUT_END
    void diagnostic_vu_power_two_carry() {vu_control_.diagnostic_power_two_carry();}
    [[nodiscard]] std::optional<FloatBounds> observe_vu_vector_bounds(unsigned index,unsigned lane) const {return vu_control_.observe_vector_bounds(index,lane);}
    void restore_vu_vector_bounds(unsigned index,unsigned lane,FloatBounds bounds) {vu_control_.restore_vector_bounds(index,lane,bounds);}
    [[nodiscard]] ObservedQuadword observe_vu_vector(unsigned index) const {return vu_control_.observe_vector(index);}
    void write_vu_vector(unsigned index,ObservedQuadword word) {vu_control_.write_vector(index,word);}
    void vu_subtract(unsigned destination,unsigned left,unsigned right,unsigned mask) {
        vu_control_.subtract(destination,left,right,mask);
    }
    void vu_add(unsigned destination,unsigned left,unsigned right,unsigned mask,unsigned broadcast=4,bool subtract=false,bool to_accumulator=false) {
        vu_control_.add(destination,left,right,mask,broadcast,subtract,to_accumulator);
    }
    void vu_rotate(unsigned destination,unsigned source,unsigned mask) {vu_control_.rotate(destination,source,mask);}
    void vu_product(unsigned destination,unsigned left,unsigned right,unsigned mask,unsigned broadcast,bool accumulate,bool to_accumulator,bool outer=false) {
        vu_control_.product(destination,left,right,mask,broadcast,accumulate,to_accumulator,outer);
    }
    void vu_vector_store_increment(unsigned source,unsigned base,unsigned mask) {
        vu_control_.vector_store_increment(source,base,mask);
    }
    void vu_add_integer(unsigned destination,unsigned left,unsigned right) {vu_control_.add_integer(destination,left,right);}
    [[nodiscard]] ObservedDoubleword read_vu_memory(std::uint32_t physical,std::size_t bytes) const {
        return vu_control_.read_memory(physical,bytes);
    }
    void write_vu_memory(std::uint32_t physical,std::size_t bytes,ObservedDoubleword value) {
        vu_control_.write_memory(physical,bytes,value);
    }
    // INTC sources enabled in I_MASK (an interrupt reaches COP0 IP2 when one
    // of them is pending).
    [[nodiscard]] std::uint32_t intc_mask() const noexcept { return intc_mask_; }
    // Samples implemented interrupt sources at the shared EE clock. Unknown
    // timing is refused only when it can affect the requested source bits.
    [[nodiscard]] bool intc_interrupt_pending();
    [[nodiscard]] bool dmac_interrupt_pending() { sync_iop(); return iop_.ee_dma_interrupt_pending(); }
    [[nodiscard]] ObservedWord observe_intc(std::uint32_t needed = 0x7FFFu);
    [[nodiscard]] ObservedDoubleword observe_gs_csr();
    [[nodiscard]] ObservedWord observe_dma_word(std::uint32_t physical);
    void write_dma_word(std::uint32_t physical, ObservedWord word);
    // Runs the IOP up to the current EE time.
    void sync_iop();
    // RUNTIME_HELPERS_BEGIN
    [[nodiscard]] std::optional<std::uint64_t> idle_poll_horizon() const;
    [[nodiscard]] std::uint32_t settle_interrupt_sample(std::uint64_t stamp,std::uint32_t status);
    void settle_idle_timers();
    // RUNTIME_HELPERS_END
    [[nodiscard]] bool dma_reads_ram() const noexcept { return iop_.reads_ee_ram(); }
    [[nodiscard]] bool dma_writes_ram() const noexcept { return iop_.writes_ee_ram(); }
    void attach_ee_ram(std::span<std::uint8_t> ram,
                       std::unordered_map<std::uint32_t,std::uint32_t>* unknown = nullptr,
                       std::span<std::uint8_t> scratchpad = {},
                       std::unordered_map<std::uint32_t,StoredFpuRelation>* metadata = nullptr) {
        iop_.attach_ee_ram(ram, unknown, scratchpad,metadata);
    }
    void load_iop_program(std::span<const std::uint8_t> bytes, std::uint32_t address, std::uint32_t entry) {
        iop_.load_program(bytes, address, entry);
    }
    // Bytes the guest sent through the EE SIO TX FIFO since reset.
    [[nodiscard]] const std::string& sio_output() const noexcept { return sio_output_; }

private:
    // EE timers T0..T3 (Tn_COUNT/MODE/COMP/HOLD at 0x10000000 + n * 0x800).
    struct Timer {
        std::uint32_t mode{};
        std::uint16_t count_at_anchor{};
        std::uint64_t anchor_cycle{};
        std::optional<std::uint16_t> compare; // unset until written
        std::optional<std::uint16_t> hold; // T0/T1 only; reset contents not established
    };
    [[nodiscard]] std::uint64_t now() const;
    [[nodiscard]] std::uint16_t timer_count(const Timer& timer) const;
    void advance_timer(unsigned index);
    // RUNTIME_HELPERS_BEGIN
    void advance_timer_at(unsigned index,std::uint64_t stamp);
    // RUNTIME_HELPERS_END
    void advance_vertical();
    void advance_gs_interrupts();
    void advance_vif_interrupts();
    [[nodiscard]] std::uint32_t intc_status(std::uint32_t needed);
    void write_timer(unsigned index, unsigned reg, std::uint32_t value);
    [[nodiscard]] std::uint32_t read_timer(unsigned index, unsigned reg);

    std::array<Timer, 4> timers_{};
    GsRegisters gs_;
    MchRegisters mch_;
    Iop iop_;
    // 0x1000F500: unidentified register written by the BIOS reset code.
    std::optional<std::uint32_t> f500_;
    // An explicit diagnostic may omit F510's unknown zero-write effect.
    bool diagnostic_omit_f510_{};
    // DEV9 0x1F801470/0x1F801472: write latches, same rule as 0x1000F500.
    std::array<std::optional<std::uint16_t>, 2> dev9_147x_{};
    // EE SIO line setup (LCR, FCR, BGR) as last written; transmission is
    // instantaneous (hypothesis: baud timing is not modeled).
    std::array<std::optional<std::uint32_t>, 3> sio_setup_{};
    std::string sio_output_;
    std::uint32_t intc_mask_{};
    std::uint32_t intc_stat_{};
    std::uint32_t intc_unknown_{};
    std::uint64_t vertical_generation_{~0ull};
    std::uint64_t vertical_sample_{};
    Gif gif_;
    Vif vif0_{Vif::Unit::Vu0},vif1_{Vif::Unit::Vu1};
    IpuRegisters ipu_;
    mutable VuControl vu_control_;
    // Video encoder serial bus and its two masters.
    VideoEncoderBus video_encoder_;
    VideoEncoderMaster encoder_master_190x_{VideoEncoderMaster::Layout::Byte190x};
    VideoEncoderMaster encoder_master_1a_{VideoEncoderMaster::Layout::Halfword1A};
    const std::uint64_t* clock_{};
    const std::uint64_t* stall_clock_{};
    const ConsoleHardware* hardware_{};
};
} // namespace ps2native
