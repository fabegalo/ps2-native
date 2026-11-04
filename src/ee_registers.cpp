#include "ps2native/ee_registers.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace ps2native {
namespace {
std::string hex(std::uint32_t value, int digits = 8) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(digits) << std::setfill('0') << value;
    return stream.str();
}

constexpr std::uint32_t kTimersBase = 0x10000000u;
constexpr std::uint32_t kTimersEnd = 0x10002000u;
constexpr std::uint32_t kF500 = 0x1000F500u;
// SIF registers, EE side (ps2tek): MSCOM, SMCOM, MSFLG, SMFLG, CTRL, BD6 at
// 0x1000F200 + n * 0x10; the state is shared with the IOP, which owns it.
constexpr std::uint32_t kSifBase = 0x1000F200u, kSifEnd = 0x1000F270u;
// Tn_MODE fields (ps2tek).
constexpr std::uint32_t kModeClock = 3u << 0;
constexpr std::uint32_t kModeGateEnable = 1u << 2;
constexpr std::uint32_t kModeClearOnCompare = 1u << 6;
constexpr std::uint32_t kModeEnable = 1u << 7;
constexpr std::uint32_t kModeCompareIrq = 1u << 8;
constexpr std::uint32_t kModeOverflowIrq = 1u << 9;
constexpr std::uint32_t kModeConfig = 0x3FFu;
constexpr std::uint32_t kModeCompareFlag = 1u << 10, kModeOverflowFlag = 1u << 11;
enum TimerReg : unsigned { Count = 0, Mode = 1, Comp = 2, Hold = 3 };
const char* timer_reg_name(unsigned reg) {
    static constexpr const char* kNames[] = {"COUNT", "MODE", "COMP", "HOLD"};
    return reg < 4u ? kNames[reg] : "?";
}

constexpr std::uint32_t kModeHblank = 3u;

// Clock edges for BUSCLK, BUSCLK/16 and BUSCLK/256 occur at fixed multiples
// of the period since reset; HBLANK edges come from the GS line clock.
GsRegisters::LineClock timer_clock(std::uint32_t mode, const GsRegisters& gs) {
    switch (mode & kModeClock) {
        case 0: return {0u, kEeCyclesPerBusCycle};
        case 1: return {0u, kEeCyclesPerBusCycle * 16u};
        case 2: return {0u, kEeCyclesPerBusCycle * 256u};
        default: return gs.hblank_held() ? GsRegisters::LineClock{} : gs.line_clock();
    }
}
} // namespace

void EeRegisters::reset(const ConsoleHardware* hardware, std::span<const std::uint8_t> rom0, const NvmImage* nvm,
                        const DiagnosticAssumptions* assumptions, std::span<const std::uint8_t> rom1) {
    hardware_ = hardware;
    iop_.reset(rom0, hardware, nvm, assumptions && assumptions->sbus_configuration_latches,
               assumptions ? assumptions->iop_dmacinten : std::nullopt, rom1,
               assumptions && assumptions->iop_sio2_irq_clear,
               assumptions && assumptions->spu2_quiescent_reset);
    if(assumptions && assumptions->spu2_pio_word_cycles)
        iop_.diagnostic_spu2_pio_word_cycles(*assumptions->spu2_pio_word_cycles);
    // Hypothesis: counters, modes (disabled) and the latch are zero/unset after reset.
    timers_ = {};
    f500_.reset();
    diagnostic_omit_f510_=assumptions && assumptions->omit_f510_zero_write;
    dev9_147x_ = {};
    sio_setup_ = {};
    sio_output_.clear();
    gs_.reset(0u, assumptions ? assumptions->gs_reset_syncv : std::nullopt);
    mch_.reset();
    // Hypothesis: every source masked at power-on. The kernel reads I_MASK
    // before writing it (EnableIntc tests the bit, then reverses it).
    intc_mask_ = 0u;
    intc_stat_ = 0u; // EE User's Manual, I_STAT initial state.
    intc_unknown_ = 0u;
    vertical_generation_ = ~0ull;
    vertical_sample_ = 0;
    // Hypothesis: GIF not stopped, PATH3 unmasked, continuous mode at power-on.
    gif_.reset();
    vif0_.reset();
    vif1_.reset();
    ipu_.reset();
    vu_control_.reset();
    video_encoder_.reset(assumptions);
    encoder_master_190x_.reset();
    encoder_master_1a_.reset();
}

// Deterministic EE/IOP ordering: the IOP never runs ahead of the EE, and it
// is brought up to the EE time before every EE access that reads IOP state or
// changes state the IOP observes (the GS video timing). IOP events at or
// before an EE access are therefore ordered before it, and both EE engines
// see the same IOP.
void EeRegisters::sync_iop() {
    iop_.run_until(now() / kEeCyclesPerIopCycle, &gs_);
    iop_.advance_gif_dma(now(),gif_,gs_);
}

std::uint64_t EeRegisters::now() const {
    if (clock_ == nullptr) throw DeviceRefusal("no EE clock is attached to the EE registers");
    return *clock_+(stall_clock_ ? *stall_clock_ : 0);
}

std::uint16_t EeRegisters::timer_count(const Timer& timer) const {
    if ((timer.mode & kModeEnable) == 0u) return timer.count_at_anchor;
    // The anchor never precedes the clock epoch: a GS line clock change
    // re-anchors every timer counting HBLANK (see write()).
    const auto clock = timer_clock(timer.mode, gs_);
    if (!clock.period) return timer.count_at_anchor; // known absence of external edges
    const auto ticks = (now() - clock.epoch) / clock.period - (timer.anchor_cycle - clock.epoch) / clock.period;
    return static_cast<std::uint16_t>(timer.count_at_anchor + ticks);
}

void EeRegisters::advance_timer(unsigned index) {
    auto& timer=timers_[index];
    const auto stamp=now();
    if (!(timer.mode & kModeEnable)) {timer.anchor_cycle=stamp;return;}
    const auto clock=timer_clock(timer.mode,gs_);
    if (!clock.period) { timer.anchor_cycle=stamp; return; }
    const auto ticks=(stamp-clock.epoch)/clock.period-(timer.anchor_cycle-clock.epoch)/clock.period;
    if (!ticks) return;
    if ((timer.mode & (kModeClearOnCompare|kModeCompareIrq)) && !timer.compare)
        throw DeviceRefusal("T"+std::to_string(index)+" counts with undefined COMP (reset value not verified)");
    const auto start=std::uint64_t{timer.count_at_anchor};
    const auto overflow=65536u-start;
    const auto target=std::uint64_t{timer.compare.value_or(0)};
    auto distance=(target+65536u-start)&0xFFFFu;
    if (!distance) distance=65536u;
    const bool compared=timer.compare && ticks>=distance;
    // A nonzero zero-return target suppresses overflow after the first compare.
    const bool overflowed=ticks>=overflow && (!(timer.mode&kModeClearOnCompare) ||
                                             target==0 || overflow<=distance);
    auto flags=timer.mode&(kModeCompareFlag|kModeOverflowFlag);
    if (compared && (timer.mode&kModeCompareIrq)) flags|=kModeCompareFlag;
    if (overflowed && (timer.mode&kModeOverflowIrq)) flags|=kModeOverflowFlag;
    if (flags & ~timer.mode) intc_stat_|=1u<<(9u+index); // only rising flag edges
    auto count=(start+ticks)&0xFFFFu;
    if ((timer.mode&kModeClearOnCompare) && compared)
        count=(ticks-distance)%(target?target:65536u);
    timer.mode|=flags;
    timer.count_at_anchor=static_cast<std::uint16_t>(count);
    timer.anchor_cycle=stamp;
}

void EeRegisters::advance_vertical() {
    const auto stamp=now();
    const auto clock=gs_.vertical_clock();
    if(clock.state==GsRegisters::VerticalClock::State::Unknown && stamp>vertical_sample_)
        intc_unknown_|=0xCu;
    const auto from=vertical_generation_==gs_.timing_generation()?vertical_sample_+1u:stamp;
    if(clock.state==GsRegisters::VerticalClock::State::Running) {
        if(clock.next_edge(from,false)<=stamp)intc_stat_|=1u<<2u;
        if(clock.next_edge(from,true)<=stamp)intc_stat_|=1u<<3u;
    }
    vertical_generation_=gs_.timing_generation();vertical_sample_=stamp;
}
ObservedWord EeRegisters::observe_intc(std::uint32_t needed) {
    for(unsigned i=0;i<4;++i)advance_timer(i);
    if (needed & 1u) {
        advance_gs_interrupts();
    }
    if(needed&0xCu) {
        advance_vertical();
    }
    // Unsupported VIF IRQ, VU, IPU and SBUS commands fail before producing
    // effects. Their reset-zero INTC latches cannot receive a modeled edge yet.
    return {intc_stat_, intc_unknown_ & ~intc_stat_ & needed};
}
std::uint32_t EeRegisters::intc_status(std::uint32_t needed) {
    const auto observed = observe_intc(needed);
    if (observed.unknown & 1u)
        throw DeviceRefusal("I_STAT: unmasked GS event timing unknown since last acknowledge");
    if (observed.unknown & 0xCu)
        throw DeviceRefusal("I_STAT: vertical timing unknown since last acknowledge");
    return observed.value;
}
bool EeRegisters::intc_interrupt_pending() {
    return intc_mask_ && (intc_status(intc_mask_)&intc_mask_);
}

void EeRegisters::advance_gs_interrupts() {
    const auto request = gs_.take_interrupt_request(now());
    if (request.pending) {
        intc_stat_ |= 1u;
        intc_unknown_ &= ~1u; // a definite request establishes the latch
    } else if (request.unknown && !(intc_stat_ & 1u)) {
        intc_unknown_ |= 1u;
    }
}
ObservedWord EeRegisters::observe_dma_word(std::uint32_t physical) {
    sync_iop();
    return iop_.ee_dma_observe(physical);
}
void EeRegisters::write_dma_word(std::uint32_t physical, ObservedWord word) {
    sync_iop();
    iop_.ee_dma_write_observed(physical, word);
}

ObservedDoubleword EeRegisters::observe_gs_csr() {
    advance_gs_interrupts();
    auto result=gs_.observe_csr();
    if(hardware_ && hardware_->gs_revision_id) {
        const auto& identity=*hardware_->gs_revision_id;
        result.value|=(std::uint64_t{identity[0]}<<16u)|(std::uint64_t{identity[1]}<<24u);
        result.unknown&=~std::uint64_t{0xFFFF0000u};
    }
    return result;
}

void EeRegisters::write_timer(unsigned index, unsigned reg, std::uint32_t value) {
    auto& timer = timers_[index];
    const auto name = "T" + std::to_string(index) + "_" + timer_reg_name(reg);
    switch (reg) {
        case Count:
            advance_timer(index);
            timer.count_at_anchor = static_cast<std::uint16_t>(value);
            timer.anchor_cycle = now();
            return;
        case Mode: {
            if (value & kModeEnable) {
                if (value & kModeGateEnable)
                    throw DeviceRefusal(name + " " + hex(value) + ": HBLANK/VBLANK gating is not modeled");
                if ((value & kModeClock) == kModeHblank && !gs_.hblank_held())
                    (void)gs_.line_clock(); // unknown modes still refuse; held sync is a stopped clock
            }
            // The counter keeps its value across the mode change and continues
            // from it with the new clock source (or stops when disabled).
            advance_timer(index);
            timer.anchor_cycle = now();
            timer.mode = (value & kModeConfig) |
                         (timer.mode & (kModeCompareFlag|kModeOverflowFlag) & ~value);
            return;
        }
        case Comp:
            advance_timer(index);
            timer.compare = static_cast<std::uint16_t>(value);
            return;
        case Hold:
            // EE User's Manual v6, pp. 35/39: T0/T1 HOLD are writable, low
            // 16 bits. SBUS capture must be added when that source is modeled.
            if (index >= 2u) throw DeviceRefusal(name + ": counter hold register does not exist");
            timer.hold = static_cast<std::uint16_t>(value);
            return;
        default:
            throw DeviceRefusal(name + ": counter hold (SBUS capture) is not modeled");
    }
}

std::uint32_t EeRegisters::read_timer(unsigned index, unsigned reg) {
    if (reg != Hold) advance_timer(index);
    const auto& timer = timers_[index];
    const auto name = "T" + std::to_string(index) + "_" + timer_reg_name(reg);
    switch (reg) {
        case Count:
            return timer_count(timer);
        case Comp:
            if (!timer.compare) throw DeviceRefusal(name + " read before any write (reset value not verified)");
            return *timer.compare;
        case Mode:
            return timer.mode;
        case Hold:
            if (index >= 2u) throw DeviceRefusal(name + ": counter hold register does not exist");
            if (!timer.hold) throw DeviceRefusal(name + ": counter hold read before write (reset value not verified)");
            return *timer.hold;
        default:
            throw DeviceRefusal(name + ": counter hold (SBUS capture) is not modeled");
    }
}

std::uint64_t EeRegisters::read(std::uint32_t physical, std::size_t bytes) {
    if(physical>=0x1F402000u && physical<0x1F402020u) {
        sync_iop();return iop_.ee_cdvd_read(physical,bytes);
    }
    if(physical==IpuRegisters::kPosition) {
        if(bytes!=4u) throw DeviceRefusal("IPU_BP requires a 32-bit access");
        return ipu_.read_position();
    }
    if(physical==IpuRegisters::kControl) {
        if(bytes!=4u) throw DeviceRefusal("IPU_CTRL requires a 32-bit access");
        return ipu_.read_control();
    }
    if((physical>=0x10003800u && physical<0x10003A00u) || (physical>=0x10003C00u && physical<0x10003E00u)) {
        if(bytes!=4u || (physical&15u)) throw DeviceRefusal("VIF registers require aligned 32-bit accesses");
        auto& vif=(physical&0x400u) ? vif1_ : vif0_;
        return vif.read_register(physical&0x3FFu);
    }
    if (Iop::ee_dma_address(physical)) {
        if (bytes != 4u) throw DeviceRefusal("SIF1/DMAC requires 32-bit accesses");
        sync_iop();
        return iop_.ee_dma_read(physical);
    }
    if (physical >= kIopRamEePhysical && physical < kIopRamEePhysical + 0x00800000u) {
        sync_iop();
        return iop_.ee_read(physical - kIopRamEePhysical, bytes);
    }
    if (physical >= kTimersBase && physical < kTimersEnd) {
        const auto offset = physical & 0x7FFu;
        if (bytes != 4u || offset % 16u != 0u || offset >= 0x40u)
            throw DeviceRefusal("EE timer access of " + std::to_string(bytes) + " byte(s) at offset " +
                                hex(offset, 3) + " is not modeled (32-bit Tn_COUNT/MODE/COMP only)");
        return read_timer((physical >> 11) & 3u, offset / 16u);
    }
    if (physical >= kGsPrivilegedBase && physical < kGsPrivilegedEnd) {
        advance_gs_interrupts();
        return gs_.read(physical, bytes);
    }
    if (physical >= kSifBase && physical < kSifEnd) {
        if (bytes != 4u || (physical & 0xFu) != 0u)
            throw DeviceRefusal("SIF access of " + std::to_string(bytes) + " byte(s) at " + hex(physical) +
                                " is not modeled (32-bit registers only)");
        sync_iop();
        return iop_.ee_sif_read((physical - kSifBase) / 0x10u);
    }
    if (physical == kGifStat) {
        if (bytes != 4u) throw DeviceRefusal("GIF_STAT read of " + std::to_string(bytes) + " byte(s) is not modeled");
        return gif_.read_stat();
    }
    if (physical == kF500) {
        // Unidentified register; what reads return has not been verified.
        throw DeviceRefusal("0x1000F500 read: an unidentified register; its read value is not verified");
    }
    if (physical == 0x1000F510u)
        throw DeviceRefusal("0x1000F510 read: VU0-interface configuration readback is not verified");
    if (physical == kDmacEnabler) {
        if (bytes != 4u)
            throw DeviceRefusal("D_ENABLER read of " + std::to_string(bytes) + " byte(s) is not modeled (32-bit only)");
        if (hardware_ == nullptr)
            throw DeviceRefusal("D_ENABLER read: no console hardware profile selected; its value identifies the "
                                "mainboard and is not in ROM0 (select one, e.g. --console=SCPH-77008)");
        if (!hardware_->d_enabler)
            throw DeviceRefusal("D_ENABLER read: the value of " + std::string(hardware_->model) + ":" +
                                std::string(hardware_->mainboard) + " has not been measured (" +
                                std::string(hardware_->source) + ")");
        return (*hardware_->d_enabler&~0x10000u)|(iop_.ee_dma_suspended() ? 0x10000u : 0u);
    }
    if (physical == 0x1F803800u) {
        if(bytes!=2u) throw DeviceRefusal("MPU board ID requires a 16-bit read");
        if(!hardware_ || !hardware_->mpu_board_id)
            throw DeviceRefusal("MPU board ID has not been measured for this profile");
        return *hardware_->mpu_board_id;
    }
    if (physical == kToolBoardIdPhysical) {
        if (bytes != 1u)
            throw DeviceRefusal("tool board id access of " + std::to_string(bytes) +
                                " byte(s) is not modeled (8-bit read only)");
        return kRetailToolBoardId; // no tool board (retail value: hypothesis)
    }
    if (physical == kDev9Revision) {
        if (bytes != 1u)
            throw DeviceRefusal("DEV9_R_REV access of " + std::to_string(bytes) +
                                " byte(s) is not modeled (8-bit read only)");
        if (hardware_ == nullptr || !hardware_->dev9_revision)
            throw DeviceRefusal("DEV9_R_REV read: no console profile with the SSBUS revision is selected");
        return *hardware_->dev9_revision;
    }
    if (physical == kDev9Reg1470 || physical == kDev9Reg1472)
        throw DeviceRefusal("DEV9 " + hex(physical) + " read: an undocumented register; its read value is not verified");
    if (physical >= kMchBase && physical < kMchEnd) return mch_.read(physical, bytes, now());
    if (physical == kIntcMask) {
        if (bytes != 4u) throw DeviceRefusal("I_MASK read of " + std::to_string(bytes) + " byte(s) is not modeled");
        return intc_mask_;
    }
    if (physical >= kVideoEncoderMaster190x && physical < kVideoEncoderMaster190x + kVideoEncoderMasterBytes)
        return encoder_master_190x_.read(physical - kVideoEncoderMaster190x, bytes, video_encoder_);
    if (physical >= kVideoEncoderMaster1A && physical < kVideoEncoderMaster1A + kVideoEncoderMasterBytes)
        return encoder_master_1a_.read(physical - kVideoEncoderMaster1A, bytes, video_encoder_);
    if (physical == kIntcStat) {
        if(bytes!=4u)throw DeviceRefusal("I_STAT requires a 32-bit read");
        return intc_status(kIntcSources);
    }
    if (physical == kSioIsr) {
        if (bytes != 4u) throw DeviceRefusal("SIO_ISR read of " + std::to_string(bytes) + " byte(s) is not modeled");
        // Bits 12-15: TX FIFO level, 0 because transmission is instantaneous;
        // bits 8-11: RX FIFO level, 0 because nothing is received; bits 0-2:
        // interrupt status, 0 with interrupts disabled (hypothesis, as PCSX2,
        // which notes it is not hardware tested).
        return 0u;
    }
    if (physical >= kSioLcr && physical < kSioTxFifo + 0x80u)
        throw DeviceRefusal("EE SIO read: only SIO_ISR is modeled (LSR/RX FIFO status and receive are not)");
    throw DeviceRefusal("no register model");
}

void EeRegisters::write128(std::uint32_t physical, std::uint64_t low, std::uint64_t high) {
    if (physical==0x10007010u) {
        if(ipu_.write_fifo(low,high)) intc_stat_|=1u<<8;
        return;
    }
    if (physical==0x10004000u || physical==0x10005000u) {
        auto& vif=(physical&0x1000u) ? vif1_ : vif0_;
        vif.write_fifo(low, high, gif_, gs_);
        return;
    }
    if (physical == kGifFifo) return gif_.write_fifo(low, high, gs_);
    throw DeviceRefusal("128-bit store: no register model");
}

void EeRegisters::write(std::uint32_t physical, std::size_t bytes, std::uint64_t value) {
    if(physical>=0x1F402000u && physical<0x1F402020u) {
        sync_iop();iop_.ee_cdvd_write(physical,bytes,static_cast<std::uint32_t>(value));return;
    }
    if(physical==0x1000F590u) {
        // EE User's Manual: CPND is bit 16. Preserve board identification
        // bits; writes changing undocumented configuration still refuse.
        if(bytes!=4u || !hardware_ || !hardware_->d_enabler ||
           (value&~0x10000ull)!=(*hardware_->d_enabler&~0x10000u))
            throw DeviceRefusal("D_ENABLEW requires a 32-bit CPND-only change to the selected profile");
        sync_iop();
        iop_.set_ee_dma_suspended((value&0x10000u)!=0);
        return;
    }
    if(physical==IpuRegisters::kCommand) {
        if(bytes!=4u && bytes!=8u) throw DeviceRefusal("IPU_CMD requires a 32/64-bit access");
        if(ipu_.write_command(static_cast<std::uint32_t>(value))) intc_stat_|=1u<<8;
        return;
    }
    if(physical==IpuRegisters::kControl) {
        if(bytes!=4u) throw DeviceRefusal("IPU_CTRL requires a 32-bit access");
        if(ipu_.write_control(static_cast<std::uint32_t>(value))) intc_stat_|=1u<<8;
        return;
    }
    if((physical>=0x10003800u && physical<0x10003A00u) || (physical>=0x10003C00u && physical<0x10003E00u)) {
        if(bytes!=4u || (physical&15u)) throw DeviceRefusal("VIF registers require aligned 32-bit accesses");
        auto& vif=(physical&0x400u) ? vif1_ : vif0_;
        vif.write_register(physical&0x3FFu,static_cast<std::uint32_t>(value),gif_);return;
    }
    if (Iop::ee_dma_address(physical)) {
        if (bytes != 4u) throw DeviceRefusal("SIF1/DMAC requires 32-bit accesses");
        sync_iop();
        iop_.ee_dma_write(physical, static_cast<std::uint32_t>(value));
        return;
    }
    if (physical >= kIopRamEePhysical && physical < kIopRamEePhysical + 0x00800000u) {
        sync_iop();
        iop_.ee_write(physical-kIopRamEePhysical,bytes,value);
        return;
    }
    if (physical >= kTimersBase && physical < kTimersEnd) {
        const auto offset = physical & 0x7FFu;
        if (bytes != 4u || offset % 16u != 0u || offset >= 0x40u)
            throw DeviceRefusal("EE timer access of " + std::to_string(bytes) + " byte(s) at offset " +
                                hex(offset, 3) + " is not modeled (32-bit Tn_COUNT/MODE/COMP only)");
        write_timer((physical >> 11) & 3u, offset / 16u, static_cast<std::uint32_t>(value));
        return;
    }
    if (physical >= kGsPrivilegedBase && physical < kGsPrivilegedEnd) {
        sync_iop(); // the IOP's VBLANK sources follow the GS timing
        advance_gs_interrupts();
        advance_vertical();
        const auto generation=gs_.timing_generation();
        gs_.write(physical, bytes, value, now(), [this] {
            // A timer counting HBLANK keeps the edges of the old line clock up
            // to now and counts those of the new one from now on; a later read
            // refuses if the new state has no HBLANK.
            for (unsigned i=0;i<timers_.size();++i) {
                auto& timer=timers_[i];
                if ((timer.mode & kModeEnable) != 0u && (timer.mode & kModeClock) == kModeHblank) {
                    advance_timer(i);
                    timer.anchor_cycle = now();
                }
            }
        });
        if(generation!=gs_.timing_generation()) {
            vertical_generation_=gs_.timing_generation();
            vertical_sample_=now();
        }
        advance_gs_interrupts();
        return;
    }
    if (physical >= kSifBase && physical < kSifEnd) {
        if (bytes != 4u || (physical & 0xFu) != 0u)
            throw DeviceRefusal("SIF access of " + std::to_string(bytes) + " byte(s) at " + hex(physical) +
                                " is not modeled (32-bit registers only)");
        sync_iop(); // the IOP sees the write only from now on
        iop_.ee_sif_write((physical - kSifBase) / 0x10u, static_cast<std::uint32_t>(value));
        return;
    }
    if (physical == kGifCtrl || physical == kGifMode) {
        if (bytes != 4u)
            throw DeviceRefusal(std::string(physical == kGifCtrl ? "GIF_CTRL" : "GIF_MODE") + " write of " +
                                std::to_string(bytes) + " byte(s): only 32-bit writes are modeled");
        if (physical == kGifCtrl) gif_.write_ctrl(static_cast<std::uint32_t>(value));
        else gif_.write_mode(static_cast<std::uint32_t>(value));
        return;
    }
    if (physical >= kGifFifo && physical < kGifFifo + 0x10u)
        throw DeviceRefusal("GIF FIFO write of " + std::to_string(bytes) + " byte(s): the FIFO takes quadwords (SQ)");
    if (physical == kF500 && bytes == 4u) {
        // Written by the BIOS reset code before the RDRAM setup. Its function
        // is undocumented (PCSX2 keeps it as plain storage): the value is
        // latched and has no modeled effect.
        f500_ = static_cast<std::uint32_t>(value);
        return;
    }
    if (physical == 0x1000F510u) {
        if(!diagnostic_omit_f510_) throw DeviceRefusal("no register model");
        // Hardware observations (wisi, PSX-Place, 2019-12-01) report writable
        // interface bits and an effect on F590 readback. Firmware initializes
        // it to zero before initializing VU0. The effect is unknown; this is
        // an explicit diagnostic omission, not a hardware implementation.
        // Other configurations and F510/F590 readback remain explicit refusals.
        if (bytes != 4u || value != 0u)
            throw DeviceRefusal("0x1000F510 VU0-interface configuration: only 32-bit zero initialization modeled");
        return;
    }
    if (physical == kToolBoardIdPhysical)
        throw DeviceRefusal("tool board id write: no evidence of what a write does");
    if (physical == kDev9Reg1470 || physical == kDev9Reg1472) {
        if (bytes != 2u)
            throw DeviceRefusal("DEV9 " + hex(physical) + " write of " + std::to_string(bytes) +
                                " byte(s) is not modeled (16-bit stores only)");
        // Undocumented (PCSX2 stores it as an unknown DEV9 write): latched,
        // no modeled effect; DEV9 itself is not modeled, so nothing can
        // observe a missing effect without failing explicitly.
        dev9_147x_[physical == kDev9Reg1472 ? 1u : 0u] = static_cast<std::uint16_t>(value);
        return;
    }
    if (physical == kSioTxFifo) {
        if (bytes != 1u)
            throw DeviceRefusal("SIO TX FIFO write of " + std::to_string(bytes) +
                                " byte(s) is not modeled (byte stores only)");
        sio_output_.push_back(static_cast<char>(value));
        return;
    }
    if (physical == kSioIer) {
        if (bytes != 4u || value != 0u)
            throw DeviceRefusal("SIO_IER " + hex(static_cast<std::uint32_t>(value)) +
                                ": SIO interrupts need the INTC, not modeled (only 32-bit 0 is accepted)");
        return;
    }
    if (physical == kSioLcr || physical == kSioFcr || physical == kSioBgr) {
        if (bytes != 4u)
            throw DeviceRefusal("EE SIO setup write of " + std::to_string(bytes) + " byte(s) is not modeled");
        // Line format, FIFO control and baud rate only shape the serial
        // signal; with instantaneous transmission they are latched as written.
        // The FIFO reset bits have nothing to clear (TX drains at once, RX is empty).
        sio_setup_[physical == kSioLcr ? 0u : physical == kSioFcr ? 1u : 2u] = static_cast<std::uint32_t>(value);
        return;
    }
    if (physical >= kMchBase && physical < kMchEnd) {
        mch_.write(physical, bytes, value, now());
        return;
    }
    if (physical == kIntcMask) {
        if (bytes != 4u) throw DeviceRefusal("I_MASK write of " + std::to_string(bytes) + " byte(s) is not modeled");
        for(unsigned i=0;i<4;++i)advance_timer(i);
        intc_mask_ ^= static_cast<std::uint32_t>(value) & kIntcSources;
        return;
    }
    for (auto [base, master] : {std::pair{kVideoEncoderMaster190x, &encoder_master_190x_},
                                std::pair{kVideoEncoderMaster1A, &encoder_master_1a_}}) {
        if (physical < base || physical >= base + kVideoEncoderMasterBytes) continue;
        if (physical + bytes > base + kVideoEncoderMasterBytes)
            throw DeviceRefusal(hex(physical) + " write crosses the end of the video encoder master block at " +
                                hex(base));
        master->write(physical - base, bytes, static_cast<std::uint32_t>(value), video_encoder_);
        return;
    }
    if (physical == kIntcStat) {
        if (bytes != 4u) throw DeviceRefusal("I_STAT write of " + std::to_string(bytes) + " byte(s) is not modeled");
        if (value & 1u) advance_gs_interrupts();
        // Sample timer edges before acknowledging their latched request.
        for(unsigned i=0;i<4;++i)advance_timer(i);
        // Unknown vertical timing cannot be invented. Clearing these latches
        // starts a new observation interval; a later read still validates it.
        if(value&0xCu)advance_vertical();
        intc_stat_ &= ~static_cast<std::uint32_t>(value);
        intc_unknown_ &= ~static_cast<std::uint32_t>(value);
        return;
    }
    throw DeviceRefusal("no register model");
}
} // namespace ps2native
