// SPDX-License-Identifier: GPL-3.0-only
// Adapter for PS2Recomp's IopModuleLoader (revision recorded in NOTICE).
#pragma once
#include "ps2native/iop.hpp"
#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

namespace ps2native {
std::uint32_t sdk_iop_bus_read(Iop&,std::uint32_t address,unsigned bytes);
void sdk_iop_bus_write(Iop&,std::uint32_t address,unsigned bytes,std::uint32_t value);
}
namespace ps2x::iop::detail {
// The upstream loader's memory interface borrows the console's only IOP RAM.
class IopMemory {
public:
    static constexpr std::uint32_t RamSize=ps2native::kIopRamBytes,HeapBase=0x00010000u,HeapLimit=0x001F0000u;
    explicit IopMemory(ps2native::Iop& iop):iop_(iop) {}
    [[nodiscard]] static std::uint32_t physicalAddress(std::uint32_t address) noexcept {return address&0x1FFFFFFFu;}
    [[nodiscard]] std::uint32_t read32(std::uint32_t address) const {
        if(address&3u)throw ps2native::DeviceRefusal("PS2Recomp IOP loader: unaligned read");
        if(address>=0xC0000000u)throw ps2native::DeviceRefusal("PS2Recomp IOP KSEG2 read unsupported");
        const auto physical=physicalAddress(address);
        return physical<RamSize?static_cast<std::uint32_t>(iop_.ee_read(physical,4)):ps2native::sdk_iop_bus_read(iop_,physical,4);
    }
    [[nodiscard]] std::uint8_t read8(std::uint32_t address) const {
        if(address>=0xC0000000u)throw ps2native::DeviceRefusal("PS2Recomp IOP KSEG2 read unsupported");
        const auto physical=physicalAddress(address);
        return static_cast<std::uint8_t>(physical<RamSize?iop_.ee_read(physical,1):ps2native::sdk_iop_bus_read(iop_,physical,1));
    }
    [[nodiscard]] bool ownsRamRange(std::uint32_t address,std::size_t size) const noexcept {
        if(address>=0xC0000000u)return false;
        address=physicalAddress(address);return address<=RamSize && size<=RamSize-address;
    }
    [[nodiscard]] std::string readString(std::uint32_t address,std::size_t limit=1024u) const {
        if(!ownsRamRange(address,1u))throw ps2native::DeviceRefusal("PS2Recomp IOP string outside RAM: address="+
            std::to_string(address)+" limit="+std::to_string(limit));
        const auto available=std::min<std::size_t>(limit,RamSize-physicalAddress(address));
        std::string value;
        for(std::size_t i=0;i<available;++i) {const auto byte=read8(address+static_cast<std::uint32_t>(i));if(!byte)return value;value.push_back(static_cast<char>(byte));}
        if(available<limit)throw ps2native::DeviceRefusal("PS2Recomp IOP string reaches the end of RAM without a terminator");
        throw ps2native::DeviceRefusal("PS2Recomp IOP string exceeds its bound");
    }
    void write32(std::uint32_t address,std::uint32_t value) {
        if(address&3u)throw ps2native::DeviceRefusal("PS2Recomp IOP loader: unaligned write");
        if(address>=0xC0000000u)throw ps2native::DeviceRefusal("PS2Recomp IOP KSEG2 write unsupported");
        const auto physical=physicalAddress(address);
        if(physical<RamSize)iop_.ee_write(physical,4,value);else ps2native::sdk_iop_bus_write(iop_,physical,4,value);
    }
    [[nodiscard]] bool writeRam(std::uint32_t address,const void* source,std::size_t size) {
        if(!ownsRamRange(address,size) || (!source && size))return false;
        address=physicalAddress(address);
        const auto* bytes=static_cast<const std::uint8_t*>(source);
        for(std::size_t offset=0;offset<size;) {
            const auto count=std::min<std::size_t>(8,size-offset);
            const auto width=count>=8?8u:count>=4?4u:count>=2?2u:1u;
            std::uint64_t value=0;
            for(unsigned byte=0;byte<width;++byte)value|=std::uint64_t{bytes[offset+byte]}<<(8u*byte);
            iop_.ee_write(address+static_cast<std::uint32_t>(offset),width,value);offset+=width;
        }
        return true;
    }
    [[nodiscard]] bool zeroRam(std::uint32_t address,std::size_t size) {
        if(!ownsRamRange(address,size))return false;
        address=physicalAddress(address);
        for(std::size_t offset=0;offset<size;++offset)iop_.ee_write(address+static_cast<std::uint32_t>(offset),1,0);
        return true;
    }
    struct Allocation {std::uint32_t address,size;};
    [[nodiscard]] std::uint32_t allocate(std::uint32_t size,std::uint32_t alignment=16u,std::optional<std::uint32_t> fixed=std::nullopt);
    [[nodiscard]] bool freeAllocation(std::uint32_t address);
    [[nodiscard]] std::uint32_t maxFreeMemory(std::uint32_t alignment=16u) const;
    [[nodiscard]] std::uint32_t totalFreeMemory(std::uint32_t alignment=16u) const;
    [[nodiscard]] std::optional<Allocation> allocationContaining(std::uint32_t address) const;
    void resetAllocations() noexcept {m_allocations.clear();m_heapCursor=HeapBase;}
    [[nodiscard]] std::uint16_t read16(std::uint32_t address) const {
        if(address>=0xC0000000u)throw ps2native::DeviceRefusal("SDK IOP KSEG2 read unsupported");
        const auto physical=physicalAddress(address);
        return static_cast<std::uint16_t>(physical<RamSize?iop_.ee_read(physical,2):ps2native::sdk_iop_bus_read(iop_,physical,2));
    }
    void write8(std::uint32_t address,std::uint8_t value) {
        if(address>=0xC0000000u)throw ps2native::DeviceRefusal("SDK IOP KSEG2 write unsupported");
        const auto physical=physicalAddress(address);
        if(physical<RamSize)iop_.ee_write(physical,1,value);else ps2native::sdk_iop_bus_write(iop_,physical,1,value);
    }
    void write16(std::uint32_t address,std::uint16_t value) {
        if(address>=0xC0000000u)throw ps2native::DeviceRefusal("SDK IOP KSEG2 write unsupported");
        const auto physical=physicalAddress(address);
        if(physical<RamSize)iop_.ee_write(physical,2,value);else ps2native::sdk_iop_bus_write(iop_,physical,2,value);
    }
    [[nodiscard]] bool readRam(std::uint32_t address,void* destination,std::size_t size) const {
        if(!ownsRamRange(address,size) || (!destination && size))return false;
        auto* bytes=static_cast<std::uint8_t*>(destination);
        for(std::size_t n=0;n<size;++n)bytes[n]=read8(address+static_cast<std::uint32_t>(n));
        return true;
    }
    [[nodiscard]] bool isHardwareAddress(std::uint32_t address) const {const auto p=physicalAddress(address);return (p>=0x1F801000u && p<0x1FA00000u)||(p>=0x1D000000u && p<0x1D001000u);}
    [[nodiscard]] std::uint32_t interruptStatus() const {return read32(0x1F801070u);}
    [[nodiscard]] std::uint32_t interruptMask() const {return read32(0x1F801074u);}
    [[nodiscard]] std::uint32_t interruptControl() const {return read32(0x1F801078u);}
    void setInterruptMask(std::uint32_t value) {write32(0x1F801074u,value);}
    void setInterruptControl(std::uint32_t value) {write32(0x1F801078u,value);}

private:
    [[nodiscard]] std::pair<std::uint32_t,std::uint32_t> freeMemory(std::uint32_t alignment) const;
    ps2native::Iop& iop_;
    void markOwned(std::uint32_t address,std::size_t size) {if(!ownsRamRange(address,size))throw ps2native::DeviceRefusal("SDK allocation outside canonical IOP RAM");}
    std::vector<Allocation> m_allocations;
    std::uint32_t m_heapCursor{HeapBase};
};
}
