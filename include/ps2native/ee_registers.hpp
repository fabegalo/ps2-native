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
    [[nodiscard]] const GsRegisters& gs() const noexcept { return gs_; }
    [[nodiscard]] const MchRegisters& mch() const noexcept { return mch_; }
    [[nodiscard]] const Iop& iop() const noexcept { return iop_; }
    void mount_disc(std::shared_ptr<DiscImage> disc,Iop::DiscMedium medium,bool diagnostic_cav_nominal_timing=false) {
        iop_.mount_disc(std::move(disc),medium,diagnostic_cav_nominal_timing);
    }
    [[nodiscard]] const VideoEncoderBus& video_encoder() const noexcept { return video_encoder_; }
    [[nodiscard]] ObservedWord observe_ipu_control() const noexcept {return ipu_.observe_control();}
    [[nodiscard]] std::uint32_t read_vu_control(unsigned index) const {
        if (index==29u && (vif0_.processing() || vif1_.processing()))
            throw DeviceRefusal("VU status with active VIF processing is not modeled");
        vu_control_.advance_q(clock_ ? now() : 0);
        return vu_control_.read(index);
    }
    void write_vu_control(unsigned index, std::uint32_t value) { vu_control_.write(index,value); }
    [[nodiscard]] ObservedWord observe_vu_control(unsigned index) const {
        if (index==29u && (vif0_.processing() || vif1_.processing()))
            throw DeviceRefusal("VU status with active VIF processing is not modeled");
        vu_control_.advance_q(clock_ ? now() : 0);
        return vu_control_.observe(index);
    }
    void write_vu_control_observed(unsigned index, ObservedWord word) {vu_control_.write_observed(index,word);}
    void vu_advance_q(std::uint64_t cycle) { vu_control_.advance_q(cycle); }
    [[nodiscard]] std::uint64_t vu_q_ready_cycle() const noexcept { return vu_control_.q_ready_cycle(); }
    void vu_issue_q(std::uint64_t cycle,unsigned left,unsigned right,unsigned left_lane,unsigned right_lane,bool square_root) {
        vu_control_.issue_q(cycle,left,right,left_lane,right_lane,square_root);
    }
    void vu_integer_store(unsigned source,unsigned base,unsigned mask) {vu_control_.integer_store(source,base,mask);}
    [[nodiscard]] ObservedWord observe_vu_data(std::uint32_t address) const {return vu_control_.observe_data(address);}
    [[nodiscard]] ObservedQuadword observe_vu_vector(unsigned index) const {return vu_control_.observe_vector(index);}
    void write_vu_vector(unsigned index,ObservedQuadword word) {vu_control_.write_vector(index,word);}
    void vu_subtract(unsigned destination,unsigned left,unsigned right,unsigned mask) {
        vu_control_.subtract(destination,left,right,mask);
    }
    void vu_add(unsigned destination,unsigned left,unsigned right,unsigned mask,unsigned broadcast=4,bool subtract=false) {
        vu_control_.add(destination,left,right,mask,broadcast,subtract);
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
    [[nodiscard]] bool dma_reads_ram() const noexcept { return iop_.reads_ee_ram(); }
    [[nodiscard]] bool dma_writes_ram() const noexcept { return iop_.writes_ee_ram(); }
    void attach_ee_ram(std::span<std::uint8_t> ram,
                       std::unordered_map<std::uint32_t,std::uint32_t>* unknown = nullptr) {
        iop_.attach_ee_ram(ram, unknown);
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
    void advance_vertical();
    void advance_gs_interrupts();
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
