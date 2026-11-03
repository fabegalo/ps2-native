#include "ps2native/memory.hpp"

#include <algorithm>
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
constexpr std::uint32_t kPageMaskBits = 0x01FFE000u;
constexpr std::uint32_t kEntryHiBits = 0xFFFFE0FFu;  // VPN2 | ASID
constexpr std::uint32_t kEntryLo0Bits = 0x83FFFFFFu; // S | PFN | C | D | V | G
constexpr std::uint32_t kEntryLo1Bits = 0x03FFFFFFu; // PFN | C | D | V | G
constexpr std::uint32_t kEntryLoScratchpad = 0x80000000u;
constexpr std::uint32_t kScratchpadSpan = 16u * 1024u;

// Address bits compared against VPN2. An entry with the S bit maps the whole
// 16 KiB scratchpad whatever its PageMask: the real BIOS writes PageMask 0
// for its scratchpad entry and then uses its stack at 0x70003FF0.
std::uint32_t entry_vpn_mask(const TlbEntry& e) {
    if ((e.entry_lo0 & kEntryLoScratchpad) != 0u) return ~(kScratchpadSpan - 1u);
    return ~(e.page_mask | 0x1FFFu);
}

std::string access_name(bool write, std::size_t size, const char* what) {
    std::ostringstream stream;
    stream << (write ? 'W' : 'R') << size * 8u << " (" << what << ')';
    return stream.str();
}
// Register regions handled (or refused) by EeRegisters.
bool is_ee_register_region(BusRegion region) {
    return (region >= BusRegion::EeTimers && region <= BusRegion::EeRegisters) ||
           region == BusRegion::GsRegisters || region == BusRegion::IopRegisters || region == BusRegion::IopMemory ||
           region == BusRegion::Block1A;
}
BusError device_refusal(BusFault fault, bool write, std::size_t size, std::uint32_t address,
                        std::uint32_t physical, BusRegion region, const char* what, const std::string& detail) {
    return BusError(fault, write, static_cast<std::uint8_t>(size), address, physical, region,
                    "EE bus " + access_name(write, size, what) + " at " + hex32(address) + " (physical " +
                    hex32(physical) + ", " + bus_region_name(region) + "): " + bus_fault_name(fault) +
                    (detail.empty() ? std::string() : " (" + detail + ")"));
}
} // namespace

const char* bus_region_name(BusRegion region) noexcept {
    switch (region) {
        case BusRegion::MainRam: return "main RAM";
        case BusRegion::BiosRom: return "BIOS ROM0";
        case BusRegion::Scratchpad: return "scratchpad";
        case BusRegion::EeTimers: return "EE timers";
        case BusRegion::Ipu: return "IPU";
        case BusRegion::Gif: return "GIF";
        case BusRegion::Vif0: return "VIF0";
        case BusRegion::Vif1: return "VIF1";
        case BusRegion::EeFifos: return "VIF/GIF/IPU FIFOs";
        case BusRegion::Dmac: return "DMAC";
        case BusRegion::Intc: return "INTC";
        case BusRegion::Sio: return "SIO";
        case BusRegion::Sif: return "SIF/SBUS";
        case BusRegion::Mch: return "MCH (RDRAM controller)";
        case BusRegion::DmacExtended: return "DMAC extended";
        case BusRegion::EeRegisters: return "EE registers";
        case BusRegion::VuMemory: return "VU0/VU1 memory";
        case BusRegion::GsRegisters: return "GS privileged registers";
        case BusRegion::IopMemory: return "IOP memory";
        case BusRegion::Block1A: return "0x1A000000 block";
        case BusRegion::Rom1: return "ROM1";
        case BusRegion::Rom2: return "ROM2";
        case BusRegion::IopRegisters: return "IOP registers";
        case BusRegion::Unmapped: return "unmapped";
    }
    return "?";
}

BusRegion classify_physical(std::uint32_t p) noexcept {
    if (p < Memory::kMainRamBytes) return BusRegion::MainRam;
    if (p >= 0x10000000u && p < 0x10010000u) {
        if (p < 0x10002000u) return BusRegion::EeTimers;
        if (p < 0x10003000u) return BusRegion::Ipu;
        if (p < 0x10003800u) return BusRegion::Gif;
        if (p < 0x10003C00u) return BusRegion::Vif0;
        if (p < 0x10004000u) return BusRegion::Vif1;
        if (p < 0x10008000u) return BusRegion::EeFifos;
        if (p < 0x1000F000u) return BusRegion::Dmac;
        if (p < 0x1000F100u) return BusRegion::Intc;
        if (p < 0x1000F200u) return BusRegion::Sio;
        if (p < 0x1000F400u) return BusRegion::Sif;
        if (p < 0x1000F500u) return BusRegion::Mch;
        if (p < 0x1000F600u) return BusRegion::DmacExtended;
        return BusRegion::EeRegisters;
    }
    if (p >= 0x11000000u && p < 0x11010000u) return BusRegion::VuMemory;
    if (p >= 0x12000000u && p < 0x12002000u) return BusRegion::GsRegisters;
    if (p >= 0x1A000000u && p < 0x1A000020u) return BusRegion::Block1A;
    if (p >= 0x1C000000u && p < 0x1C800000u) return BusRegion::IopMemory;
    if (p >= 0x1E000000u && p < 0x1E400000u) return BusRegion::Rom1;
    if (p >= 0x1E400000u && p < 0x1E800000u) return BusRegion::Rom2;
    if (p >= 0x1F800000u && p < 0x1FA00000u) return BusRegion::IopRegisters;
    if (p >= Memory::kBiosRomPhysical && p - Memory::kBiosRomPhysical < Memory::kBiosRomBytes)
        return BusRegion::BiosRom;
    return BusRegion::Unmapped;
}

const char* bus_fault_name(BusFault fault) noexcept {
    switch (fault) {
        case BusFault::Unaligned: return "address error (unaligned)";
        case BusFault::OutsideProgramRam: return "outside EE main RAM";
        case BusFault::TlbRefill: return "TLB refill";
        case BusFault::TlbInvalid: return "TLB invalid";
        case BusFault::TlbModified: return "TLB modified";
        case BusFault::TlbMultipleMatch: return "TLB multiple match";
        case BusFault::KusegWithErl: return "kuseg with Status.ERL";
        case BusFault::Device: return "device not implemented";
        case BusFault::RomWrite: return "write to ROM";
        case BusFault::Unmapped: return "unmapped physical address";
    }
    return "?";
}

BusError::BusError(BusFault fault_, bool write_, std::uint8_t bytes_, std::uint32_t virtual_address_,
                   std::optional<std::uint32_t> physical_address_, BusRegion region_,
                   const std::string& message)
    : std::runtime_error(message), fault(fault_), write(write_), bytes(bytes_),
      virtual_address(virtual_address_), physical_address(physical_address_), region(region_) {}

Memory::Memory() : ram_(kMainRamBytes) {}

std::size_t Memory::physical_address(std::uint32_t address, std::size_t size) {
    std::uint64_t base = address;
    if (address >= 0x80000000u && address < 0x82000000u) base = address - 0x80000000u;
    else if (address >= 0xA0000000u && address < 0xA2000000u) base = address - 0xA0000000u;
    if (base > kMainRamBytes || size > kMainRamBytes - base)
        throw BusError(BusFault::OutsideProgramRam, false, static_cast<std::uint8_t>(size), address,
                       std::nullopt, BusRegion::Unmapped,
                       "Guest address is not in implemented EE main RAM");
    return static_cast<std::size_t>(base);
}

void Memory::load(const ElfImage& elf) {
    mapping_ = Mapping::ElfProgram;
    rom_.clear();
    scratchpad_.clear();
    tlb_ = {};
    std::fill(ram_.begin(), ram_.end(), std::uint8_t{0});
    for (const auto& seg : elf.segments) {
        const auto start = physical_address(seg.vaddr, seg.memory_size);
        std::copy_n(elf.bytes.data() + seg.file_offset, seg.file_size, ram_.begin() + start);
    }
}

void Memory::reset_for_boot(std::span<const std::uint8_t> rom0, const ConsoleHardware* hardware,
                            const NvmImage* nvm) {
    if (rom0.size() != kBiosRomBytes)
        throw std::runtime_error("BIOS ROM0 must be exactly 4 MiB to be mapped at 0x1FC00000");
    mapping_ = Mapping::HardwareReset;
    std::fill(ram_.begin(), ram_.end(), std::uint8_t{0});
    rom_.assign(rom0.begin(), rom0.end());
    scratchpad_.assign(kScratchpadBytes, 0);
    tlb_ = {};
    tlb_defined_ = {};
    asid_ = 0;
    error_level_ = true;
    registers_.reset(hardware, rom_, nvm);
}

void Memory::set_translation_context(std::uint8_t asid, bool error_level) noexcept {
    asid_ = asid;
    error_level_ = error_level;
}

void Memory::tlb_write(unsigned index, const TlbEntry& entry) {
    if (index >= kTlbEntries) throw std::runtime_error("TLB index " + std::to_string(index) + " out of range");
    tlb_[index] = {entry.page_mask & kPageMaskBits, entry.entry_hi & kEntryHiBits,
                   entry.entry_lo0 & kEntryLo0Bits, entry.entry_lo1 & kEntryLo1Bits};
    tlb_defined_[index] = true;
}

const TlbEntry& Memory::tlb_entry(unsigned index) const {
    if (index >= kTlbEntries) throw std::runtime_error("TLB index " + std::to_string(index) + " out of range");
    if (!tlb_defined_[index])
        throw std::runtime_error("TLBR of entry " + std::to_string(index) +
                                 ", undefined since reset (never written)");
    return tlb_[index];
}

// Entries never written since reset hold unknown contents. They are assumed
// not to match any address (hypothesis): the real BIOS writes only entry 0
// (scratchpad) before using it, which would be unsafe otherwise.
std::optional<unsigned> Memory::tlb_probe(std::uint32_t entry_hi) const {
    for (unsigned i = 0; i < kTlbEntries; ++i) {
        if (!tlb_defined_[i]) continue;
        const auto& e = tlb_[i];
        const auto vpn_mask = entry_vpn_mask(e);
        const bool global = (e.entry_lo0 & e.entry_lo1 & 1u) != 0u;
        if (((entry_hi ^ e.entry_hi) & vpn_mask) == 0u &&
            (global || (e.entry_hi & 0xFFu) == (entry_hi & 0xFFu)))
            return i;
    }
    return std::nullopt;
}

std::uint32_t Memory::translate_tlb(std::uint32_t address, std::size_t size, bool write,
                                    bool& scratchpad) const {
    const auto bytes = static_cast<std::uint8_t>(size);
    auto fail = [&](BusFault fault, const std::string& detail) -> BusError {
        return BusError(fault, write, bytes, address, std::nullopt, BusRegion::Unmapped,
                        "EE bus " + access_name(write, size, "TLB") + " at " + hex32(address) + ": " +
                        bus_fault_name(fault) + " exception not modeled (" + detail + ")");
    };
    std::optional<unsigned> match;
    for (unsigned i = 0; i < kTlbEntries; ++i) {
        if (!tlb_defined_[i]) continue; // assumed not to match (see tlb_probe)
        const auto& e = tlb_[i];
        const auto vpn_mask = entry_vpn_mask(e);
        if (((address ^ e.entry_hi) & vpn_mask) != 0u) continue;
        const bool global = (e.entry_lo0 & e.entry_lo1 & 1u) != 0u;
        if (!global && (e.entry_hi & 0xFFu) != asid_) continue;
        if (match) throw fail(BusFault::TlbMultipleMatch, "entries " + std::to_string(*match) +
                              " and " + std::to_string(i));
        match = i;
    }
    if (!match) {
        const auto undefined = std::count(tlb_defined_.begin(), tlb_defined_.end(), false);
        throw fail(BusFault::TlbRefill, "no entry for ASID " + std::to_string(asid_) +
                   (undefined != 0 ? "; " + std::to_string(undefined) +
                                     " TLB entries undefined since reset are assumed not to match"
                                   : std::string()));
    }
    const auto& e = tlb_[*match];
    if ((e.entry_lo0 & kEntryLoScratchpad) != 0u) {
        // The S bit maps the 16 KiB scratchpad at the entry's 16 KiB aligned VPN2;
        // the match above already bounds the (naturally aligned) access to it.
        scratchpad = true;
        return address & (kScratchpadSpan - 1u);
    }
    const auto half = ((e.page_mask | 0x1FFFu) + 1u) >> 1;
    const auto lo = (address & half) != 0u ? e.entry_lo1 : e.entry_lo0;
    if ((lo & 2u) == 0u) throw fail(BusFault::TlbInvalid, "entry " + std::to_string(*match));
    if (write && (lo & 4u) == 0u) throw fail(BusFault::TlbModified, "entry " + std::to_string(*match));
    const auto frame = ((lo >> 6) & 0xFFFFFu) << 12;
    return (frame & ~(half - 1u)) | (address & (half - 1u));
}

Memory::Target Memory::resolve(std::uint32_t address, std::size_t size, bool write, const char* what) const {
    if ((address & (size - 1u)) != 0)
        throw BusError(BusFault::Unaligned, write, static_cast<std::uint8_t>(size), address, std::nullopt,
                       BusRegion::Unmapped, std::string("Unaligned guest ") + what);
    if (mapping_ == Mapping::ElfProgram) {
        const auto offset = physical_address(address, size);
        return {static_cast<std::uint32_t>(offset), BusRegion::MainRam, static_cast<std::uint32_t>(offset)};
    }
    std::uint32_t physical = 0;
    if (address >= 0x80000000u && address < 0xC0000000u) {
        physical = address & 0x1FFFFFFFu; // KSEG0 (cached) / KSEG1 (uncached): unmapped.
    } else {
        if (address < 0x80000000u && error_level_)
            throw BusError(BusFault::KusegWithErl, write, static_cast<std::uint8_t>(size), address,
                           std::nullopt, BusRegion::Unmapped,
                           "EE bus " + access_name(write, size, what) + " at " + hex32(address) +
                           ": kuseg access while Status.ERL = 1 (unmapped kuseg is not modeled)");
        bool scratchpad = false;
        physical = translate_tlb(address, size, write, scratchpad);
        if (scratchpad) return {physical, BusRegion::Scratchpad, physical};
    }
    const auto region = classify_physical(physical);
    auto refuse = [&](BusFault fault) {
        return device_refusal(fault, write, size, address, physical, region, what, {});
    };
    switch (region) {
        case BusRegion::MainRam:
            return {physical, region, physical};
        case BusRegion::BiosRom:
            if (write) throw refuse(BusFault::RomWrite);
            return {physical, region, physical - kBiosRomPhysical};
        case BusRegion::Unmapped:
            throw refuse(BusFault::Unmapped);
        default:
            if (is_ee_register_region(region)) return {physical, region, 0u};
            throw refuse(BusFault::Device);
    }
}

std::uint64_t Memory::read_le(std::uint32_t address, std::size_t size, const char* what, bool notify) const {
    const auto target = resolve(address, size, false, what);
    std::uint64_t value = 0;
    if (is_ee_register_region(target.region)) {
        // Only instruction fetches are not notified: never execute from registers.
        if (!notify)
            throw device_refusal(BusFault::Device, false, size, address, target.physical, target.region, what,
                                 "instruction fetch from device registers");
        try {
            value = registers_.read(target.physical, size);
        } catch (const DeviceRefusal& refusal) {
            throw device_refusal(BusFault::Device, false, size, address, target.physical, target.region, what,
                                 refusal.what());
        }
    } else {
        const auto& storage = target.region == BusRegion::BiosRom ? rom_
                            : target.region == BusRegion::Scratchpad ? scratchpad_ : ram_;
        for (std::size_t i = 0; i < size; ++i)
            value |= static_cast<std::uint64_t>(storage[target.offset + i]) << (8u * i);
    }
    if (notify && observer_ != nullptr)
        observer_->on_bus_access({false, static_cast<std::uint8_t>(size), address, target.physical,
                                  target.region, value});
    return value;
}
void Memory::write_le(std::uint32_t address, std::size_t size, std::uint64_t value, const char* what) {
    const auto target = resolve(address, size, true, what);
    if (is_ee_register_region(target.region)) {
        try {
            registers_.write(target.physical, size, value);
        } catch (const DeviceRefusal& refusal) {
            throw device_refusal(BusFault::Device, true, size, address, target.physical, target.region, what,
                                 refusal.what());
        }
    } else {
        auto& storage = target.region == BusRegion::Scratchpad ? scratchpad_ : ram_;
        for (std::size_t i = 0; i < size; ++i)
            storage[target.offset + i] = static_cast<std::uint8_t>(value >> (8u * i));
    }
    if (observer_ != nullptr)
        observer_->on_bus_access({true, static_cast<std::uint8_t>(size), address, target.physical,
                                  target.region, value});
}
std::uint32_t Memory::translate(std::uint32_t address, bool write) const {
    return resolve(address & ~3u, 4, write, "CACHE").physical;
}
std::uint8_t Memory::read8(std::uint32_t address) const {
    return static_cast<std::uint8_t>(read_le(address, 1, "LB", true));
}
void Memory::write8(std::uint32_t address, std::uint8_t value) {
    write_le(address, 1, value, "SB");
}
std::uint16_t Memory::read16(std::uint32_t address) const {
    return static_cast<std::uint16_t>(read_le(address, 2, "LH", true));
}
void Memory::write16(std::uint32_t address, std::uint16_t value) {
    write_le(address, 2, value, "SH");
}
std::uint32_t Memory::read32(std::uint32_t address) const {
    return static_cast<std::uint32_t>(read_le(address, 4, "LW", true));
}
void Memory::write32(std::uint32_t address, std::uint32_t value) {
    write_le(address, 4, value, "SW");
}
std::uint64_t Memory::read64(std::uint32_t address) const {
    return read_le(address, 8, "LD", true);
}
void Memory::write64(std::uint32_t address, std::uint64_t value) {
    write_le(address, 8, value, "SD");
}
std::uint32_t Memory::fetch32(std::uint32_t address) const {
    return static_cast<std::uint32_t>(read_le(address, 4, "instruction fetch", false));
}
}  // namespace ps2native
