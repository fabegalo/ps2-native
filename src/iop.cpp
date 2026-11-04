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
// the GS vertical clock, when known) and DMA raise them here; every other
// source is unknown, so its bits are never reported as 0.
constexpr std::array<unsigned, 6> kTimerIrq{4u, 5u, 6u, 14u, 15u, 16u};
constexpr std::uint32_t kTimerIrqs = (1u << 4) | (1u << 5) | (1u << 6) | (1u << 14) | (1u << 15) | (1u << 16);
constexpr std::uint32_t kVblankStartIrq = 1u << 0, kVblankEndIrq = 1u << 11;
// IRQ 3 (DMA) follows the DICR master flag (ps2tek: bus error bit 15, or
// master enable bit 23 with a channel flag in DICR 24-30 or DICR2 24-29).
// Flags rise only when a transfer ends or hits a bus error, and no transfer
// can start here (see CHCR below), so the source is known and never raised.
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
// raised only by a channel transfer or a bus error; no channel can be started
// here (CHCR is not modeled), so every flag reads 0 and only the written
// enable/mode fields are kept.
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
bool is_latch(std::uint32_t address) {
    return (address & 3u) == 0u && (dma_channel_field(address) ||
           std::find(std::begin(kLatches), std::end(kLatches), address) != std::end(kLatches));
}
} // namespace

Iop::Iop() : ram_(kIopRamBytes), ram_written_(kIopRamBytes) {}

void Iop::reset(std::span<const std::uint8_t> rom0, const ConsoleHardware* hardware, const NvmImage* nvm) {
    rom_.assign(rom0.begin(), rom0.end());
    std::fill(ram_.begin(), ram_.end(), std::uint8_t{0});
    std::fill(ram_written_.begin(), ram_written_.end(), std::uint8_t{0});
    gpr_ = {};
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
    ee_dma_suspended_=hardware && hardware->d_enabler && (*hardware->d_enabler&0x10000u);
    // SIF: the communication registers are unknown until written. The flags
    // are 0 (hypothesis: SIFMAN and the EE kernel each wait for the other side
    // to set a flag, which presumes it clear at power-on; PCSX2 also starts
    // them at 0).
    sif_mscom_.reset();
    sif_smcom_.reset();
    sif_msflg_ = 0u;
    sif_smflg_ = 0u;
    video_generation_.reset();
    video_ = {};
    gpr_known_ = {};
    latches_.clear();
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
    sif0_source_tag_=sif0_source_end_=sif0_dest_end_=false; sif0_metrics_={};sif0_last_qword_={};
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
    // No transfer is in process while the receiver awaits an absent tag.
    // In this qword-atomic bus model STR can clear at that boundary; a
    // payload in progress still requires documented CPND suspension.
    const bool waiting_receiver_stop=channel==5u && field==0u && value==0u &&
        ee_dma_setup_[5][2].value_or(0u)==0u && sif0_fifo_.empty();
    if(channel==5u && (ee_dma_setup_[5][0].value_or(0u)&0x100u) && !suspended_stop && !waiting_receiver_stop)
        throw DeviceRefusal("EE SIF0 DMA register write while busy");
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
    // An empty receiver has no in-flight payload to discard. Keep active
    // packets and buffered data protected from an unmodeled abort.
    const bool idle_stop=field==2u && value==0u && !sif1_have_tag_ &&
        !sif1_words_ && sif1_fifo_.empty();
    if((iop_sif1_[2]&kChcrStart) && !idle_stop)
        throw IopStop{"IOP SIF1 register write while busy address="+hex(address)+" value="+hex(value)+
                      " remaining="+std::to_string(sif1_words_)+" fifo="+std::to_string(sif1_fifo_.size())};
    // ps2sdk sceSifSetDChain uses 0x41000300; RAM is coherent here.
    if (field == 2u && value != 0u && value != 0x01000600u && value != 0x41000300u)
        throw IopStop{"IOP SIF1: only destination chain mode without TTE supported"};
    iop_sif1_[field] = value;
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
            if((id!=0u && id!=3u) || (tag&0x0C000000u))
                throw IopStop{"EE SIF1 source chain: only REFE/REF without priority change modeled"};
            if((address&15u) || address>ee_ram_.size() || std::uint64_t{qwc}*16u>ee_ram_.size()-address)
                throw IopStop{"EE SIF1 referenced source outside aligned main RAM"};
            ee_sif1_chain_end_=id==0u || ((tag&0x80000000u) && (ee_sif1_chcr_&0x80u));
            ee_sif1_chcr_=(ee_sif1_chcr_&0xFFFFu)|(tag&0xFFFF0000u);
            ee_sif1_madr_=address; ee_sif1_qwc_=qwc; *tadr+=16u;
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
        // END+IRQ has the same endpoint in the documented tag format and
        // emulator references. Non-END IRQ stopping behavior is not proven.
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
        if (sif1_end_) {
            const auto* dicr = latch(kDicr);
            const auto* dicr2 = latch(kDicr2);
            if (!dicr || !dicr->has_value() || !dicr2 || !dicr2->has_value())
                throw IopStop{"SIF1 DMA completion: interrupt masks not programmed (pending state unknown)"};
            iop_sif1_[2] &= ~kChcrStart;
            // Channel 10: completion mask 19, tag IRQ enable 10, flag 27.
            // IQE is independent of the transfer-completion mask (ps2tek).
            const bool raise=(**dicr2&(1u<<19)) || (sif1_tag_irq_ && (**dicr2&(1u<<10)));
            for (auto& [address, value] : latches_)
                if (address == kDicr2 && value && raise) *value |= 1u << 27;
            if (enabled(kDicr, 1u << 23) && enabled(kDicr2, 1u << 27)) {
                const auto* control = latch(0x1F80157Cu);
                if (!control || !control->has_value()) throw IopStop{"SIF1 DMA completion: DMACINTEN not programmed"};
                if ((**control & 3u) == 1u) istat_ |= kDmaIrq;
            }
            events_.push_back({cycle_, "SIF1 IOP destination complete"});
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
    // Channel 9: completion mask 18, flag 26, master control as for SIF1.
    const auto* dicr=latch(kDicr);const auto* dicr2=latch(kDicr2);
    if(!dicr || !*dicr || !dicr2 || !*dicr2)
        throw IopStop{"SIF0 source completion: interrupt masks not programmed"};
    const auto masks=**dicr2;
    const auto* control=latch(0x1F80157Cu);
    const bool irq=(masks&(1u<<18u)) && (**dicr&(1u<<23u));
    if(irq && (!control || !*control))throw IopStop{"SIF0 completion: DMACINTEN not programmed"};
    for(auto& [address,value]:latches_)
        if(address==kDicr2 && value && (masks&(1u<<18u))) *value|=1u<<26u;
    if(irq && ((**control&3u)==1u))istat_|=kDmaIrq;
    iop_sif0_[2]&=~kChcrStart;
    events_.push_back({cycle_,"SIF0 IOP source complete (FIFO may still contain data)"});
}
void Iop::pump_sif0() {
    // Same bounded, instruction-clocked throughput hypothesis as SIF1.
    // This moves guest data only. No command decoding, RPC reply, or HLE.
    const auto enabled=[&](std::uint32_t address,std::uint32_t mask) {
        const auto* value=latch(address);
        return value && *value && ((**value&mask)==mask);
    };
    const auto observed_source=[&](std::uint32_t address)->std::optional<std::uint32_t> {
        if(address>ram_.size() || 4u>ram_.size()-address)return std::nullopt;
        std::uint32_t value=0;
        for(unsigned b=0;b<4;++b) {
            if(!ram_written_[address+b])return std::nullopt;
            value|=std::uint32_t{ram_[address+b]}<<(b*8u);
        }
        return value;
    };
    const auto source_word=[&](std::uint32_t address) {
        const auto value=observed_source(address);
        if(!value)throw IopStop{"SIF0 source consumes undefined IOP RAM or exceeds its bounds"};
        return *value;
    };
    if((iop_sif0_[2]&kChcrStart) && enabled(0x1F801578u,1u) && enabled(0x1F801570u,0x800u)) {
        if(!sif0_source_tag_) {
            if(sif0_fifo_.size()>28u)++sif0_metrics_.source_full_cycles;
            else {
                const auto at=iop_sif0_[3];
                const auto tag=source_word(at),words=source_word(at+4u);
                const auto low=source_word(at+8u),high=source_word(at+12u);
                const auto source=tag&0xFFFFFFu;
                if((tag&0x7F000003u) || (words&0xFF000000u))
                    throw IopStop{"SIF0 source tag: IRQ/reserved bits not modeled"};
                if(source>ram_.size() || std::uint64_t{words}*4u>ram_.size()-source)
                    throw IopStop{"SIF0 source tag payload outside IOP RAM"};
                sif0_fifo_.push_back(low);sif0_fifo_.push_back(high);
                // Upper half of an EE destination tag is unused: occupancy
                // padding only, never returned to the guest or copied to RAM.
                sif0_fifo_.push_back(0);sif0_fifo_.push_back(0);
                // The real bus qword contains the EE tag and the following
                // IOP tag. Unknown following bytes may be ignored by the EE
                // tag parser, but must never become invented payload padding.
                sif0_last_qword_={low,high,observed_source(at+16u),observed_source(at+20u)};
                iop_sif0_[0]=source;iop_sif0_[3]+=16u;iop_sif0_written_|=1u;
                sif0_source_words_=words;sif0_source_end_=(tag&0x80000000u)!=0;
                sif0_source_tag_=true;
            }
        } else if(sif0_source_words_) {
            if(sif0_fifo_.size()>28u)++sif0_metrics_.source_full_cycles;
            else {
                std::array<std::uint32_t,4> qword{};
                const auto count=std::min(4u,sif0_source_words_);
                for(unsigned w=0;w<4;++w) {
                    if(w<count)qword[w]=source_word(iop_sif0_[0]+w*4u);
                    else {
                        if(!sif0_last_qword_[w])throw IopStop{"SIF0 partial qword would expose unknown retained bus data"};
                        qword[w]=*sif0_last_qword_[w];
                    }
                }
                for(const auto word:qword)sif0_fifo_.push_back(word);
                for(unsigned w=0;w<4;++w)sif0_last_qword_[w]=qword[w];
                iop_sif0_[0]+=count*4u;sif0_source_words_-=count;
            }
        }
        sif0_metrics_.peak_fifo_words=std::max(sif0_metrics_.peak_fifo_words,sif0_fifo_.size());
        if(sif0_source_tag_ && !sif0_source_words_) {
            if(sif0_source_end_)finish_iop_sif0();
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
        const auto tag=sif0_fifo_[0],dest=sif0_fifo_[1];const auto id=(tag>>28u)&7u;
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
        for(unsigned w=0;w<4;++w) {
            const auto word=sif0_fifo_.front();sif0_fifo_.pop_front();
            for(unsigned b=0;b<4;++b)ee_ram_[*madr+w*4u+b]=static_cast<std::uint8_t>(word>>(b*8u));
            if(ee_ram_unknown_)ee_ram_unknown_->erase(*madr+w*4u);
        }
        *madr+=16u;--*qwc;sif0_metrics_.bytes+=16u;
    }
    if(!*qwc && sif0_dest_end_) {
        *chcr&=~0x100u;ee_dstat_|=0x20u;
        events_.push_back({cycle_,"SIF0 EE destination complete"});
    }
}

void Iop::run_until(std::uint64_t cycle, const GsRegisters* video) {
    // A GS write at this boundary must affect the next interrupt decision.
    if(video) refresh_video(*video);
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
            step();
            ++cycle_;
            if (timers_running_) tick_timers();
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

void Iop::ee_write(std::uint32_t offset,std::size_t bytes,std::uint64_t value) {
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
        throw IopStop{"GPR " + std::to_string(index) + " used while its value is unknown (" + gpr_origin_[index] + ")"};
    return gpr_[index];
}
std::uint32_t Iop::masked_reg(unsigned index, std::uint32_t mask) const {
    if ((gpr_defined_ & (1u << index)) == 0u && (gpr_known_[index] & mask) != mask) return reg(index);
    return gpr_[index];
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
bool Iop::regs_equal(unsigned rs, unsigned rt) const {
    const auto known = [&](unsigned r) { return (gpr_defined_ & (1u << r)) != 0u ? ~0u : gpr_known_[r]; };
    if (((gpr_[rs] ^ gpr_[rt]) & known(rs) & known(rt)) != 0u) return false;
    return reg(rs) == reg(rt);
}
std::optional<std::uint32_t> Iop::stored_reg(unsigned index) const {
    if ((gpr_defined_ & (1u << index)) == 0u) return std::nullopt;
    return gpr_[index];
}
void Iop::set_reg(unsigned index, std::uint32_t value) {
    if (index != 0u) write_ = PendingLoad{index, value, true, {}, ~0u};
}
// A load of never-written RAM leaves the register undefined (its value is
// unknown); only a later use of that register stops the IOP.
void Iop::load_reg(unsigned index, std::uint32_t value) {
    if (index != 0u)
        pending_load_ = PendingLoad{index, value, !undefined_load_, undefined_load_.value_or(std::string()),
                                    undefined_load_ ? undefined_known_ : ~0u};
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
        for (std::size_t i = 0; i < bytes; ++i) {
            if (ram_written_[a.physical + i] == 0u && !undefined_load_)
                undefined_load_ = std::string(what) + " of IOP RAM " + hex(a.physical + static_cast<std::uint32_t>(i)) +
                                  " at IOP " + hex(pc_) + ", never written with a known value";
            value |= static_cast<std::uint32_t>(ram_[a.physical + i]) << (8u * i);
        }
        return value;
    }
    if (a.physical >= kRomPhysical && a.physical - kRomPhysical < kRomBytes) {
        for (std::size_t i = 0; i < bytes; ++i)
            value |= static_cast<std::uint32_t>(rom_[a.physical - kRomPhysical + i]) << (8u * i);
        return value;
    }
    return io_read(a.physical, bytes, what);
}

void Iop::store(std::uint32_t address, std::size_t bytes, std::optional<std::uint32_t> value, const char* what) {
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
        for (std::size_t i = 0; i < bytes; ++i) {
            ram_[a.physical + i] = static_cast<std::uint8_t>(value.value_or(0u) >> (8u * i));
            ram_written_[a.physical + i] = value ? 1u : 0u;
        }
        return;
    }
    if (a.physical >= kRomPhysical && a.physical - kRomPhysical < kRomBytes)
        throw IopStop{std::string("store ") + what + " to ROM0 at " + hex(address)};
    io_write(a.physical, bytes, value, what);
}

std::uint32_t Iop::io_read(std::uint32_t physical, std::size_t bytes, const char* what) {
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
    if(physical==0x1F803800u) {
        if(bytes!=2u) throw IopStop{name+": MPU board ID requires a 16-bit read"};
        if(!hardware_ || !hardware_->mpu_board_id) throw IopStop{name+": MPU board ID has not been measured for this profile"};
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
        if (bytes != 1u) throw IopStop{name + ": only 8-bit reads are modeled"};
        if (hardware_ == nullptr || !hardware_->dev9_revision)
            throw IopStop{name + " (DEV9_R_REV): no console profile with the SSBUS revision"};
        return *hardware_->dev9_revision;
    }
    if (physical == kCdvdNStatus) {
        if (bytes != 1u) throw IopStop{name + ": only 8-bit reads are modeled"};
        return kCdvdNStatusIdle;
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
        return result;
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
                    if ((result & (1u << 15)) || ((result & (1u << 23)) && (flags || flags2))) result |= 1u << 31;
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
        if (bytes != 4u || !value) throw IopStop{"SIF1 DMA requires known 32-bit accesses"};
        iop_sif1_write(physical, *value);
        return;
    }
    const auto name = std::string("store ") + what + " of IOP register " + hex(physical);
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
        return;
    }
    if (is_latch(physical)) {
        if (bytes != 4u) throw IopStop{name + ": only 32-bit accesses are modeled"};
        if (dma_channel_field(physical) == 2u && (!value || (*value & kChcrStart) != 0u))
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
                return;
            }
        latches_.push_back({physical, value});
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
    return kTimerIrqs | kDmaIrq |
           (video_.state != GsRegisters::VerticalClock::State::Unknown ? kVblankStartIrq | kVblankEndIrq : 0u);
}

void Iop::refresh_video(const GsRegisters& video) {
    if (video_generation_ != video.timing_generation()) {
        // The EE synchronizes the IOP before changing GS timing. Process the
        // new generation from this boundary, using the shared IOP time unit.
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
    const auto merge_base = [&](unsigned r) {
        if (delayed && delayed->reg == r) {
            if (!delayed->defined && !undefined_load_) undefined_load_ = delayed->origin;
            return delayed->value;
        }
        return reg(r);
    };

    switch (op) {
        case 0x00:
            switch (fn) {
                case 0x00: set_reg(rd, reg(rt) << sa); break;                                   // SLL
                case 0x02: set_reg(rd, reg(rt) >> sa); break;                                   // SRL
                case 0x03: set_reg(rd, static_cast<std::uint32_t>(static_cast<std::int32_t>(reg(rt)) >> sa)); break;
                case 0x04: set_reg(rd, reg(rt) << (reg(rs) & 31u)); break;                      // SLLV
                case 0x06: set_reg(rd, reg(rt) >> (reg(rs) & 31u)); break;                      // SRLV
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
                case 0x21: set_reg(rd, reg(rs) + reg(rt)); break;                               // ADDU
                case 0x22: {                                                                    // SUB
                    const auto a = reg(rs), b = reg(rt), r = a - b;
                    if (((a ^ b) & (a ^ r)) >> 31) throw overflow();
                    set_reg(rd, r);
                    break;
                }
                case 0x23: set_reg(rd, reg(rs) - reg(rt)); break;                               // SUBU
                case 0x24: and_regs(rd, rs, rt); break;                                          // AND
                case 0x25: set_reg(rd, reg(rs) | reg(rt)); break;
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
        case 0x0C: set_reg(rt, masked_reg(rs, uimm) & uimm); break;
        case 0x0D: set_reg(rt, reg(rs) | uimm); break;
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
        case 0x22: case 0x26: {                                                                 // LWL / LWR
            const auto address = reg(rs) + imm;
            const auto shift = address & 3u;
            const auto aligned = address & ~3u;
            auto value = merge_base(rt);
            if (op == 0x22) {
                for (unsigned k = 0; k <= shift; ++k) {
                    const auto bit = 8u * (k + 3u - shift);
                    value = (value & ~(0xFFu << bit)) | (load(aligned + k, 1u, "LWL") << bit);
                }
            } else {
                for (unsigned k = shift; k < 4u; ++k) {
                    const auto bit = 8u * (k - shift);
                    value = (value & ~(0xFFu << bit)) | (load(aligned + k, 1u, "LWR") << bit);
                }
            }
            load_reg(rt, value);
            break;
        }
        case 0x28: { const auto v = stored_reg(rt); store(reg(rs) + imm, 1u, v ? std::optional<std::uint32_t>(*v & 0xFFu) : v, "SB"); break; }
        case 0x29: { const auto v = stored_reg(rt); store(reg(rs) + imm, 2u, v ? std::optional<std::uint32_t>(*v & 0xFFFFu) : v, "SH"); break; }
        case 0x2B: store(reg(rs) + imm, 4u, stored_reg(rt), "SW"); break;
        case 0x2A: case 0x2E: {                                                                 // SWL / SWR
            const auto address = reg(rs) + imm;
            const auto shift = address & 3u;
            const auto aligned = address & ~3u;
            const auto value = reg(rt);
            if (op == 0x2A)
                for (unsigned k = 0; k <= shift; ++k) store(aligned + k, 1u, (value >> (8u * (k + 3u - shift))) & 0xFFu, "SWL");
            else
                for (unsigned k = shift; k < 4u; ++k) store(aligned + k, 1u, (value >> (8u * (k - shift))) & 0xFFu, "SWR");
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
