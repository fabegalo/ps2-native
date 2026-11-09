// SPDX-License-Identifier: GPL-3.0-only
// Device interface for upstream SIF services; real DMA owns transfer completion.
#include "sdk/kernel_interface.hpp"
namespace ps2native {
namespace {
constexpr std::uint32_t sifDmaSerial=0xA0010410u,sifDmaTags=0xA0010800u,sifSoftwareRegs=0x80010400u;
}
bool SdkMemory::validateGuestRange(std::uint32_t address,std::size_t size,bool output) const {
    if(!size || size>UINT32_MAX || size>std::uint64_t{UINT32_MAX}-address+1u)return false;
    try {
        for(std::size_t offset=0;offset<size;) {
            const auto target=memory_.resolve(address+static_cast<std::uint32_t>(offset),1,output,"PS2Recomp guest buffer");
            if(target.region!=BusRegion::MainRam && target.region!=BusRegion::Scratchpad)return false;
            offset+=std::min(size-offset,std::size_t{4096u-((address+offset)&4095u)});
        }
        (void)memory_.resolve(address+static_cast<std::uint32_t>(size-1u),1,output,"PS2Recomp guest buffer end");
        return true;
    }catch(const BusError&){return false;}
}
void SdkMemory::initializeKernelDevices() {
    // Upstream PS2Memory treats an absent D_CTRL in its HLE environment as DMAE=1.
    // Publish that kernel initialization through the real canonical DMAC register.
    memory_.write32(0xB000E000u,1u);
    // The upstream HLE register map initially reads these environment fields as zero.
    for(const auto address:{0xB000E030u,0xB000E040u,0xB000E050u})memory_.write32(address,0u);
    // Upstream HLE starts both VIF MODE fields at zero. Establish that state
    // through STMOD, leaving CYCLE to the guest's own STCYCL command.
    memory_.write128(0xB0004000u,0x05000000u,0u);
    memory_.write128(0xB0005000u,0x05000000u,0u);
    // The SDK kernel starts with a 60-Hz VBlank service. Use the existing GS
    // NTSC clock and BIOS NTSC SYNCV layout (525 half-lines, 480 displayed),
    // so EE and IOP see the same physical video edges instead of a host timer.
    // SDK GS CSR starts with FIELD=0; its scheduler flips FIELD at VBlank.
    // Keep that compatibility profile in the single GS, including soft reset.
    const_cast<GsRegisters&>(memory_.gs()).initialize_kernel_video(0x00C7800601A01801ull,false);
    for(unsigned i=0;i<3;++i)memory_.write32(sifSoftwareRegs+i*4u,0);
    memory_.write32(sifDmaSerial,0);
}
int SdkMemory::setSifDma(std::uint32_t address,std::uint32_t count) {
        if(!count || count>32 || address>UINT32_MAX-count*16u)return 0;
        // One bounded hardware submission; a busy channel never reports acceptance.
        if(memory_.read32(0xB000C400u)&0x100u)return 0;
        std::vector<std::array<std::uint32_t,4>> tags;
        bool interrupt=false;
        for(unsigned i=0;i<count;++i) {
            std::array<std::uint32_t,4> d;
            for(unsigned field=0;field<4;++field)d[field]=memory_.read32(address+i*16u+field*4u);
            const auto size=(std::uint64_t{d[2]}+15u)&~std::uint64_t{15};
            if(!size || size>0xFFFF0u || (d[0]&15u) || (d[1]&3u) ||
               d[1]>0x200000u || size>0x200000u-d[1] || (d[3]&~0x46u))return 0;
            if(((d[3]&2u) && i+1<count) || ((d[3]&4u) && !(d[3]&0x40u)))
                throw std::runtime_error("SIF DMA intermediate interrupt continuation is not implemented");
            const auto source=memory_.resolve(d[0],16,false,"SIF DMA source");
            if(source.region!=BusRegion::MainRam || size>Memory::kMainRamBytes-source.physical ||
               size>std::uint64_t{UINT32_MAX}-d[0]+1u)return 0;
            const auto pool=sifDmaTags&0x1FFFFFFFu;
            if(source.physical<pool+count*64u && source.physical+size>pool)return 0;
            for(std::uint64_t offset=0;offset<size;offset+=16u) {
                const auto part=memory_.resolve(d[0]+static_cast<std::uint32_t>(offset),16,false,"SIF DMA source");
                if(part.region!=BusRegion::MainRam || part.physical!=source.physical+offset)return 0;
            }
            // EE REF tag sends the IOP destination tag, then REF/REFE sends payload.
            const auto position=sifDmaTags+static_cast<std::uint32_t>(tags.size())*16u;
            tags.push_back({0x30000001u,(position+48u)&0x1FFFFFFFu,0,0});
            const auto qwc=static_cast<std::uint32_t>(size/16u);
            tags.push_back({(i+1==count?0u:0x30000000u)|qwc|((d[3]&2u)?0x80000000u:0u),source.physical,0,0});
            // NEXT skips the embedded IOP tag after the referenced payload.
            tags.push_back({0x20000000u,(position+64u)&0x1FFFFFFFu,0,0});
            tags.push_back({d[1]|((d[3]&0x40u)?0x80000000u:0u)|((d[3]&4u)?0x40000000u:0u),qwc*4u,0,0});
            interrupt|=(d[3]&2u)!=0;
        }
        auto serial=memory_.read32(sifDmaSerial);serial=serial>=0x7FFFFFFFu?1u:serial+1u;
        // Preserve unrelated arbitration knowledge while enabling SIF1 explicitly.
        auto priority=memory_.iop().ee_dma_observe(0x1000E020u);
        priority.value|=1u<<22u;priority.unknown&=~(1u<<22u);
        for(unsigned i=0;i<tags.size();++i)for(unsigned field=0;field<4;++field)memory_.write32(sifDmaTags+i*16u+field*4u,tags[i][field]);
        memory_.store32_observed(0xB000E020u,priority);
        memory_.write32(0xB000E000u,memory_.read32(0xB000E000u)|1u);
        memory_.write32(0xB000C420u,0);memory_.write32(0xB000C430u,sifDmaTags&0x1FFFFFFFu);
        memory_.write32(0xB000C400u,interrupt?0x185u:0x105u);
        memory_.write32(sifDmaSerial,serial);return static_cast<int>(serial);
    }

int SdkMemory::sifDmaStat(std::uint32_t id) {
    const auto latest=memory_.read32(sifDmaSerial);
    return id && id==latest && (memory_.read32(0xB000C400u)&0x100u)?0:-1;
}
void SdkMemory::setSifDChain() {
    const auto chcr=memory_.iop().ee_dma_observe(0x1000C000u);
    if(!chcr.unknown && (chcr.value&0x100u)) {
        if(chcr.value!=0x184u)throw std::runtime_error("SifSetDChain cannot replace an active SIF0 transfer");
        return;
    }
    memory_.write32(0xB000C020u,0);memory_.write32(0xB000C000u,0x184u);
}
std::uint32_t SdkMemory::sifReg(std::uint32_t reg,std::optional<std::uint32_t> value) {
    if(reg>=0x80000000u && reg<=0x80000002u) {
        const auto address=sifSoftwareRegs+(reg&3u)*4u,previous=memory_.read32(address);
        if(value)memory_.write32(address,*value);
        return previous;
    }
    if(reg>=1 && reg<=4) {
        const auto address=0xB000F200u+(reg-1u)*16u;
        if(value){memory_.write32(address,*value);return 0;}
        return memory_.read32(address);
    }
    throw std::runtime_error("unsupported SIF register "+std::to_string(reg));
}
int SdkMemory::setIrqCauseEnabled(bool dmac,std::uint32_t cause,bool enabled) {
    if(cause>=(dmac?10u:15u))return -1;
    const auto mask=1u<<(cause+(dmac?16u:0u));
    const auto address=dmac?0xB000E010u:0xB000F010u;
    const bool previous=(memory_.read32(address)&mask)!=0;
    if(previous!=enabled)memory_.write32(address,mask);
    return previous!=enabled?1:0;
}
}
