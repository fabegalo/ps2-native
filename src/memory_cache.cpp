#include "ps2native/memory.hpp"
#include <algorithm>

namespace ps2native {
namespace {
constexpr std::uint32_t valid=0x20u, dirty=0x40u, locked=8u, lrf=0x10u;
constexpr std::uint32_t tag_fields=0xFFFFF078u;
void discard_cached_relation(auto& relations,const auto& line,unsigned index) {
    if(!(line.tag&valid) || !line.pfn_known)return;
    const auto physical=(line.tag&0xFFFFF000u)|(index<<6u);
    for(unsigned offset=0;offset<64;offset+=4)relations.erase(0x40000000u|(physical+offset));
}
ObservedWord cached_word(const auto& line,unsigned offset) {
    ObservedWord word{};
    for(unsigned i=0;i<4;++i) {
        word.value|=std::uint32_t{line.bytes[offset+i]}<<(8u*i);
        word.unknown|=std::uint32_t{line.unknown[offset+i]}<<(8u*i);
    }
    return word;
}
}
void Memory::reset_data_cache() {
    data_cache_model_=false;
    data_cache_uncached_hits_=false;
    cache_config_=0;
    cache_clock_=nullptr;
    data_cache_={};
    std::erase_if(fpu_relations_,[](const auto& entry){return (entry.first&0x40000000u)!=0;});
    for(auto& set:data_cache_) for(auto& line:set) line.unknown.fill(0xFFu);
}
void Memory::flush_hle_cache(unsigned operation) {
    // EE FlushCache: 0 writeback D-cache, 1 invalidate D-cache, 2 invalidate I-cache.
    if(operation>2)throw DeviceRefusal("unsupported HLE FlushCache operation");
    if(operation==0 && data_cache_model_) {
        for(unsigned index=0;index<data_cache_.size();++index)for(const auto& line:data_cache_[index])if(line.tag&dirty) {
            if(!(line.tag&valid) || !line.pfn_known)
                throw DeviceRefusal("HLE FlushCache dirty line has no known valid physical tag");
            if(((line.tag&0xFFFFF000u)|(index<<6u))>=kMainRamBytes)
                throw DeviceRefusal("HLE FlushCache writeback outside main RAM is not modeled");
        }
        for(unsigned index=0;index<data_cache_.size();++index)for(auto& line:data_cache_[index])writeback_data_cache(line,index);
    } else if(operation==1 && data_cache_model_) {
        for(unsigned index=0;index<data_cache_.size();++index)for(auto& line:data_cache_[index]) {
            discard_cached_relation(fpu_relations_,line,index);line.tag&=~(valid|dirty|locked);
        }
    } else if(operation==2) {
        for(auto& entry:fetch_pages_)entry.key=UINT64_MAX;
        for(auto& entry:code_checks_)entry.key=UINT64_MAX;
    }
}
void Memory::diagnostic_data_cache(bool translate_uncached_hits) {
    if(mapping_==Mapping::ElfProgram || data_cache_model_ ||
       (cache_clock_ && *cache_clock_) || (cache_config_&0x10000u))
        throw DeviceRefusal("D-cache experiment must be enabled before CPU execution in boot/Direct ELF mode");
    data_cache_model_=true;
    data_cache_uncached_hits_=translate_uncached_hits;
}
bool Memory::uses_data_cache(const Target& target) const {
    if(!data_cache_model_ || !(cache_config_&0x10000u) || target.region==BusRegion::Scratchpad) return false;
    if(target.cache_mode==2 || target.cache_mode==7) return false;
    if(target.cache_mode!=3) throw DeviceRefusal("D-cache: only writeback/write-allocate cache mode 3 is modeled");
    if(target.region!=BusRegion::MainRam && target.region!=BusRegion::BiosRom && target.region!=BusRegion::Rom1)
        throw DeviceRefusal("D-cache refill from a device aperture is not modeled");
    return true;
}
void Memory::writeback_data_cache(DataCacheLine& line,unsigned index) const {
    if(!(line.tag&dirty)) return;
    if(!(line.tag&valid) || !line.pfn_known) throw DeviceRefusal("D-cache dirty line has no known valid physical tag");
    const auto physical=(line.tag&0xFFFFF000u)|(index<<6u);
    if(physical>=kMainRamBytes) throw DeviceRefusal("D-cache writeback outside main RAM is not modeled");
    // Complete older DMA accesses before exposing this CPU writeback.
    if(registers_.dma_reads_ram() || registers_.dma_writes_ram()) registers_.sync_iop();
    // A writeback publishes new RAM provenance even if its bit hull matches
    // older RAM. Cache certificates remain attached to the resident line.
    for(unsigned offset=0;offset<64;offset+=4)fpu_relations_.erase(physical+offset);
    std::copy(line.bytes.begin(),line.bytes.end(),ram_.begin()+physical);
    for(unsigned i=0;i<64;i+=8) {
        std::uint64_t unknown=0;
        for(unsigned j=0;j<8;++j) unknown|=std::uint64_t{line.unknown[i+j]}<<(8u*j);
        set_storage_unknown({physical+i,BusRegion::MainRam,physical+i},8,unknown);
    }
    // Publish the certificate of the exact dirty word with its data. An
    // unrelated old RAM origin must never survive a matching-byte writeback.
    for(unsigned offset=0;offset<64;offset+=4) {
        const auto found=fpu_relations_.find(0x40000000u|(physical+offset));
        const auto word=cached_word(line,offset);
        if(found!=fpu_relations_.end() && found->second.word.value==word.value && found->second.word.unknown==word.unknown) {
            auto stored=found->second;stored.dma_generation=kExactFpuDmaGeneration;
            fpu_relations_[physical+offset]=std::move(stored);
        }
    }
    line.tag&=~dirty;
}
Memory::DataCacheLine& Memory::data_cache_line(std::uint32_t address,const Target& target) const {
    const auto index=(address>>6u)&63u;
    auto& set=data_cache_[index];
    DataCacheLine* hit=nullptr;
    for(auto& line:set) if(line.tag&valid) {
        if(!line.pfn_known) throw DeviceRefusal("D-cache hit test uses an unknown PFN");
        if((line.tag&0xFFFFF000u)==(target.physical&0xFFFFF000u)) {
            if(hit) throw DeviceRefusal("D-cache has duplicate valid tags in both ways");
            hit=&line;
        }
    }
    if(hit) return *hit;
    if((set[0].tag&locked) && (set[1].tag&locked)) throw DeviceRefusal("D-cache has both ways locked");
    // The data cache uses XOR of LRF even when a way is invalid. Invalid-way
    // priority is an instruction-cache rule, not the data-cache replacement rule.
    const auto way=(set[0].tag&locked) ? 1u : (set[1].tag&locked) ? 0u
                   : ((set[0].tag^set[1].tag)>>4u)&1u;
    auto& line=set[way];
    writeback_data_cache(line,index);
    // Certificates are host metadata, not cache payload. A refill must not
    // resurrect an older origin merely because bytes/unknown bits coincide.
    discard_cached_relation(fpu_relations_,line,index);
    const auto offset=target.offset&~63u;
    const auto& storage=target.region==BusRegion::BiosRom ? rom_
                        : target.region==BusRegion::Rom1 ? rom1_ : ram_;
    if(offset>storage.size() || 64u>storage.size()-offset) throw DeviceRefusal("D-cache refill exceeds storage");
    std::copy_n(storage.begin()+offset,64,line.bytes.begin());
    for(unsigned i=0;i<64;i+=8) {
        auto source=target;source.offset=offset+i;
        const auto unknown=storage_unknown(source,8);
        for(unsigned j=0;j<8;++j) line.unknown[i+j]=static_cast<std::uint8_t>(unknown>>(8u*j));
    }
    if(target.region==BusRegion::MainRam)for(unsigned i=0;i<64;i+=4) {
        const auto key=0x40000000u|static_cast<std::uint32_t>(offset+i);
        fpu_relations_.erase(key);
        const auto found=fpu_relations_.find(static_cast<std::uint32_t>(offset+i));
        const auto word=cached_word(line,i);
        if(found!=fpu_relations_.end() && (found->second.dma_generation==kExactFpuDmaGeneration || found->second.dma_generation==registers_.iop().ee_ram_write_generation()) &&
           found->second.word.value==word.value && found->second.word.unknown==word.unknown) {
            const auto stored=found->second;fpu_relations_[key]=stored;
        }
    }
    line.tag=(target.physical&0xFFFFF000u)|valid|((line.tag^lrf)&lrf);
    line.pfn_known=true;
    return line;
}
ObservedWord Memory::data_cache_operation(unsigned op,std::uint32_t address,ObservedWord tag) {
    if(!data_cache_model_) throw DeviceRefusal("D-cache contents require the explicit functional cache experiment");
    const auto index=(address>>6u)&63u;
    auto& set=data_cache_[index];
    auto* line=&set[address&1u];
    if(op==0x18 || op==0x1A || op==0x1C) {
        const auto target=resolve(address&~3u,4,false,"CACHE");
        const bool uncached_hypothesis=data_cache_uncached_hits_ &&
            (target.cache_mode==2 || target.cache_mode==7) && target.region==BusRegion::MainRam;
        if((target.cache_mode!=3 && !uncached_hypothesis) || target.region==BusRegion::Scratchpad)
            throw DeviceRefusal("D-cache hit operation requires a cached address");
        line=nullptr;
        for(auto& candidate:set) if(candidate.tag&valid) {
            if(!candidate.pfn_known) throw DeviceRefusal("D-cache hit operation uses an unknown PFN");
            if((candidate.tag&0xFFFFF000u)==(target.physical&0xFFFFF000u)) {
                if(line) throw DeviceRefusal("D-cache hit operation matches both ways");
                line=&candidate;
            }
        }
        if(!line) return tag;
    }
    switch(op) {
        case 0x10: return {line->tag,~tag_fields|(line->pfn_known ? 0u : 0xFFFFF000u)};
        case 0x11: {
            ObservedWord data{};
            const auto offset=address&60u;
            for(unsigned i=0;i<4;++i) {
                data.value|=std::uint32_t{line->bytes[offset+i]}<<(8u*i);
                data.unknown|=std::uint32_t{line->unknown[offset+i]}<<(8u*i);
            }
            return data;
        }
        case 0x12:
            if(tag.unknown&tag_fields) throw DeviceRefusal("D-cache tag store consumes unknown tag fields");
            if((tag.value&locked) && ((line->tag|set[(address&1u)^1u].tag)&locked))
                throw DeviceRefusal("D-cache relock or locking both ways is undefined");
            discard_cached_relation(fpu_relations_,*line,index);
            line->tag=tag.value&tag_fields;
            if(!(line->tag&valid)) line->tag&=~dirty;
            line->pfn_known=true;
            break;
        case 0x13: {
            const auto offset=address&60u;
            if((line->tag&valid) && line->pfn_known)
                fpu_relations_.erase(0x40000000u|(line->tag&0xFFFFF000u)|(index<<6u)|offset);
            for(unsigned i=0;i<4;++i) {
                line->bytes[offset+i]=static_cast<std::uint8_t>((tag.value&~tag.unknown)>>(8u*i));
                line->unknown[offset+i]=static_cast<std::uint8_t>(tag.unknown>>(8u*i));
            }
            break;
        }
        case 0x14: case 0x18:
            writeback_data_cache(*line,index);
            [[fallthrough]];
        case 0x16: case 0x1A:
            discard_cached_relation(fpu_relations_,*line,index);
            line->tag&=~(valid|dirty|locked);
            break;
        case 0x1C: writeback_data_cache(*line,index);break;
        default: throw DeviceRefusal("D-cache operation is not modeled");
    }
    return tag;
}
} // namespace ps2native
