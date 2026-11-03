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

void Iop::reset(std::span<const std::uint8_t> rom0, const ConsoleHardware* hardware) {
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
    if (rom_.size() != kRomBytes) stop_ = "no ROM0 mapped for the IOP";
}

void Iop::run_until(std::uint64_t cycle) {
    while (!stop_ && cycle_ < cycle) {
        const auto pc = pc_;
        const auto saved_target = branch_target_;
        const bool saved_slot = in_delay_slot_;
        try {
            step();
            ++cycle_;
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

std::uint32_t Iop::reg(unsigned index) const {
    if ((gpr_defined_ & (1u << index)) == 0u)
        throw IopStop{"GPR " + std::to_string(index) + " used while its value is unknown (" + gpr_origin_[index] + ")"};
    return gpr_[index];
}
std::optional<std::uint32_t> Iop::stored_reg(unsigned index) const {
    if ((gpr_defined_ & (1u << index)) == 0u) return std::nullopt;
    return gpr_[index];
}
void Iop::set_reg(unsigned index, std::uint32_t value) {
    if (index != 0u) write_ = PendingLoad{index, value, true, {}};
}
// A load of never-written RAM leaves the register undefined (its value is
// unknown); only a later use of that register stops the IOP.
void Iop::load_reg(unsigned index, std::uint32_t value) {
    if (index != 0u)
        pending_load_ = PendingLoad{index, value, !undefined_load_, undefined_load_.value_or(std::string())};
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
    const auto name = std::string("load ") + what + " of IOP register " + hex(physical);
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
    if (physical == kIStat)
        throw IopStop{name + ": no IOP interrupt source is modeled, so the pending bits are unknown"};
    if (is_latch(physical)) {
        if (bytes != 4u) throw IopStop{name + ": only 32-bit accesses are modeled"};
        for (auto& [address, value] : latches_)
            if (address == physical) {
                if (!value && !undefined_load_) undefined_load_ = name + " at IOP " + hex(pc_) + ": unknown value written back";
                const auto result = value.value_or(0u);
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
    if (physical == kIStat && bytes == 4u) {
        // Acknowledge: clears the pending bits written as 0. Nothing is ever
        // raised (no source is modeled) and reads stay refused, so there is
        // no state to change.
        return;
    }
    if (is_latch(physical)) {
        if (bytes != 4u) throw IopStop{name + ": only 32-bit accesses are modeled"};
        if (dma_channel_field(physical) == 2u && (!value || (*value & kChcrStart) != 0u))
            throw IopStop{name + " = " + (value ? hex(*value) : std::string("(unknown)")) +
                          ": DMA transfers are not modeled"};
        if (physical == kICtrl && value) value = *value & 1u;
        if (physical == kDicr && value) value = *value & kDicrWritable;
        if (physical == kDicr2 && value) value = *value & kDicr2Writable;
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
        case 0x40: // OpenConfig: mode (0 read, 1 write), block, block count
            if (s_params_.size() != 3u || s_params_[0] > 1u || s_params_[1] > 2u)
                throw IopStop{name + ": parameters outside the documented OpenConfig form"};
            if (s_params_[0] != 0u) throw IopStop{name + ": writing the configuration is not modeled"};
            // One status byte, 0 = success (ps2tek leaves it unspecified;
            // DobieStation and PCSX2 return 0).
            s_results_.push_back(0u);
            break;
        case 0x41: // ReadConfig: 16 bytes of the open configuration block
            throw IopStop{name + ": ReadConfig returns the configuration stored in this console's MECHACON "
                          "NVM (EEPROM), which differs per console; no NVM image is supplied"};
        default:
            throw IopStop{std::string("store ") + what + " of " + name + ": this MECHACON command is not modeled"};
    }
    s_params_.clear();
}

std::uint32_t Iop::cop0_read(unsigned index) const {
    if (index == Prid) {
        if (hardware_ == nullptr || !hardware_->iop_prid)
            throw IopStop{"COP0 PRId read: no console profile with a measured IOP revision is selected"};
        return *hardware_->iop_prid;
    }
    if (index >= cop0_.size() || !cop0_[index])
        throw IopStop{"COP0 register " + std::to_string(index) + " read before any write"};
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
            if ((value & (kSrIec | kSrKuc | kSrSwc | kSrRe)) != 0u)
                throw IopStop{"COP0 SR = " + hex(value) +
                              ": interrupts, user mode, swapped caches and reverse endianness are not modeled"};
            cop0_[Sr] = value;
            return;
        case Cause:
            if (value != 0u) throw IopStop{"COP0 Cause = " + hex(value) + ": software interrupts are not modeled"};
            cop0_[Cause] = value;
            return;
        default:
            throw IopStop{"COP0 register " + std::to_string(index) + " write is not modeled"};
    }
}

void Iop::step() {
    const auto pc = pc_;
    const auto word = fetch(pc);
    const bool delay_slot = in_delay_slot_;
    const auto after = branch_target_;
    std::optional<std::uint32_t> target; // taken branch or jump of this instruction
    bool branch = false;
    write_.reset();
    undefined_load_.reset();
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
                case 0x0C: throw IopStop{"SYSCALL exception (not vectored)"};
                case 0x0D: throw IopStop{"BREAK exception (not vectored)"};
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
                case 0x24: set_reg(rd, reg(rs) & reg(rt)); break;
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
        case 0x04: branch_if(reg(rs) == reg(rt)); break;
        case 0x05: branch_if(reg(rs) != reg(rt)); break;
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
        case 0x0C: set_reg(rt, reg(rs) & uimm); break;
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
    if (delayed) {
        gpr_[delayed->reg] = delayed->value;
        if (delayed->defined) gpr_defined_ |= 1u << delayed->reg;
        else {
            gpr_defined_ &= ~(1u << delayed->reg);
            gpr_origin_[delayed->reg] = delayed->origin;
        }
    }
    if (write_) {
        gpr_[write_->reg] = write_->value;
        gpr_defined_ |= 1u << write_->reg;
    }
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
