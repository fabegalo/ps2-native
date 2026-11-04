#include "ps2native/iop.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace ps2native {
namespace {
std::string hex(std::uint32_t value, int digits = 8) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(digits) << std::setfill('0') << value;
    return stream.str();
}

// Stops the IOP: the reason names the instruction or access.
struct IopStop {
    std::string reason;
};

constexpr std::uint32_t kResetVector = 0xBFC00000u;
constexpr std::uint32_t kRomPhysical = 0x1FC00000u;
constexpr std::uint32_t kRomBytes = 4u * 1024u * 1024u;
constexpr std::uint32_t kPostPhysical = 0x1F802070u;
// Interrupt controller (ps2tek): I_STAT pending sources (writing 0 to a bit
// acknowledges it, 1 has no effect), I_MASK enabled sources, I_CTRL global
// enable in bit 0 ("Reading this register returns bit 0 AND clears it").
constexpr std::uint32_t kIStat = 0x1F801070u;
constexpr std::uint32_t kIMask = 0x1F801074u;
constexpr std::uint32_t kICtrl = 0x1F801078u;
// I_STAT lines (ps2tek "IOP Interrupts"). The timers, VBLANK start/end (from
// the GS vertical clock, when known), CDVD and DMA raise them here; every other
// source is unknown, so its bits are never reported as 0.
constexpr std::array<unsigned, 6> kTimerIrq{4u, 5u, 6u, 14u, 15u, 16u};
constexpr std::uint32_t kTimerIrqs = (1u << 4) | (1u << 5) | (1u << 6) | (1u << 14) | (1u << 15) | (1u << 16);
constexpr std::uint32_t kVblankStartIrq = 1u << 0, kVblankEndIrq = 1u << 11;
// IRQ 3 (DMA) follows the DICR master flag (ps2tek: bus error bit 15, or
// master enable bit 23 with a channel flag in DICR 24-30 or DICR2 24-29).
// Flags are raised by the modeled SIF/CDVD transfers. Unsupported DMA starts
// refuse before changing channel state; they cannot manufacture completion.
constexpr std::uint32_t kDmaIrq = 1u << 3;
constexpr const char* kIrqNames[] = {"VBLANK start", "GPU", "CDVD", "DMA", "timer 0", "timer 1", "timer 2", "SIO0",
                                     "SIO1", "SPU2", "PIO", "VBLANK end", "DVD", "PCMCIA", "timer 3", "timer 4",
                                     "timer 5", "SIO2", "HTR0", "HTR1", "HTR2", "HTR3", "USB", "EXTR", "FWRE", "FDMA"};
// Timers (ps2tek): count +0, mode +4, target +8. Mode bits: 0 gate enable,
// 3 zero return, 4 compare IRQ, 5 overflow IRQ, 6 repeat, 7 LEVL (toggle),
// 8 external clock, 9 timer 2 prescaler (1/8), 10 IRQ enabled, 11 compare
// raised, 12 overflow raised, 13-14 timer 4/5 prescaler (1, 8, 16, 256).
constexpr std::array<std::uint32_t, 6> kTimerBase{0x1F801100u, 0x1F801110u, 0x1F801120u,
                                                  0x1F801480u, 0x1F801490u, 0x1F8014A0u};
constexpr std::uint32_t kModeGate = 1u << 0, kModeZeroReturn = 1u << 3, kModeCompareIrq = 1u << 4,
                        kModeOverflowIrq = 1u << 5, kModeRepeat = 1u << 6, kModeLevl = 1u << 7,
                        kModeExternal = 1u << 8, kModeIrqEnabled = 1u << 10, kModeCompareFlag = 1u << 11,
                        kModeOverflowFlag = 1u << 12;
std::optional<unsigned> timer_index(std::uint32_t physical) {
    for (unsigned i = 0; i < kTimerBase.size(); ++i)
        if (physical >= kTimerBase[i] && physical < kTimerBase[i] + 0x10u) return i;
    return std::nullopt;
}
// DMA interrupt control (ps2tek). DICR: completion mode 0-6, bus error flag
// 15, channel masks 16-22, master enable 23, channel flags 24-30 (write 1 to
// clear), master flag 31. DICR2: tag interrupt enables 0-12 (only 4, 9 and 10
// settable), masks of channels 7-12 in 16-21, their flags in 24-29. A flag is
// raised by modeled channel transfers; unmodeled channel starts still fail.
// CDVD N command status (ps2tek bits: 0 error, 2 DEV9 device connected, 4
// test mode, 5 power-off ready, 6 drive ready, 7 busy; bit 3 is "unknown"
// there and MECHA_INIT in PCSX2, which EECONF waits for before reading the
// MECHACON configuration). No N command can be issued here (0x1F402004 is not
// modeled), so busy and error are 0. Bit 3 and bit 6 set and bits 4/5 clear
// once the MECHACON has started are hypotheses (PCSX2 shows the same); bit 2:
// every console profile is a slim model, whose network adapter is on the
// mainboard (fact).
constexpr std::uint32_t kCdvdNStatus = 0x1F402005u;
// Expansion-bay device window (IOP 0xB6000000; ps2sdk's arcade drivers put
// their ATA registers there). EECONF writes 0 to 0xB600000A on consoles whose
// DEV9_R_REV high nibble is 3 (expansion bay); what the slim models decode
// there is not established, so the write is recorded as an event (no other
// effect) and reads stop the IOP.
constexpr std::uint32_t kExpansionBayRegister = 0x1600000Au;
// SIF registers, IOP side (ps2tek "SIF Registers"; ps2sdk sif_mmio_hwport.h).
constexpr std::uint32_t kSifMscom = 0x1D000000u, kSifSmcom = 0x1D000010u, kSifMsflg = 0x1D000020u,
                        kSifSmflg = 0x1D000030u;
constexpr std::uint32_t kSifBd6 = 0x1D000060u;
// i.LINK (IEEE 1394) link controller, ps2sdk iop_mmio_hwport.h names. EECONF
// (2.20+ ROMs; ps2sdk eeconf.c) checks the high nibble of register 0x7C for 1,
// then starts a bus reset through the PHY and waits for NodeID bit 0.
constexpr std::uint32_t kIlinkNodeId = 0x1F808400u;
constexpr std::uint32_t kIlinkPhyAccess = 0x1F808414u;
constexpr std::uint32_t kIlinkIntr0 = 0x1F808420u, kIlinkIntr1 = 0x1F808428u, kIlinkIntr2 = 0x1F808430u;
constexpr std::uint32_t kIlinkUbufReceiveClear = 0x1F80844Cu;
constexpr std::uint32_t kIlinkRegister7C = 0x1F80847Cu;
// Hypothesis: 0x10000001, the value a PCSX2 developer reports reading on their
// own (unnamed) retail console; the i.LINK PHY answers on every SCPH-7xxxx and
// SCPH-9xxxx in the PS2Ident database, so the controller is present.
constexpr std::uint32_t kIlinkRegister7CValue = 0x10000001u;
constexpr std::uint32_t kPhyWrite = 0x40000000u, kPhyRead = 0x80000000u;
constexpr std::uint8_t kCdvdNStatusIdle = 0x4Cu;
// CDVD S commands (ps2tek): parameters are written to 0x1F402017 before the
// command byte goes to 0x1F402016; 0x1F402017 reads bit 7 busy and bit 6 "no
// result data"; results are read from 0x1F402018. Commands complete before
// the next access (hypothesis: MECHACON timing is not modeled).
constexpr std::uint32_t kCdvdSCommand = 0x1F402016u;
constexpr std::uint32_t kCdvdSStatus = 0x1F402017u;
constexpr std::uint32_t kCdvdSResult = 0x1F402018u;
constexpr std::uint8_t kSStatusNoResult = 0x40u;
constexpr std::uint32_t kDicr = 0x1F8010F4u;
constexpr std::uint32_t kDicrWritable = 0x00FF007Fu;
constexpr std::uint32_t kDicr2 = 0x1F801574u;
constexpr std::uint32_t kDicr2Writable = 0x003F0610u;

// COP0 (R3000A): BPC 3, BDA 5, JUMPDEST 6, DCIC 7, BadVaddr 8, BDAM 9,
// BPCM 11, SR 12, Cause 13, EPC 14, PRId 15.
enum Cop0Reg : unsigned { Bpc = 3, Bda = 5, Jumpdest = 6, Dcic = 7, BadVaddr = 8, Bdam = 9, Bpcm = 11,
                          Sr = 12, Cause = 13, Epc = 14, Prid = 15 };
constexpr std::uint32_t kSrIec = 1u << 0;
constexpr std::uint32_t kSrKuc = 1u << 1;
constexpr std::uint32_t kSrIsc = 1u << 16;
constexpr std::uint32_t kSrSwc = 1u << 17;
constexpr std::uint32_t kSrBev = 1u << 22;
constexpr std::uint32_t kSrRe = 1u << 25;

// Bus configuration registers the IOP reset code writes from its tables
// (memory control and SSBUS delay/base registers, DMA priority, SIO2 port
// setup, cache configuration). Latched as written; they set bus timing and
// decoding, which is not modeled. Reads before a write are refused, except
// the two reset values below.
constexpr std::uint32_t kLatches[] = {
    0x1F801000u, 0x1F801004u, 0x1F801008u, 0x1F80100Cu, 0x1F801010u, 0x1F801014u, 0x1F801018u, 0x1F80101Cu,
    0x1F801020u, 0x1F801060u, kIMask, kICtrl, 0x1F8010F0u, kDicr, 0x1F801400u, 0x1F801404u, 0x1F801408u, 0x1F80140Cu, 0x1F801410u,
    0x1F801414u, 0x1F801418u, 0x1F80141Cu, 0x1F801420u, 0x1F801450u, 0x1F801570u, kDicr2, 0x1F801578u, 0x1F80157Cu, 0x1F8015F0u, 0x1F808240u, 0x1F808244u,
    0x1F808248u, 0x1F80824Cu, 0x1F808250u, 0x1F808254u, 0x1F808258u, 0x1F80825Cu, 0x1F808268u, 0xFFFE0130u,
    0xFFFE0140u, 0xFFFE0144u,
};
// DMA channel registers (ps2tek): channels 0-6 at 0x1F801080 + n*0x10,
// 7 onwards at 0x1F801500 + (n-7)*0x10 (DMACMAN's register table goes up to
// 0x1F801568); MADR +0, BCR +4, CHCR +8, TADR +0xC.
// Latched as written while no transfer starts: a CHCR write with the
// start/busy bit 24 set is refused, so the busy bit of every latched CHCR is 0.
constexpr std::uint32_t kChcrStart = 1u << 24;
std::optional<unsigned> dma_channel_field(std::uint32_t address) {
    if (address >= 0x1F801080u && address < 0x1F8010F0u) return (address & 0xFu) / 4u;
    if (address >= 0x1F801500u && address < 0x1F801570u) return (address & 0xFu) / 4u;
    return std::nullopt;
}
bool is_spu2_voice_routing(std::uint32_t address) {
    if(address<0x1F900000u || address>=0x1F900800u)return false;
    const auto reg=(address-0x1F900000u)&0x3FFu;
    return reg>=0x180u && reg<=0x198u && !(reg&1u);
}
bool is_spu2_effect_address(std::uint32_t address) {
    if(address<0x1F900000u || address>=0x1F900800u)return false;
    const auto reg=(address-0x1F900000u)&0x3FFu;
    return reg==0x2E0u || reg==0x2E2u || reg==0x33Cu;
}
bool is_spu2_voice_parameter(std::uint32_t address) {
    return address>=0x1F900000u && address<0x1F900800u &&
        (address&0x3FFu)<0x180u && (address&15u)<=8u && !(address&1u);
}
bool is_spu2_voice_start(std::uint32_t address) {
    if(address<0x1F900000u || address>=0x1F900800u)return false;
    const auto reg=address&0x3FFu;
    return reg>=0x1C0u && reg<0x2E0u && (reg-0x1C0u)%12u<=2u && !(reg&1u);
}
bool is_spu2_effect_coefficient(std::uint32_t address) {
    return address>=0x1F900760u && address<0x1F9007B0u && !(address&1u) &&
           (address-0x1F900760u)%0x28u>=0x14u;
}
bool is_spu2_effect_offset(std::uint32_t address) {
    return address>=0x1F900000u && address<0x1F900800u && !(address&1u) &&
           (address&0x3FFu)>=0x2E4u && (address&0x3FFu)<=0x33Au;
}
bool is_spu2_boot_config(std::uint32_t address) {
    if(is_spu2_effect_coefficient(address) || is_spu2_effect_offset(address))return true;
    if(is_spu2_voice_parameter(address) || is_spu2_voice_start(address))return true;
    if(is_spu2_voice_routing(address) || is_spu2_effect_address(address))return true;
    if(address==0x1F9007C0u || address==0x1F9007C6u || address==0x1F9007C8u)return true;
    // PS2SDK spu2_mmio_hwport.h: two banks, 0x28 apart, containing
    // master, effect, external-input and sound-input L/R levels.
    return address>=0x1F900760u && address<0x1F9007B0u && !(address&1u) &&
           (address-0x1F900760u)%0x28u<0x10u;
}
bool is_latch(std::uint32_t address) {
    // SEND3 is sixteen command descriptors. Programming them does not
    // start serial activity; CTRL.0 and both DMA starts still refuse.
    // ps2tek SIO2 registers; ps2sdk sio2_regN_set/get (32-bit MMIO).
    return (address & 3u) == 0u && ((address >= 0x1F808200u && address <= 0x1F80823Cu) ||
           dma_channel_field(address) ||
           std::find(std::begin(kLatches), std::end(kLatches), address) != std::end(kLatches));
}
} // namespace

Iop::Iop() : ram_(kIopRamBytes), ram_written_(kIopRamBytes) {}

void Iop::reset(std::span<const std::uint8_t> rom0, const ConsoleHardware* hardware, const NvmImage* nvm,
                bool diagnostic_sbus_latches, std::optional<std::uint32_t> diagnostic_dmacinten,
                std::span<const std::uint8_t> rom1, bool diagnostic_sio2_irq_clear,
                bool diagnostic_spu2_quiescent) {
    if(diagnostic_dmacinten && (*diagnostic_dmacinten&~3u))
        throw DeviceRefusal("diagnostic IOP DMACINTEN supports only bits 0-1");
    rom_.assign(rom0.begin(), rom0.end());
    rom1_.assign(rom1.begin(),rom1.end());
    std::fill(ram_.begin(), ram_.end(), std::uint8_t{0});
    std::fill(ram_written_.begin(), ram_written_.end(), std::uint8_t{0});
    gpr_ = {};
    gpr_sio2_epoch_={};sio2_control_epoch_=undefined_sio2_epoch_=0;
    diagnostic_sio2_port_timing_.reset();
    sio2_digital_pads_={};sio2_response_status_=0;
    sio2_input_.clear();sio2_output_.clear();sio2_complete_at_.reset();
    diagnostic_sio2_timeout_cycles_=sio2_reply_bytes_=sio2_interrupt_enable_=0;
    sio2_completion_flag_=sio2_response_valid_=false;sio2_diagnostic_control_.reset();
    gpr_defined_ = 1u;
    gpr_origin_.fill("undefined since reset");
    hi_.reset();
    lo_.reset();
    pc_ = kResetVector;
    branch_target_.reset();
    in_delay_slot_ = false;
    pending_load_.reset();
    write_.reset();
    cop0_ = {};
    // R3000A reset: BEV = 1, kernel mode, interrupts disabled; the other SR
    // bits are taken as 0 (hypothesis: the reset code writes SR before it
    // depends on them, except IsC/SwC, which must be clear to run from ROM).
    cop0_[Sr] = kSrBev;
    hardware_ = hardware;
    nvm_ = nvm;
    s_params_.clear();
    s_command_.reset();
    s_results_.clear();
    config_.reset();
    ilink_bus_reset_ = false;
    timers_ = {};
    timers_running_ = false;
    istat_ = 0u;
    sio2_irq_acknowledged_=diagnostic_sio2_irq_clear;
    diagnostic_spu2_quiescent_=diagnostic_spu2_quiescent;
    diagnostic_spu2_7ca_latch_=false;
    diagnostic_spu2_reserved_latches_=false;
    spu2_control_={};spu2_symbolic_voices_={};spu2_keys_={};
    spu2_endx_.fill(ObservedWord{0u,0xFFFFFFu});
    spu2_release_upper_={};spu2_release_pending_={};
    diagnostic_spu2_effect_end_.reset();
    diagnostic_spu2_effect_start_.reset();
    diagnostic_spu2_effect_address_reserved_zero_=false;
    diagnostic_spu2_inverted_effect_area_inactive_=false;
    spu2_effect_area_dirty_={};
    spu2_next_sample_={};spu2_last_sample_={};spu2_sample_count_={};spu2_clock_active_=false;
    spu2_transfer_={};spu2_ram_.clear();spu2_ram_written_.clear();diagnostic_spu2_pio_cycles_=0;
    spu2_adma_={};diagnostic_spu2_adma_cycles_=0;diagnostic_spu2_admas_control_latch_=false;
    spu2_adma_bus_available_=0;spu2_adma_last_grant_=1u;
    gif_dma_cycle_=0;
    gif_dma_stop_.reset();
    dma_irq_output_=false;
    // Cold drive: no command or host tray/power event has been submitted.
    // Only NOP/NOPsync completion is modeled; other commands fail before issue.
    cdvd_irq_=0; cdvd_ncommand_.reset(); cdvd_complete_at_.reset();
    disc_.reset();cdvd_type_=cdvd_status_=cdvd_sticky_=0;
    diagnostic_cav_nominal_timing_=false;
    cdvd_howto_.reset();cdvd_params_.clear();cdvd_lba_=cdvd_sectors_=0;
    cdvd_sector_cycles_=0;cdvd_sector_at_=0;cdvd_sector_offset_=2048;
    ee_dma_suspended_=hardware && hardware->d_enabler && (*hardware->d_enabler&0x10000u);
    // SIF: the communication registers are unknown until written. The flags
    // are 0 (hypothesis: SIFMAN and the EE kernel each wait for the other side
    // to set a flag, which presumes it clear at power-on; PCSX2 also starts
    // them at 0).
    sif_mscom_.reset();
    sif_smcom_.reset();
    sif_msflg_ = 0u;
    sif_smflg_ = 0u;
    diagnostic_sbus_latches_=diagnostic_sbus_latches;
    diagnostic_sif_ready_=0;
    sbus_control_write_.reset(); sbus_bd6_write_.reset();
    video_generation_.reset();
    video_ = {};
    gpr_known_ = {};
    latches_.clear();
    sio2_irq_output_=false;diagnostic_sio2_reset_completion_=false;
    sio2_pending_reply_.clear();diagnostic_sio2_unplugged_ports_=0;diagnostic_sio2_infrared_absent_=false;
    sio2_dma_={};diagnostic_sio2_dma_cycles_=0;sio2_dma_next_=0;sio2_dma_turn_=0;
    if(diagnostic_dmacinten)latches_.push_back({0x1F80157Cu,*diagnostic_dmacinten});
    // Reset values the reset code reads before writing (hypotheses):
    // 0x1F801010 (boot ROM delay/size): the code keeps only bit 12 (ROM bus
    // width, set by the board) and writes the rest; taken as 0, the width the
    // code itself writes. 0x1F801450: bit 3 selects the PS1-mode path and bit 31
    // an EE handshake through SIF; both 0 at power-on in PS2 mode.
    latches_.push_back({0x1F801010u, 0u});
    latches_.push_back({0x1F801450u, 0u});
    // I_CTRL: INTRMAN's CpuSuspendIntr reads it before anything writes it (no
    // write precedes it in the three BIOS); taken as 0, interrupts disabled at
    // power-on like SR.IEc (hypothesis, also what PCSX2 shows).
    latches_.push_back({kICtrl, 0u});
    cycle_ = 0;
    stop_.reset();
    stop_cycle_ = 0;
    events_.clear();
    ee_ram_ = {};
    ee_ram_unknown_ = nullptr;
    ee_dctrl_ = ee_dstat_ = ee_sif1_chcr_ = ee_sif1_madr_ = ee_sif1_qwc_ = 0;
    ee_dma_setup_ = {}; ee_dma_global_ = {};
    // EE User's Manual v6 p.66: PCE resets to zero; CDE/CPC reset
    // values are unspecified. Disabled priority makes unknown CDE inert.
    ee_dma_global_[0] = 0u;
    ee_dma_global_unknown_ = {0x03FF03FFu, 0u, 0u, 0u};
    iop_sif1_ = {};
    sif1_fifo_.clear();
    sif1_have_tag_ = sif1_end_ = sif1_tag_irq_ = false;
    ee_sif1_chain_end_=false;
    sif1_words_ = 0;
    sif1_metrics_ = {};
    iop_sif0_ = {}; iop_sif0_written_=0; sif0_fifo_.clear(); sif0_source_words_=0;
    sif0_source_tag_=sif0_source_end_=sif0_source_irq_=sif0_dest_end_=false; sif0_metrics_={};
    sif0_last_qword_={};
    if (rom_.size() != kRomBytes) stop_ = "no ROM0 mapped for the IOP";
}

void Iop::load_program(std::span<const std::uint8_t> bytes, std::uint32_t address, std::uint32_t entry) {
    if ((address & 3u) || (entry & 3u) || address >= kIopRamBytes || bytes.empty() ||
        bytes.size() > kIopRamBytes - address || entry < address ||
        static_cast<std::uint64_t>(entry) + 4u > address + bytes.size())
        throw std::runtime_error("invalid IOP bare-metal image range or entry");
    const auto ee_ram = ee_ram_;
    auto* ee_ram_unknown = ee_ram_unknown_;
    reset({}, nullptr);
    ee_ram_ = ee_ram;
    ee_ram_unknown_ = ee_ram_unknown;
    std::copy(bytes.begin(), bytes.end(), ram_.begin() + address);
    std::fill_n(ram_written_.begin() + address, bytes.size(), std::uint8_t{1});
    pc_ = entry;
    stop_.reset();
    // Documented development environment: known registers, no firmware services.
    gpr_defined_ = ~0u;
    cop0_[Sr] = 0u;
    hi_ = lo_ = 0u;
}

namespace {
constexpr std::array<std::uint32_t, 10> kEeDmaBases{
    0x10008000u,0x10009000u,0x1000A000u,0x1000B000u,0x1000B400u,
    0x1000C000u,0x1000C400u,0x1000C800u,0x1000D000u,0x1000D400u};
std::optional<std::pair<unsigned,unsigned>> ee_dma_field(std::uint32_t address) {
    for (unsigned i=0;i<kEeDmaBases.size();++i) {
        if (address>=kEeDmaBases[i] && address<=kEeDmaBases[i]+0x80u && !(address&15u)) {
            const auto field=(address-kEeDmaBases[i])/16u;
            if (field<=5u || field==8u) return std::pair{i,field};
        }
    }
    return std::nullopt;
}
bool ee_dma_field_exists(unsigned channel,unsigned field) {
    if(field<=2u)return true;
    if(field==3u)return channel<=2u || channel==4u || channel==6u || channel==9u;
    if(field<=5u)return channel<=2u;
    return channel>=8u;
}
std::optional<bool> ee_dma_priority(ObservedWord pcr, unsigned channel) {
    const auto enabled=1u<<(16u+channel);
    if (!(pcr.unknown&0x80000000u) && !(pcr.value&0x80000000u)) return true;
    if (!(pcr.unknown&enabled) && (pcr.value&enabled)) return true;
    if (!(pcr.unknown&(0x80000000u|enabled))) return false;
    return std::nullopt;
}
}
bool Iop::ee_dma_address(std::uint32_t address) {
    return ee_dma_field(address).has_value() ||
        (address>=0x1000E000u && address<=0x1000E050u && !(address&15u));
}
std::uint32_t Iop::ee_dma_read(std::uint32_t address) const {
    switch (address) {
        case 0x1000E000u: return ee_dctrl_;
        case 0x1000E010u: return ee_dstat_;
        case 0x1000C400u: return ee_sif1_chcr_;
        case 0x1000C410u: return ee_sif1_madr_;
        case 0x1000C420u: return ee_sif1_qwc_;
        default: break;
    }
    if(address>=0x1000E020u && address<=0x1000E050u && !(address&15u)) {
        const auto index=(address-0x1000E020u)/16u;
        const auto& value=ee_dma_global_[index];
        if(!value)throw DeviceRefusal("EE DMAC global register read before write");
        if(ee_dma_global_unknown_[index])throw DeviceRefusal("EE DMAC global read consumes unknown reset bits");
        return *value;
    }
    if(const auto f=ee_dma_field(address)) {
        const auto [channel,field]=*f;
        if(!ee_dma_field_exists(channel,field))throw DeviceRefusal("EE DMAC absent channel register read");
        const auto& value=ee_dma_setup_[channel][field];
        if(!value)throw DeviceRefusal("EE DMAC channel register read before write");
        return *value;
    }
    throw DeviceRefusal("EE DMAC register not modeled");
}
void Iop::ee_dma_write(std::uint32_t address, std::uint32_t value) {
    if (address == 0x1000E000u) {
        if (value & ~1u) throw DeviceRefusal("D_CTRL: only DMAE supported (no stall/MFIFO/arbitration)");
        ee_dctrl_ = value; return;
    }
    if (address == 0x1000E010u) {
        // Other channels cannot start until their engines exist, so they
        // cannot fabricate completion/stall flags. Masks are independent.
        ee_dstat_ = (ee_dstat_ & ~(value & 0xE3FFu)) ^ (value & 0x63FF0000u); return;
    }
    if(address>=0x1000E020u && address<=0x1000E050u && !(address&15u)) {
        constexpr std::uint32_t masks[]{0x83FF03FFu,0x00FF00FFu,0x7FFFFFF0u,0x7FFFFFF0u};
        const auto index=(address-0x1000E020u)/16u;
        ee_dma_global_[index]=value&masks[index]; ee_dma_global_unknown_[index]=0; return;
    }
    const auto decoded=ee_dma_field(address);
    if(!decoded)throw DeviceRefusal("EE DMAC register not modeled");
    const auto [channel,field]=*decoded;
    // EE User's Manual p.62: suspend via CPND, then clear STR before
    // reconfiguring an active channel. A stopped transfer is not completion.
    const bool suspended_stop=ee_dma_suspended_ && field==0u && !(value&0x100u);
    // A receiver waiting for a tag has no transfer in process: in this
    // qword-atomic bus model, STR can be cleared without losing a bus beat.
    // Do not extend this to a payload in progress or synthesize its end IRQ.
    const bool waiting_receiver_stop=channel==5u && field==0u && value==0u &&
        ee_dma_setup_[5][2].value_or(0u)==0u && sif0_fifo_.empty();
    if(channel==5u && (ee_dma_setup_[5][0].value_or(0u)&0x100u) && !suspended_stop && !waiting_receiver_stop)
        throw DeviceRefusal("EE SIF0 DMA register write while busy");
    if(channel==2u && (ee_dma_setup_[2][0].value_or(0u)&0x100u) && !suspended_stop)
        throw DeviceRefusal("EE GIF DMA register write while busy");
    if(channel==6u && (ee_sif1_chcr_ & 0x100u) && !suspended_stop) throw DeviceRefusal("EE SIF1 DMA register write while busy");
    if(!ee_dma_field_exists(channel,field)) {
        // SDK dma_channel_initialize also clears unused channel slots. Only
        // inert zero stores are accepted; no value for an absent read is made up.
        if(value)throw DeviceRefusal("EE DMAC nonzero write to absent channel register");
        return;
    }
    if (address == 0x1000C400u) {
        const bool chain=(value&0xCu)==4u;
        if((chain && (value&~0xFFFF0185u)) ||
           (!chain && value!=0u && value!=1u && value!=0x101u))
            throw DeviceRefusal("EE SIF1: only normal or source chain mode without TTE supported");
        if(chain && (value&0x100u) && (ee_sif1_qwc_ || !ee_dma_setup_[6][3]))
            throw DeviceRefusal("EE SIF1 chain requires programmed TADR and zero initial QWC (resume not modeled)");
        const auto size = static_cast<std::size_t>(ee_sif1_qwc_) * 16u;
        if ((value & 0x100u) && (ee_sif1_madr_ > ee_ram_.size() || size > ee_ram_.size() - ee_sif1_madr_))
            throw DeviceRefusal("EE SIF1 DMA source outside main RAM");
        if ((value&0x100u) && !ee_dma_priority(ee_dma_observe(0x1000E020u),6u).has_value())
            throw DeviceRefusal("EE SIF1 priority depends on unknown D_PCR reset bits");
        ee_sif1_chcr_ = value; ee_sif1_chain_end_=!chain; return;
    }
    if (address == 0x1000C410u) {
        if (value & 15u) throw DeviceRefusal("EE SIF1 MADR must be qword aligned (scratchpad unsupported)");
        ee_sif1_madr_ = value; return;
    }
    if (address == 0x1000C420u) { ee_sif1_qwc_ = value & 0xFFFFu; return; }
    if(channel==5u && field==0u && (value&0x100u)) {
        if((value&~0xFFFF0184u) || (value&0xCu)!=4u)
            throw DeviceRefusal("EE SIF0 requires destination chain mode (no scratchpad/stall)");
        if(!ee_dma_setup_[5][2] || *ee_dma_setup_[5][2])
            throw DeviceRefusal("EE SIF0 start requires programmed zero QWC (resume not modeled)");
        ee_dma_setup_[5][0]=value; sif0_dest_end_=false; return;
    }
    if(channel==2u && field==0u && (value&0x100u)) {
        if(value!=0x101u)throw DeviceRefusal("EE GIF DMA supports normal RAM-to-GIF mode only");
        const auto madr=ee_dma_setup_[2][1],qwc=ee_dma_setup_[2][2];
        if(!madr || !qwc)throw DeviceRefusal("EE GIF DMA requires programmed MADR and QWC");
        if(*madr>ee_ram_.size() || std::uint64_t{*qwc}*16u>ee_ram_.size()-*madr)
            throw DeviceRefusal("EE GIF DMA source outside main RAM (scratchpad unsupported)");
        if(!ee_dma_priority(ee_dma_observe(0x1000E020u),2u).has_value())
            throw DeviceRefusal("EE GIF DMA priority is unknown");
        ee_dma_setup_[2][0]=value;return;
    }
    if(field==0u && (value&0x100u))
        throw DeviceRefusal("EE DMAC channel "+std::to_string(channel)+" transfer engine is not modeled");
    constexpr std::uint32_t masks[]{0xFFFF01FDu,0xFFFFFFF0u,0xFFFFu,0xFFFFFFF0u,
                                   0xFFFFFFF0u,0xFFFFFFF0u,0u,0u,0x3FF0u};
    if(field!=0u && field!=2u && (value&15u))throw DeviceRefusal("EE DMAC address must be qword aligned");
    const auto mask=(field==1u && channel>=8u) ? 0x7FFFFFF0u : masks[field];
    ee_dma_setup_[channel][field]=value&mask;
}
ObservedWord Iop::ee_dma_observe(std::uint32_t address) const {
    if(address==0x1000E020u) return {*ee_dma_global_[0], ee_dma_global_unknown_[0]};
    if(const auto f=ee_dma_field(address); f && f->second==0u && f->first!=6u &&
       !ee_dma_setup_[f->first][0]) {
        // EE User's Manual v6, p.74: STR starts clear. The other writable
        // fields are not given reset values here; preserve them as symbolic.
        // Observing idle must not invent a mode, tag, or programmed address.
        return {0u,0xFFFF00FDu};
    }
    return {ee_dma_read(address), 0u};
}
void Iop::ee_dma_write_observed(std::uint32_t address, ObservedWord word) {
    if(!word.unknown) {ee_dma_write(address,word.value); return;}
    if(address!=0x1000E020u)throw DeviceRefusal("unknown MMIO bits cannot be stored in this DMA register");
    if((ee_sif1_chcr_&0x100u) && !ee_dma_priority(word,6u).has_value())
        throw DeviceRefusal("active EE SIF1 priority would become unknown");
    ee_dma_global_[0]=word.value&~word.unknown&0x83FF03FFu;
    ee_dma_global_unknown_[0]=word.unknown&0x83FF03FFu;
}
std::uint32_t Iop::iop_sif1_read(std::uint32_t address) const {
    return iop_sif1_.at((address - 0x1F801530u) / 4u);
}
void Iop::iop_sif1_write(std::uint32_t address, std::uint32_t value) {
    const auto field = (address - 0x1F801530u) / 4u;
    // Clearing an armed receiver waiting for its next tag does not discard
    // any bus beat. Stopping a payload or buffered stream remains unmodeled.
    const bool idle_stop=field==2u && value==0u && !sif1_have_tag_ &&
        !sif1_words_ && sif1_fifo_.empty();
    if((iop_sif1_[2]&kChcrStart) && !idle_stop)
        throw IopStop{"IOP SIF1 register write while busy address="+hex(address)+" value="+hex(value)+
                      " remaining="+std::to_string(sif1_words_)+" fifo="+std::to_string(sif1_fifo_.size())};
    // SIF1 is a destination-tag stream. SDK sceSifSetDChain arms it with
    // 0x41000300 (including snooping; this model has coherent IOP memory).
    // Keep the documented synthetic chain configuration too; other DMA modes
    // remain refused instead of treating an arbitrary CHCR as success.
    if (field == 2u && value != 0u && value != 0x01000600u && value != 0x41000300u)
        throw IopStop{"IOP SIF1: only destination chain mode without TTE supported"};
    iop_sif1_[field] = value;
}

void Iop::advance_gif_dma(std::uint64_t ee_cycle,Gif& gif,GsRegisters& gs) {
    if(gif_dma_stop_)throw DeviceRefusal(*gif_dma_stop_);
    auto& chcr=ee_dma_setup_[2][0];auto& madr=ee_dma_setup_[2][1];auto& qwc=ee_dma_setup_[2][2];
    if(!(chcr.value_or(0u)&0x100u) || ee_dma_suspended_ || !(ee_dctrl_&1u)) {
        gif_dma_cycle_=ee_cycle;return;
    }
    const auto priority=ee_dma_priority(ee_dma_observe(0x1000E020u),2u);
    if(!priority)throw DeviceRefusal("EE GIF DMA priority is unknown");
    if(!*priority){gif_dma_cycle_=ee_cycle;return;}
    // Functional throughput hypothesis: one qword per BUSCLK. The existing
    // GIF parser refuses unmodeled masking/arbitration instead of dropping data.
    while(ee_cycle>=gif_dma_cycle_ && ee_cycle-gif_dma_cycle_>=kEeCyclesPerBusCycle) {
        if(*qwc) {
            if(ee_ram_unknown_)
                for(unsigned w=0;w<4;++w)if(ee_ram_unknown_->contains(*madr+w*4u))
                    throw DeviceRefusal("EE GIF DMA source consumes unknown RAM bits");
            std::uint64_t low=0,high=0;
            for(unsigned b=0;b<8;++b) {
                low|=std::uint64_t{ee_ram_[*madr+b]}<<(8u*b);
                high|=std::uint64_t{ee_ram_[*madr+8u+b]}<<(8u*b);
            }
            try {gif.write_fifo(low,high,gs);}
            catch(const DeviceRefusal& error){gif_dma_stop_=error.what();throw;}
            *madr+=16u;--*qwc;
        }
        gif_dma_cycle_+=kEeCyclesPerBusCycle;
        if(!*qwc){*chcr&=~0x100u;ee_dstat_|=1u<<2u;gif_dma_cycle_=ee_cycle;return;}
    }
}

void Iop::pump_sif1() {
    // Experimental throughput: one qword per IOP instruction, bounded at 32
    // words. It preserves backpressure/order, not physical bus latency.
    bool source_enabled=!ee_dma_suspended_ && (ee_dctrl_&1u) && (ee_sif1_chcr_&0x100u);
    if(source_enabled) {
        const auto priority=ee_dma_priority(ee_dma_observe(0x1000E020u),6u);
        if(!priority)throw IopStop{"EE SIF1 priority is unknown"};
        source_enabled=*priority;
    }
    if (source_enabled) {
        if((ee_sif1_chcr_&4u) && !ee_sif1_qwc_ && !ee_sif1_chain_end_) {
            auto& tadr=ee_dma_setup_[6][3];
            if(!tadr || (*tadr&15u) || *tadr>ee_ram_.size() || ee_ram_.size()-*tadr<16u)
                throw IopStop{"EE SIF1 source tag outside aligned main RAM"};
            if(ee_ram_unknown_ && (ee_ram_unknown_->contains(*tadr) || ee_ram_unknown_->contains(*tadr+4u)))
                throw IopStop{"EE SIF1 source tag consumes unknown RAM bits"};
            std::uint32_t tag=0,address=0;
            for(unsigned b=0;b<4;++b) {tag|=std::uint32_t{ee_ram_[*tadr+b]}<<(8*b);address|=std::uint32_t{ee_ram_[*tadr+4+b]}<<(8*b);}
            const auto id=(tag>>28u)&7u, qwc=tag&0xFFFFu;
            if((id!=0u && id!=2u && id!=3u) || (tag&0x0C000000u))
                throw IopStop{"EE SIF1 source chain: only REFE/NEXT/REF without priority change modeled"};
            // NEXT sends the qwords following this tag, then fetches the tag
            // at ADDR. A zero-QWC NEXT still changes TADR without completion.
            const auto source=id==2u ? *tadr+16u : address;
            if((source&15u) || source>ee_ram_.size() || std::uint64_t{qwc}*16u>ee_ram_.size()-source)
                throw IopStop{"EE SIF1 referenced source outside aligned main RAM"};
            if(id==2u && ((address&15u) || address>ee_ram_.size() || ee_ram_.size()-address<16u))
                throw IopStop{"EE SIF1 NEXT tag outside aligned main RAM"};
            ee_sif1_chain_end_=id==0u || ((tag&0x80000000u) && (ee_sif1_chcr_&0x80u));
            ee_sif1_chcr_=(ee_sif1_chcr_&0xFFFFu)|(tag&0xFFFF0000u);
            ee_sif1_madr_=source; ee_sif1_qwc_=qwc; *tadr=id==2u ? address : *tadr+16u;
            events_.push_back({cycle_,"SIF1 EE source tag id="+std::to_string(id)+" qwc="+std::to_string(qwc)});
        }
        if (ee_sif1_qwc_ && sif1_fifo_.size() <= 28u) {
            // Refuse the whole qword before enqueuing any placeholder bytes.
            if (ee_ram_unknown_ && !ee_ram_unknown_->empty())
                for (unsigned w = 0; w < 4u; ++w)
                    if (ee_ram_unknown_->contains(ee_sif1_madr_ + w * 4u))
                        throw IopStop{"EE SIF1 source consumes unknown RAM bits"};
            for (unsigned w = 0; w < 4u; ++w) {
                std::uint32_t word = 0;
                for (unsigned b = 0; b < 4u; ++b)
                    word |= std::uint32_t{ee_ram_[ee_sif1_madr_ + w * 4u + b]} << (8u * b);
                sif1_fifo_.push_back(word);
            }
            ee_sif1_madr_ += 16u;
            --ee_sif1_qwc_;
            sif1_metrics_.peak_fifo_words = std::max(sif1_metrics_.peak_fifo_words,sif1_fifo_.size());
        } else if (ee_sif1_qwc_) {
            ++sif1_metrics_.source_full_cycles;
        }
        if (!ee_sif1_qwc_ && ee_sif1_chain_end_) {
            ee_sif1_chcr_ &= ~0x100u;
            ee_dstat_ |= 0x40u;
            events_.push_back({cycle_, "SIF1 EE source complete (FIFO may still contain data)"});
        }
    }
    const auto enabled = [&](std::uint32_t address, std::uint32_t mask) {
        const auto* value = latch(address);
        return value && value->has_value() && ((**value & mask) == mask);
    };
    if (!(iop_sif1_[2] & kChcrStart) || !enabled(0x1F801578u, 1u) ||
        !enabled(0x1F801570u, 0x8000u)) return;
    if (!sif1_have_tag_) {
        if (sif1_fifo_.size() < 4u) { ++sif1_metrics_.destination_empty_cycles; return; }
        const auto tag = sif1_fifo_[0], words = sif1_fifo_[1];
        if ((tag & 0x3F000000u) || (words & 0xFF000003u) || sif1_fifo_[2] || sif1_fifo_[3])
            throw IopStop{"SIF1 destination tag: reserved bits or non-qword word count"};
        if((tag&0xC0000000u)==0x40000000u)
            throw IopStop{"SIF1 destination tag IRQ without END: stop/continue behavior not verified"};
        const auto dest = tag & 0xFFFFFFu;
        if ((dest & 3u) || dest > ram_.size() || std::uint64_t{words} * 4u > ram_.size() - dest)
            throw IopStop{"SIF1 DMA destination outside aligned IOP RAM"};
        for (unsigned w = 0; w < 4u; ++w) sif1_fifo_.pop_front();
        iop_sif1_[0] = dest;
        sif1_words_ = words;
        sif1_end_ = (tag & 0x80000000u) != 0;
        sif1_tag_irq_ = (tag & 0x40000000u) != 0;
        sif1_have_tag_ = true;
        events_.push_back({cycle_, "SIF1 IOP tag destination=" + hex(dest) + " bytes=" + std::to_string(words * 4u)});
    }
    if (sif1_words_) {
        if (sif1_fifo_.size() < 4u) { ++sif1_metrics_.destination_empty_cycles; return; }
        for (unsigned w = 0; w < 4u; ++w) {
            const auto value = sif1_fifo_.front(); sif1_fifo_.pop_front();
            for (unsigned b = 0; b < 4u; ++b) {
                ram_[iop_sif1_[0]] = static_cast<std::uint8_t>(value >> (8u * b));
                ram_written_[iop_sif1_[0]++] = 1u;
            }
        }
        sif1_words_ -= 4u;
        sif1_metrics_.bytes += 16u;
    }
    if (!sif1_words_) {
        sif1_have_tag_ = false;
        if (sif1_end_ || sif1_tag_irq_) {
            const auto* dicr = latch(kDicr);
            const auto* dicr2 = latch(kDicr2);
            if (!dicr || !dicr->has_value() || !dicr2 || !dicr2->has_value())
                throw IopStop{"SIF1 DMA completion: interrupt masks not programmed (pending state unknown)"};
            // DICR2 IQE10 is independent of completion mask 19. A tag IRQ
            // occurs only after its payload; it need not end the chain.
            const bool raise=(sif1_end_ && (**dicr2&(1u<<19))) ||
                             (sif1_tag_irq_ && (**dicr2&(1u<<10)));
            for (auto& [address, value] : latches_)
                if (address == kDicr2 && value && raise) *value |= 1u << 27;
            update_dma_irq();
            if(sif1_tag_irq_ && raise)events_.push_back({cycle_, "SIF1 IOP tag IRQ"});
            if(sif1_end_) {
                iop_sif1_[2] &= ~kChcrStart;
                diagnostic_sif_ready_&=~0x40u;
                events_.push_back({cycle_, "SIF1 IOP destination complete"});
            }
        }
    }
}

std::uint32_t Iop::iop_sif0_read(std::uint32_t address) const {
    const auto field=(address-0x1F801520u)/4u;
    if(!(iop_sif0_written_&(1u<<field)))throw IopStop{"IOP SIF0 register read before write"};
    return iop_sif0_.at(field);
}
void Iop::iop_sif0_write(std::uint32_t address,std::uint32_t value) {
    const auto field=(address-0x1F801520u)/4u;
    if(iop_sif0_[2]&kChcrStart)throw IopStop{"IOP SIF0 register write while busy"};
    if(field==2u && value!=0 && value!=0x01000701u)
        throw IopStop{"IOP SIF0 supports source chain with tag transfer only"};
    if(field==2u && (value&kChcrStart) && !(iop_sif0_written_&8u))
        throw IopStop{"IOP SIF0 TADR not programmed"};
    if((field==0u || field==3u) && ((value&3u) || value>=ram_.size()))
        throw IopStop{"IOP SIF0 source/tag address outside aligned RAM"};
    iop_sif0_[field]=value;
    iop_sif0_written_|=static_cast<std::uint8_t>(1u<<field);
    if(field==1u)iop_sif0_written_|=0x30u; // both BCR halfwords known
    if(field==2u) {sif0_source_tag_=false;sif0_source_words_=0;}
}
void Iop::finish_iop_sif0() {
    // Channel 9: completion mask 18, tag enable 9, flag 26. IQE is
    // independent of completion masking, as for SIF1.
    const auto* dicr=latch(kDicr);const auto* dicr2=latch(kDicr2);
    if(!dicr || !*dicr || !dicr2 || !*dicr2)
        throw IopStop{"SIF0 source completion: interrupt masks not programmed"};
    const auto masks=**dicr2;
    const bool raise=(sif0_source_end_ && (masks&(1u<<18u))) ||
                     (sif0_source_irq_ && (masks&(1u<<9u)));
    for(auto& [address,value]:latches_)
        if(address==kDicr2 && value && raise) *value|=1u<<26u;
    update_dma_irq();
    if(sif0_source_irq_ && raise)events_.push_back({cycle_,"SIF0 IOP tag IRQ"});
    if(sif0_source_end_) {
        iop_sif0_[2]&=~kChcrStart;
        diagnostic_sif_ready_&=~0x20u;
        events_.push_back({cycle_,"SIF0 IOP source complete (FIFO may still contain data)"});
    }
}
void Iop::update_dma_irq() {
    const auto* first=latch(kDicr);
    const auto* second=latch(kDicr2);
    const auto control=first ? first->value_or(0u) : 0u;
    const auto pending=(control&0x7F000000u) |
        (second ? second->value_or(0u)&0x3F000000u : 0u);
    bool output=false;
    if((control&(1u<<23)) && pending) {
        const auto* gate=latch(0x1F80157Cu);
        if(!gate || !*gate)throw IopStop{"DMA pending interrupt: DMACINTEN not programmed"};
        output=(**gate&3u)==1u;
    }
    // DICR's master output is recomputed on control writes as well as on
    // completion. I_STAT captures its rising edge; acknowledging I_STAT
    // alone does not invent another edge while the DMA flags stay asserted.
    if(output && !dma_irq_output_)istat_|=kDmaIrq;
    dma_irq_output_=output;
}
void Iop::pump_sif0() {
    // Same bounded, instruction-clocked throughput hypothesis as SIF1.
    // This moves guest data only. No command decoding, RPC reply, or HLE.
    const auto enabled=[&](std::uint32_t address,std::uint32_t mask) {
        const auto* value=latch(address);
        return value && *value && ((**value&mask)==mask);
    };
    const auto observed_source=[&](std::uint32_t address)->std::optional<ObservedWord> {
        if(address>ram_.size() || 4u>ram_.size()-address)return std::nullopt;
        ObservedWord word;
        for(unsigned b=0;b<4;++b) {
            word.value|=std::uint32_t{ram_[address+b]}<<(b*8u);
            if(!ram_written_[address+b])word.unknown|=0xFFu<<(b*8u);
        }
        return word;
    };
    const auto source_word=[&](std::uint32_t address) {
        const auto word=observed_source(address);
        if(!word || word->unknown)throw IopStop{"SIF0 tag consumes undefined or out-of-range IOP RAM at "+hex(address)};
        return word->value;
    };
    if((iop_sif0_[2]&kChcrStart) && enabled(0x1F801578u,1u) && enabled(0x1F801570u,0x800u)) {
        if(!sif0_source_tag_) {
            if(sif0_fifo_.size()>28u)++sif0_metrics_.source_full_cycles;
            else {
                const auto at=iop_sif0_[3];
                const auto tag=source_word(at),words=source_word(at+4u);
                const auto low=source_word(at+8u),high=source_word(at+12u);
                const auto source=tag&0xFFFFFFu;
                if((tag&0x3F000003u) || (words&0xFF000000u))
                    throw IopStop{"SIF0 source tag: reserved bits not modeled at "+hex(at)+
                                  " tag="+hex(tag)+" words="+hex(words)};
                if((tag&0xC0000000u)==0x40000000u)
                    throw IopStop{"SIF0 source tag IRQ without END: stop/continue behavior not verified"};
                if(source>ram_.size() || std::uint64_t{words}*4u>ram_.size()-source)
                    throw IopStop{"SIF0 source tag payload outside IOP RAM"};
                sif0_fifo_.push_back({low,0});sif0_fifo_.push_back({high,0});
                // Upper half of an EE destination tag is unused: occupancy
                // padding only, never returned to the guest or copied to RAM.
                sif0_fifo_.push_back({0,0});sif0_fifo_.push_back({0,0});
                // Hardware measurements reported with PCSX2's SIF research:
                // an incomplete payload retains corresponding lanes of the
                // preceding full bus qword (initially EE tag + next IOP tag).
                sif0_last_qword_={ObservedWord{low,0},ObservedWord{high,0},observed_source(at+16u),observed_source(at+20u)};
                iop_sif0_[0]=source;iop_sif0_[3]+=16u;iop_sif0_written_|=1u;
                sif0_source_words_=words;sif0_source_end_=(tag&0x80000000u)!=0;
                sif0_source_irq_=(tag&0x40000000u)!=0;
                sif0_source_tag_=true;
            }
        } else if(sif0_source_words_) {
            if(sif0_fifo_.size()>28u)++sif0_metrics_.source_full_cycles;
            else {
                std::array<ObservedWord,4> qword{};
                const auto count=std::min(4u,sif0_source_words_);
                for(unsigned w=0;w<4;++w) {
                    if(w<count) {
                        const auto word=observed_source(iop_sif0_[0]+w*4u);
                        if(!word)throw IopStop{"SIF0 payload outside IOP RAM"};
                        qword[w]=*word;
                    } else qword[w]=sif0_last_qword_[w].value_or(ObservedWord{0,~0u});
                }
                for(const auto word:qword)sif0_fifo_.push_back(word);
                for(unsigned w=0;w<4;++w)sif0_last_qword_[w]=qword[w];
                iop_sif0_[0]+=count*4u;sif0_source_words_-=count;
            }
        }
        sif0_metrics_.peak_fifo_words=std::max(sif0_metrics_.peak_fifo_words,sif0_fifo_.size());
        if(sif0_source_tag_ && !sif0_source_words_) {
            if(sif0_source_end_ || sif0_source_irq_)finish_iop_sif0();
            sif0_source_tag_=false;
        }
    }
    auto& chcr=ee_dma_setup_[5][0];auto& madr=ee_dma_setup_[5][1];auto& qwc=ee_dma_setup_[5][2];
    if(ee_dma_suspended_ || !(ee_dctrl_&1u) || !(chcr.value_or(0u)&0x100u))return;
    // An armed, empty receiver remains busy; no fabricated completion.
    if(sif0_fifo_.empty()){++sif0_metrics_.destination_empty_cycles;return;}
    const auto priority=ee_dma_priority(ee_dma_observe(0x1000E020u),5u);
    if(!priority)throw IopStop{"EE SIF0 priority depends on unknown D_PCR bits"};
    if(!*priority)return;
    if(!qwc.value_or(0u)) {
        if(sif0_fifo_.size()<4u){++sif0_metrics_.destination_empty_cycles;return;}
        if(sif0_fifo_[0].unknown || sif0_fifo_[1].unknown)
            throw IopStop{"SIF0 destination tag consumes unknown FIFO bits"};
        const auto tag=sif0_fifo_[0].value,dest=sif0_fifo_[1].value;const auto id=(tag>>28u)&7u;
        if((tag&0x0FFF0000u) || (id!=0u && id!=1u && id!=7u) || (dest&0x8000000Fu))
            throw IopStop{"SIF0 destination tag: unsupported fields, ID or scratchpad/alignment"};
        const auto count=tag&0xFFFFu;
        if(dest>ee_ram_.size() || std::uint64_t{count}*16u>ee_ram_.size()-dest)
            throw IopStop{"SIF0 destination exceeds EE RAM"};
        for(unsigned w=0;w<4;++w)sif0_fifo_.pop_front();
        *chcr=(*chcr&0xFFFFu)|(tag&0xFFFF0000u);madr=dest;qwc=count;
        sif0_dest_end_=id==7u || ((tag&0x80000000u) && (*chcr&0x80u));
    } else {
        if(sif0_fifo_.size()<4u){++sif0_metrics_.destination_empty_cycles;return;}
        // DMA transports opaque payloads. It consumes tag fields, not data
        // values; retain unknown lanes for the eventual EE instruction.
        if(!ee_ram_unknown_)
            for(unsigned w=0;w<4;++w)if(sif0_fifo_[w].unknown)
                throw IopStop{"SIF0 unknown payload requires EE RAM knowledge tracking"};
        for(unsigned w=0;w<4;++w) {
            const auto word=sif0_fifo_.front();sif0_fifo_.pop_front();
            for(unsigned b=0;b<4;++b)ee_ram_[*madr+w*4u+b]=static_cast<std::uint8_t>(word.value>>(b*8u));
            if(ee_ram_unknown_) {
                if(word.unknown)(*ee_ram_unknown_)[*madr+w*4u]=word.unknown;
                else ee_ram_unknown_->erase(*madr+w*4u);
            }
        }
        *madr+=16u;--*qwc;sif0_metrics_.bytes+=16u;
    }
    if(!*qwc && sif0_dest_end_) {
        *chcr&=~0x100u;ee_dstat_|=0x20u;
        events_.push_back({cycle_,"SIF0 EE destination complete"});
    }
}

void Iop::mount_disc(std::shared_ptr<DiscImage> disc,DiscMedium medium,bool diagnostic_cav_nominal_timing) {
    if(cycle_ || stop_ || !disc)throw DeviceRefusal("CDVD mount requires an active reset state and a disc image");
    if(medium!=DiscMedium::Ps2Cd && medium!=DiscMedium::Ps2Dvd)
        throw DeviceRefusal("CDVD medium is unsupported");
    (void)disc->boot_path(); // Validate PS2 BOOT2, without loading guest code.
    disc_=std::move(disc);cdvd_type_=medium==DiscMedium::Ps2Cd ? 0x12u : 0x14u;
    // Host explicitly supplies a detected disc: tray closed, spindle on,
    // paused. Spin-up/detection latency is outside this mount operation.
    cdvd_status_=cdvd_sticky_=0x0Au;
    diagnostic_cav_nominal_timing_=diagnostic_cav_nominal_timing;
}

void Iop::pump_cdvd() {
    if(!cdvd_sectors_)return;
    if(cdvd_sector_offset_==2048) {
        if(cycle_<cdvd_sector_at_)return;
        try {disc_->read_sector(cdvd_lba_,cdvd_sector_);}
        catch(const std::exception& error){throw IopStop{std::string("CDVD sector read: ")+error.what()};}
        cdvd_sector_offset_=0;cdvd_status_=6u;cdvd_sticky_|=cdvd_status_;
    }
    const auto regptr=[&](std::uint32_t address)->std::optional<std::uint32_t>& {
        for(auto& item:latches_)if(item.first==address)return item.second;
        throw IopStop{"CDVD DMA register has not been initialized"};
    };
    auto& chcr=regptr(0x1F8010B8u);
    if(!chcr || !(*chcr&kChcrStart))return; // A real sector waits for its receiver.
    const auto enable=latch(0x1F801578u),priority=latch(0x1F8010F0u);
    if(!enable || !*enable || !priority || !*priority)throw IopStop{"CDVD DMA enable/priority is unknown"};
    if(!(**enable&1u) || !(**priority&(1u<<15u)))return;
    auto& madr=regptr(0x1F8010B0u);auto& bcr=regptr(0x1F8010B4u);
    if(!madr || !bcr)throw IopStop{"CDVD DMA address/block control is unknown"};
    const auto bytes=(*bcr&0xFFFFu)*4u,blocks=*bcr>>16u;
    if(!bytes || !blocks || bytes>2048u || (2048u%bytes) ||
       *madr>ram_.size() || bytes>ram_.size()-*madr)
        throw IopStop{"CDVD DMA block size/count/address is unsupported"};
    auto& dicr=regptr(kDicr);
    if(blocks==1u && !dicr)throw IopStop{"CDVD DMA interrupt mask is unknown"};
    std::copy_n(cdvd_sector_.begin()+cdvd_sector_offset_,bytes,ram_.begin()+*madr);
    std::fill_n(ram_written_.begin()+*madr,bytes,std::uint8_t{1});
    *madr+=bytes;*bcr-=0x10000u;cdvd_sector_offset_+=bytes;
    if(blocks==1u) {
        *chcr&=~kChcrStart;
        if(*dicr&(1u<<19u))*dicr|=1u<<27u;
        update_dma_irq();events_.push_back({cycle_,"CDVD DMA3 complete"});
    }
    if(cdvd_sector_offset_==2048) {
        events_.push_back({cycle_,"CDVD sector "+std::to_string(cdvd_lba_)+" transferred (2048 bytes)"});
        ++cdvd_lba_;--cdvd_sectors_;
        if(cdvd_sectors_)cdvd_sector_at_=cycle_+cdvd_sector_cycles_;
        else {cdvd_irq_|=2u;cdvd_status_=0xAu;cdvd_sticky_|=cdvd_status_;}
    }
}

void Iop::run_until(std::uint64_t cycle, const GsRegisters* video) {
    // EE writes are preceded by a run to the write boundary. Observe the new
    // clock before step() decides whether the next instruction is interrupted.
    // Refreshing only after retirement leaves that decision one generation old.
    if (video != nullptr) refresh_video(*video);
    if (video == nullptr && video_generation_) {
        // Detached: the VBLANK sources are unknown again.
        video_generation_.reset();
        video_ = {};
    }
    while (!stop_ && cycle_ < cycle) {
        const auto pc = pc_;
        const auto saved_target = branch_target_;
        const bool saved_slot = in_delay_slot_;
        try {
            pump_sif1();
            pump_sif0();
            pump_cdvd();
            if(diagnostic_sio2_dma_cycles_)pump_sio2_dma();
            if(sio2_complete_at_ && cycle_>=*sio2_complete_at_) {
                sio2_complete_at_.reset();
                sio2_output_=std::move(sio2_pending_reply_); // clocked wire bytes plus opaque DMA padding
                sio2_response_valid_=true;
                sio2_completion_flag_=true;update_sio2_irq();
                events_.push_back({cycle_,"DIAGNOSTIC SIO2 serial packet complete"});
            }
            if(cdvd_complete_at_ && cycle_>=*cdvd_complete_at_) {
                cdvd_complete_at_.reset(); cdvd_irq_|=2u;
                events_.push_back({cycle_,"CDVD N command completion"});
            }
            if(cdvd_irq_) istat_|=1u<<2u;
            step();
            ++cycle_;
            if (timers_running_) tick_timers();
            if(spu2_clock_active_)tick_spu2_quiet();
            if (video != nullptr) tick_video();
        } catch (const IopStop& stop) {
            pc_ = pc;
            branch_target_ = saved_target;
            in_delay_slot_ = saved_slot;
            stop_ = "IOP " + hex(pc) + (saved_slot ? " (delay slot)" : "") + " after " + std::to_string(cycle_) +
                    " instructions: " + stop.reason;
            stop_cycle_ = cycle_;
            events_.push_back({cycle_, "stop: " + *stop_});
        }
    }
}

std::uint32_t Iop::ee_cdvd_read(std::uint32_t physical,std::size_t bytes) {
    if(stopped())throw DeviceRefusal("CDVD read after the IOP stopped: "+*stop_);
    if(physical<0x1F402000u || physical>=0x1F402020u)throw DeviceRefusal("outside CDVD register window");
    if(physical==0x1F40200Au || physical==0x1F40200Bu)
        throw DeviceRefusal("EE CDVD drive status: bits 6-7 are not established");
    try { return io_read(physical,bytes,"EE CDVD read"); }
    catch(const IopStop& error) { throw DeviceRefusal(error.reason); }
}
void Iop::ee_cdvd_write(std::uint32_t physical,std::size_t bytes,std::uint32_t value) {
    if(stopped())throw DeviceRefusal("CDVD write after the IOP stopped: "+*stop_);
    if(physical<0x1F402000u || physical>=0x1F402020u)throw DeviceRefusal("outside CDVD register window");
    try { io_write(physical,bytes,value,"EE CDVD write"); }
    catch(const IopStop& error) { throw DeviceRefusal(error.reason); }
}

std::uint64_t Iop::ee_read(std::uint32_t offset, std::size_t bytes) const {
    if (offset >= kIopRamBytes || bytes > kIopRamBytes - offset)
        throw DeviceRefusal("IOP RAM offset " + hex(offset) + " is beyond the 2 MiB of IOP RAM");
    for (std::size_t i = 0; i < bytes; ++i)
        if (ram_written_[offset + i] == 0u)
            throw DeviceRefusal("IOP RAM " + hex(offset) + " has not been written by the IOP (undefined since "
                                "power-on); " + (stop_ ? "the IOP stopped: " + *stop_
                                                       : "the IOP is at " + hex(pc_) + " after " +
                                                             std::to_string(cycle_) + " instructions"));
    if (stop_)
        throw DeviceRefusal("IOP RAM " + hex(offset) + " read after the IOP stopped (its later state is unknown): " +
                            *stop_);
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes; ++i) value |= static_cast<std::uint64_t>(ram_[offset + i]) << (8u * i);
    return value;
}

void Iop::ee_write(std::uint32_t offset, std::size_t bytes, std::uint64_t value) {
    if(bytes!=1u && bytes!=2u && bytes!=4u && bytes!=8u)
        throw DeviceRefusal("EE write to IOP RAM requires 1/2/4/8 bytes");
    if(offset>=kIopRamBytes || bytes>kIopRamBytes-offset)
        throw DeviceRefusal("EE write outside the 2 MiB of IOP RAM");
    if(stop_) throw DeviceRefusal("EE write to IOP RAM after the IOP stopped: "+*stop_);
    for(std::size_t i=0;i<bytes;++i) {
        ram_[offset+i]=static_cast<std::uint8_t>(value>>(8u*i));
        ram_written_[offset+i]=1;
    }
}

std::uint32_t Iop::reg(unsigned index) const {
    if ((gpr_defined_ & (1u << index)) == 0u)
        throw IopStop{"GPR " + std::to_string(index) + " used while its value is unknown (" + gpr_origin_[index] + "); instruction=" + hex(fetch(pc_))};
    return gpr_[index];
}
std::uint32_t Iop::masked_reg(unsigned index, std::uint32_t mask) const {
    if ((gpr_defined_ & (1u << index)) == 0u && (gpr_known_[index] & mask) != mask) return reg(index);
    return gpr_[index];
}
void Iop::and_immediate(unsigned destination, unsigned source, std::uint32_t mask) {
    if(gpr_defined_&(1u<<source))return set_reg(destination,gpr_[source]&mask);
    const auto known=gpr_known_[source]|~mask; // AND with zero establishes a known zero.
    const auto value=gpr_[source]&mask&known;
    if(known==~0u)return set_reg(destination,value);
    if(destination)write_=PendingLoad{destination,value,false,gpr_origin_[source],known};
}
void Iop::and_regs(unsigned rd, unsigned rs, unsigned rt) {
    const auto known = [&](unsigned r) { return (gpr_defined_ & (1u << r)) != 0u ? ~0u : gpr_known_[r]; };
    const auto a = gpr_[rs], b = gpr_[rt], ka = known(rs), kb = known(rt);
    const auto k = (ka & kb) | (ka & ~a) | (kb & ~b);
    if (k == ~0u) return set_reg(rd, a & b);
    if (rd == 0u) return;
    const auto& origin = (ka != ~0u ? gpr_origin_[rs] : gpr_origin_[rt]);
    write_ = PendingLoad{rd, a & b & k, false, origin, k};
}
void Iop::or_value(unsigned rd,unsigned rs,std::uint32_t b,std::uint32_t kb,const std::string& b_origin) {
    const auto a=gpr_[rs],ka=(gpr_defined_&(1u<<rs)) ? ~0u : gpr_known_[rs];
    // A known one fixes an OR result even if the other operand is unknown.
    const auto known=(ka&kb)|(ka&a)|(kb&b);
    if(known==~0u)return set_reg(rd,a|b);
    if(!rd)return;
    const auto& origin=ka!=~0u ? gpr_origin_[rs] : b_origin;
    // Only OR with known reset/start bits preserves the opaque sample. Mixing
    // another unknown value or changing configuration discards the proof.
    const auto epoch=kb==~0u && !(b&~0xDu) ? gpr_sio2_epoch_[rs] : 0u;
    write_=PendingLoad{rd,(a|b)&known,false,origin,known,epoch};
}
bool Iop::regs_equal(unsigned rs, unsigned rt) const {
    const auto known = [&](unsigned r) { return (gpr_defined_ & (1u << r)) != 0u ? ~0u : gpr_known_[r]; };
    if (((gpr_[rs] ^ gpr_[rt]) & known(rs) & known(rt)) != 0u) return false;
    return reg(rs) == reg(rt);
}
std::optional<std::uint32_t> Iop::stored_reg(unsigned index) const {
    if ((gpr_defined_ & (1u << index)) == 0u) return std::nullopt;
    return gpr_[index];
}
void Iop::shift_right(unsigned destination, unsigned source, unsigned amount) {
    if (!destination) return;
    if (!amount) return copy_reg(destination, source);
    if (gpr_defined_ & (1u << source)) return set_reg(destination, gpr_[source] >> amount);
    // Vacated high bits are zero; every other output bit retains the
    // knowledge of its corresponding source bit. No numeric guess is made.
    const auto known = (gpr_known_[source] >> amount) | (~0u << (32u - amount));
    const auto value = (gpr_[source] >> amount) & known;
    if (known == ~0u) return set_reg(destination, value);
    write_ = PendingLoad{destination, value, false, gpr_origin_[source], known};
}
void Iop::shift_left(unsigned destination, unsigned source, unsigned amount) {
    if(!destination)return;
    if(!amount)return copy_reg(destination,source);
    if(gpr_defined_&(1u<<source))return set_reg(destination,gpr_[source]<<amount);
    const auto known=(gpr_known_[source]<<amount)|((1u<<amount)-1u);
    const auto value=(gpr_[source]<<amount)&known;
    if(known==~0u)return set_reg(destination,value);
    write_=PendingLoad{destination,value,false,gpr_origin_[source],known};
}
void Iop::add_sub_unsigned(unsigned destination,unsigned left,unsigned right,bool subtract) {
    if(!destination)return;
    if(subtract && left==right)return set_reg(destination,0u);
    const auto ka=(gpr_defined_&(1u<<left)) ? ~0u : gpr_known_[left];
    const auto kb=(gpr_defined_&(1u<<right)) ? ~0u : gpr_known_[right];
    const auto value=subtract ? gpr_[left]-gpr_[right] : gpr_[left]+gpr_[right];
    const auto unknown=~ka|~kb;
    if(!unknown)return set_reg(destination,value);
    // Carry and borrow only travel upwards. Every bit below the first
    // unknown input bit is exact, including wrap of the retained field.
    // Higher bits remain conservative even when a carry could be proved.
    const auto known=(unknown&(0u-unknown))-1u;
    write_=PendingLoad{destination,value&known,false,ka!=~0u ? gpr_origin_[left] : gpr_origin_[right],known};
}
void Iop::copy_reg(unsigned destination, unsigned source) {
    if (!destination) return;
    if (gpr_defined_ & (1u << source)) return set_reg(destination, gpr_[source]);
    // ADDU with $zero is an exact copy, including an opaque device sample.
    // Queue the ALU write normally so the R3000A load delay remains intact.
    write_ = PendingLoad{destination, gpr_[source], false, gpr_origin_[source],
                         gpr_known_[source], gpr_sio2_epoch_[source]};
}
void Iop::set_reg(unsigned index, std::uint32_t value) {
    if (index != 0u) write_ = PendingLoad{index, value, true, {}, ~0u};
}
// A load of never-written RAM leaves the register undefined (its value is
// unknown); only a later use of that register stops the IOP.
void Iop::load_reg(unsigned index, std::uint32_t value) {
    if (index != 0u)
        pending_load_ = PendingLoad{index, value, !undefined_load_, undefined_load_.value_or(std::string()),
                                    undefined_load_ ? undefined_known_ : ~0u,undefined_sio2_epoch_};
}

Iop::Access Iop::translate(std::uint32_t address, std::size_t bytes, bool write, const char* what) const {
    const auto kind = std::string(write ? "store" : "load") + " " + what;
    if ((address & (bytes - 1u)) != 0u)
        throw IopStop{kind + " at " + hex(address) + ": address error exception (not vectored)"};
    if ((cop0_[Sr].value_or(0u) & kSrKuc) != 0u) throw IopStop{kind + ": user mode is not modeled"};
    if (address >= 0xFFFE0000u) return {address, false, true};
    if (address >= 0xC0000000u) throw IopStop{kind + " at " + hex(address) + ": KSEG2 outside the cache control"};
    if (address >= 0xA0000000u) return {address - 0xA0000000u, false, false};
    if (address >= 0x80000000u) return {address - 0x80000000u, true, false};
    return {address, true, false};
}

std::uint32_t Iop::fetch(std::uint32_t address) const {
    if ((address & 3u) != 0u) throw IopStop{"instruction fetch at " + hex(address) + ": address error"};
    const auto a = translate(address, 4u, false, "fetch");
    if (a.physical < kIopRamBytes) {
        for (unsigned i = 0; i < 4u; ++i)
            if (ram_written_[a.physical + i] == 0u)
                throw IopStop{"instruction fetch from IOP RAM " + hex(a.physical) + ", never written"};
        return static_cast<std::uint32_t>(ram_[a.physical]) | (static_cast<std::uint32_t>(ram_[a.physical + 1]) << 8) |
               (static_cast<std::uint32_t>(ram_[a.physical + 2]) << 16) |
               (static_cast<std::uint32_t>(ram_[a.physical + 3]) << 24);
    }
    if(a.physical>=0x1E000000u && a.physical<0x1E400000u) {
        const auto offset=a.physical-0x1E000000u;
        if(offset>rom1_.size() || 4u>rom1_.size()-offset)
            throw IopStop{"instruction fetch: ROM1 bytes are not supplied for this firmware"};
        std::uint32_t value=0;
        for(unsigned i=0;i<4;++i)value|=std::uint32_t{rom1_[offset+i]}<<(i*8u);
        return value;
    }
    if (a.physical >= kRomPhysical && a.physical - kRomPhysical < kRomBytes) {
        const auto o = a.physical - kRomPhysical;
        return static_cast<std::uint32_t>(rom_[o]) | (static_cast<std::uint32_t>(rom_[o + 1]) << 8) |
               (static_cast<std::uint32_t>(rom_[o + 2]) << 16) | (static_cast<std::uint32_t>(rom_[o + 3]) << 24);
    }
    throw IopStop{"instruction fetch from " + hex(address) + ": not RAM or ROM"};
}

std::uint32_t Iop::load(std::uint32_t address, std::size_t bytes, const char* what) {
    const auto a = translate(address, bytes, false, what);
    const auto sr = *cop0_[Sr];
    if ((sr & kSrSwc) != 0u) throw IopStop{std::string("load ") + what + " with Status.SwC: not modeled"};
    if ((sr & kSrIsc) != 0u && a.cached)
        throw IopStop{std::string("load ") + what + " at " + hex(address) +
                      " with Status.IsC (cache isolated): cache contents are not modeled"};
    if (a.kseg2) return io_read(a.physical, bytes, what);
    std::uint32_t value = 0;
    if (a.physical < kIopRamBytes) {
        std::uint32_t unknown=0;
        for (std::size_t i = 0; i < bytes; ++i) {
            if (ram_written_[a.physical + i] == 0u) {
                unknown|=0xFFu<<(8u*i);
                if(!undefined_load_)undefined_load_=std::string(what)+" of IOP RAM "+hex(a.physical+static_cast<std::uint32_t>(i))+
                    " at IOP "+hex(pc_)+", never written with a known value";
            }
            value |= static_cast<std::uint32_t>(ram_[a.physical + i]) << (8u * i);
        }
        if(unknown) {
            const auto width=bytes==4u ? ~0u : (1u<<(bytes*8u))-1u;
            auto known=width&~unknown;
            const auto kind=std::string_view(what);
            if(bytes==1u && (kind=="LBU" || (known&0x80u)))known|=0xFFFFFF00u;
            if(bytes==2u && (kind=="LHU" || (known&0x8000u)))known|=0xFFFF0000u;
            undefined_known_=known;
        }
        return value;
    }
    if (a.physical >= kRomPhysical && a.physical - kRomPhysical < kRomBytes) {
        for (std::size_t i = 0; i < bytes; ++i)
            value |= static_cast<std::uint32_t>(rom_[a.physical - kRomPhysical + i]) << (8u * i);
        return value;
    }
    if(a.physical>=0x1E000000u && a.physical<0x1E400000u) {
        const auto offset=a.physical-0x1E000000u;
        if(offset>rom1_.size() || bytes>rom1_.size()-offset)
            throw IopStop{std::string(what)+": ROM1 bytes are not supplied for this firmware"};
        for(std::size_t i=0;i<bytes;++i)value|=std::uint32_t{rom1_[offset+i]}<<(i*8u);
        return value;
    }
    return io_read(a.physical, bytes, what);
}


void Iop::reset_sio2_completion() {
    if(sio2_completion_flag_ && !diagnostic_sio2_reset_completion_)
        throw IopStop{"SIO2 CTRL reset effect on pending completion is unverified; explicit reset-latch experiment required"};
    if(diagnostic_sio2_reset_completion_) {sio2_completion_flag_=false;update_sio2_irq();}
}
void Iop::update_sio2_irq() {
    // PS2SDK MX4SIO sio2regs.h: CTRL.9 masks the latched STAT.0
    // completion source. Enabling a pending source raises a new output
    // edge; acknowledging I_STAT alone cannot manufacture another one.
    const bool output=sio2_completion_flag_ && (sio2_interrupt_enable_&0x200u);
    if(output && !sio2_irq_output_)istat_|=1u<<17u;
    sio2_irq_output_=output;
}
void Iop::diagnostic_sio2_digital_pad(unsigned port,std::uint16_t buttons,std::uint32_t packet_cycles) {
    if(port>=2u || !packet_cycles || cycle_)
        throw DeviceRefusal("SIO2 digital pad needs port 0/1 and positive packet cycles before execution");
    if(diagnostic_sio2_timeout_cycles_ && diagnostic_sio2_timeout_cycles_!=packet_cycles)
        throw DeviceRefusal("SIO2 peripherals must share the explicit packet duration experiment");
    if(diagnostic_sio2_unplugged_ports_&(1u<<port))
        throw DeviceRefusal("SIO2 digital pad cannot attach to an explicitly unplugged port");
    diagnostic_sio2_timeout_cycles_=packet_cycles;
    sio2_digital_pads_[port]=static_cast<std::uint16_t>(buttons|6u); // no L3/R3 on the digital pad
}
void Iop::diagnostic_sio2_port_timing(unsigned bits) {
    if(bits>3u || cycle_)throw DeviceRefusal("SIO2 port timing bits require a two-bit sample before guest execution");
    diagnostic_sio2_port_timing_=bits;
}
void Iop::diagnostic_sio2_dma_word_cycles(std::uint32_t cycles) {
    if(!cycles || cycle_)throw DeviceRefusal("SIO2 DMA experiment requires positive word cycles before guest execution");
    diagnostic_sio2_dma_cycles_=cycles;
}
void Iop::write_sio2_dma(unsigned channel,unsigned field,std::size_t bytes,std::uint32_t value) {
    auto& dma=sio2_dma_[channel];
    if(dma.registers[2]&kChcrStart)throw IopStop{"SIO2 DMA register write while busy"};
    if(field==4u && bytes==2u) {
        dma.registers[1]=(dma.registers[1]&0xFFFF0000u)|(value&0xFFFFu);dma.written|=8u;
    } else if(field==6u && bytes==2u) {
        dma.registers[1]=(dma.registers[1]&0xFFFFu)|((value&0xFFFFu)<<16u);dma.written|=16u;
    } else {
        if(bytes!=4u || field>8u || (field&3u))throw IopStop{"SIO2 DMA requires word registers or BCR halfwords"};
        const auto index=field/4u;
        if(index==2u && (value&kChcrStart)) {
            if(!diagnostic_sio2_dma_cycles_)throw IopStop{"SIO2 DMA transfers are not modeled without explicit word-cycle timing"};
            // Bit 30 requests snooping; this IOP RAM model is coherent.
            if((value&~0x40000000u)!=(0x01000201u-channel))throw IopStop{"SIO2 DMA supports incrementing slice transfers in the channel direction only: "+hex(value)};
            if((dma.written&3u)!=3u)throw IopStop{"SIO2 DMA requires known MADR and both BCR halves"};
            const auto size=dma.registers[1]&0xFFFFu,blocks=dma.registers[1]>>16u;
            if(!size || !blocks)throw IopStop{"SIO2 DMA zero block fields are not modeled"};
            const auto at=dma.registers[0];const auto total=std::uint64_t{size}*blocks*4u;
            if((at&3u) || at>ram_.size() || total>ram_.size()-at)
                throw IopStop{"SIO2 DMA range or alignment is outside IOP RAM"};
            dma.block_words=size;dma.words_left=size;
        }
        dma.registers[index]=value;dma.written|=1u<<index;
        if(index==1u)dma.written|=24u;
    }
    if((dma.written&24u)==24u)dma.written|=2u;
}
std::uint32_t Iop::read_sio2_dma(unsigned channel,unsigned field,std::size_t bytes) const {
    const auto& dma=sio2_dma_[channel];
    if(bytes==2u && (field==4u || field==6u)) {
        if(!(dma.written&(field==4u ? 8u : 16u)))throw IopStop{"SIO2 DMA register read before write"};
        return (dma.registers[1]>>((field&2u)*8u))&0xFFFFu;
    }
    if(bytes!=4u || field>8u || (field&3u))throw IopStop{"SIO2 DMA requires word registers or BCR halfwords"};
    if(!(dma.written&(1u<<(field/4u))))throw IopStop{"SIO2 DMA register read before write"};
    return dma.registers[field/4u];
}
void Iop::pump_sio2_dma() {
    if(!diagnostic_sio2_dma_cycles_ || cycle_<sio2_dma_next_)return;
    const auto enabled=latch(0x1F801578u),priority=latch(0x1F801570u);
    if(!enabled || !*enabled || !(**enabled&1u) || !priority || !*priority)return;
    int chosen=-1;unsigned best=8;
    for(unsigned n=0;n<2;++n) {
        const auto channel=(sio2_dma_turn_+n)&1u;const auto& dma=sio2_dma_[channel];
        const auto shift=16u+channel*4u,field=(**priority>>shift)&0xFu;
        if(!(dma.registers[2]&kChcrStart) || !(field&8u))continue;
        if(channel==0u ? sio2_input_.size()>252u : sio2_output_.size()<4u)continue;
        if((field&7u)<best){chosen=static_cast<int>(channel);best=field&7u;}
    }
    if(chosen<0)return;
    const auto channel=static_cast<unsigned>(chosen);auto& dma=sio2_dma_[channel];const auto at=dma.registers[0];
    if(!channel) {
        for(unsigned b=0;b<4;++b)if(!ram_written_[at+b])
            throw IopStop{"SIO2 DMA input requires known source byte at "+hex(at+b)};
        for(unsigned b=0;b<4;++b)sio2_input_.push_back(ram_[at+b]);
    } else for(unsigned b=0;b<4;++b) {
        const auto byte=sio2_output_.front();sio2_output_.pop_front();
        ram_[at+b]=static_cast<std::uint8_t>(byte.value);ram_written_[at+b]=byte.unknown ? 0u : 1u;
    }
    ++dma.words;dma.registers[0]+=4u;
    if(!--dma.words_left) {
        dma.registers[1]-=1u<<16u;
        if(dma.registers[1]>>16u)dma.words_left=dma.block_words;
        else {
            dma.registers[2]&=~kChcrStart;
            const auto address=0x1F801574u;
            for(auto& [physical,value]:latches_)if(physical==address && value) {
                if(*value&(1u<<(20u+channel)))*value|=1u<<(28u+channel);
                break;
            }
            update_dma_irq();events_.push_back({cycle_,"SIO2 DMA"+std::to_string(11u+channel)+" complete after actual FIFO payload"});
        }
    }
    sio2_dma_turn_=channel^1u;sio2_dma_next_=cycle_+diagnostic_sio2_dma_cycles_;
}

void Iop::diagnostic_sio2_infrared_timeout(std::uint32_t cycles) {
    if(!cycles || cycle_ || sio2_control_epoch_)
        throw DeviceRefusal("SIO2 infrared experiment requires positive timeout cycles before guest execution");
    if(diagnostic_sio2_timeout_cycles_ && diagnostic_sio2_timeout_cycles_!=cycles)
        throw DeviceRefusal("SIO2 absent devices must use the same explicitly assumed timeout");
    diagnostic_sio2_timeout_cycles_=cycles;diagnostic_sio2_infrared_absent_=true;
}
void Iop::diagnostic_sio2_unplugged_ports(unsigned mask,std::uint32_t timeout_cycles) {
    if(!mask || (mask&~0xFu) || !timeout_cycles || cycle_)
        throw DeviceRefusal("SIO2 unplugged-port experiment requires a four-port mask and positive timeout before execution");
    if(diagnostic_sio2_timeout_cycles_ && diagnostic_sio2_timeout_cycles_!=timeout_cycles)
        throw DeviceRefusal("SIO2 unplugged ports must use the same explicitly assumed timeout");
    for(unsigned p=0;p<2u;++p)if((mask&(1u<<p)) && sio2_digital_pads_[p])
        throw DeviceRefusal("SIO2 unplugged mask conflicts with an attached digital pad");
    diagnostic_sio2_timeout_cycles_=timeout_cycles;diagnostic_sio2_unplugged_ports_=mask;
}
void Iop::start_sio2_infrared(std::uint32_t interrupt_enable) {
    if(!diagnostic_sio2_timeout_cycles_)
        throw IopStop{"SIO2 transfer is not modeled (explicit infrared timeout experiment required)"};
    if(sio2_complete_at_ || !sio2_control_epoch_ || !sio2_output_.empty())
        throw IopStop{"SIO2 infrared start while busy, unconfigured, or with unread data"};
    std::size_t consumed=0,clocked=0;std::deque<ObservedWord> reply;
    unsigned pad_ports=0,pad_no_ack=0,absent_packets=0;
    for(unsigned n=0;n<16;++n) {
        const auto descriptor=latch(0x1F808200u+4u*n);
        if(!descriptor || !*descriptor)throw IopStop{"SIO2 command descriptor is unknown"};
        const auto word=**descriptor;
        if(!word)break;
        const auto tx=(word>>8u)&0x1FFu,rx=(word>>18u)&0x1FFu,port=word&3u;
        if((word&0xF80200CCu)!=0x40u || ((word&0x30u)!=0u && (word&0x30u)!=0x30u) || !tx || tx>127u || tx!=rx)
            throw IopStop{"SIO2 infrared descriptor length or port is unsupported"};
        const bool unplugged=(diagnostic_sio2_unplugged_ports_&(1u<<port))!=0u;
        if(port>1u && !unplugged)throw IopStop{"SIO2 port is not established as disconnected"};
        std::size_t input=tx,output=rx;
        if(word&0x30u) {
            // PS2SDK mcman uses one BCR slice per descriptor (0x90-byte
            // slots for 4-byte probes). The unused output lanes have no
            // measured value: transport their uncertainty, never zero-fill.
            input=sio2_dma_[0].block_words*4u;output=sio2_dma_[1].block_words*4u;
            if(input<tx || output<rx || input>256u || output>256u || n)
                throw IopStop{"SIO2 DMA descriptor stride or multiple padded descriptors are not modeled"};
            if((sio2_dma_[0].registers[2]&kChcrStart) || !(sio2_dma_[1].registers[2]&kChcrStart))
                throw IopStop{"SIO2 DMA descriptor requires complete input and an armed output channel"};
        }
        if(consumed+input>sio2_input_.size())throw IopStop{"SIO2 infrared command input is incomplete"};
        if(port<2u && sio2_digital_pads_[port] && sio2_input_[consumed]==0x01u) {
            if(tx<2u)throw IopStop{"SIO2 digital pad packet does not contain a command"};
            if(sio2_input_[consumed+1u]==0x42u) {
                if(tx!=5u)throw IopStop{"SIO2 digital pad supports only a five-byte button poll"};
                const auto buttons=*sio2_digital_pads_[port];
                for(const auto byte:{0xFFu,0x41u,0x5Au,unsigned(buttons&0xFFu),unsigned(buttons>>8u)})
                    reply.push_back({byte,0});
            } else {
                // A standard digital pad does not implement analog/config
                // commands. It releases DAT and withholds ACK; the explicit
                // serial timeout completes this failed request, not a mode change.
                for(unsigned b=0;b<rx;++b)reply.push_back({0xFFu,0});
                pad_no_ack|=1u<<port;
            }
            pad_ports|=1u<<port;
        } else {
            if(!unplugged && (!diagnostic_sio2_infrared_absent_ || sio2_input_[consumed]!=0x61u))
                throw IopStop{"SIO2 peripheral is not modeled by the absent infrared experiment"};
            for(unsigned b=0;b<rx;++b)reply.push_back({0xFFu,0}); // explicitly absent device cannot drive DAT
            ++absent_packets;
        }
        for(std::size_t b=rx;b<output;++b)reply.push_back({0,0xFFu});
        consumed+=input;clocked+=rx;
    }
    if(!consumed || consumed!=sio2_input_.size())
        throw IopStop{"SIO2 infrared command input does not match its descriptors"};
    if(reply.size()>256u)throw IopStop{"SIO2 reply exceeds the bounded RX FIFO"};
    if(pad_ports && absent_packets)throw IopStop{"SIO2 mixed connected/absent response status is not modeled"};
    sio2_response_status_=pad_ports ? (0x1000u|(pad_ports==3u ? 0x200u : 0x100u)) : 0x1D100u;
    if(pad_no_ack)sio2_response_status_|=0xC000u|(pad_no_ack<<16u);
    sio2_interrupt_enable_=interrupt_enable&0x300u;
    update_sio2_irq();
    sio2_diagnostic_control_=interrupt_enable&~0xDu;
    sio2_reply_bytes_=static_cast<std::uint32_t>(clocked);sio2_pending_reply_=std::move(reply);
    sio2_complete_at_=cycle_+diagnostic_sio2_timeout_cycles_;
    sio2_input_.clear();sio2_response_valid_=false;++sio2_control_epoch_;
    events_.push_back({cycle_,"DIAGNOSTIC SIO2 serial start: "+std::to_string(clocked)+" clocked bytes; DMA padding opaque"});
}

void Iop::diagnostic_spu2_pio_word_cycles(std::uint32_t cycles) {
    if(!cycles || cycle_)throw DeviceRefusal("SPU2 PIO experiment requires positive word cycles before guest execution");
    diagnostic_spu2_pio_cycles_=cycles;
}
void Iop::diagnostic_spu2_effect_end_reset(std::array<std::uint16_t,2> values) {
    if(cycle_ || values[0]>0xFu || values[1]>0xFu)
        throw DeviceRefusal("SPU2 initial EEA experiment requires two four-bit addresses before guest execution");
    diagnostic_spu2_effect_end_=values;
}
std::optional<std::uint16_t> Iop::spu2_ram_halfword(std::uint32_t word_address) const {
    if(word_address>=0x100000u)throw DeviceRefusal("SPU2 RAM word address outside 2 MiB");
    // Voices 1 and 3 capture their pre-volume output into 512-word rings.
    // Zero mixer gain does not prove these samples. Conservatively keep
    // each whole ring unknown while the decoder/envelope is symbolic.
    for(unsigned core=0;core<2u;++core)for(const unsigned voice:{1u,3u}) {
        const auto base=0x400u+core*0x800u+(voice==3u ? 0x200u : 0u);
        if((spu2_symbolic_voices_[core]&(1u<<voice)) && word_address>=base && word_address<base+0x200u)
            return std::nullopt;
    }
    if(spu2_ram_written_.empty() || !spu2_ram_written_[word_address])return std::nullopt;
    return spu2_ram_[word_address];
}
void Iop::validate_spu2_pio(unsigned core,const ObservedWord& control) const {
    if((spu2_adma_[0].normal && spu2_adma_[0].next) || (spu2_adma_[1].normal && spu2_adma_[1].next))
        throw IopStop{"SPU2 PIO arbitration with pending normal DMA is not modeled"};
    const auto previous=spu2_control_[core];
    if(spu2_transfer_[core].address && (*spu2_transfer_[core].address&7u) &&
       (!previous || (previous->unknown&0x30u) || (previous->value&0x30u)!=0x10u))
        throw IopStop{"SPU2 unaligned PIO start is not modeled"};
    if(spu2_effects_active(0) || spu2_effects_active(1))
        throw IopStop{"SPU2 PIO bus arbitration with active effects is not modeled"};
    if(!diagnostic_spu2_pio_cycles_)throw IopStop{"SPU2 PIO transfer timing requires an explicit word-cycle experiment"};
    if(spu2_adma_[0].enabled || spu2_adma_[1].enabled)throw IopStop{"SPU2 PIO and ADMA cannot share the sound RAM path"};
    if(!diagnostic_spu2_quiescent_ || (control.unknown&0x80F0u) || (control.value&0x80F0u)!=0x8010u)
        throw IopStop{"SPU2 PIO requires an enabled core with effects and IRQ disabled"};
    for(unsigned c=0;c<2;++c) {
        const auto word=c==core ? std::optional<ObservedWord>{control} : spu2_control_[c];
        if(!word || ((word->value|word->unknown)&0x40u))
            throw IopStop{"SPU2 PIO requires both shared-RAM IRQ comparators explicitly disabled"};
    }
    if(!spu2_transfer_[core].address)throw IopStop{"SPU2 PIO transfer address is incomplete"};
    if(spu2_transfer_[core^1u].next)throw IopStop{"SPU2 simultaneous PIO arbitration is not modeled"};
}
void Iop::diagnostic_spu2_adma_word_cycles(std::uint32_t cycles) {
    if(!cycles || cycle_)throw DeviceRefusal("SPU2 ADMA experiment requires positive word cycles before guest execution");
    diagnostic_spu2_adma_cycles_=cycles;
}
bool Iop::spu2_input_quiet(unsigned core,std::uint32_t mixer) const {
    for(unsigned channel=0;channel<2;++channel)if(mixer&(0x80u>>channel)) {
        const auto gain=latch(0x1F90076Cu+core*0x28u+channel*2u);
        // A closed route or an explicitly zero constant gain proves a zero
        // contribution at the next sample edge, without guessing a mute bit.
        if(!gain || !*gain || **gain!=0u)return false;
    }
    return true;
}
void Iop::validate_spu2_adma(unsigned core) const {
    if(spu2_effects_active(0) || spu2_effects_active(1))
        throw IopStop{"SPU2 ADMA bus arbitration with active effects is not modeled"};
    if(!diagnostic_spu2_adma_cycles_ || !diagnostic_spu2_quiescent_)
        throw IopStop{"SPU2 AutoDMA transfers require an explicit bus throughput and quiet reset experiment"};
    for(unsigned c=0;c<2;++c) {
        const auto word=spu2_control_[c];
        if(!word || ((word->value|word->unknown)&0x40u))
            throw IopStop{"SPU2 ADMA requires both shared-RAM IRQ comparators explicitly disabled"};
    }
    const auto control=spu2_control_[core];
    if((control->unknown&0x80F0u) || (control->value&0x80F0u)!=0x8000u)
        throw IopStop{"SPU2 ADMA requires an enabled core, stopped normal transfer and disabled effects/IRQ"};
    const auto mix=latch(0x1F900198u+core*0x400u);
    if(!mix || !*mix || !spu2_input_quiet(core,**mix))
        throw IopStop{"SPU2 ADMA audible input mixing is not modeled (closed routes or known zero input gains required)"};
    if(spu2_transfer_[0].next || spu2_transfer_[1].next || !spu2_transfer_[core].fifo.empty())
        throw IopStop{"SPU2 normal transfer and ADMA cannot share the sound RAM path"};
}
void Iop::validate_spu2_normal_dma(unsigned core,std::uint32_t words) const {
    if(!diagnostic_spu2_adma_cycles_ || !diagnostic_spu2_quiescent_)
        throw IopStop{"SPU2 normal DMA requires the explicit shared-bus throughput and quiet-reset experiment"};
    const auto& state=spu2_adma_[core];const auto control=spu2_control_[core];
    if(!state.control_written || state.enabled || !control || (control->unknown&0x80F0u) ||
       (control->value&0x80F0u)!=0x8020u)
        throw IopStop{"SPU2 normal DMA requires ADMAS written off and an enabled core in DMA-write mode without effects/IRQ"};
    const auto target=spu2_transfer_[core].address;
    if(!target || *target<0x2800u || std::uint64_t{*target}+std::uint64_t{words}*2u>0x100000u)
        throw IopStop{"SPU2 normal DMA target is unknown, overlaps dynamic buffers or wraps sound RAM"};
    for(unsigned c=0;c<2u;++c) {
        const auto attr=spu2_control_[c];
        if(!attr || ((attr->value|attr->unknown)&0x40u))
            throw IopStop{"SPU2 normal DMA requires both shared-RAM IRQ comparators disabled"};
        if(spu2_transfer_[c].next || !spu2_transfer_[c].fifo.empty())
            throw IopStop{"SPU2 normal DMA and PIO arbitration is not modeled"};
        if(spu2_effects_active(c)) {
            const auto area=spu2_effect_area(c);
            if(area.first<=area.second && *target<=area.second && *target+words*2u>area.first)
                throw IopStop{"SPU2 normal DMA overlaps an uncomputed active effect workspace"};
        }
        const auto& other=spu2_adma_[c];
        if(c!=core && other.next && other.normal) {
            const auto first=*spu2_transfer_[c].address,last=first+other.remaining*2u;
            if(*target<last && *target+words*2u>first)
                throw IopStop{"SPU2 concurrent normal DMA destinations overlap"};
        }
    }
}
std::uint32_t Iop::read_spu2_dma(unsigned core,std::uint32_t field,std::size_t bytes) const {
    const auto& state=spu2_adma_[core];
    if(bytes==2u && (field==4u || field==6u)) {
        if(!(state.written&(field==4u ? 2u : 4u)))throw IopStop{"SPU2 DMA BCR halfword is unknown before write"};
        return (state.dma[1]>>((field&2u)*8u))&0xFFFFu;
    }
    if(bytes!=4u || (field!=0u && field!=4u && field!=8u))throw IopStop{"SPU2 DMA requires words or BCR halfwords"};
    const auto need=field==0u ? 1u : field==4u ? 6u : 8u;
    if((state.written&need)!=need)throw IopStop{"SPU2 DMA register is unknown before write"};
    return state.dma[field/4u];
}
void Iop::write_spu2_dma(unsigned core,std::uint32_t field,std::size_t bytes,std::uint32_t value) {
    auto& state=spu2_adma_[core];
    if(state.next)throw IopStop{"SPU2 DMA register change while real data is pending is not modeled"};
    if(bytes==2u && (field==4u || field==6u)) {
        const auto shift=(field&2u)*8u;
        state.dma[1]=(state.dma[1]&~(0xFFFFu<<shift))|((value&0xFFFFu)<<shift);
        state.written|=field==4u ? 2u : 4u;return;
    }
    if(bytes!=4u || (field!=0u && field!=4u && field!=8u))throw IopStop{"SPU2 DMA requires known words or BCR halfwords"};
    if(field==8u && (value&kChcrStart)) {
        if(value!=0x01000201u)throw IopStop{"SPU2 DMA supports incrementing RAM-to-device request mode only"};
        if(state.enabled)validate_spu2_adma(core);
        if((state.written&7u)!=7u)throw IopStop{"SPU2 DMA needs programmed address and both BCR halves"};
        const auto blocks=state.dma[1]>>16u,block_words=state.dma[1]&0xFFFFu;
        const auto words=std::uint64_t{blocks}*block_words;
        if(!blocks || !block_words || (state.enabled && words%256u) || words>ram_.size()/4u || (state.dma[0]&3u) ||
           state.dma[0]>ram_.size() || words*4u>ram_.size()-state.dma[0])
            throw IopStop{"SPU2 DMA needs bounded aligned IOP RAM and nonzero blocks (ADMA requires whole stereo banks)"};
        if(!state.enabled)validate_spu2_normal_dma(core,static_cast<std::uint32_t>(words));
        const auto enable=latch(0x1F801578u),priority=latch(core ? 0x1F801570u : 0x1F8010F0u);
        if(!enable || !*enable || !priority || !*priority)throw IopStop{"SPU2 DMA global enable/priority is unknown"};
        const auto irq=latch(core ? kDicr2 : kDicr);
        if(!irq || !*irq)throw IopStop{"SPU2 DMA interrupt control is unknown"};
        state.remaining=static_cast<std::uint32_t>(words);state.block_remaining=block_words;
        state.normal=!state.enabled;
        state.next=cycle_+diagnostic_spu2_adma_cycles_;
    }
    state.dma[field/4u]=value;state.written|=field==0u ? 1u : field==4u ? 6u : 8u;
}
void Iop::consume_spu2_input(unsigned core,std::uint64_t sample_cycle) {
    auto& state=spu2_adma_[core];
    if(!state.enabled || !state.primed || !state.ready[state.read_bank])return;
    const auto left=0x2000u+core*0x400u+state.read_bank*0x100u+state.read_position;
    const auto right=left+0x200u;
    if(spu2_ram_written_.empty() || !spu2_ram_written_[left] || !spu2_ram_written_[right])
        throw IopStop{"SPU2 ADMA input consumes unknown sound RAM"};
    const auto signed_sample=[](std::uint16_t raw)->std::int16_t {
        return static_cast<std::int16_t>(raw<=0x7FFFu ? std::int32_t{raw} : std::int32_t{raw}-0x10000);
    };
    state.last_input=Spu2Sample{sample_cycle,signed_sample(spu2_ram_[left]),signed_sample(spu2_ram_[right])};
    ++state.frames;
    if(++state.read_position==256u) {
        state.ready[state.read_bank]=false;state.read_bank^=1u;state.read_position=0;
    }
}
bool Iop::tick_spu2_adma(unsigned core) {
    auto& state=spu2_adma_[core];if(!state.next || cycle_<*state.next)return false;
    const auto enabled=latch(0x1F801578u),priority=latch(core ? 0x1F801570u : 0x1F8010F0u);
    const auto priority_bit=core ? 8u : 0x80000u;
    if(!enabled || !*enabled || !priority || !*priority)throw IopStop{"SPU2 DMA enable became unknown during transfer"};
    if(!(**enabled&1u) || !(**priority&priority_bit) || (!state.normal && state.ready[state.fill_bank])) {
        state.next=cycle_+diagnostic_spu2_adma_cycles_;return false; // true backpressure; no catch-up burst
    }
    const auto source=state.dma[0];
    if(!state.normal)for(unsigned b=0;b<4;++b)if(!ram_written_[source+b])throw IopStop{"SPU2 DMA consumes unknown IOP RAM bytes"};
    const auto target=state.normal ? *spu2_transfer_[core].address : 0x2000u+core*0x400u+state.fill_bank*0x100u+
        (state.fill_word>=128u ? 0x200u : 0u)+(state.fill_word%128u)*2u;
    if(spu2_ram_.empty()){spu2_ram_.resize(0x100000u);spu2_ram_written_.resize(0x100000u);}
    for(unsigned n=0;n<2;++n) {
        spu2_ram_[target+n]=static_cast<std::uint16_t>(ram_[source+2u*n]|(std::uint16_t{ram_[source+2u*n+1u]}<<8u));
        spu2_ram_written_[target+n]=(ram_written_[source+2u*n] && ram_written_[source+2u*n+1u]) ? 1u : 0u;
    }
    state.dma[0]+=4u;
    if(state.normal) {
        ++state.normal_words;auto& transfer=spu2_transfer_[core];
        transfer.address=(target+2u)&0xFFFFFu;
        transfer.high=static_cast<std::uint16_t>(*transfer.address>>16u);
        transfer.low=static_cast<std::uint16_t>(*transfer.address);
    } else ++state.words;
    if(!(--state.block_remaining)) {
        state.dma[1]-=0x10000u;state.block_remaining=state.dma[1]&0xFFFFu;
    }
    if(!state.normal && ++state.fill_word==256u) {
        state.ready[state.fill_bank]=true;
        // Explicit timing experiment: input starts at the next 48 kHz edge
        // after a full initial stereo bank, and waits if a bank is not ready.
        if(!state.primed){state.primed=true;state.read_bank=state.fill_bank;state.read_position=0;}
        state.fill_bank^=1u;state.fill_word=0;
    }
    if(--state.remaining)state.next=cycle_+diagnostic_spu2_adma_cycles_;
    else {
        state.next.reset();state.dma[2]&=~kChcrStart;
        const auto reg=core ? kDicr2 : kDicr,mask=core ? 0x10000u : 0x100000u,flag=core ? 0x1000000u : 0x10000000u;
        for(auto& [address,value]:latches_)if(address==reg && value && (*value&mask))*value|=flag;
        update_dma_irq();events_.push_back({cycle_,"DIAGNOSTIC SPU2 core "+std::to_string(core)+
            (state.normal ? " normal DMA real payload complete" : " ADMA real payload complete")});
    }
    return true;
}
void Iop::diagnostic_spu2_effect_start_reset(std::array<std::uint32_t,2> values) {
    if(cycle_ || values[0]>0xFFFFFu || values[1]>0xFFFFFu || (values[0]&7u) || (values[1]&7u))
        throw DeviceRefusal("SPU2 initial ESA experiment requires aligned 20-bit addresses before execution");
    diagnostic_spu2_effect_start_=values;
}
void Iop::diagnostic_spu2_effect_address_reserved_zero() {
    if(cycle_)throw DeviceRefusal("SPU2 effect-address readback experiment must be selected before execution");
    diagnostic_spu2_effect_address_reserved_zero_=true;
}
bool Iop::spu2_effects_active(unsigned core) const {
    return spu2_control_[core] && (spu2_control_[core]->value&0x8080u)==0x8080u;
}
void Iop::diagnostic_spu2_inverted_effect_area_inactive() {
    if(cycle_)throw DeviceRefusal("SPU2 inverted-area experiment must be selected before execution");
    diagnostic_spu2_inverted_effect_area_inactive_=true;
}
std::pair<std::uint32_t,std::uint32_t> Iop::spu2_effect_area(unsigned core) const {
    const auto base=0x1F900000u+core*0x400u;
    const auto field=[&](std::uint32_t address)->std::optional<std::uint32_t> {
        const auto value=latch(address);return value ? *value : std::nullopt;
    };
    const auto high=field(base+0x2E0u),low=field(base+0x2E2u),end=field(base+0x33Cu);
    if(((!high || !low) && !diagnostic_spu2_effect_start_) || (!end && !diagnostic_spu2_effect_end_))
        throw IopStop{"SPU2 effects require a known workspace start and end"};
    const auto initial=diagnostic_spu2_effect_start_ ? (*diagnostic_spu2_effect_start_)[core] : 0u;
    const auto first=((high.value_or(initial>>16u)&0xFu)<<16u)|(low.value_or(initial)&0xFFFFu);
    const auto last=((end ? *end : (*diagnostic_spu2_effect_end_)[core])&0xFu)<<16u|0xFFFFu;
    if(first>last && !(first&7u) && first>=0x2800u && diagnostic_spu2_inverted_effect_area_inactive_)
        return {first,first-1u}; // empty under the explicit experiment; does not recover prior RAM knowledge
    if((first&7u) || first>last || first<0x2800u)
        throw IopStop{"SPU2 effect workspace alignment, wrap or overlap with capture/input buffers is not modeled"};
    return {first,last};
}
void Iop::write_spu2_control(std::uint32_t physical,ObservedWord word) {
    word.unknown&=0xFFFFu;word.value&=0xFFFFu&~word.unknown;
    if(word.unknown&0x8030u)throw IopStop{"SPU2 control write leaves core enable or transfer mode unknown"};
    const auto core=(physical-0x1F90019Au)/0x400u;
    const auto mode=word.value&0x30u;
    if((spu2_symbolic_voices_[0] || spu2_symbolic_voices_[1] || spu2_effects_active(0) || spu2_effects_active(1)) &&
       ((word.value|word.unknown)&0x40u))
        throw IopStop{"SPU2 unknown voice RAM reads cannot enable either shared-RAM IRQ comparator"};
    if(spu2_adma_[core].enabled && (mode || !(word.value&0x8000u) || ((word.value|word.unknown)&0xC0u)))
        throw IopStop{"SPU2 active ADMA requires its core enabled and normal transfer/effects/IRQ disabled"};
    if((spu2_adma_[0].enabled || spu2_adma_[1].enabled) && ((word.value|word.unknown)&0x40u))
        throw IopStop{"SPU2 IRQ changes while ADMA uses shared RAM are not modeled"};
    if(mode==0x30u)throw IopStop{"SPU2 DMA read processing/control configuration is not modeled"};
    const bool normal_pending=(spu2_adma_[0].normal && spu2_adma_[0].next) ||
                              (spu2_adma_[1].normal && spu2_adma_[1].next);
    if(normal_pending && ((word.value|word.unknown)&0xC0u))
        throw IopStop{"SPU2 effects/IRQ changes during normal DMA are not modeled"};
    if(spu2_adma_[core].normal && spu2_adma_[core].next &&
       ((word.value&0x8030u)!=0x8020u))throw IopStop{"SPU2 normal DMA cancellation while data is pending is not modeled"};
    auto& transfer=spu2_transfer_[core];
    if(transfer.next && (mode!=0x10u || !(word.value&0x8000u)))
        throw IopStop{"SPU2 PIO cancellation while real FIFO data is pending is not modeled"};
    if((spu2_transfer_[0].next || spu2_transfer_[1].next) && ((word.value|word.unknown)&0x40u))
        throw IopStop{"SPU2 IRQ changes during a shared-RAM PIO transfer are not modeled"};
    if(word.value&0x8000u) {
        // The explicit cold-state experiment has no active voices/inputs.
        // Mute polarity is not needed to prove a zero source sum.
        if(!diagnostic_spu2_quiescent_ || ((word.value|word.unknown)&0x40u) || (word.unknown&0x80u))
            throw IopStop{"SPU2 active voices, effects or IRQ processing is not modeled"};
    }
    std::optional<std::pair<std::uint32_t,std::uint32_t>> invalidate_effects;
    if((word.value&0x8080u)==0x8080u) {
        for(unsigned channel=0;channel<2u;++channel) {
            const auto gain=latch(0x1F900764u+core*0x28u+channel*2u);
            if(!gain || !*gain || **gain!=0u)throw IopStop{"SPU2 uncomputed effects require known zero left and right output gains"};
        }
        const auto other=spu2_control_[core^1u];
        if(!other || ((other->value|other->unknown)&0x40u))
            throw IopStop{"SPU2 effect RAM writes require both IRQ comparators disabled"};
        if(mode || spu2_transfer_[0].next || spu2_transfer_[1].next || spu2_adma_[0].enabled || spu2_adma_[1].enabled)
            throw IopStop{"SPU2 active effects and transfer bus arbitration is not modeled"};
        invalidate_effects=spu2_effect_area(core);
    }
    if(mode==0x10u)validate_spu2_pio(core,word);
    // Validate before changing retained control, clocks or pending work.
    if(invalidate_effects && !spu2_ram_written_.empty()) {
        // The DSP may write any workspace tap. Preserve backing bytes but
        // invalidate knowledge; never pretend that processing cleared RAM.
        std::fill(spu2_ram_written_.begin()+invalidate_effects->first,
                  spu2_ram_written_.begin()+invalidate_effects->second+1u,std::uint8_t{0});
    }
    spu2_effect_area_dirty_[core]=false;
    if(word.value&0x8000u) {
        // 36.864 MHz / 48 kHz = 768 IOP cycles; reset-aligned phase is modeled.
        if(!spu2_next_sample_[core])spu2_next_sample_[core]=(cycle_/768u+1u)*768u;
    } else spu2_next_sample_[core].reset();
    spu2_control_[core]=word;
    if(mode==0x10u && !transfer.fifo.empty() && !transfer.next)
        transfer.next=cycle_+diagnostic_spu2_pio_cycles_;
    spu2_clock_active_=spu2_next_sample_[0].has_value() || spu2_next_sample_[1].has_value();
}
void Iop::tick_spu2_quiet() {
    for(unsigned core=0;core<2;++core) {
        if(spu2_next_sample_[core] && cycle_>=*spu2_next_sample_[core]) {
            if(spu2_effect_area_dirty_[core] && spu2_effects_active(core)) {
                // ESA/EEA halfword writes are not atomic as a group. Consume
                // their final configuration at the next synthesis boundary,
                // before any RAM-dependent audio state can be observed.
                const auto area=spu2_effect_area(core);
                if(!spu2_ram_written_.empty())std::fill(spu2_ram_written_.begin()+area.first,
                    spu2_ram_written_.begin()+area.second+1u,std::uint8_t{0});
                spu2_effect_area_dirty_[core]=false;
            }
            const auto off=std::uint32_t{spu2_keys_[core][2].value_or(0)}|
                (std::uint32_t{spu2_keys_[core][3].value_or(0)}<<16u);
            const auto on=std::uint32_t{spu2_keys_[core][0].value_or(0)}|
                (std::uint32_t{spu2_keys_[core][1].value_or(0)}<<16u);
            const auto releasing=off&spu2_symbolic_voices_[core]&~on;
            if(releasing)for(unsigned voice=0;voice<24u;++voice)if(releasing&(1u<<voice)) {
                auto& upper=spu2_release_upper_[core][voice];
                if(upper && *upper==0u)continue; // another KOFF cannot raise a stopped envelope
                upper=std::uint16_t{0x7FFFu};spu2_release_pending_[core]|=1u<<voice;
            }
            if(spu2_release_pending_[core])for(unsigned voice=0;voice<24u;++voice)
                if(spu2_release_pending_[core]&(1u<<voice)) {
                    auto& upper=spu2_release_upper_[core][voice];
                    const auto adsr=latch(0x1F900008u+core*0x400u+voice*16u);
                    if(!adsr || !*adsr || (**adsr&0x20u) || (**adsr&31u)>11u) {
                        upper.reset();spu2_release_pending_[core]&=~(1u<<voice);continue;
                    }
                    // Linear release shift <= 11 advances every 48 kHz edge.
                    // A reachable envelope is in [0,32767]. Subtracting
                    // the same positive step shrinks that interval to zero.
                    const auto step=8u<<(11u-(**adsr&31u));
                    *upper=static_cast<std::uint16_t>(*upper>step ? *upper-step : 0u);
                    if(!*upper)spu2_release_pending_[core]&=~(1u<<voice);
                }
            // Pending key commands are consumed at a synthesis boundary.
            // Their observable readback is not an active-voice bitmap.
            for(auto& key:spu2_keys_[core])if(key)key=0;
            spu2_endx_[core].unknown|=spu2_symbolic_voices_[core];
            consume_spu2_input(core,*spu2_next_sample_[core]);
            spu2_last_sample_[core]=Spu2Sample{*spu2_next_sample_[core],0,0};
            ++spu2_sample_count_[core];*spu2_next_sample_[core]+=768u;
        }
        auto& transfer=spu2_transfer_[core];
        if(transfer.next && cycle_>=*transfer.next) {
            if(spu2_ram_.empty()){spu2_ram_.resize(0x100000u);spu2_ram_written_.resize(0x100000u);}
            const auto address=*transfer.address;
            spu2_ram_[address]=transfer.fifo.front();spu2_ram_written_[address]=1u;
            transfer.fifo.pop_front();++transfer.words;
            transfer.address=(address+1u)&0xFFFFFu;
            transfer.high=static_cast<std::uint16_t>(*transfer.address>>16u);
            transfer.low=static_cast<std::uint16_t>(*transfer.address);
            if(transfer.fifo.empty()) {
                transfer.next.reset();
                events_.push_back({cycle_,"DIAGNOSTIC SPU2 core "+std::to_string(core)+" real PIO FIFO drained"});
            } else *transfer.next+=diagnostic_spu2_pio_cycles_;
        }
    }
    // One shared, bounded bus slot per configured word interval. Channel
    // priority/DMACEN gates still apply. Cross-controller grant order is an
    // explicit round-robin experiment, not a measured IOP arbitration rule.
    if(cycle_>=spu2_adma_bus_available_)for(unsigned n=0;n<2;++n) {
        const auto core=(spu2_adma_last_grant_+1u+n)&1u;
        if(tick_spu2_adma(core)) {
            spu2_adma_last_grant_=core;spu2_adma_bus_available_=cycle_+diagnostic_spu2_adma_cycles_;break;
        }
    }
}
void Iop::reset_sio2_observed(ObservedWord word,std::uint64_t epoch) {
    if(word.unknown&1u)throw IopStop{"SIO2 control start bit is unknown"};
    if(word.value&1u) {
        if(!epoch || epoch!=sio2_control_epoch_)throw IopStop{"SIO2 start does not preserve the current opaque control sample"};
        start_sio2_infrared(sio2_diagnostic_control_.value_or(sio2_interrupt_enable_));return;
    }
    if((word.unknown&0xCu) || (word.value&0xCu)!=0xCu)
        throw IopStop{"SIO2 symbolic write does not establish both reset bits"};
    if(!epoch || epoch!=sio2_control_epoch_)
        throw IopStop{"SIO2 symbolic reset does not preserve the current opaque control sample"};
    // The guest read this exact sample and only ORed the reset command.
    // Preserve its unresolved configuration; do not invent readback bits.
    reset_sio2_completion();
    ++sio2_control_epoch_;
    sio2_input_.clear();sio2_output_.clear();sio2_complete_at_.reset();sio2_response_valid_=false;
    for(auto& [address,value]:latches_)if(address==0x1F808268u){value.reset();return;}
    throw IopStop{"SIO2 control sample has no matching configured register"};
}

void Iop::store(std::uint32_t address, std::size_t bytes, std::optional<std::uint32_t> value, const char* what,
                const ObservedWord* observed,std::uint64_t sio2_epoch) {
    const auto a = translate(address, bytes, true, what);
    const auto sr = *cop0_[Sr];
    if ((sr & kSrSwc) != 0u) throw IopStop{std::string("store ") + what + " with Status.SwC: not modeled"};
    // Isolated cache: cached stores reach only the cache (the reset code
    // clears the cache this way; instructions are always fetched from memory,
    // so the cache contents are not modeled). Uncached (KSEG1/KSEG2) accesses
    // still reach the bus (hypothesis: the reset code writes its POST register
    // through KSEG1 while IsC is set).
    if ((sr & kSrIsc) != 0u && a.cached && !a.kseg2) return;
    if (a.kseg2) return io_write(a.physical, bytes, value, what);
    if (a.physical < kIopRamBytes) {
        const auto raw=value.value_or(observed ? observed->value : 0u);
        for (std::size_t i = 0; i < bytes; ++i) {
            ram_[a.physical + i] = static_cast<std::uint8_t>(raw >> (8u * i));
            // Copying opaque data does not consume it. Preserve each fully
            // known byte, while a partially known byte remains unknown.
            ram_written_[a.physical + i] = value || (observed && !(observed->unknown&(0xFFu<<(8u*i)))) ? 1u : 0u;
        }
        return;
    }
    if(a.physical>=0x1E000000u && a.physical<0x1E400000u)
        throw IopStop{std::string("store ")+what+" to read-only ROM1 at "+hex(address)};
    if (a.physical >= kRomPhysical && a.physical - kRomPhysical < kRomBytes)
        throw IopStop{std::string("store ") + what + " to ROM0 at " + hex(address)};
    if((a.physical==0x1F90019Au || a.physical==0x1F90059Au) && bytes==2u && observed) {
        write_spu2_control(a.physical,*observed);return;
    }
    if((is_spu2_voice_start(a.physical) || is_spu2_effect_address(a.physical)) && bytes==2u && observed) {
        const auto mask=(a.physical&2u) ? (is_spu2_voice_start(a.physical) ? 0xFFF8u : 0xFFFFu) : 0xFu;
        if(observed->unknown&mask)throw IopStop{"SPU2 address configuration consumes unknown address bits"};
        io_write(a.physical,bytes,observed->value&mask,what);return;
    }
    if((a.physical==0x1F900340u || a.physical==0x1F900342u ||
        a.physical==0x1F900740u || a.physical==0x1F900742u) && bytes==2u && observed) {
        // ENDX is clear-on-write, not write-one-to-clear. Its payload bits
        // are not consumed, including reserved bits copied from readback.
        io_write(a.physical,bytes,0u,what);return;
    }
    if(a.physical>=0x1F900000u && a.physical<0x1F900800u &&
       (a.physical&0x3FFu)>=0x1A0u && (a.physical&0x3FFu)<=0x1A6u && bytes==2u && observed) {
        const auto mask=(a.physical&2u) ? 0xFFu : 0xFFFFu;
        if(observed->unknown&mask)throw IopStop{"SPU2 key command consumes an unknown voice mask"};
        io_write(a.physical,bytes,observed->value&mask,what);return;
    }
    if(a.physical==0x1F808268u && bytes==4u && observed) {
        reset_sio2_observed(*observed,sio2_epoch);return;
    }
    io_write(a.physical, bytes, value, what);
}

std::uint32_t Iop::io_read(std::uint32_t physical, std::size_t bytes, const char* what) {
    if(physical>=0x1F900000u && physical<0x1F900800u &&
       (physical&0x3FFu)<0x180u && (physical&15u)==0xAu) {
        if(bytes!=2u)throw IopStop{"SPU2 ENVX requires a halfword read"};
        const auto& upper=spu2_release_upper_[(physical>>10u)&1u][(physical&0x3FFu)/16u];
        if(!upper || *upper)throw IopStop{"SPU2 ENVX is unknown before a proven completed linear release"};
        return 0u;
    }
    if(physical==0x1F900340u || physical==0x1F900342u || physical==0x1F900740u || physical==0x1F900742u) {
        if(bytes!=2u)throw IopStop{"SPU2 ENDX requires halfword reads"};
        const auto& end=spu2_endx_[(physical>>10u)&1u];
        const auto shift=(physical&2u)*8u,mask=(physical&2u) ? 0xFFu : 0xFFFFu;
        if((end.unknown>>shift)&mask)throw IopStop{"SPU2 ENDX depends on an unknown decoder or reset state"};
        if(physical&2u) {
            undefined_load_="SPU2 ENDX: reserved upper voice bits are unknown";
            undefined_known_=0xFFu|(std::string_view(what)=="LHU" ? 0xFFFF0000u : 0u);
        }
        return (end.value>>shift)&mask;
    }
    if(physical>=0x1F900000u && physical<0x1F900800u &&
       (physical&0x3FFu)>=0x1A0u && (physical&0x3FFu)<=0x1A6u) {
        if(bytes!=2u || (physical&1u))throw IopStop{"SPU2 key command readback requires a halfword"};
        const auto& key=spu2_keys_[(physical>>10u)&1u][((physical&0x3FFu)-0x1A0u)/2u];
        if(!key)throw IopStop{"SPU2 key command readback before a known write"};
        if(physical&2u) {
            undefined_load_="SPU2 key command: reserved upper voice bits are unknown";
            undefined_known_=0xFFu|(std::string_view(what)=="LHU" ? 0xFFFF0000u : 0u);
        }
        return *key;
    }
    if((physical>=0x1F8010C0u && physical<0x1F8010CCu) || (physical>=0x1F801500u && physical<0x1F80150Cu)) {
        const auto core=physical>=0x1F801500u ? 1u : 0u;
        return read_spu2_dma(core,physical-(core ? 0x1F801500u : 0x1F8010C0u),bytes);
    }

    if(physical>=0x1F801540u && physical<0x1F801560u)
        return read_sio2_dma((physical>>4u)&1u,physical&0xFu,bytes);
    if (physical>=0x1F801520u && physical<0x1F801530u) {
        if(bytes==2u && (physical==0x1F801524u || physical==0x1F801526u)) {
            const auto upper=(physical&2u)!=0u;
            if(!(iop_sif0_written_&(upper ? 0x20u : 0x10u)))
                throw IopStop{"IOP SIF0 BCR halfword read before write"};
            return (iop_sif0_[1]>>(upper ? 16u : 0u))&0xFFFFu;
        }
        if(bytes!=4u)throw IopStop{"SIF0 DMA requires 32-bit or BCR halfword accesses"};
        return iop_sif0_read(physical);
    }
    if (physical >= 0x1F801530u && physical < 0x1F801540u) {
        if (bytes != 4u) throw IopStop{"SIF1 DMA requires 32-bit accesses"};
        return iop_sif1_read(physical);
    }
    const auto name = std::string("load ") + what + " of IOP register " + hex(physical);
    if(physical==0x1F9001A8u || physical==0x1F9001AAu || physical==0x1F9005A8u || physical==0x1F9005AAu) {
        if(bytes!=2u)throw IopStop{"SPU2 transfer address requires halfword reads"};
        const auto& transfer=spu2_transfer_[(physical-0x1F9001A8u)/0x400u];
        const auto half=(physical&2u) ? transfer.low : transfer.high;
        if(!half)throw IopStop{"SPU2 transfer address halfword is unknown before write"};
        if(!(physical&2u)) {
            undefined_load_="SPU2 transfer-address reserved high bits are unknown";
            undefined_known_=0xFu|(std::string_view(what)=="LHU" ? 0xFFFF0000u : 0u);
        }
        return *half;
    }
    if(physical==0x1F900344u || physical==0x1F900744u) {
        if(bytes!=2u)throw IopStop{"SPU2 STATX requires halfword reads"};
        const auto core=(physical-0x1F900344u)/0x400u;
        const auto word=spu2_control_[core];
        if(!word || !spu2_next_sample_[core])throw IopStop{"SPU2 STATX is not established by a quiet enabled core"};
        // Busy follows actual queued data. Normal-DMA ready is established
        // only by a transfer we have observed, never by a cold readback.
        undefined_load_="SPU2 STATX: status outside the quiet transfer/IRQ fields is unknown";
        auto known=0x7FFu;
        if((word->value|word->unknown)&0xFu)known&=~0xFu;
        if(word->value&0x30u)known&=~0x30u; // transfer-mode echo fields remain unverified
        undefined_known_=known|(std::string_view(what)=="LHU" ? 0xFFFF0000u : 0u);
        const auto& dma=spu2_adma_[core];
        const bool normal_mode=(word->value&0x30u)==0x20u;
        if(normal_mode && !dma.normal)undefined_known_&=~0x80u;
        if(spu2_transfer_[core].next || (dma.normal && dma.next))return 0x400u;
        return normal_mode && dma.normal ? 0x80u : 0u;
    }
    if(physical==0x1F9001B0u || physical==0x1F9005B0u) {
        if(bytes!=2u)throw IopStop{"SPU2 AutoDMA status requires a halfword read"};
        const auto core=(physical-0x1F9001B0u)/0x400u;
        const auto& state=spu2_adma_[core];
        const bool retained=diagnostic_spu2_admas_control_latch_ && state.control_written;
        if((!diagnostic_spu2_adma_cycles_ || !state.next || state.normal) && !retained)
            throw IopStop{"SPU2 AutoDMA inactive/readback flags are not verified"};
        // Separate, explicit whole-control latch hypothesis. This is only
        // readback of a known guest write, never a cold status default.
        // Without opt-in, only an ongoing transfer's busy bit is established.
        if(retained)return state.enabled ? 1u<<core : 0u;
        // ps2tek ADMA_CTRL: own bit 0/1 indicates an ongoing ADMA write.
        // A real queued transfer proves busy, without inventing reset,
        // completion or unrelated readback flags. Full unknown consumption
        // still fails; BNE can prove nonzero from this one active bit.
        const auto busy=1u<<core;
        undefined_load_="SPU2 AutoDMA flags outside the ongoing transfer bit are unknown";
        undefined_known_=busy|(std::string_view(what)=="LHU" ? 0xFFFF0000u : 0u);
        return busy;
    }
    if(physical==0x1F90019Au || physical==0x1F90059Au) {
        if(bytes!=2u)throw IopStop{"SPU2 control requires halfword reads"};
        auto word=spu2_control_[(physical-0x1F90019Au)/0x400u];
        if(!word && diagnostic_spu2_quiescent_)word=ObservedWord{0u,0x7FFFu};
        if(!word)throw IopStop{name+": SPU2 control before a known halfword write"};
        if(word->unknown) {
            undefined_load_="SPU2 control: other control fields are unknown";
            undefined_known_=~word->unknown;
        }
        return word->value;
    }
    if(is_spu2_boot_config(physical)) {
        // PS2SDK spu2regs.h: digital-output and constant level configuration.
        // This does not establish core state, IRQ status or audio samples.
        if(is_spu2_effect_offset(physical))throw IopStop{"SPU2 effect-offset readback is not verified"};
        if(bytes!=2u)throw IopStop{"SPU2 output configuration requires halfword reads"};
        const auto value=latch(physical);
        if((physical==0x1F90033Cu || physical==0x1F90073Cu) && (!value || !*value) && diagnostic_spu2_effect_end_)
            return (*diagnostic_spu2_effect_end_)[(physical>>10u)&1u];
        if(!value || !*value)throw IopStop{name+": SPU2 output configuration before a known halfword write"};
        if(is_spu2_voice_start(physical)) {
            const auto mask=(physical&2u) ? 0xFFF8u : 0xFu;
            undefined_load_="SPU2 voice start address: reserved readback bits are unknown";
            undefined_known_=mask|(std::string_view(what)=="LHU" ? 0xFFFF0000u : 0u);
            return **value&mask;
        }
        if(is_spu2_effect_address(physical) && !(physical&2u)) {
            // ESA high and EEA identify the high four bits of a 20-bit
            // word address. Reserved readback bits have not been measured.
            if(diagnostic_spu2_effect_address_reserved_zero_)return **value&0xFu;
            undefined_load_="SPU2 effect-address reserved readback bits are unknown";
            undefined_known_=0xFu|(std::string_view(what)=="LHU" ? 0xFFFF0000u : 0u);
            return **value&0xFu;
        }
        if(is_spu2_voice_routing(physical) && ((physical&0x3FFu)!=0x198u) && (physical&2u)) {
            // Each upper half selects voices 16..23. Its unused eight
            // readback bits are unverified; consumers may use the bitmap.
            undefined_load_="SPU2 voice-routing reserved readback bits are unknown";
            undefined_known_=0xFFu|(std::string_view(what)=="LHU" ? 0xFFFF0000u : 0u);
            return **value&0xFFu;
        }
        return **value;
    }
    if(physical==0x1F808264u) {
        if(bytes!=1u)throw IopStop{"SIO2 output FIFO requires byte reads"};
        if(sio2_output_.empty())throw IopStop{"SIO2 output FIFO has no completed serial byte"};
        const auto byte=sio2_output_.front();sio2_output_.pop_front();
        if(byte.unknown) {
            undefined_load_="SIO2 unclocked DMA padding has no measured value";
            undefined_known_=0xFFFFFF00u|(~byte.unknown&0xFFu);
        }
        return byte.value;
    }
    if(physical==0x1F80826Cu) {
        if(bytes!=4u)throw IopStop{"SIO2 response status requires a word read"};
        if(sio2_complete_at_) {
            // PS2SDK MX4SIO sio2regs.h: STAT.12 is clear while executing.
            // No other status bit is inferred before serial completion.
            undefined_load_="SIO2 response bits before serial completion are unknown";
            undefined_known_=0x1000u;return 0;
        }
        if(!sio2_response_valid_)throw IopStop{"SIO2 response status is unknown before a completed transfer"};
        // ps2tek: disconnected response, not a successful peripheral reply.
        return sio2_response_status_;
    }
    if(physical==0x1F808270u || physical==0x1F808274u) {
        if(bytes!=4u)throw IopStop{"SIO2 RECV2/RECV3 require word reads"};
        // These read-only status samples have no established numeric value.
        // Firmware may transport them; only consuming their bits must stop.
        undefined_load_="SIO2 RECV2/RECV3 response bits are unverified";
        if(physical==0x1F808270u && diagnostic_sio2_port_timing_) {
            undefined_known_=0x30u;return *diagnostic_sio2_port_timing_<<4u;
        }
        undefined_known_=0;return 0;
    }
    if(physical==0x1F808280u) {
        if(bytes!=4u || !diagnostic_sio2_timeout_cycles_)
            throw IopStop{"SIO2 interrupt status bit layout is unverified"};
        return sio2_completion_flag_ ? 1u : 0u;
    }
    if(physical==0x1F808268u) {
        if(bytes!=4u)throw IopStop{"SIO2 control requires a 32-bit read"};
        const bool configured=latch(physical)!=nullptr && sio2_control_epoch_!=0;
        // Experimental readback only: self-clearing command bits and retained
        // configuration. Default boot keeps the original unresolved sample.
        if(diagnostic_sio2_timeout_cycles_ && sio2_diagnostic_control_)
            return *sio2_diagnostic_control_;
        undefined_load_="SIO2 control reset/status readback is not verified";
        undefined_known_=configured ? 1u : 0u; // no accepted transfer can be busy
        undefined_sio2_epoch_=configured && std::string_view(what)=="LW" ? sio2_control_epoch_ : 0u;
        return 0; // unknown backing bits, never a numeric register response
    }
    if(physical==0x1D000040u) {
        if(bytes!=4u) throw IopStop{name+": only 32-bit control reads modeled"};
        // Only the experimental SIF0/1 ready bits have a value. In
        // particular, do not invent the widely used F0000002 constant.
        if(!undefined_load_) {
            undefined_load_=name+": unverified SIF_CTRL bits";
            undefined_known_=diagnostic_sbus_latches_ ? 0x60u : 0u;
        }
        return diagnostic_sif_ready_;
    }
    if (physical == 0x1F803800u) {
        if(bytes!=2u) throw IopStop{name+": MPU board ID requires a 16-bit read"};
        if(!hardware_ || !hardware_->mpu_board_id)
            throw IopStop{name+": MPU board ID has not been measured for this profile"};
        return *hardware_->mpu_board_id;
    }
    if (physical == kToolBoardIdPhysical) {
        if (bytes != 1u) throw IopStop{name + ": only 8-bit reads are modeled"};
        return kRetailToolBoardId;
    }
    if (physical == kSifMscom || physical == kSifSmcom || physical == kSifMsflg || physical == kSifSmflg) {
        if (bytes != 4u) throw IopStop{name + ": only 32-bit SIF accesses are modeled"};
        if (physical == kSifMsflg) return sif_msflg_;
        if (physical == kSifSmflg) return sif_smflg_;
        const auto& value = physical == kSifMscom ? sif_mscom_ : sif_smcom_;
        if (!value && !undefined_load_)
            undefined_load_ = name + " at IOP " + hex(pc_) + " before any write (reset value unknown)";
        return value.value_or(0u);
    }
    if (physical == kSifBd6) {
        if (bytes != 4u) throw IopStop{name + ": only 32-bit reads are modeled"};
        // SIFMAN stays resident only when BD6 reads 0x1D000060 or a value
        // below 0x1000 (ps2sdk sifman.c, "based on the module from SCE SDK
        // 3.1.0"), as on every retail console. Hypothesis: the value below
        // 0x1000 (PCSX2 reads 0 on the IOP side); its low 12 bits are unknown.
        if (!undefined_load_) {
            undefined_load_ = name + " at IOP " + hex(pc_) + ": SIF_BD6 bits 0-11 are not established";
            undefined_known_ = 0xFFFFF000u;
        }
        return 0u;
    }
    if (physical == kIlinkRegister7C) {
        if (bytes != 4u) throw IopStop{name + ": only 32-bit reads are modeled"};
        return kIlinkRegister7CValue;
    }
    if (physical == kIlinkNodeId) {
        if (bytes != 4u) throw IopStop{name + ": only 32-bit reads are modeled"};
        if (!ilink_bus_reset_) throw IopStop{name + " (NodeID) before any bus reset: its contents are not established"};
        // Hypothesis: the self-identification of a bus with no other node ends
        // before the next read, so bit 0 (node ID valid) reads 1. The node and
        // bus numbers are unknown.
        if (!undefined_load_) {
            undefined_load_ = name + " at IOP " + hex(pc_) + ": NodeID bits 1-31 after a bus reset are not established";
            undefined_known_ = 1u;
        }
        return 1u;
    }
    if (physical == kDev9Revision) {
        // ps2sdk dev9regs.h defines a 16-bit register. The profile records
        // only its measured revision byte; preserve the unmeasured lane.
        if(bytes!=1u && bytes!=2u)throw IopStop{name+": only byte and halfword reads are modeled"};
        if(hardware_==nullptr || !hardware_->dev9_revision)
            throw IopStop{name+" (DEV9_R_REV): no console profile with the SSBUS revision"};
        if(bytes==2u) {
            undefined_load_="DEV9_R_REV bits 8..15 have not been measured for this profile";
            undefined_known_=0xFFu|(std::string_view(what)=="LHU" ? 0xFFFF0000u : 0u);
        }
        return *hardware_->dev9_revision;
    }
    if (physical == kCdvdNStatus) {
        if (bytes != 1u) throw IopStop{name + ": only 8-bit reads are modeled"};
        return (cdvd_complete_at_ || cdvd_sectors_) ? ((kCdvdNStatusIdle&~0x40u)|0x80u) : kCdvdNStatusIdle;
    }
    if(physical==0x1F402006u) {
        if(bytes!=1u || !disc_)throw IopStop{name+": CDVD error requires an explicit disc and byte access"};
        return 0; // No media error: invalid/unimplemented operations refuse before issue.
    }
    if(physical==0x1F40200Au || physical==0x1F40200Bu || physical==0x1F40200Fu) {
        if(bytes!=1u || !disc_)throw IopStop{name+": requires a byte access and an explicitly mounted disc"};
        if(physical==0x1F40200Fu)return cdvd_type_;
        // Bits 6-7 are undocumented, not assumed zero. The defined drive
        // flags can be masked/compared through the existing knowledge model.
        if(!undefined_load_) {
            undefined_load_=name+": drive status bits 6-7 are not established";
            undefined_known_=0xFFFFFF3Fu;
        }
        return physical==0x1F40200Au ? cdvd_status_ : cdvd_sticky_;
    }
    if(physical==0x1F402008u) {
        if(bytes!=1u) throw IopStop{name+": CDVD I_STAT requires a byte"};
        return cdvd_irq_;
    }
    if(physical==0x1F402004u) {
        if(bytes!=1u || !cdvd_ncommand_) throw IopStop{name+": N command byte is unknown before issue"};
        return *cdvd_ncommand_;
    }
    if (physical == kCdvdSCommand || physical == kCdvdSStatus || physical == kCdvdSResult) {
        if (bytes != 1u) throw IopStop{name + ": only 8-bit accesses are modeled"};
        if (physical == kCdvdSStatus) return s_results_.empty() ? kSStatusNoResult : 0u;
        if (physical == kCdvdSCommand) {
            if (!s_command_) throw IopStop{name + " before any S command (reset value unknown)"};
            return *s_command_;
        }
        if (s_results_.empty()) throw IopStop{name + ": no result byte is pending"};
        const auto result = s_results_.front();
        s_results_.pop_front();
        if(result.known!=0xFFu) {
            undefined_load_=result.origin;
            undefined_known_=result.known|(std::string_view(what)=="LBU" ? 0xFFFFFF00u : 0u);
        }
        return result.value;
    }
    if (const auto timer = timer_index(physical)) return timer_read(*timer, physical & 0xFu, name);
    if (physical == kIStat) {
        if (bytes != 4u) throw IopStop{name + ": only 32-bit accesses are modeled"};
        // Timer, DMA and (when the GS timing is known) VBLANK bits are known;
        // the other sources are not modeled.
        if (!undefined_load_) {
            undefined_load_ = name + " at IOP " + hex(pc_) + ": I_STAT bits of sources other than the timers, "
                              "VBLANK and DMA (CDVD, SIO2, SPU2, ...) are not modeled";
            undefined_known_ = modeled_irqs();
        }
        return istat_;
    }
    if (is_latch(physical)) {
        if (bytes != 4u) throw IopStop{name + ": only 32-bit accesses are modeled"};
        for (auto& [address, value] : latches_)
            if (address == physical) {
                if (!value && !undefined_load_) undefined_load_ = name + " at IOP " + hex(pc_) + ": unknown value written back";
                auto result = value.value_or(0u);
                if (physical == kDicr) {
                    const auto* second = latch(kDicr2);
                    const auto flags = result & 0x7F000000u;
                    const auto flags2 = second ? second->value_or(0u) & 0x3F000000u : 0u;
                    const auto* gate=latch(0x1F80157Cu);
                    if ((result & (1u << 15)) || ((result & (1u << 23)) && (flags || flags2) &&
                        gate && *gate && (**gate&1u))) result |= 1u << 31;
                }
                if (physical == kICtrl) {
                    // Only bit 0 exists; the read clears it.
                    value = 0u;
                    return result & 1u;
                }
                return result;
            }
        // Unknown reset value: the loaded register is unknown, not invented.
        if (!undefined_load_) undefined_load_ = name + " at IOP " + hex(pc_) + " before any write (reset value unknown)";
        return 0u;
    }
    throw IopStop{name + ": device not implemented"};
}

void Iop::io_write(std::uint32_t physical, std::size_t bytes, std::optional<std::uint32_t> value, const char* what) {
    if(physical==0x1F900770u || physical==0x1F900772u || physical==0x1F900798u || physical==0x1F90079Au) {
        if(bytes!=2u)throw IopStop{"SPU2 current master volume accepts only halfword write attempts"};
        // MVOLX is read-only. A write cannot change the current gain or
        // establish readback; its data has no effect, even if unknown.
        return;
    }
    if(physical==0x1F900340u || physical==0x1F900342u || physical==0x1F900740u || physical==0x1F900742u) {
        if(bytes!=2u || !value || !diagnostic_spu2_quiescent_)
            throw IopStop{"SPU2 ENDX clear requires a known halfword and established voice state"};
        // Writing either bank clears that bank, independently of data.
        // A later decoder event can set it again; symbolic voices lose
        // this proof at the next sample edge, not by an invented END event.
        const auto mask=(physical&2u) ? 0xFF0000u : 0xFFFFu;
        auto& end=spu2_endx_[(physical>>10u)&1u];end.value&=~mask;end.unknown&=~mask;
        return;
    }
    if((physical>=0x1F8010C0u && physical<0x1F8010CCu) || (physical>=0x1F801500u && physical<0x1F80150Cu)) {
        if(!value)throw IopStop{"SPU2 DMA register write consumes unknown bits"};
        const auto core=physical>=0x1F801500u ? 1u : 0u;
        write_spu2_dma(core,physical-(core ? 0x1F801500u : 0x1F8010C0u),bytes,*value);return;
    }
    if(physical==0x1F9001B0u || physical==0x1F9005B0u) {
        if(bytes!=2u || !value)throw IopStop{"SPU2 AutoDMA control requires a known halfword write"};
        const auto core=(physical-0x1F9001B0u)/0x400u;auto& state=spu2_adma_[core];
        if(*value) {
            if(*value!=(1u<<core))throw IopStop{"SPU2 AutoDMA read mode and other control fields are not modeled"};
            validate_spu2_adma(core);
            if(!state.enabled){state.ready={};state.primed=false;state.fill_bank=state.fill_word=state.read_bank=state.read_position=0;}
            state.enabled=true;
        } else {
            if(state.next)throw IopStop{"SPU2 AutoDMA disable with real DMA data pending is not modeled"};
            state.enabled=false; // preserve the actual RAM bytes, never clear a buffer synthetically
        }
        state.control_written=true;return;
    }

    if(physical==0x1F9001A8u || physical==0x1F9001AAu || physical==0x1F9005A8u || physical==0x1F9005AAu) {
        if(bytes!=2u || !value)throw IopStop{"SPU2 transfer address requires a known halfword write"};
        auto& transfer=spu2_transfer_[(physical-0x1F9001A8u)/0x400u];
        if(transfer.next)throw IopStop{"SPU2 transfer address change while PIO is busy is not modeled"};
        if(spu2_adma_[(physical-0x1F9001A8u)/0x400u].next)
            throw IopStop{"SPU2 transfer address change while DMA data is pending is not modeled"};
        const auto control=spu2_control_[(physical-0x1F9001A8u)/0x400u];
        if((physical&2u) && (*value&7u) && control && (control->value&0x30u)==0x10u)
            throw IopStop{"SPU2 unaligned PIO address reconfiguration is not modeled"};
        // TSA is configured independently of starting a transfer. Keep its
        // low halfword intact; transfer-mode validation decides whether the
        // configured address can be consumed, without silently rounding it.
        if(physical&2u)transfer.low=static_cast<std::uint16_t>(*value);
        else transfer.high=static_cast<std::uint16_t>(*value&0xFu);
        if(transfer.high && transfer.low)transfer.address=(std::uint32_t{*transfer.high}<<16u)|*transfer.low;
        return;
    }
    if(physical==0x1F9001ACu || physical==0x1F9005ACu) {
        if(bytes!=2u || !value)throw IopStop{"SPU2 PIO FIFO requires a known halfword write"};
        const auto core=(physical-0x1F9001ACu)/0x400u;
        auto& transfer=spu2_transfer_[core];const auto control=spu2_control_[core];
        if(spu2_adma_[core].enabled)throw IopStop{"SPU2 normal FIFO and ADMA cannot run together"};
        if(!diagnostic_spu2_pio_cycles_)throw IopStop{"SPU2 PIO FIFO timing requires an explicit word-cycle experiment"};
        if(!control || (control->unknown&0x30u) || (control->value&0x20u))
            throw IopStop{"SPU2 FIFO requires a known stopped or PIO transfer mode"};
        if(transfer.fifo.size()>=32u)throw IopStop{"SPU2 32-halfword PIO FIFO is full; overflow behavior is not modeled"};
        if(control->value&0x10u)validate_spu2_pio(core,*control);
        transfer.fifo.push_back(static_cast<std::uint16_t>(*value));
        if((control->value&0x10u) && !transfer.next)transfer.next=cycle_+diagnostic_spu2_pio_cycles_;
        return;
    }

    if(physical>=0x1F900000u && physical<0x1F900800u &&
       (physical&0x3FFu)>=0x1A0u && (physical&0x3FFu)<=0x1A6u) {
        if(bytes!=2u || !value || !diagnostic_spu2_quiescent_)
            throw IopStop{"SPU2 KON/KOFF requires established voice state and a known halfword mask"};
        const auto core=(physical>>10u)&1u,index=((physical&0x3FFu)-0x1A0u)/2u;
        const auto half=*value&((index&1u) ? 0xFFu : 0xFFFFu);
        const auto mask=half<<((index&1u)*16u);
        if(index<2u && mask) {
            // Reads by an unknown decoder cannot raise IRQ when both
            // comparators are explicitly disabled. Check the other core too.
            for(const auto& control:spu2_control_)
                if(!control || ((control->value|control->unknown)&0x40u))
                    throw IopStop{"SPU2 KON requires both RAM IRQ comparators explicitly disabled"};
            if(!spu2_next_sample_[core])throw IopStop{"SPU2 KON requires an enabled synthesis clock"};
            for(unsigned voice=0;voice<24u;++voice)if(mask&(1u<<voice)) {
                for(unsigned channel=0;channel<2u;++channel) {
                    const auto gain=latch(0x1F900000u+core*0x400u+voice*16u+channel*2u);
                    if(!gain || !*gain || **gain!=0u)
                        throw IopStop{"SPU2 KON requires known constant zero left and right voice gain; audible synthesis is not modeled"};
                }
            }
            // Do not fabricate ENVX, ENDX, NAX, LSAX or decoded samples.
            // A new attack invalidates any earlier release-completion proof.
            for(unsigned voice=0;voice<24u;++voice)if(mask&(1u<<voice))spu2_release_upper_[core][voice].reset();
            spu2_release_pending_[core]&=~mask;
            spu2_symbolic_voices_[core]|=mask;
        }
        spu2_keys_[core][index]=static_cast<std::uint16_t>(half);
        return;
    }

    if(physical>=0x1F801540u && physical<0x1F801560u) {
        if(!value)throw IopStop{"SIO2 DMA register write consumes unknown bits"};
        write_sio2_dma((physical>>4u)&1u,physical&0xFu,bytes,*value);return;
    }
    if (physical>=0x1F801520u && physical<0x1F801530u) {
        if(bytes==2u && (physical==0x1F801524u || physical==0x1F801526u) && value) {
            if(iop_sif0_[2]&kChcrStart)throw IopStop{"IOP SIF0 register write while busy"};
            const auto shift=(physical&2u)*8u;
            iop_sif0_[1]=(iop_sif0_[1]&~(0xFFFFu<<shift))|((*value&0xFFFFu)<<shift);
            iop_sif0_written_|=physical&2u ? 0x20u : 0x10u;
            if((iop_sif0_written_&0x30u)==0x30u)iop_sif0_written_|=2u;
            return;
        }
        if(bytes!=4u || !value)throw IopStop{"SIF0 DMA requires known 32-bit or BCR halfword accesses"};
        iop_sif0_write(physical,*value); return;
    }
    if (physical >= 0x1F801530u && physical < 0x1F801540u) {
        if(bytes==2u && (physical==0x1F801534u || physical==0x1F801536u) && value) {
            const auto shift=(physical&2u)*8u;
            iop_sif1_write(0x1F801534u,(iop_sif1_[1]&~(0xFFFFu<<shift))|((*value&0xFFFFu)<<shift));
            return;
        }
        if (bytes != 4u || !value) throw IopStop{"SIF1 DMA requires known 32-bit accesses"};
        iop_sif1_write(physical, *value);
        return;
    }
    const auto name = std::string("store ") + what + " of IOP register " + hex(physical);
    const auto spu2_offset=physical&0x7FFu;
    const bool unassigned_spu2=(spu2_offset>=0x346u && spu2_offset<=0x3FFu) ||
        (spu2_offset>=0x746u && spu2_offset<=0x75Fu) ||
        (spu2_offset>=0x7B0u && spu2_offset<=0x7BFu);
    if(physical>=0x1F900000u && physical<0x1F901000u && unassigned_spu2) {
        if(!diagnostic_spu2_reserved_latches_ || bytes!=2u || (physical&1u) || !value)
            throw IopStop{"SPU2 unassigned register write side effects are unknown (diagnostic hypothesis disabled or invalid access)"};
        for(auto& [address,stored]:latches_)if(address==physical){stored=*value&0xFFFFu;return;}
        latches_.push_back({physical,*value&0xFFFFu});return;
    }
    if(physical==0x1F9007CAu) {
        if(!diagnostic_spu2_7ca_latch_ || bytes!=2u || !value)
            throw IopStop{"SPU2 0x7CA write side effects are unknown (diagnostic latch hypothesis disabled or invalid access)"};
        for(auto& [address,stored]:latches_)if(address==physical){stored=*value&0xFFFFu;return;}
        latches_.push_back({physical,*value&0xFFFFu});
        return;
    }
    if(physical==0x1F90019Au || physical==0x1F90059Au) {
        if(bytes!=2u || !value)throw IopStop{"SPU2 control requires a known or observed halfword write"};
        write_spu2_control(physical,ObservedWord{*value,0u});return;
    }
    if(is_spu2_boot_config(physical)) {
        if(bytes!=2u || !value)throw IopStop{"SPU2 output configuration requires a known halfword write"};
        if((physical==0x1F900764u || physical==0x1F900766u || physical==0x1F90078Cu || physical==0x1F90078Eu) &&
           *value && spu2_effects_active((physical-0x1F900764u)/0x28u))
            throw IopStop{"SPU2 effect gain cannot expose an uncomputed DSP output"};
        if(is_spu2_effect_coefficient(physical) || is_spu2_effect_offset(physical)) {
            const auto core=is_spu2_effect_offset(physical) ? (physical>>10u)&1u : (physical-0x1F900760u)/0x28u;
            const auto control=spu2_control_[core];
            if(!control || (control->unknown&0x80u))
                throw IopStop{"SPU2 effect parameters require a known effect-enable state"};
            // Active DSP state is already unknown throughout its circular
            // workspace, and its output gain is proven zero. Parameter
            // changes neither restore RAM knowledge nor produce audible data.
        }
        if(is_spu2_voice_parameter(physical) || is_spu2_voice_start(physical)) {
            if(!diagnostic_spu2_quiescent_)throw IopStop{"SPU2 voice configuration requires established inactive voices"};
            if(is_spu2_voice_parameter(physical) && (physical&15u)<4u && *value &&
               (spu2_symbolic_voices_[(physical>>10u)&1u]&(1u<<((physical&0x3FFu)/16u)))) {
                const auto& upper=spu2_release_upper_[(physical>>10u)&1u][(physical&0x3FFu)/16u];
                if(!upper || *upper)throw IopStop{"SPU2 voice gain cannot expose an unknown envelope before proven release completion"};
            }
            if(is_spu2_voice_parameter(physical) && (physical&15u)<4u && (*value&0x8000u))
                throw IopStop{"SPU2 voice volume sweep is not modeled"};
            if(is_spu2_voice_start(physical))*value&=(physical&2u) ? 0xFFF8u : 0xFu;
        }
        if(is_spu2_effect_address(physical)) {
            if((spu2_adma_[0].normal && spu2_adma_[0].next) || (spu2_adma_[1].normal && spu2_adma_[1].next))
                throw IopStop{"SPU2 effect workspace change during normal DMA is not modeled"};
            const auto core=(physical-0x1F900000u)/0x400u;
            const auto control=spu2_control_[core];
            if(!control || (control->unknown&0x80u))
                throw IopStop{"SPU2 effect workspace configuration requires a known effect-enable state"};
            if(spu2_effects_active(core)) {
                // The DSP consumes the area on a synthesis edge, not between
                // consecutive CPU stores of its independently addressed halves.
                spu2_effect_area_dirty_[core]=true;
            }
        }
        if((physical==0x1F900198u || physical==0x1F900598u) &&
           spu2_adma_[(physical-0x1F900198u)/0x400u].enabled && !spu2_input_quiet((physical-0x1F900198u)/0x400u,*value))
            throw IopStop{"SPU2 audible ADMA input mixing is not modeled"};
        if(physical>=0x1F900760u && physical<0x1F9007B0u &&
           ((physical-0x1F900760u)%0x28u==0xCu || (physical-0x1F900760u)%0x28u==0xEu)) {
            const auto core=(physical-0x1F900760u)/0x28u,channel=((physical-0x1F900760u)%0x28u-0xCu)/2u;
            const auto mixer=latch(0x1F900198u+core*0x400u);
            if(spu2_adma_[core].enabled && (!mixer || !*mixer || ((**mixer&(0x80u>>channel)) && *value)))
                throw IopStop{"SPU2 nonzero ADMA input gain on an open direct route is not modeled"};
        }
        // These masks route/modulate voices; MMIX routes mixer inputs.
        // In the opt-in inactive-source state they cannot create samples.
        // Key-on, streaming and effects starts still refuse separately.
        if(is_spu2_voice_routing(physical) && !diagnostic_spu2_quiescent_)
            throw IopStop{"SPU2 voice routing requires established inactive voices and inputs"};
        // SPDIF_OUT can route the already modeled PCM mixer. Direct
        // input bypass and 24/32-bit streaming require their own real data
        // path; retaining a setting must never pretend to execute that path.
        if(physical==0x1F9007C0u && (*value&0x104u))
            throw IopStop{"SPU2 SPDIF bypass and direct streaming are not modeled"};
        if(physical>=0x1F900760u && physical<0x1F9007B0u &&
           (physical-0x1F900760u)%0x28u<4u && (*value&0x8000u))
            throw IopStop{"SPU2 master volume sweep is not modeled"};
        for(auto& [address,stored]:latches_)if(address==physical){stored=*value&0xFFFFu;return;}
        latches_.push_back({physical,*value&0xFFFFu});return;
    }
    if(physical==0x1F808260u) {
        if(bytes!=1u || !value)throw IopStop{"SIO2 input FIFO requires a known byte"};
        if(sio2_complete_at_)throw IopStop{"SIO2 input FIFO write while a command is active"};
        if(sio2_input_.size()>=256u)throw IopStop{"SIO2 input exceeds the modeled descriptor capacity"};
        sio2_input_.push_back(static_cast<std::uint8_t>(*value));return;
    }
    if(physical==0x1F808280u) {
        if(bytes!=4u || !value || !diagnostic_sio2_timeout_cycles_ || (*value&~1u))
            throw IopStop{"SIO2 interrupt acknowledge layout is unverified"};
        if(*value&1u)sio2_completion_flag_=false;
        update_sio2_irq();return; // the IOP pending latch is acknowledged separately at I_STAT
    }
    if(physical==0x1F402008u) {
        if(bytes!=1u || !value) throw IopStop{name+": CDVD I_STAT requires a known byte"};
        cdvd_irq_&=static_cast<std::uint8_t>(~*value);
        return;
    }
    if(physical==0x1F402006u) {
        if(bytes!=1u || !value || *value!=0x80u || cdvd_sectors_)
            throw IopStop{name+": CDVD HOWTO supports idle 2048-byte data mode 0x80 only"};
        cdvd_howto_=std::uint8_t{0x80};return;
    }
    if(physical==0x1F402005u) {
        if(bytes!=1u || !value || cdvd_complete_at_ || cdvd_sectors_ || cdvd_params_.size()>=16u)
            throw IopStop{name+": CDVD parameter FIFO overflow, unknown byte or busy drive"};
        cdvd_params_.push_back(static_cast<std::uint8_t>(*value));return;
    }
    if(physical==0x1F402004u) {
        if(bytes!=1u || !value)throw IopStop{name+": CDVD command requires a known byte"};
        if(cdvd_complete_at_ || cdvd_sectors_)throw IopStop{name+": CDVD command issued while busy"};
        if(*value==6u && disc_) {
            if(cdvd_type_!=0x12u || cdvd_howto_!=0x80u || cdvd_params_.size()!=11u ||
               cdvd_params_[10]!=0u || (cdvd_params_[9]&0x7Fu)>5u) {
                std::string params;for(const auto byte:cdvd_params_)params+=" "+hex(byte,2);
                throw IopStop{name+": ReadCd requires CD media, HOWTO 0x80, 11 parameters and 2048-byte sectors; params"+params};
            }
            const bool cav=(cdvd_params_[9]&0x80u)!=0;
            if(cav && !diagnostic_cav_nominal_timing_)
                throw IopStop{name+": CAV radial timing is not verified (explicit diagnostic nominal timing required)"};
            const auto word=[&](unsigned at){return std::uint32_t{cdvd_params_[at]} |
                (std::uint32_t{cdvd_params_[at+1]}<<8u) | (std::uint32_t{cdvd_params_[at+2]}<<16u) |
                (std::uint32_t{cdvd_params_[at+3]}<<24u);};
            const auto lba=word(0),count=word(4);
            if(!count || std::uint64_t{lba}+count>0xFFFFFFFFull)
                throw IopStop{name+": ReadCd invalid sector range"};
            // Functional timing hypothesis from ps2tek: CD 1/2/4/12/24x,
            // 30/100 ms seeks. No IRQ until real sectors reach guest memory.
            constexpr unsigned speed[]={24,1,2,4,12,24};
            cdvd_sector_cycles_=491520u/speed[cdvd_params_[9]&0x7Fu];
            if(cav)events_.push_back({cycle_,"DIAGNOSTIC CAV uses nominal CLV sector time; radial speed unverified"});
            const auto delta=lba>cdvd_lba_ ? lba-cdvd_lba_ : cdvd_lba_-lba;
            const std::uint64_t seek=delta<8u ? std::uint64_t{delta}*cdvd_sector_cycles_ :
                                      delta<4371u ? 1105920u : 3686400u;
            cdvd_sector_at_=cycle_+seek+cdvd_sector_cycles_;cdvd_sector_offset_=2048;
            cdvd_lba_=lba;cdvd_sectors_=count;cdvd_status_=delta ? 0x12u : 6u;cdvd_sticky_|=cdvd_status_;
            cdvd_ncommand_=std::uint8_t{6};cdvd_params_.clear();
            events_.push_back({cycle_,"CDVD ReadCd lba="+std::to_string(lba)+" count="+std::to_string(count)});
            return;
        }
        if(*value>1u || !cdvd_params_.empty())
            throw IopStop{name+": only parameter-free CDVD NOP/NOPsync or mounted ReadCd commands modeled"};
        cdvd_ncommand_=static_cast<std::uint8_t>(*value);
        // Functional scheduling hypothesis: completion after one IOP cycle,
        // never an inline success reply or a synthesized media read.
        cdvd_complete_at_=cycle_+1u;
        return;
    }
    if(physical==0x1D000040u) {
        if(!diagnostic_sbus_latches_ || bytes!=4u || !value || (*value!=0x20u && *value!=0x40u))
            throw IopStop{name+": unverified control operation outside SIF ready-bit experiment"};
        // Functional hypothesis from SDK single-bit setup and observed
        // emulator behavior: write-one toggles ready; completion clears it.
        diagnostic_sif_ready_^=*value;
        return;
    }
    if (physical == kPostPhysical && (bytes == 1u || bytes == 4u)) {
        if (!value) throw IopStop{name + ": unknown value"};
        // Boot progress code (POST); 8-bit display value, no other effect.
        events_.push_back({cycle_, "POST " + hex(*value & 0xFFu, 2)});
        return;
    }
    if (physical == kCdvdSStatus || physical == kCdvdSCommand) {
        if (bytes != 1u) throw IopStop{name + ": only 8-bit accesses are modeled"};
        if (!value) throw IopStop{name + ": unknown value"};
        if (physical == kCdvdSStatus) {
            if (s_params_.size() >= 16u) throw IopStop{name + ": more than 16 S command parameters"};
            s_params_.push_back(static_cast<std::uint8_t>(*value));
            return;
        }
        mechacon_command(static_cast<std::uint8_t>(*value), what);
        return;
    }
    if (physical == kIlinkPhyAccess) {
        if (bytes != 4u) throw IopStop{name + ": only 32-bit writes are modeled"};
        if (!value || (*value & (kPhyWrite | kPhyRead)) != kPhyWrite || (*value & 0xFFFFu) != 0u)
            throw IopStop{name + " = " + (value ? hex(*value) : std::string("(unknown)")) +
                          ": only PHY register writes are modeled"};
        const auto phy_register = (*value >> 24) & 0x3Fu, data = (*value >> 16) & 0xFFu;
        events_.push_back({cycle_, "i.LINK PHY register " + std::to_string(phy_register) + " = " + hex(data, 2)});
        // IEEE 1394a PHY register 1, bit 6 (IBR): initiate a bus reset.
        if (phy_register == 1u && (data & 0x40u) != 0u) ilink_bus_reset_ = true;
        return;
    }
    if (physical == kIlinkIntr0 || physical == kIlinkIntr1 || physical == kIlinkIntr2 ||
        physical == kIlinkUbufReceiveClear) {
        if (bytes != 4u) throw IopStop{name + ": only 32-bit writes are modeled"};
        // Write-1-to-clear acknowledges; no i.LINK interrupt source is modeled,
        // so nothing is pending and reads stay refused.
        if (!value || *value != 0xFFFFFFFFu) throw IopStop{name + ": only clearing every bit is modeled"};
        return;
    }
    if (physical == kExpansionBayRegister) {
        if (bytes != 2u) throw IopStop{name + ": only 16-bit writes are modeled"};
        if (!value) throw IopStop{name + ": unknown value"};
        events_.push_back({cycle_, "expansion-bay register " + hex(physical) + " = " + hex(*value & 0xFFFFu, 4) +
                                       " (effect not modeled)"});
        return;
    }
    if (physical == kSifMscom || physical == kSifSmcom || physical == kSifMsflg || physical == kSifSmflg) {
        if (bytes != 4u) throw IopStop{name + ": only 32-bit SIF accesses are modeled"};
        if (!value) throw IopStop{name + ": unknown value"};
        if (physical == kSifMscom) throw IopStop{name + ": SIF_MSCOM is written by the EE only (IOP write effect unknown)"};
        if (physical == kSifSmcom) sif_smcom_ = *value;
        if (physical == kSifMsflg) sif_msflg_ &= ~*value; // the IOP clears the bits it writes
        if (physical == kSifSmflg) sif_smflg_ |= *value;  // and sets its own flags
        return;
    }
    if (const auto timer = timer_index(physical)) return timer_write(*timer, physical & 0xFu, value, name);
    if (physical == kIStat && bytes == 4u) {
        // Acknowledge: clears the pending bits written as 0.
        if (!value) throw IopStop{name + ": unknown value"};
        istat_ &= *value;
        if(!(*value&(1u<<17u)))sio2_irq_acknowledged_=true;
        return;
    }
    if (is_latch(physical)) {
        if (bytes != 4u) throw IopStop{name + ": only 32-bit accesses are modeled"};
        if(physical>=0x1F8010B0u && physical<=0x1F8010B8u) {
            const auto old=latch(0x1F8010B8u);
            if(old && *old && (**old&kChcrStart))throw IopStop{name+": CDVD DMA registers written while busy"};
            if(physical==0x1F8010B8u && value && (*value&kChcrStart)) {
                if((*value!=0x41000200u && *value!=0x01000200u) || cdvd_howto_!=0x80u || !disc_)
                    throw IopStop{name+": CDVD DMA requires request-mode device-to-RAM and HOWTO 0x80"};
                const auto addr=latch(0x1F8010B0u),count=latch(0x1F8010B4u);
                if(!addr || !*addr || !count || !*count)throw IopStop{name+": CDVD DMA address/count unknown"};
                const auto nbytes=(**count&0xFFFFu)*4u,blocks=**count>>16u;
                if(!nbytes || !blocks || nbytes>2048u || 2048u%nbytes || (**addr&3u) ||
                   **addr>ram_.size() || std::uint64_t{nbytes}*blocks>ram_.size()-**addr)
                    throw IopStop{name+": CDVD DMA block size/count/address is unsupported"};
            }
        }
        // SIO2 CTRL bit 0 starts a transfer (ps2tek SIO2). Idle setup was
        // already latched here; do not silently accept a command start.
        if(physical==0x1F808268u && (!value || (*value&1u))) {
            if(!value || !diagnostic_sio2_timeout_cycles_)throw IopStop{name+": SIO2 transfer is not modeled"};
            if((*value&~1u)!=0u && (*value&~1u)!=0xCu && (*value&~1u)!=0x3BCu && (*value&~1u)!=0xB0u && (*value&~1u)!=0x3B0u)
                throw IopStop{"SIO2 transfer configuration is not verified: "+hex(*value)};
            start_sio2_infrared(*value);return;
        }
        if(physical==0x1F808268u && *value!=0u && *value!=0xCu && *value!=0x3BCu &&
           (!diagnostic_sio2_timeout_cycles_ || (*value&~0x3BCu)))
            throw IopStop{"SIO2 control configuration is not verified"};
        if(physical==0x1F808268u) {
            if(*value&4u)reset_sio2_completion();
            sio2_interrupt_enable_=*value&0x300u; // MX4SIO: CTRL.9 completion interrupt enable
            update_sio2_irq();
            ++sio2_control_epoch_;
            if(diagnostic_sio2_timeout_cycles_)sio2_diagnostic_control_=*value&~0xDu;
            if(!diagnostic_sio2_timeout_cycles_ || (*value&4u)) {
                sio2_complete_at_.reset();sio2_response_valid_=false;
            }
            if(!diagnostic_sio2_timeout_cycles_ || (*value&8u)){sio2_input_.clear();sio2_output_.clear();}
        }
        if (dma_channel_field(physical) == 2u && (!value || ((*value & kChcrStart) != 0u && physical!=0x1F8010B8u)))
            throw IopStop{name + " = " + (value ? hex(*value) : std::string("(unknown)")) +
                          ": DMA transfers are not modeled"};
        if (physical == kICtrl && value) value = *value & 1u;
        if ((physical == kDicr || physical == kDicr2) && value) {
            const auto old = latch(physical);
            const auto flags = physical == kDicr ? 0x7F000000u : 0x3F000000u;
            const auto previous = old ? old->value_or(0u) : 0u;
            value = (*value & (physical == kDicr ? kDicrWritable : kDicr2Writable)) |
                    (previous & flags & ~*value); // completion flags are W1C
        }
        for (auto& [address, latched] : latches_)
            if (address == physical) {
                latched = value;
                if(physical==kDicr || physical==kDicr2 || physical==0x1F80157Cu)update_dma_irq();
                return;
            }
        latches_.push_back({physical, value});
        if(physical==kDicr || physical==kDicr2 || physical==0x1F80157Cu)update_dma_irq();
        return;
    }
    throw IopStop{name + " = " + (value ? hex(*value) : std::string("(unknown)")) + ": device not implemented"};
}

void Iop::mechacon_command(std::uint8_t command, const char* what) {
    std::string params;
    for (const auto byte : s_params_) params += " " + hex(byte, 2);
    const auto name = std::string("S command ") + hex(command, 2) + " (params" + (params.empty() ? " none" : params) + ")";
    events_.push_back({cycle_, name});
    // A new command discards unread results (hypothesis).
    s_results_.clear();
    s_command_ = command;
    switch (command) {
        case 0x03: // MechaconVersion: four bytes from this console profile.
            if(s_params_.size()!=1u || s_params_[0]!=0u)
                throw IopStop{name+": only the parameter-free MechaconVersion subcommand is modeled"};
            if(!hardware_ || !hardware_->mechacon)
                throw IopStop{name+": no measured MECHACON version in this console profile"};
            for(const auto byte:hardware_->mechacon->version)s_results_.push_back(byte);
            break;
        case 0x05: // UpdateStickyFlags: actual latch update, not a stub.
            if(!s_params_.empty() || !disc_)throw IopStop{name+": requires no parameters and an explicit disc"};
            if(cdvd_complete_at_ || cdvd_sectors_)s_results_.push_back(1u);
            else {cdvd_sticky_=cdvd_status_;s_results_.push_back(0u);}
            break;
        case 0x36: { // ReadRegionParams (PS2SDK: no input, 15-byte result).
            if(!s_params_.empty())throw IopStop{name+": ReadRegionParams requires no parameter bytes"};
            if(!hardware_ || !hardware_->mechacon || hardware_->mechacon->version[1]<6u)
                throw IopStop{name+": region-result layout is only established for a version-6 MECHACON profile"};
            if(!nvm_)throw IopStop{name+": ReadRegionParams needs the matching console NVM"};
            const auto offset=nvm_layout_info(nvm_->layout).region_parameters;
            if(offset>nvm_->bytes.size() || 8u>nvm_->bytes.size()-offset)
                throw IopStop{name+": NVM region record is out of range"};
            // Perform the actual EEPROM read. Profile/region strings never
            // replace its bytes. The encrypted region/unlock flags and the
            // undocumented suffix cannot be inferred from this plaintext
            // record, so keep those bytes unknown instead of fabricating 0.
            s_results_.push_back(0u); // successful validated read, no storage fault modeled
            for(unsigned n=0;n<2;++n)s_results_.push_back({0u,0u,"MECHACON region/unlock flags are not established by the supplied NVM plaintext"});
            for(unsigned n=0;n<8;++n)s_results_.push_back(nvm_->bytes[offset+n]);
            for(unsigned n=0;n<4;++n)s_results_.push_back({0u,0u,"MECHACON ReadRegionParams suffix is not verified"});
            break;
        }
        case 0x0A: { // ReadNVM: address high, low -> status, data high, data low (ps2sdk)
            if (s_params_.size() != 2u) throw IopStop{name + ": ReadNVM takes a 2-byte word address"};
            const auto address = static_cast<std::uint32_t>(s_params_[0] << 8 | s_params_[1]);
            if (address >= kNvmWords)
                throw IopStop{name + ": NVM word address " + hex(address, 4) + " beyond the 512-word EEPROM "
                              "(MECHACON answer not established)"};
            if (nvm_ == nullptr)
                throw IopStop{name + ": ReadNVM returns this console's MECHACON NVM (EEPROM), which differs per "
                              "console; no NVM image is supplied (--nvm)"};
            const auto word = nvm_->word(address);
            s_results_.push_back(0u);
            s_results_.push_back(static_cast<std::uint8_t>(word >> 8));
            s_results_.push_back(static_cast<std::uint8_t>(word));
            break;
        }
        case 0x40: { // OpenConfig: mode (0 read, 1 write), area, block count (ps2sdk order)
            if (s_params_.size() != 3u || s_params_[0] > 1u || s_params_[1] >= kNvmConfigAreas)
                throw IopStop{name + ": parameters outside the documented OpenConfig form"};
            if (s_params_[0] != 0u)
                throw IopStop{name + ": writing the configuration is not modeled (the NVM image is never modified)"};
            if (hardware_ == nullptr || !hardware_->mechacon)
                throw IopStop{name + ": no console profile with MECHACON data, so the configuration areas are unknown"};
            const auto& area = nvm_layout_info(hardware_->mechacon->layout).config[s_params_[1]];
            if (s_params_[2] == 0u || s_params_[2] > area.blocks)
                throw IopStop{name + ": " + std::to_string(s_params_[2]) + " blocks requested, area " +
                              std::to_string(s_params_[1]) + " holds " + std::to_string(area.blocks) +
                              " (MECHACON answer not established)"};
            config_ = OpenConfig{s_params_[1], s_params_[2], 0u};
            // One status byte, 0 = success (ps2tek leaves it unspecified;
            // DobieStation and PCSX2 return 0).
            s_results_.push_back(0u);
            break;
        }
        case 0x41: { // ReadConfig: the next 16-byte block of the open area
            if (!s_params_.empty()) throw IopStop{name + ": ReadConfig takes no parameters"};
            if (!config_) throw IopStop{name + ": ReadConfig without an open configuration area"};
            if (config_->next >= config_->blocks)
                throw IopStop{name + ": ReadConfig past the " + std::to_string(config_->blocks) +
                              " blocks opened (MECHACON answer not established)"};
            if (nvm_ == nullptr)
                throw IopStop{name + ": ReadConfig returns the configuration stored in this console's MECHACON "
                              "NVM (EEPROM), which differs per console; no NVM image is supplied (--nvm)"};
            // Raw bytes, checksum byte included: the firmware verifies it.
            for (const auto byte : nvm_->config_block(config_->area, config_->next)) s_results_.push_back(byte);
            ++config_->next;
            break;
        }
        case 0x43: // CloseConfig: one status byte, 0 (as OpenConfig)
            if (!s_params_.empty()) throw IopStop{name + ": CloseConfig takes no parameters"};
            config_.reset();
            s_results_.push_back(0u);
            break;
        default:
            throw IopStop{std::string("store ") + what + " of " + name + ": this MECHACON command is not modeled"};
    }
    s_params_.clear();
}

std::uint32_t Iop::cop0_read(unsigned index) {
    if (index == Prid) {
        if (hardware_ == nullptr || !hardware_->iop_prid)
            throw IopStop{"COP0 PRId read: no console profile with a measured IOP revision is selected"};
        return *hardware_->iop_prid;
    }
    if (index >= cop0_.size() || !cop0_[index])
        throw IopStop{"COP0 register " + std::to_string(index) + " read before any write"};
    if (index == Cause) {
        // IP2 (bit 10) follows the IOP interrupt line.
        const auto line = interrupt_line();
        if (!line && !undefined_load_) {
            undefined_load_ = "COP0 Cause at IOP " + hex(pc_) + ": IP2 depends on interrupt sources that are not modeled";
            undefined_known_ = ~(1u << 10);
        }
        return (*cop0_[Cause] & ~(1u << 10)) | (line.value_or(false) ? 1u << 10 : 0u);
    }
    return *cop0_[index];
}

void Iop::cop0_write(unsigned index, std::uint32_t value) {
    switch (index) {
        case Bpc: case Bda: case Jumpdest: case Dcic: case Bdam: case Bpcm:
            if (index == Dcic && value != 0u)
                throw IopStop{"COP0 DCIC = " + hex(value) + ": hardware breakpoints are not modeled"};
            cop0_[index] = value;
            return;
        case Sr:
            if ((value & (kSrKuc | kSrSwc | kSrRe)) != 0u)
                throw IopStop{"COP0 SR = " + hex(value) +
                              ": user mode, swapped caches and reverse endianness are not modeled"};
            cop0_[Sr] = value;
            return;
        case Cause:
            // Only IP0/IP1 (software interrupts) are writable.
            if ((value & 0x300u) != 0u) throw IopStop{"COP0 Cause = " + hex(value) + ": software interrupts are not modeled"};
            cop0_[Cause] = cop0_[Cause].value_or(0u) & ~0x300u;
            return;
        default:
            throw IopStop{"COP0 register " + std::to_string(index) + " write is not modeled"};
    }
}

void Iop::commit(const PendingLoad& load) {
    gpr_[load.reg] = load.value;
    gpr_sio2_epoch_[load.reg]=load.sio2_epoch;
    if (load.defined) gpr_defined_ |= 1u << load.reg;
    else {
        gpr_defined_ &= ~(1u << load.reg);
        gpr_origin_[load.reg] = load.origin;
        gpr_known_[load.reg] = load.known;
    }
}

void Iop::enter_exception(std::uint32_t code, std::uint32_t pc, bool delay_slot) {
    // EPC is the branch when the exception hits its delay slot (Cause.BD).
    cop0_[Epc] = delay_slot ? pc - 4u : pc;
    cop0_[Cause] = (cop0_[Cause].value_or(0u) & ~0x8000007Cu) | (code << 2) | (delay_slot ? 0x80000000u : 0u);
    const auto sr = *cop0_[Sr];
    cop0_[Sr] = (sr & ~0x3Fu) | ((sr << 2) & 0x3Cu); // push KU/IE: kernel mode, interrupts off
    pc_ = (sr & kSrBev) != 0u ? 0xBFC00180u : 0x80000080u;
    branch_target_.reset();
    in_delay_slot_ = false;
}

const std::optional<std::uint32_t>* Iop::latch(std::uint32_t physical) const {
    for (const auto& [address, value] : latches_)
        if (address == physical) return &value;
    return nullptr;
}

std::optional<bool> Iop::interrupt_line() const {
    const auto* ctrl = latch(kICtrl);
    if (ctrl == nullptr || !*ctrl) return std::nullopt;
    if ((**ctrl & 1u) == 0u) return false;
    const auto* mask = latch(kIMask);
    if (mask == nullptr || !*mask) return std::nullopt;
    if ((istat_ & **mask & modeled_irqs()) != 0u) return true;
    if ((**mask & ~modeled_irqs()) != 0u) return std::nullopt;
    return false;
}

std::uint32_t Iop::modeled_irqs() const noexcept {
    // Cold SIO2 has no in-flight command. Every path that could start one
    // (CTRL.0, FIFO, DMA) refuses before changing state, so IRQ17 cannot rise.
    // This proves the idle source only, not peripheral responses or timing.
    // The opt-in cold-state experiment has no active voices or inputs. Only
    // quiet clocks with effects/IRQ disabled are accepted; no IRQ9 is created.
    return kTimerIrqs | kDmaIrq | (1u<<2u) | (sio2_irq_acknowledged_ ? (1u<<17u) : 0u) |
           (diagnostic_spu2_quiescent_ ? (1u<<9u) : 0u) |
           (video_.state != GsRegisters::VerticalClock::State::Unknown ? kVblankStartIrq | kVblankEndIrq : 0u);
}

void Iop::refresh_video(const GsRegisters& video) {
    if (video_generation_ != video.timing_generation()) {
        // Retain the existing IOP-cycle granularity: the next tick processes
        // edges from this boundary, never edges preceding the timing change.
        const auto from = cycle_ * kEeCyclesPerIopCycle;
        video_generation_ = video.timing_generation();
        video_ = video.vertical_clock();
        if (video_.state == GsRegisters::VerticalClock::State::Running) {
            next_vblank_start_ = video_.next_edge(from, false);
            next_vblank_end_ = video_.next_edge(from, true);
        }
    }
}

void Iop::tick_video() {
    const auto now = cycle_ * kEeCyclesPerIopCycle;
    if (video_.state != GsRegisters::VerticalClock::State::Running) return;
    const auto period = video_.half_line * video_.field;
    for (; next_vblank_start_ <= now; next_vblank_start_ += period) istat_ |= kVblankStartIrq;
    for (; next_vblank_end_ <= now; next_vblank_end_ += period) istat_ |= kVblankEndIrq;
}

std::uint32_t Iop::ee_sif_read(unsigned index) const {
    const auto name = "SIF register " + hex(0x1000F200u + index * 0x10u) + " read";
    if (stop_) throw DeviceRefusal(name + " after the IOP stopped (its later state is unknown): " + *stop_);
    switch (index) {
        case 0:
            if (!sif_mscom_) throw DeviceRefusal(name + ": SIF_MSCOM not written since reset");
            return *sif_mscom_;
        case 1:
            if (!sif_smcom_) throw DeviceRefusal(name + ": SIF_SMCOM not written by the IOP since reset");
            return *sif_smcom_;
        case 2: return sif_msflg_;
        case 3: return sif_smflg_;
        default: throw DeviceRefusal(name + ": not modeled");
    }
}

void Iop::ee_sif_write(unsigned index, std::uint32_t value) {
    const auto name = "SIF register " + hex(0x1000F200u + index * 0x10u) + " = " + hex(value);
    switch (index) {
        case 0: sif_mscom_ = value; return;
        case 2: sif_msflg_ |= value; return;  // the EE sets its flags
        case 3: sif_smflg_ &= ~value; return; // and clears the IOP's
        case 1: throw DeviceRefusal(name + ": SIF_SMCOM is written by the IOP only (EE write effect unknown)");
        case 4:
            if(!diagnostic_sbus_latches_) throw DeviceRefusal(name+": not modeled (SBUS configuration experiment required)");
            if(value!=0x100u) throw DeviceRefusal(name+": SBUS experiment only supports clock initialization, no IRQ/reset");
            sbus_control_write_=value;
            events_.push_back({cycle_,"DIAGNOSTIC SBUS CTRL configuration latch = "+hex(value)});
            return;
        case 6:
            if(!diagnostic_sbus_latches_) throw DeviceRefusal(name+": not modeled (SBUS configuration experiment required)");
            if(value!=0xFFu) throw DeviceRefusal(name+": SBUS experiment only supports SDK initialization");
            sbus_bd6_write_=value;
            events_.push_back({cycle_,"DIAGNOSTIC SBUS BD6 configuration latch = "+hex(value)});
            return;
        default: throw DeviceRefusal(name + ": not modeled");
    }
}

void Iop::tick_timers() {
    for (unsigned i = 0; i < timers_.size(); ++i) {
        auto& t = timers_[i];
        if (!t.configured) continue;
        if (++t.phase < t.prescale) continue;
        t.phase = 0;
        const auto width = i < 3u ? 0xFFFFu : 0xFFFFFFFFu;
        bool irq = false;
        if ((t.mode & kModeZeroReturn) != 0u && t.count == t.target) {
            t.count = 0u; // zero return: the count after the target is 0
        } else {
            const auto next = (*t.count + 1u) & width;
            if (next == 0u) {
                t.mode |= kModeOverflowFlag;
                irq = irq || (t.mode & kModeOverflowIrq) != 0u;
            }
            t.count = next;
        }
        if (t.count == t.target) {
            t.mode |= kModeCompareFlag;
            irq = irq || (t.mode & kModeCompareIrq) != 0u;
        }
        if (irq) {
            if ((t.mode & kModeIrqEnabled) != 0u) istat_ |= 1u << kTimerIrq[i];
            // One-shot unless repeat; repeat with LEVL toggles bit 10.
            if ((t.mode & kModeRepeat) == 0u) t.mode &= ~kModeIrqEnabled;
            else if ((t.mode & kModeLevl) != 0u) t.mode ^= kModeIrqEnabled;
        }
    }
}

std::uint32_t Iop::timer_read(unsigned index, std::uint32_t reg, const std::string& name) {
    auto& t = timers_[index];
    switch (reg) {
        case 0x0u:
            if (!t.count) {
                if (!undefined_load_) undefined_load_ = name + " at IOP " + hex(pc_) + ": timer count before any write";
                return 0u;
            }
            return *t.count;
        case 0x4u: {
            if (!t.configured) {
                if (!undefined_load_) undefined_load_ = name + " at IOP " + hex(pc_) + ": timer mode before any write";
                return 0u;
            }
            const auto mode = t.mode;
            t.mode &= ~(kModeCompareFlag | kModeOverflowFlag); // reads clear the raised flags
            return mode;
        }
        case 0x8u:
            if (!t.target) {
                if (!undefined_load_) undefined_load_ = name + " at IOP " + hex(pc_) + ": timer target before any write";
                return 0u;
            }
            return *t.target;
        default:
            throw IopStop{name + ": not a timer register"};
    }
}

void Iop::timer_write(unsigned index, std::uint32_t reg, std::optional<std::uint32_t> value, const std::string& name) {
    auto& t = timers_[index];
    const auto shown = name + " = " + (value ? hex(*value) : std::string("(unknown)"));
    if (!value) throw IopStop{shown + ": unknown value"};
    const auto width = index < 3u ? 0xFFFFu : 0xFFFFFFFFu;
    switch (reg) {
        case 0x0u: t.count = *value & width; return;
        case 0x4u: {
            const auto mode = *value & 0xFFFFu;
            if ((mode & kModeGate) != 0u) throw IopStop{shown + ": timer gates are not modeled"};
            if ((mode & kModeExternal) != 0u && (index == 0u || index == 1u || index == 3u))
                throw IopStop{shown + ": the pixel clock and HBLANK timer sources are not modeled"};
            if (!t.target && (mode & (kModeZeroReturn | kModeCompareIrq)) != 0u)
                throw IopStop{shown + ": compare against a target never written"};
            // Writes reset the count and set bit 10 (ps2tek); clearing the
            // raised flags is a hypothesis.
            t.mode = (mode & ~(kModeCompareFlag | kModeOverflowFlag)) | kModeIrqEnabled;
            t.count = 0u;
            t.phase = 0u;
            t.prescale = 1u;
            if (index == 2u && (mode & (1u << 9)) != 0u) t.prescale = 8u;
            if (index >= 4u) t.prescale = std::array<std::uint32_t, 4>{1u, 8u, 16u, 256u}[(mode >> 13) & 3u];
            t.configured = true;
            timers_running_ = true;
            return;
        }
        case 0x8u:
            t.target = *value & width;
            if ((t.mode & kModeLevl) == 0u) t.mode |= kModeIrqEnabled;
            return;
        default:
            throw IopStop{shown + ": not a timer register"};
    }
}

void Iop::step() {
    // Interrupt: taken before the instruction at pc_ when SR.IEc and SR.IM2
    // (the IOP INTC drives hardware interrupt 0, Cause bit 10).
    if (const auto sr = *cop0_[Sr]; (sr & kSrIec) != 0u && (sr & (1u << 10)) != 0u) {
        const auto line = interrupt_line();
        if (!line) {
            std::string sources;
            const auto* mask = latch(kIMask);
            for (unsigned bit = 0; bit < std::size(kIrqNames); ++bit)
                if (mask != nullptr && *mask && (**mask & ~modeled_irqs() & (1u << bit)) != 0u)
                    sources += std::string(sources.empty() ? "" : ", ") + kIrqNames[bit];
            if (mask != nullptr && *mask && (**mask & (kVblankStartIrq | kVblankEndIrq) & ~modeled_irqs()) != 0u)
                sources += "; VBLANK: " + (video_.reason.empty() ? std::string("no GS attached") : video_.reason);
            throw IopStop{"interrupts enabled while the interrupt line depends on " +
                          (sources.empty() ? std::string("I_MASK or I_CTRL, never written")
                                           : "sources that are not modeled (" + sources + ")")};
        }
        if (*line) {
            // The load in flight completes; EPC is the interrupted instruction.
            if (pending_load_) commit(*pending_load_);
            pending_load_.reset();
            return enter_exception(0u, pc_, in_delay_slot_);
        }
    }
    const auto pc = pc_;
    const auto word = fetch(pc);
    const bool delay_slot = in_delay_slot_;
    const auto after = branch_target_;
    std::optional<std::uint32_t> target; // taken branch or jump of this instruction
    bool branch = false;
    std::optional<std::uint32_t> exception; // ExcCode raised by this instruction
    write_.reset();
    undefined_load_.reset();
    undefined_known_ = 0u;
    undefined_sio2_epoch_=0;
    const auto delayed = pending_load_; // becomes visible after this instruction reads its operands
    pending_load_.reset();

    const unsigned op = word >> 26, rs = (word >> 21) & 31u, rt = (word >> 16) & 31u, rd = (word >> 11) & 31u,
                   sa = (word >> 6) & 31u, fn = word & 63u;
    const auto imm = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(word & 0xFFFFu)));
    const auto uimm = word & 0xFFFFu;
    const auto unsupported = [&] { return IopStop{"instruction " + hex(word) + " is not implemented"}; };
    const auto overflow = [&] { return IopStop{"integer overflow exception (not vectored)"}; };
    const auto branch_if = [&](bool taken) {
        branch = true;
        if (taken) target = pc + 4u + (imm << 2);
    };
    // LWL/LWR merge with the register as it will be after a load in flight.
    const auto merge_base = [&](unsigned r)->PendingLoad {
        if(delayed && delayed->reg==r)return *delayed;
        const bool defined=(gpr_defined_&(1u<<r))!=0u;
        return PendingLoad{r,gpr_[r],defined,defined ? std::string{} : gpr_origin_[r],defined ? ~0u : gpr_known_[r]};
    };
    const auto store_value = [&](std::uint32_t address,std::size_t bytes,unsigned r,const char* what) {
        const auto mask=bytes==4u ? ~0u : (1u<<(bytes*8u))-1u;
        const auto known=(gpr_defined_&(1u<<r)) ? ~0u : gpr_known_[r];
        if((known&mask)==mask)store(address,bytes,gpr_[r]&mask,what);
        else {
            const ObservedWord observed{gpr_[r],~known};
            store(address,bytes,std::nullopt,what,&observed,bytes==4u ? gpr_sio2_epoch_[r] : 0u);
        }
    };

    switch (op) {
        case 0x00:
            switch (fn) {
                case 0x00: shift_left(rd, rt, sa); break;                                      // SLL
                case 0x02: shift_right(rd, rt, sa); break;                                   // SRL
                case 0x03: set_reg(rd, static_cast<std::uint32_t>(static_cast<std::int32_t>(reg(rt)) >> sa)); break;
                case 0x04: shift_left(rd, rt, masked_reg(rs,31u)&31u); break;                  // SLLV
                case 0x06: shift_right(rd, rt, masked_reg(rs,31u)&31u); break;                 // SRLV
                case 0x07: set_reg(rd, static_cast<std::uint32_t>(static_cast<std::int32_t>(reg(rt)) >> (reg(rs) & 31u))); break;
                case 0x08: branch = true; target = reg(rs); break;                              // JR
                case 0x09: branch = true; target = reg(rs); set_reg(rd, pc + 8u); break;        // JALR
                case 0x0C: exception = 8u; break;                                              // SYSCALL
                case 0x0D: exception = 9u; break;                                              // BREAK
                case 0x10: if (!hi_) throw IopStop{"MFHI before any multiply/divide"}; set_reg(rd, *hi_); break;
                case 0x11: hi_ = reg(rs); break;                                                // MTHI
                case 0x12: if (!lo_) throw IopStop{"MFLO before any multiply/divide"}; set_reg(rd, *lo_); break;
                case 0x13: lo_ = reg(rs); break;                                                // MTLO
                case 0x18: {                                                                    // MULT
                    const auto p = static_cast<std::int64_t>(static_cast<std::int32_t>(reg(rs))) *
                                   static_cast<std::int32_t>(reg(rt));
                    lo_ = static_cast<std::uint32_t>(p);
                    hi_ = static_cast<std::uint32_t>(static_cast<std::uint64_t>(p) >> 32);
                    break;
                }
                case 0x19: {                                                                    // MULTU
                    const auto p = static_cast<std::uint64_t>(reg(rs)) * reg(rt);
                    lo_ = static_cast<std::uint32_t>(p);
                    hi_ = static_cast<std::uint32_t>(p >> 32);
                    break;
                }
                case 0x1A: {                                                                    // DIV
                    const auto n = static_cast<std::int32_t>(reg(rs)), d = static_cast<std::int32_t>(reg(rt));
                    // R3000A results for x/0 and INT_MIN/-1 (no exception).
                    if (d == 0) { lo_ = n >= 0 ? 0xFFFFFFFFu : 1u; hi_ = static_cast<std::uint32_t>(n); }
                    else if (n == std::numeric_limits<std::int32_t>::min() && d == -1) { lo_ = 0x80000000u; hi_ = 0u; }
                    else { lo_ = static_cast<std::uint32_t>(n / d); hi_ = static_cast<std::uint32_t>(n % d); }
                    break;
                }
                case 0x1B: {                                                                    // DIVU
                    const auto n = reg(rs), d = reg(rt);
                    if (d == 0u) { lo_ = 0xFFFFFFFFu; hi_ = n; }
                    else { lo_ = n / d; hi_ = n % d; }
                    break;
                }
                case 0x20: {                                                                    // ADD
                    const auto a = reg(rs), b = reg(rt), r = a + b;
                    if (((a ^ r) & (b ^ r)) >> 31) throw overflow();
                    set_reg(rd, r);
                    break;
                }
                case 0x21: // ADDU; either $zero operand makes this an exact move.
                    if (!rs) copy_reg(rd, rt);
                    else if (!rt) copy_reg(rd, rs);
                    else add_sub_unsigned(rd,rs,rt,false);
                    break;
                case 0x22: {                                                                    // SUB
                    const auto a = reg(rs), b = reg(rt), r = a - b;
                    if (((a ^ b) & (a ^ r)) >> 31) throw overflow();
                    set_reg(rd, r);
                    break;
                }
                case 0x23: add_sub_unsigned(rd,rs,rt,true); break;                              // SUBU
                case 0x24: and_regs(rd, rs, rt); break;                                          // AND
                case 0x25: or_value(rd,rs,gpr_[rt],(gpr_defined_&(1u<<rt)) ? ~0u : gpr_known_[rt],gpr_origin_[rt]); break;
                case 0x26: set_reg(rd, reg(rs) ^ reg(rt)); break;
                case 0x27: set_reg(rd, ~(reg(rs) | reg(rt))); break;
                case 0x2A: set_reg(rd, static_cast<std::int32_t>(reg(rs)) < static_cast<std::int32_t>(reg(rt)) ? 1u : 0u); break;
                case 0x2B: set_reg(rd, reg(rs) < reg(rt) ? 1u : 0u); break;
                default: throw unsupported();
            }
            break;
        case 0x01: {
            const auto s = static_cast<std::int32_t>(reg(rs));
            switch (rt) {
                case 0x00: branch_if(s < 0); break;                                             // BLTZ
                case 0x01: branch_if(s >= 0); break;                                            // BGEZ
                case 0x10: branch_if(s < 0); set_reg(31, pc + 8u); break;                       // BLTZAL
                case 0x11: branch_if(s >= 0); set_reg(31, pc + 8u); break;                      // BGEZAL
                default: throw unsupported();
            }
            break;
        }
        case 0x02: branch = true; target = ((pc + 4u) & 0xF0000000u) | ((word & 0x03FFFFFFu) << 2); break;  // J
        case 0x03: branch = true; target = ((pc + 4u) & 0xF0000000u) | ((word & 0x03FFFFFFu) << 2);         // JAL
                   set_reg(31, pc + 8u); break;
        case 0x04: branch_if(regs_equal(rs, rt)); break;                                    // BEQ
        case 0x05: branch_if(!regs_equal(rs, rt)); break;                                   // BNE
        case 0x06: branch_if(static_cast<std::int32_t>(reg(rs)) <= 0); break;
        case 0x07: branch_if(static_cast<std::int32_t>(reg(rs)) > 0); break;
        case 0x08: {                                                                            // ADDI
            const auto a = reg(rs), r = a + imm;
            if (((a ^ r) & (imm ^ r)) >> 31) throw overflow();
            set_reg(rt, r);
            break;
        }
        case 0x09: set_reg(rt, reg(rs) + imm); break;
        case 0x0A: set_reg(rt, static_cast<std::int32_t>(reg(rs)) < static_cast<std::int32_t>(imm) ? 1u : 0u); break;
        case 0x0B: set_reg(rt, reg(rs) < imm ? 1u : 0u); break;
        case 0x0C: and_immediate(rt, rs, uimm); break;
        case 0x0D: or_value(rt,rs,uimm,~0u,{}); break;
        case 0x0E: set_reg(rt, reg(rs) ^ uimm); break;
        case 0x0F: set_reg(rt, uimm << 16); break;
        case 0x10:                                                                              // COP0
            if (rs == 0x00) load_reg(rt, cop0_read(rd));                                        // MFC0 (load delay)
            else if (rs == 0x04) cop0_write(rd, reg(rt));                                       // MTC0
            else if (rs == 0x10 && fn == 0x10) {                                                // RFE
                const auto sr = cop0_read(Sr);
                cop0_write(Sr, (sr & ~0xFu) | ((sr >> 2) & 0xFu));
            } else throw unsupported();
            break;
        case 0x20: load_reg(rt, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(load(reg(rs) + imm, 1u, "LB"))))); break;
        case 0x21: load_reg(rt, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(load(reg(rs) + imm, 2u, "LH"))))); break;
        case 0x23: load_reg(rt, load(reg(rs) + imm, 4u, "LW")); break;
        case 0x24: load_reg(rt, load(reg(rs) + imm, 1u, "LBU")); break;
        case 0x25: load_reg(rt, load(reg(rs) + imm, 2u, "LHU")); break;
        case 0x22: case 0x26: { // LWL/LWR: byte lanes merge with a pending load, including its uncertainty.
            const auto address=reg(rs)+imm,shift=address&3u,aligned=address&~3u;
            const auto prior=merge_base(rt);
            auto value=prior.value,known=prior.defined ? ~0u : prior.known;
            auto origin=prior.origin;
            const auto first=op==0x22u ? 0u : shift,last=op==0x22u ? shift+1u : 4u;
            for(unsigned k=first;k<last;++k) {
                const auto bit=8u*(op==0x22u ? k+3u-shift : k-shift);
                undefined_load_.reset();undefined_known_=0;
                const auto byte=load(aligned+k,1u,op==0x22u ? "LWL" : "LWR");
                const auto byte_known=undefined_load_ ? undefined_known_&0xFFu : 0xFFu;
                if(undefined_load_ && byte_known!=0xFFu)origin=*undefined_load_;
                value=(value&~(0xFFu<<bit))|(byte<<bit);
                known=(known&~(0xFFu<<bit))|(byte_known<<bit);
            }
            undefined_load_.reset();undefined_sio2_epoch_=0;
            if(known!=~0u)undefined_load_=origin.empty() ? "IOP unaligned merge retains unknown source/register bytes" : origin;
            undefined_known_=known;load_reg(rt,value);break;
        }
        case 0x28: store_value(reg(rs)+imm,1u,rt,"SB");break;
        case 0x29: store_value(reg(rs)+imm,2u,rt,"SH");break;
        case 0x2B: store_value(reg(rs)+imm,4u,rt,"SW");break;
        case 0x2A: case 0x2E: { // SWL/SWR transport each selected byte; they never inspect the payload's value.
            const auto address=reg(rs)+imm,shift=address&3u,aligned=address&~3u;
            const auto value=gpr_[rt],known=(gpr_defined_&(1u<<rt)) ? ~0u : gpr_known_[rt];
            const auto first=op==0x2Au ? 0u : shift,last=op==0x2Au ? shift+1u : 4u;
            for(unsigned k=first;k<last;++k) {
                const auto bit=8u*(op==0x2Au ? k+3u-shift : k-shift);
                const ObservedWord byte{(value>>bit)&0xFFu,((~known)>>bit)&0xFFu};
                store(aligned+k,1u,byte.unknown ? std::nullopt : std::optional<std::uint32_t>{byte.value},op==0x2Au ? "SWL" : "SWR",&byte);
            }
            break;
        }
        default:
            throw unsupported();
    }
    if (branch && delay_slot) throw IopStop{"branch or jump in a delay slot (undefined)"};

    // Commit: the load in flight lands first, then this instruction's result
    // (which wins when both write the same register).
    if (delayed) commit(*delayed);
    if (exception) return enter_exception(*exception, pc, delay_slot);
    if (write_) commit(*write_);
    if (delay_slot) {
        pc_ = *after;
        in_delay_slot_ = false;
        branch_target_.reset();
    } else if (branch) {
        pc_ = pc + 4u;
        in_delay_slot_ = true;
        branch_target_ = target.value_or(pc + 8u);
    } else {
        pc_ = pc + 4u;
    }
}
} // namespace ps2native
