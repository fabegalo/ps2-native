#include "ps2native/memory.hpp"
#include "ps2native/ee.hpp"
#include "ps2native/sha256.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ps2native {
std::string Memory::code_identity(std::uint32_t pc) const {
    const auto target=resolve(pc,4u,false,"fetch");
    const auto physical=target.physical;
    if(target.region==BusRegion::BiosRom)return "EE ROM0 sha256="+sha256_hex(rom_);
    if(target.region==BusRegion::Rom1)return "EE ROM1 sha256="+sha256_hex(rom1_);
    if(target.region==BusRegion::MainRam) {
        const auto base=physical&~4095u;std::vector<std::uint8_t> bytes;
        for(unsigned at=base;at<base+4096u;at+=4) {
            const auto word=diagnostic_ram(at,4);
            for(const auto value:{word.value&~word.unknown,word.unknown})
                for(unsigned b=0;b<4;++b)bytes.push_back(static_cast<std::uint8_t>(value>>(b*8u)));
        }
        return "EE loaded RAM page base="+std::to_string(base)+" observed_sha256="+sha256_hex(bytes);
    }
    return "EE mapped code physical="+std::to_string(physical);
}
void Memory::configure_rtc(std::uint64_t utc_epoch_seconds) {
    if(cache_clock_ && *cache_clock_)throw DeviceRefusal("RTC input must precede EE execution");
    registers_.configure_rtc(utc_epoch_seconds);
}
ObservedDoubleword Memory::diagnostic_ram(std::uint32_t offset, std::size_t bytes) const {
    if (!bytes || bytes > 8u || offset >= ram_.size() || bytes > ram_.size() - offset)
        throw std::out_of_range("diagnostic RAM range outside backing memory");
    ObservedDoubleword result{};
    for (std::size_t i = 0; i < bytes; ++i) {
        const auto at = offset + static_cast<std::uint32_t>(i);
        result.value |= std::uint64_t{ram_[at]} << (8u * i);
        const auto unknown = unknown_words_.find(at & ~3u);
        if (unknown != unknown_words_.end())
            result.unknown |= std::uint64_t{(unknown->second >> ((at & 3u) * 8u)) & 255u} << (8u * i);
    }
    return result;
}
namespace {
// memcpy permits unaligned host storage and avoids aliasing assumptions.
// Guest ordering is handled by the caller before accessing RAM/cache bytes.
std::uint64_t load_little(const std::uint8_t* bytes, std::size_t size) noexcept {
    if constexpr (std::endian::native==std::endian::little) {
        switch (size) {
            case 1: return *bytes;
            case 2: { std::uint16_t value;std::memcpy(&value,bytes,2);return value; }
            case 4: { std::uint32_t value;std::memcpy(&value,bytes,4);return value; }
            case 8: { std::uint64_t value;std::memcpy(&value,bytes,8);return value; }
        }
    }
    std::uint64_t value=0;
    for (std::size_t i=0;i<size;++i) value|=std::uint64_t{bytes[i]}<<(8u*i);
    return value;
}
void store_little(std::uint8_t* bytes, std::size_t size, std::uint64_t value) noexcept {
    if constexpr (std::endian::native==std::endian::little) {
        // Copy from the full 64-bit object: its first size bytes are exactly
        // the enabled low bytes on little-endian hosts, including partial SW.
        std::memcpy(bytes,&value,size);
    } else {
        for (std::size_t i=0;i<size;++i) bytes[i]=static_cast<std::uint8_t>(value>>(8u*i));
    }
}
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
    if (p >= 0x1F402000u && p < 0x1F402020u) return BusRegion::IopRegisters; // shared CDVD on SBUS
    if (p >= 0x1F800000u && p < 0x1FA00000u) return BusRegion::IopRegisters;
    if (p >= Memory::kBiosRomPhysical && p - Memory::kBiosRomPhysical < Memory::kBiosRomBytes)
        return BusRegion::BiosRom;
    return BusRegion::Unmapped;
}

const char* bus_fault_name(BusFault fault) noexcept {
    switch (fault) {
        case BusFault::Unaligned: return "address error (unaligned)";
        case BusFault::Privilege: return "address error (privilege)";
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
    if(hle_kernel_)throw std::runtime_error("Detach HLE before replacing console memory");
    hle_used_=false;
    reset_tlb_cache();
    reset_data_cache();
    diagnostic_ee_early_digit_carry_=false;fpu_arithmetic_started_=false;
    unknown_words_.clear();fpu_relations_.clear();
    privilege_ = Privilege::Kernel;
    mapping_ = Mapping::ElfProgram;
    rom_.clear();
    rom1_.clear();
    scratchpad_.clear();
    tlb_ = {};
    std::fill(ram_.begin(), ram_.end(), std::uint8_t{0});
    for (const auto& seg : elf.segments) {
        const auto start = physical_address(seg.vaddr, seg.memory_size);
        std::copy_n(elf.bytes.data() + seg.file_offset, seg.file_size, ram_.begin() + start);
    }
}

void Memory::reset_for_boot(std::span<const std::uint8_t> rom0, const ConsoleHardware* hardware,
                            const NvmImage* nvm, const DiagnosticAssumptions* assumptions,
                            std::span<const std::uint8_t> rom1) {
    if(hle_kernel_)throw std::runtime_error("Detach HLE before resetting the console");
    if (rom0.size() != kBiosRomBytes)
        throw std::runtime_error("BIOS ROM0 must be exactly 4 MiB to be mapped at 0x1FC00000");
    hle_used_=false;
    reset_tlb_cache();
    mapping_ = Mapping::HardwareReset;
    reset_data_cache();
    diagnostic_ee_early_digit_carry_=false;fpu_arithmetic_started_=false;
    unknown_words_.clear();fpu_relations_.clear();
    std::fill(ram_.begin(), ram_.end(), std::uint8_t{0});
    rom_.assign(rom0.begin(), rom0.end());
    rom1_.assign(rom1.begin(),rom1.end());
    scratchpad_.assign(kScratchpadBytes, 0);
    tlb_ = {};
    tlb_defined_ = {};
    asid_ = 0;
    error_level_ = true;
    privilege_ = Privilege::Kernel;
    registers_.reset(hardware, rom_, nvm, assumptions, rom1_);
    registers_.attach_ee_ram(ram_, &unknown_words_, scratchpad_, &fpu_relations_);
}

void Memory::load_direct_elf(const ElfImage& elf) {
    load(elf);
    mapping_ = Mapping::DirectElf;
    scratchpad_.assign(kScratchpadBytes, 0);
    tlb_defined_ = {};
    asid_ = 0;
    error_level_ = false;
    registers_.reset(); // No firmware, NVM, kernel services or running IOP.
    registers_.attach_ee_ram(ram_, &unknown_words_, scratchpad_, &fpu_relations_);
}

void Memory::load_iop_program(std::span<const std::uint8_t> bytes, std::uint32_t address, std::uint32_t entry) {
    if (mapping_ != Mapping::DirectElf) throw std::runtime_error("IOP development payload requires Direct ELF mode");
    registers_.load_iop_program(bytes, address, entry);
}
bool Memory::intc_interrupt_pending() const {
    try {return registers_.intc_interrupt_pending();}
    catch(const DeviceRefusal& error) {
        throw device_refusal(BusFault::Device,false,4,kIntcStat,kIntcStat,BusRegion::Intc,
                             "interrupt sample",error.what());
    }
}
std::optional<std::uint64_t> Memory::idle_ram_horizon(std::uint32_t address) const {
    const auto target=resolve(address,4,false,"idle LW");
    if(target.region!=BusRegion::MainRam && target.region!=BusRegion::Scratchpad)return std::nullopt;
    return registers_.idle_poll_horizon();
}
bool Memory::interrupt_inert_address(std::uint32_t address,unsigned bytes,bool write) const {
    try {
        const auto target=resolve(address,bytes,write,"interrupt scheduling proof");
        return target.region==BusRegion::MainRam || target.region==BusRegion::Scratchpad;
    } catch(const BusError&) {return false;}
}

void Memory::set_translation_context(std::uint8_t asid, bool error_level, Privilege privilege) noexcept {
    asid_ = asid;
    error_level_ = error_level;
    privilege_ = privilege;
}
void Memory::set_cpu_context(std::uint32_t status, std::uint32_t entry_hi) noexcept {
    const auto privilege = (status & (kStatusEXL | kStatusERL)) ? Privilege::Kernel
                         : static_cast<Privilege>((status & kStatusKSU) >> 3u);
    set_translation_context(static_cast<std::uint8_t>(entry_hi), (status & kStatusERL) != 0u, privilege);
}

void Memory::tlb_write(unsigned index, const TlbEntry& entry) {
    if (index >= kTlbEntries) throw std::runtime_error("TLB index " + std::to_string(index) + " out of range");
    reset_tlb_cache();
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
                                    bool& scratchpad, unsigned& cache_mode) const {
    const auto bytes = static_cast<std::uint8_t>(size);
    auto fail = [&](BusFault fault, const std::string& detail) -> BusError {
        return BusError(fault, write, bytes, address, std::nullopt, BusRegion::Unmapped,
                        "EE bus " + access_name(write, size, "TLB") + " at " + hex32(address) + ": " +
                        bus_fault_name(fault) + " exception not modeled (" + detail + ")");
    };
    // Every supported entry compares only bits above a 4 KiB page. A unique
    // match therefore remains unique throughout this page for this ASID until
    // any guest TLB write. Cache its index, then apply the ordinary entry and
    // access checks below; never cache a physical pointer, fault or permission.
    const auto key=(address>>12u)|(std::uint32_t{asid_}<<20u);
    auto& cached=tlb_match_cache_[(key^(key>>8u)^(key>>16u))&(tlb_match_cache_.size()-1u)];
    std::optional<unsigned> match;
    if(cached.key==key)match=cached.index;
    else {
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
        cached={key,static_cast<std::uint8_t>(*match)};
        return address & (kScratchpadSpan - 1u);
    }
    const auto half = ((e.page_mask | 0x1FFFu) + 1u) >> 1;
    const auto lo = (address & half) != 0u ? e.entry_lo1 : e.entry_lo0;
    cache_mode = (lo >> 3u) & 7u;
    if ((lo & 2u) == 0u) throw fail(BusFault::TlbInvalid, "entry " + std::to_string(*match));
    if (write && (lo & 4u) == 0u) throw fail(BusFault::TlbModified, "entry " + std::to_string(*match));
    const auto frame = ((lo >> 6) & 0xFFFFFu) << 12;
    cached={key,static_cast<std::uint8_t>(*match)};
    return (frame & ~(half - 1u)) | (address & (half - 1u));
}

Memory::Target Memory::resolve(std::uint32_t address, std::size_t size, bool write, const char* what) const {
    if (privilege_ == Privilege::Reserved ||
        (privilege_ == Privilege::User && address >= 0x80000000u) ||
        (privilege_ == Privilege::Supervisor && address >= 0x80000000u &&
         (address < 0xC0000000u || address >= 0xE0000000u)))
        throw BusError(BusFault::Privilege, write, static_cast<std::uint8_t>(size), address, std::nullopt,
                       BusRegion::Unmapped, "EE bus " + access_name(write, size, what) + " at " + hex32(address) +
                       ": address segment is inaccessible in the current CPU privilege mode");
    if ((address & (size - 1u)) != 0)
        throw BusError(BusFault::Unaligned, write, static_cast<std::uint8_t>(size), address, std::nullopt,
                       BusRegion::Unmapped, std::string("Unaligned guest ") + what);
    if (mapping_ == Mapping::ElfProgram) {
        const auto offset = physical_address(address, size);
        return {static_cast<std::uint32_t>(offset), BusRegion::MainRam, static_cast<std::uint32_t>(offset)};
    }
    std::uint32_t physical = 0;
    unsigned cache_mode = 2;
    if (mapping_ == Mapping::DirectElf && address < kMainRamBytes) {
        physical = address;
        cache_mode = 3;
    } else if (mapping_ == Mapping::DirectElf && address >= 0x70000000u &&
               address - 0x70000000u < kScratchpadBytes) {
        return {address - 0x70000000u, BusRegion::Scratchpad, address - 0x70000000u};
    } else if (address >= 0x80000000u && address < 0xC0000000u) {
        physical = address & 0x1FFFFFFFu; // KSEG0 (cached) / KSEG1 (uncached): unmapped.
        if(address < 0xA0000000u) cache_mode = cache_config_ & 7u;
    } else if (address < 0x80000000u && error_level_) {
        // Level-2 error handling uses identity-mapped, uncached kuseg. Do not
        // apply a 29-bit alias mask: the entire 2 GiB segment is unmapped.
        physical = address;
    } else {
        bool scratchpad = false;
        physical = translate_tlb(address, size, write, scratchpad, cache_mode);
        if (scratchpad) return {physical, BusRegion::Scratchpad, physical};
    }
    const auto region = classify_physical(physical);
    auto refuse = [&](BusFault fault) {
        return device_refusal(fault, write, size, address, physical, region, what, {});
    };
    switch (region) {
        case BusRegion::MainRam:
            return {physical, region, physical, cache_mode};
        case BusRegion::VuMemory:
            return {physical, region, 0u, cache_mode};
        case BusRegion::BiosRom:
            if (rom_.empty()) throw refuse(BusFault::Unmapped);
            if (write) throw refuse(BusFault::RomWrite);
            return {physical, region, physical - kBiosRomPhysical, cache_mode};
        case BusRegion::Rom1: {
            const auto offset=physical-0x1E000000u;
            if(offset>rom1_.size() || size>rom1_.size()-offset)throw refuse(BusFault::Unmapped);
            if(write)throw refuse(BusFault::RomWrite);
            return {physical,region,offset,cache_mode};
        }
        case BusRegion::Unmapped:
            throw refuse(BusFault::Unmapped);
        default:
            if (is_ee_register_region(region)) return {physical, region, 0u, cache_mode};
            throw refuse(BusFault::Device);
    }
}

std::uint64_t Memory::storage_unknown(const Target& target, std::size_t size) const {
    if (unknown_words_.empty() || target.region == BusRegion::BiosRom || target.region == BusRegion::Rom1) return 0;
    const auto base = target.offset | (target.region == BusRegion::Scratchpad ? 0x80000000u : 0u);
    std::uint64_t unknown = 0;
    for (std::size_t i = 0; i < size;) {
        const auto key = base + static_cast<std::uint32_t>(i);
        const auto count=std::min(size-i,std::size_t{4u-(key&3u)});
        const auto mask=(1ull<<(count*8u))-1u;
        const auto it = unknown_words_.find(key & ~3u);
        if (it != unknown_words_.end())
            unknown |= ((it->second >> ((key & 3u) * 8u)) & mask) << (i * 8u);
        i+=count;
    }
    return unknown;
}
void Memory::set_storage_unknown(const Target& target, std::size_t size, std::uint64_t unknown) const {
    invalidate_code(target,size);
    if (!unknown && unknown_words_.empty()) return;
    const auto base = target.offset | (target.region == BusRegion::Scratchpad ? 0x80000000u : 0u);
    for (std::size_t i = 0; i < size;) {
        const auto key = base + static_cast<std::uint32_t>(i);
        const auto count=std::min(size-i,std::size_t{4u-(key&3u)});
        const auto shift = (key & 3u) * 8u;
        const auto mask=static_cast<std::uint32_t>((1ull<<(count*8u))-1u)<<shift;
        const auto bits=static_cast<std::uint32_t>(unknown>>(i*8u))<<shift & mask;
        const auto it = unknown_words_.find(key & ~3u);
        const auto next = (it == unknown_words_.end() ? 0u : it->second & ~mask) | bits;
        if (next) unknown_words_[key & ~3u] = next;
        else if (it != unknown_words_.end()) unknown_words_.erase(it);
        i+=count;
    }
}
std::uint64_t Memory::read_le(std::uint32_t address, std::size_t size, const char* what, bool notify,
                              std::uint64_t* unknown) const {
    profiling::Scope profile(profiling::Phase::Memory);
    const auto target = resolve(address, size, false, what);
    if(!idle_dma_window() && registers_.dma_writes_ram())registers_.sync_iop();
    if (unknown) *unknown = 0;
    std::uint64_t value = 0;
    try {
    if (notify && data_cache_model_ && uses_data_cache(target)) {
        const auto& line = data_cache_line(address,target);
        const auto offset=address&63u;
        value=load_little(line.bytes.data()+offset,size);
        const auto symbolic=load_little(line.unknown.data()+offset,size);
        if(symbolic && !unknown) throw DeviceRefusal("read consumes unknown D-cache bits");
        if(unknown) *unknown=symbolic;
    } else if (target.region==BusRegion::VuMemory) {
        try {
            if(!notify) throw DeviceRefusal("EE instruction fetch from VU memory is not modeled");
            const auto observed=registers_.read_vu_memory(target.physical,size);
            if(observed.unknown && !unknown) throw DeviceRefusal("read consumes unknown VU memory bits");
            value=observed.value;
            if(unknown) *unknown=observed.unknown;
        } catch(const DeviceRefusal& refusal) {
            throw device_refusal(BusFault::Device,false,size,address,target.physical,target.region,what,refusal.what());
        }
    } else if (is_ee_register_region(target.region)) {
        // Only instruction fetches are not notified: never execute from registers.
        if (!notify)
            throw device_refusal(BusFault::Device, false, size, address, target.physical, target.region, what,
                                 "instruction fetch from device registers");
        try {
            if (unknown != nullptr && size == 4u && target.physical == kIntcStat) {
                const auto observed = registers_.observe_intc();
                value = observed.value;
                *unknown = observed.unknown;
            } else if (unknown != nullptr && size == 4u && Iop::ee_dma_address(target.physical)) {
                const auto observed = registers_.observe_dma_word(target.physical);
                value = observed.value;
                *unknown = observed.unknown;
            } else if(unknown!=nullptr && (size==4u || size==8u) && target.physical==kGsCsr) {
                const auto observed=registers_.observe_gs_csr();
                value=observed.value;*unknown=observed.unknown & (size==4u ? 0xFFFFFFFFull : ~0ull);
            } else if(unknown!=nullptr && size==4u && target.physical==IpuRegisters::kControl) {
                const auto observed=registers_.observe_ipu_control();
                value=observed.value;*unknown=observed.unknown;
            } else if(unknown!=nullptr && size==4u && (target.physical==0x10003800u || target.physical==0x10003C00u)) {
                const auto observed=registers_.observe_vif_status(target.physical==0x10003C00u);
                value=observed.value;*unknown=observed.unknown;
            } else value = registers_.read(target.physical, size);
        } catch (const DeviceRefusal& refusal) {
            throw device_refusal(BusFault::Device, false, size, address, target.physical, target.region, what,
                                 refusal.what());
        }
    } else {
        const auto symbolic = storage_unknown(target, size);
        if (symbolic && !unknown)
            throw device_refusal(BusFault::Device, false, size, address, target.physical, target.region, what,
                                 "read consumes unknown RAM bits");
        if (unknown) *unknown = symbolic;
        const auto& storage = target.region == BusRegion::BiosRom ? rom_
                            : target.region == BusRegion::Rom1 ? rom1_
                            : target.region == BusRegion::Scratchpad ? scratchpad_ : ram_;
        value=load_little(storage.data()+target.offset,size);
    }
    } catch(const DeviceRefusal& error) {
        throw device_refusal(BusFault::Device,false,size,address,target.physical,target.region,what,error.what());
    }
    if (notify && observer_ != nullptr)
        observer_->on_bus_access({false, static_cast<std::uint8_t>(size), address, target.physical,
                                  target.region, value, unknown ? *unknown : 0u});
    return value;
}
void Memory::write_le(std::uint32_t address, std::size_t size, std::uint64_t value, const char* what,
                       std::uint64_t unknown) {
    profiling::Scope profile(profiling::Phase::Memory);
    // The lazy IOP must consume old EE source bytes before this CPU store.
    if (!idle_dma_window() && (registers_.dma_reads_ram() || registers_.dma_writes_ram())) registers_.sync_iop();
    const auto target = resolve(address, size, true, what);
    if(!fpu_relations_.empty() && (target.region==BusRegion::MainRam || target.region==BusRegion::Scratchpad)) {
        const auto prefix=target.region==BusRegion::Scratchpad ? 0x80000000u : uses_data_cache(target) ? 0x40000000u : 0u;
        for(auto at=target.offset&~3u;at<=((target.offset+size-1u)&~3u);at+=4u)fpu_relations_.erase(prefix|at);
    }
    const bool gs_csr_rmw = unknown && (size == 4u || size == 8u) && target.physical == kGsCsr &&
        !(unknown & 0x31Full) && [&] {
            const auto width_mask = size == 4u ? 0xFFFFFFFFull : ~0ull;
            const auto observed = registers_.observe_gs_csr();
            return ((unknown ^ observed.unknown) & ~0x31Full & width_mask) == 0u &&
                   (((value & ~unknown) ^ (observed.value & ~observed.unknown)) & ~0x231Full & width_mask) == 0u;
        }();
    if (unknown && target.region != BusRegion::MainRam && target.region != BusRegion::Scratchpad && target.region!=BusRegion::VuMemory &&
        (size != 4u || target.physical != 0x1000E020u) && !gs_csr_rmw)
        throw device_refusal(BusFault::Device, true, size, address, target.physical, target.region, what,
                             "unknown MMIO bits cannot be stored in this device");
    value &= ~std::uint64_t{unknown};
    try {
    if (data_cache_model_ && uses_data_cache(target)) {
        auto& line = data_cache_line(address,target);
        const auto offset=address&63u;
        store_little(line.bytes.data()+offset,size,value);
        store_little(line.unknown.data()+offset,size,unknown);
        if(!(line.tag&8u)) line.tag|=64u;
    } else if (target.region==BusRegion::VuMemory) {
        try {registers_.write_vu_memory(target.physical,size,{value,unknown});}
        catch(const DeviceRefusal& refusal) {
            throw device_refusal(BusFault::Device,true,size,address,target.physical,target.region,what,refusal.what());
        }
    } else if (is_ee_register_region(target.region)) {
        try {
            if (gs_csr_rmw) registers_.write(target.physical, size, value & 0x31Full);
            else if (unknown) registers_.write_dma_word(target.physical,
                {static_cast<std::uint32_t>(value),static_cast<std::uint32_t>(unknown)});
            else registers_.write(target.physical, size, value);
        } catch (const DeviceRefusal& refusal) {
            throw device_refusal(BusFault::Device, true, size, address, target.physical, target.region, what,
                                 refusal.what());
        }
    } else {
        auto& storage = target.region == BusRegion::Scratchpad ? scratchpad_ : ram_;
        store_little(storage.data()+target.offset,size,value);
        set_storage_unknown(target, size, unknown);
    }
    } catch(const DeviceRefusal& error) {
        throw device_refusal(BusFault::Device,true,size,address,target.physical,target.region,what,error.what());
    }
    if (observer_ != nullptr)
        observer_->on_bus_access({true, static_cast<std::uint8_t>(size), address, target.physical,
                                  target.region, value, unknown});
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
ObservedWord Memory::load8_observed(std::uint32_t address) const {
    std::uint64_t unknown{};const auto value=read_le(address,1,"LB",true,&unknown);
    return {static_cast<std::uint32_t>(value),static_cast<std::uint32_t>(unknown)};
}
ObservedWord Memory::load16_observed(std::uint32_t address) const {
    std::uint64_t unknown{};const auto value=read_le(address,2,"LH",true,&unknown);
    return {static_cast<std::uint32_t>(value),static_cast<std::uint32_t>(unknown)};
}
ObservedWord Memory::load32_observed(std::uint32_t address) const {
    std::uint64_t unknown{};
    const auto value=read_le(address, 4, "LW", true, &unknown);
    return {static_cast<std::uint32_t>(value),static_cast<std::uint32_t>(unknown)};
}
ObservedDoubleword Memory::load64_observed(std::uint32_t address) const {
    ObservedDoubleword word;
    word.value=read_le(address,8,"LD",true,&word.unknown);
    return word;
}
void Memory::write32(std::uint32_t address, std::uint32_t value) {
    write_le(address, 4, value, "SW");
}
void Memory::store8_observed(std::uint32_t address, ObservedWord word) {
    write_le(address,1,word.value,"SB",word.unknown&0xFFu);
}
void Memory::store16_observed(std::uint32_t address, ObservedWord word) {
    write_le(address,2,word.value,"SH",word.unknown&0xFFFFu);
}
void Memory::store_bytes_observed(std::uint32_t address,std::size_t bytes,ObservedDoubleword word) {
    if(!bytes || bytes>8u)throw std::invalid_argument("partial store requires one to eight enabled bytes");
    if(bytes<8u)word.unknown&=(1ull<<(bytes*8u))-1u;
    // Validate all selected addresses before exposing any part of an opaque
    // store. Unknown MMIO payloads cannot create partial device side effects.
    for(std::size_t b=0;b<bytes;++b) {
        const auto target=resolve(address+static_cast<std::uint32_t>(b),1,true,"partial store");
        if(word.unknown && target.region!=BusRegion::MainRam && target.region!=BusRegion::Scratchpad && target.region!=BusRegion::VuMemory)
            throw device_refusal(BusFault::Device,true,1,address+static_cast<std::uint32_t>(b),target.physical,target.region,
                                 "partial store","unknown MMIO bits cannot be stored in this device");
    }
    for(std::size_t b=0;b<bytes;++b)
        write_le(address+static_cast<std::uint32_t>(b),1,word.value>>(b*8u),"partial store",(word.unknown>>(b*8u))&0xFFu);
}
FpuAffine Memory::fpu_relation(std::uint32_t address,ObservedWord word) const {
    if(!word.unknown)return {};
    const auto target=resolve(address,4,false,"FPU relation");
    if(target.region!=BusRegion::MainRam && target.region!=BusRegion::Scratchpad)return {};
    const bool cached=uses_data_cache(target);
    const auto key=static_cast<std::uint32_t>(target.offset)|(target.region==BusRegion::Scratchpad ? 0x80000000u : cached ? 0x40000000u : 0u);
    const auto found=fpu_relations_.find(key);
    if(found==fpu_relations_.end())return {};
    const auto& stored=found->second;
    if(stored.word.value!=word.value || stored.word.unknown!=word.unknown ||
       (!cached && target.region!=BusRegion::Scratchpad && stored.dma_generation!=kExactFpuDmaGeneration && stored.dma_generation!=registers_.iop().ee_ram_write_generation()))return {};
    return stored.relation;
}
void Memory::remember_fpu_relation(std::uint32_t address,ObservedWord word,FpuAffine relation) {
    if(!word.unknown || !relation.origin)return;
    const auto target=resolve(address,4,true,"FPU relation");
    if(target.region!=BusRegion::MainRam && target.region!=BusRegion::Scratchpad)return;
    const auto key=static_cast<std::uint32_t>(target.offset)|(target.region==BusRegion::Scratchpad ? 0x80000000u : uses_data_cache(target) ? 0x40000000u : 0u);
    fpu_relations_[key]={word,relation,kExactFpuDmaGeneration};
}
void Memory::store32_observed(std::uint32_t address, ObservedWord word) {
    write_le(address, 4, word.value, "SW", word.unknown);
}
std::uint64_t Memory::read64(std::uint32_t address) const {
    return read_le(address, 8, "LD", true);
}
void Memory::write64(std::uint32_t address, std::uint64_t value) {
    write_le(address, 8, value, "SD");
}
void Memory::store64_observed(std::uint32_t address, ObservedDoubleword word) {
    write_le(address,8,word.value,"SD",word.unknown);
}
void Memory::write128(std::uint32_t address, std::uint64_t low, std::uint64_t high) {
    // The low half resolves exactly as before; a 16-byte aligned quadword never
    // spans two regions.
    const auto target = resolve(address & ~15u, 8, true, "SD");
    if ((address & 15u) != 0u || !is_ee_register_region(target.region)) {
        write_le(address, 8, low, "SD");
        write_le(address + 8u, 8, high, "SD");
        return;
    }
    try {
        registers_.write128(target.physical, low, high);
    } catch (const DeviceRefusal& refusal) {
        throw device_refusal(BusFault::Device, true, 16, address, target.physical, target.region, "SQ",
                             refusal.what());
    }
    if (observer_ != nullptr) {
        observer_->on_bus_access({true, 8, address, target.physical, target.region, low});
        observer_->on_bus_access({true, 8, address + 8u, target.physical + 8u, target.region, high});
    }
}
void Memory::store128_observed(std::uint32_t address, ObservedQuadword word) {
    if (!(word.low.unknown|word.high.unknown)) {write128(address,word.low.value,word.high.value);return;}
    const auto target=resolve(address,16,true,"SQ");
    if(target.region!=BusRegion::MainRam && target.region!=BusRegion::Scratchpad && target.region!=BusRegion::VuMemory)
        throw device_refusal(BusFault::Device,true,16,address,target.physical,target.region,"SQ",
                             "unknown bits cannot be stored in this device");
    store64_observed(address,word.low);
    store64_observed(address+8u,word.high);
}
std::uint32_t Memory::fetch32(std::uint32_t address) const {
    profiling::Scope profile(profiling::Phase::Memory);
    // Fetch never observes the data cache or emits a data-bus callback. Reuse
    // only page resolution for the same address-space context; read current
    // bytes and knowledge after DMA so stores and writeback remain visible.
    const auto page_offset=address&0xFFFu;
    const auto key=std::uint64_t{address>>12u} | (std::uint64_t{asid_}<<20u) |
        (static_cast<std::uint64_t>(privilege_)<<28u) | (std::uint64_t{error_level_}<<30u) |
        (static_cast<std::uint64_t>(mapping_)<<31u);
    auto& cached=fetch_pages_[(address>>12u)&(fetch_pages_.size()-1u)];
    Target target;
    if (!(address&3u) && cached.key==key) {
        target=cached.base;
        target.offset+=page_offset;
        target.physical+=page_offset;
    } else {
        target=resolve(address,4,false,"instruction fetch");
        if(target.region==BusRegion::MainRam || target.region==BusRegion::Scratchpad ||
           target.region==BusRegion::BiosRom || target.region==BusRegion::Rom1) {
            const auto& storage=target.region==BusRegion::MainRam ? ram_
                : target.region==BusRegion::Scratchpad ? scratchpad_
                : target.region==BusRegion::BiosRom ? rom_ : rom1_;
            const auto base=target.offset&~0xFFFu;
            // Cache only a complete storage page, never bytes, pointers or device reads.
            if((target.offset&0xFFFu)==page_offset && base<=storage.size() && storage.size()-base>=4096u) {
                cached={key,target};
                cached.base.offset=base;
                cached.base.physical-=page_offset;
            }
        }
    }
    if(target.region!=BusRegion::MainRam && target.region!=BusRegion::Scratchpad &&
       target.region!=BusRegion::BiosRom && target.region!=BusRegion::Rom1)
        return static_cast<std::uint32_t>(read_le(address,4,"instruction fetch",false));
    if(!idle_dma_window() && registers_.dma_writes_ram())registers_.sync_iop();
    if(storage_unknown(target,4))
        throw device_refusal(BusFault::Device,false,4,address,target.physical,target.region,
                             "instruction fetch","read consumes unknown RAM bits");
    const auto& storage=target.region==BusRegion::MainRam ? ram_
                       : target.region==BusRegion::Scratchpad ? scratchpad_
                       : target.region==BusRegion::BiosRom ? rom_ : rom1_;
    return static_cast<std::uint32_t>(load_little(storage.data()+target.offset,4));
}
void Memory::invalidate_code(const Target& target,std::size_t size) const noexcept {
    if(target.region!=BusRegion::MainRam && target.region!=BusRegion::Scratchpad)return;
    const auto base=target.region==BusRegion::Scratchpad ? kMainRamBytes/4096u : 0u;
    for(auto page=base+target.offset/4096u;page<=base+(target.offset+size-1u)/4096u;++page) {
        auto& version=code_versions_[page];
        if(version && !++version) {
            for(auto& entry:code_checks_)entry.key=UINT64_MAX;
            version=1u;
        }
    }
}
bool Memory::code_matches(std::uint32_t address,std::uint32_t word) const {
    // DMA may change bytes and knowledge at this exact instruction boundary.
    if(!idle_dma_window() && registers_.dma_writes_ram())registers_.sync_iop();
    const auto key=std::uint64_t{address>>2u} | (std::uint64_t{asid_}<<30u) |
        (static_cast<std::uint64_t>(privilege_)<<38u) | (std::uint64_t{error_level_}<<40u) |
        (static_cast<std::uint64_t>(mapping_)<<41u);
    auto& checked=code_checks_[((address>>2u)^(address>>12u))&(code_checks_.size()-1u)];
    const auto dma=registers_.iop().ee_ram_write_generation();
    if(!(address&3u) && checked.key==key && checked.word==word &&
       checked.version==code_versions_[checked.page] && checked.dma_generation==dma)return true;
    if(fetch32(address)!=word)return false;
    const auto target=resolve(address,4,false,"instruction fetch");
    unsigned page;
    if(target.region==BusRegion::MainRam)page=target.offset/4096u;
    else if(target.region==BusRegion::Scratchpad)page=kMainRamBytes/4096u+target.offset/4096u;
    else if(target.region==BusRegion::BiosRom || target.region==BusRegion::Rom1)page=code_pages-1u;
    else return true;
    auto& version=code_versions_[page];if(!version)version=1u;
    checked={key,version,registers_.iop().ee_ram_write_generation(),word,page};
    return true;
}
}  // namespace ps2native
