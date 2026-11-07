#include "ps2native/gs_vram.hpp"
#include "ps2native/device_timing.hpp"
#include <algorithm>
#include <string>

namespace ps2native {
GsVram::GsVram() : words_(kBytes / 4u), known_(kBytes / 4u) {}
GsVram::~GsVram(){detach_gpu();}
void GsVram::detach_gpu() noexcept {
    if(gpu_ && gpu_->vram_owner_==this)gpu_->vram_owner_=nullptr;
}
GsVram::GsVram(GsVram&& source) noexcept { *this=std::move(source); }
GsVram& GsVram::operator=(GsVram&& source) noexcept {
    if(this==&source)return *this;
    detach_gpu();
    words_=std::move(source.words_);known_=std::move(source.known_);
    revision_=source.revision_;pixel_writes_=source.pixel_writes_;
    gpu_=std::move(source.gpu_);pending_gpu_=std::move(source.pending_gpu_);
    locations_=source.locations_;pending_pages_=source.pending_pages_;pending_reads_=source.pending_reads_;
    knowledge_cached_=source.knowledge_cached_;page_knowledge_=source.page_knowledge_;
    gpu_primitives_=source.gpu_primitives_;cpu_fallbacks_=source.cpu_fallbacks_;
    fallback_reasons_=source.fallback_reasons_;
    if(gpu_ && gpu_->vram_owner_==&source)gpu_->vram_owner_=this;
    return *this;
}
GsVram::GsVram(const GsVram& source) {
    source.synchronize();
    words_=source.words_;known_=source.known_;
    revision_=source.revision_;pixel_writes_=source.pixel_writes_;
}
GsVram& GsVram::operator=(const GsVram& source) {
    if(this==&source)return *this;
    source.synchronize();synchronize();
    words_=source.words_;known_=source.known_;
    revision_=source.revision_;pixel_writes_=source.pixel_writes_;
    detach_gpu();gpu_.reset();pending_gpu_.clear();gpu_primitives_=cpu_fallbacks_=0;
    locations_={};pending_pages_={};pending_reads_={};knowledge_cached_={};fallback_reasons_={};
    return *this;
}
void GsVram::set_gpu_backend(std::shared_ptr<GsGpu> backend) {
    synchronize();detach_gpu();gpu_=std::move(backend);locations_={};knowledge_cached_={};
}
void GsVram::release_gpu() const {
    synchronize();locations_.fill(Location::Cpu);
    if(gpu_ && gpu_->vram_owner_==this)gpu_->vram_owner_=nullptr;
}
void GsVram::acquire_gpu() const {
    if(gpu_->vram_owner_==this)return;
    if(gpu_->vram_owner_)gpu_->vram_owner_->release_gpu();
    gpu_->vram_owner_=const_cast<GsVram*>(this);
    locations_.fill(Location::Cpu);
}
void GsVram::cache_page_knowledge(unsigned page) const {
    if(knowledge_cached_[page])return;
    std::uint32_t mask=0xFFFFFFFFu;
    for(unsigned word=page*2048u;word<(page+1u)*2048u;++word)mask&=known_[word];
    page_knowledge_[page]=mask;knowledge_cached_[page]=true;
}
bool GsVram::page_known(unsigned page,std::uint32_t mask) const {
    if(page>=kPages)throw DeviceRefusal("GS GPU page proof out of range");
    if(!gpu_)knowledge_cached_[page]=false;
    cache_page_knowledge(page);
    if((page_knowledge_[page]&mask)==mask)return true;
    if(!pending_pages_[page] && locations_[page]!=Location::Gpu)return false;
    synchronize_page(page);cache_page_knowledge(page);
    return (page_knowledge_[page]&mask)==mask;
}
bool GsVram::bits_known(unsigned byte_address,std::uint32_t mask) const {
    if(byte_address>=kBytes)throw DeviceRefusal("GS GPU bit proof out of range");
    const auto page=byte_address/kPageBytes;
    if(!gpu_)knowledge_cached_[page]=false;
    cache_page_knowledge(page);
    if((page_knowledge_[page]&mask)==mask)return true;
    if(pending_pages_[page] || locations_[page]==Location::Gpu)synchronize_page(page);
    return (known_[byte_address/4u]&mask)==mask;
}
void GsVram::enqueue_gpu(const GsGpuCommand& command,const std::array<bool,kPages>* texture_pages) {
    if(!gpu_)throw DeviceRefusal("GS GPU command without a backend");
    if(pending_gpu_.size()==256u)submit_gpu();
    // The common setup already proves injective CT32/24 page addressing.
    // Track every possible destination, including masked/skipped writes:
    // conservative ownership never claims that an unseen page is clean.
    const auto touch=[&](unsigned base,bool depth) {
        const auto page_height=depth && command.zpsm==10u?64u:32u;
        for(unsigned py=static_cast<unsigned>(command.top)/page_height;py<=static_cast<unsigned>(command.bottom-1)/page_height;++py)
            for(unsigned px=static_cast<unsigned>(command.left)/64u;px<=static_cast<unsigned>(command.right-1)/64u;++px) {
                const auto page=base/kPageBytes+py*(command.bw/64u)+px;
                if(page>=kPages)throw DeviceRefusal("GS GPU command page out of range");
                cache_page_knowledge(page);pending_pages_[page]=true;
                if(command.conditional)page_knowledge_[page]&=depth ?
                    (command.zpsm==1u ? 0xFF000000u : 0u) :
                    (command.frame_mask | (command.psm==1u ? 0xFF000000u : 0u));
                if(!depth && !command.fba_known && ((command.texture_flags&4u) || !(command.color&0x80000000u)) &&
                   command.psm==0u && !(command.frame_mask&0x80000000u))page_knowledge_[page]&=0x7FFFFFFFu;
                // An ordered, unconditional flat clear can establish a page
                // proof without downloading its newly produced pixels. Every
                // word must be covered and every test must pass independently
                // of destination contents; masked/unknown bits stay unknown.
                if(!command.triangle && !command.texture_flags && !command.blending &&
                   !command.conditional && !command.scanmask &&
                   (!(command.test&1u) || ((command.test>>1u)&7u)==1u) &&
                   (!(command.test&0x10000u) || ((command.test>>17u)&3u)==1u) &&
                   command.left<=static_cast<int>(px*64u) && command.right>=static_cast<int>((px+1u)*64u) &&
                   command.top<=static_cast<int>(py*page_height) && command.bottom>=static_cast<int>((py+1u)*page_height)) {
                    const auto written=depth ? (command.depth_write ?
                        (command.zpsm==1u?0xFFFFFFu:0xFFFFFFFFu):0u) :
                        ~command.frame_mask&(command.psm==0u?0xFFFFFFFFu:0xFFFFFFu);
                    const auto unknown=!depth && !command.fba_known && !(command.color&0x80000000u) ? 0x80000000u:0u;
                    page_knowledge_[page]|=written&~unknown;
                }
            }
    };
    touch(command.base,false);
    if(command.depth_write || ((command.test&0x10000u) && ((command.test>>17u)&3u)>=2u))touch(command.zbase,true);
    if(texture_pages)for(unsigned page=0;page<kPages;++page)
        pending_reads_[page]=pending_reads_[page] || (*texture_pages)[page];
    pending_gpu_.push_back(command);++gpu_primitives_;
}
void GsVram::submit_gpu() const {
    execute_pending();
}
void GsVram::execute_pending(const std::array<bool,kPages>* read_pages,unsigned read_page) const {
    if(pending_gpu_.empty())return;
    acquire_gpu();
    std::vector<GsGpuRange> uploads;
    for(unsigned page=0;page<kPages;++page)if((pending_pages_[page] || pending_reads_[page]) && locations_[page]==Location::Cpu) {
        if(!uploads.empty() && uploads.back().first_word+uploads.back().word_count==page*2048u)uploads.back().word_count+=2048u;
        else uploads.push_back({page*2048u,2048u});
    }
    std::vector<GsGpuRange> reads;
    const auto requested=[&](unsigned page){return page==read_page || (read_pages && (*read_pages)[page]);};
    if(read_pages || read_page<kPages)for(unsigned page=0;page<kPages;++page)
        if(requested(page) && (pending_pages_[page] || locations_[page]==Location::Gpu)) {
            if(!reads.empty() && reads.back().first_word+reads.back().word_count==page*2048u)reads.back().word_count+=2048u;
            else reads.push_back({page*2048u,2048u});
        }
    const auto result=gpu_->execute(words_,known_,pending_gpu_,uploads,reads);
    revision_+=result.revisions;pixel_writes_+=result.pixel_writes;
    for(const auto& range:uploads)
        for(unsigned page=range.first_word/2048u;page<(range.first_word+range.word_count)/2048u;++page)
            locations_[page]=Location::Equal;
    for(unsigned page=0;page<kPages;++page)if(pending_pages_[page])locations_[page]=Location::Gpu;
    if(read_pages || read_page<kPages)for(unsigned page=0;page<kPages;++page)
        if(requested(page) && locations_[page]==Location::Gpu) {
            locations_[page]=Location::Equal;knowledge_cached_[page]=false;
        }
    pending_gpu_.clear();pending_pages_={};pending_reads_={};
}
void GsVram::synchronize_page(unsigned page) const {
    if(!gpu_)return;
    // A CPU reader only depends on writers to its page. In particular, CLUT
    // loads from unchanged host data need not drain unrelated sprite draws.
    if(pending_pages_[page]){execute_pending(nullptr,page);return;}
    if(locations_[page]!=Location::Gpu)return;
    const GsGpuRange range{page*2048u,2048u};
    (void)gpu_->execute(words_,known_,{}, {},std::span{&range,1u});
    locations_[page]=Location::Equal;knowledge_cached_[page]=false;
}
void GsVram::synchronize() const {
    std::array<bool,kPages> all{};all.fill(true);synchronize_pages(all);
}
void GsVram::synchronize_pages(const std::array<bool,kPages>& pages) const {
    if(!pending_gpu_.empty()){execute_pending(&pages);return;}
    std::vector<GsGpuRange> reads;
    for(unsigned page=0;page<kPages;++page)if(pages[page] && locations_[page]==Location::Gpu) {
        if(!reads.empty() && reads.back().first_word+reads.back().word_count==page*2048u)reads.back().word_count+=2048u;
        else reads.push_back({page*2048u,2048u});
    }
    if(reads.empty())return;
    (void)gpu_->execute(words_,known_,{}, {},reads);
    for(unsigned page=0;page<kPages;++page)if(pages[page] && locations_[page]==Location::Gpu) {
        locations_[page]=Location::Equal;knowledge_cached_[page]=false;
    }
}
void GsVram::invalidate() {
    synchronize();
    std::fill(known_.begin(), known_.end(), 0u); ++revision_; pixel_writes_ = 0u;
    locations_.fill(Location::Cpu);knowledge_cached_={};
}
void GsVram::validate_format(unsigned psm) {
    if (psm > 1u && psm != 2u && psm != 10u && psm != 19u && psm != 20u) throw DeviceRefusal("GS pixel format " + std::to_string(psm) +
                                     " is not modeled (PSMCT32/PSMCT24/PSMCT16/PSMCT16S/PSMT8/PSMT4 only)");
}
std::uint32_t GsVram::address(std::uint32_t base, unsigned width, unsigned psm, unsigned x, unsigned y) {
    validate_format(psm);
    // BW is a page-row stride, including zero. BW=0 aliases subsequent
    // page rows; it does not mean 64 texels or permission to invent pixels.
    if (width > 4032u || width % 64u || base % 256u || x > 2047u || y > 2047u)
        throw DeviceRefusal("GS VRAM address: invalid base, buffer width or coordinate; base="+std::to_string(base)+
            " width="+std::to_string(width)+" psm="+std::to_string(psm)+" x="+std::to_string(x)+" y="+std::to_string(y));
    if (psm == 19u || psm == 20u) {
        // Indexed pages span two 64-texel BW units. The low BW bit does
        // not contribute to the page-row stride (also used by small mip levels).
        const auto page = (y / (psm == 19u ? 64u : 128u)) * (width / 128u) + x / 128u;
        const auto block = psm == 19u
            ? ((x >> 4) & 1u) | ((y >> 3) & 2u) | ((x >> 3) & 4u) |
              ((y >> 2) & 8u) | ((x >> 2) & 16u)
            : ((y >> 4) & 1u) | ((x >> 4) & 2u) | ((y >> 3) & 4u) |
              ((x >> 3) & 8u) | ((y >> 2) & 16u);
        // GS column diagrams: odd columns exchange the low/high word groups;
        // rows 2/3 select the other byte and exchange those groups again.
        const auto word = (x & 1u) | ((y & 1u) << 1) | ((x & 2u) << 1) |
                          (((x >> 2) ^ (y >> 1) ^ (y >> 2)) & 1u) << 3;
        const auto byte = psm == 19u ? ((x >> 2) & 2u) | ((y >> 1) & 1u) : (x >> 3) & 3u;
        return (base + page * 8192u + block * 256u + ((y >> 2) & 3u) * 64u + word * 4u + byte) & (kBytes - 1u);
    }
    if(psm==2u || psm==10u) {
        // GS manual 8.3.2: 64x64 pages, 16x8 blocks, 16x2 columns.
        // PSMCT16S changes only the block permutation. X bit 3 selects
        // the other halfword in a physical word, not the next word.
        const auto page=(y/64u)*(width/64u)+x/64u;
        const auto block=psm==10u
            ? ((y>>3)&1u)|((x>>3)&2u)|((y>>3)&4u)|((y>>1)&8u)|((x>>1)&16u)
            : ((y>>3)&1u)|((x>>3)&2u)|((y>>2)&4u)|((x>>2)&8u)|((y>>1)&16u);
        const auto word=(x&1u)|((y&1u)<<1)|((x&6u)<<1)|((y&6u)<<3);
        return (base+page*8192u+block*256u+word*4u+((x&8u)>>2))&(kBytes-1u);
    }
    const auto page = (y / 32u) * (width / 64u) + x / 64u;
    const auto block = ((x >> 3) & 1u) | ((y >> 2) & 2u) | ((x >> 2) & 4u) |
                       ((y >> 1) & 8u) | ((x >> 1) & 16u);
    const auto word = (x & 1u) | ((y & 1u) << 1) | ((x & 6u) << 1) | ((y & 6u) << 3);
    return (base + page * 8192u + block * 256u + word * 4u) & (kBytes - 1u);
}
std::uint32_t GsVram::read(std::uint32_t base, unsigned width, unsigned psm, unsigned x, unsigned y) const {
    return read_bits(base,width,psm,x,y,0xFFFFFFFFu);
}
std::uint32_t GsVram::read_bits(std::uint32_t base,unsigned width,unsigned psm,unsigned x,unsigned y,std::uint32_t mask) const {
    const auto addr = address(base, width, psm, x, y);
    if(gpu_)synchronize_page(addr/kPageBytes);
    const auto at = addr / 4u, shift = (addr & 3u) * 8u + (psm == 20u ? (y & 2u) * 2u : 0u);
    const auto bits = (psm == 20u ? 15u : psm == 19u ? 255u : psm==2u || psm==10u ? 0xFFFFu : psm == 1u ? 0xFFFFFFu : 0xFFFFFFFFu)&mask;
    if (((known_[at] >> shift) & bits) != bits) throw DeviceRefusal("GS VRAM read: pixel contains unwritten bits (PSM="+std::to_string(psm)+" XY="+std::to_string(x)+","+std::to_string(y)+" address="+std::to_string(addr)+" required="+std::to_string(bits)+" known="+std::to_string(known_[at]>>shift)+")");
    return (words_[at] >> shift) & bits;
}
void GsVram::write(std::uint32_t base, unsigned width, unsigned psm, unsigned x, unsigned y,
                   std::uint32_t value, std::uint32_t preserve, std::uint32_t unknown, bool conditional) {
    const auto addr = address(base, width, psm, x, y);
    write_addressed(addr,psm,y,value,preserve,unknown,conditional);
}
void GsVram::write_addressed(std::uint32_t addr, unsigned psm, unsigned y,
                            std::uint32_t value, std::uint32_t preserve,
                            std::uint32_t unknown, bool conditional) {
    const auto at = addr / 4u, shift = (addr & 3u) * 8u + (psm == 20u ? (y & 2u) * 2u : 0u);
    if (psm == 1u) preserve |= 0xFF000000u;
    if (psm == 19u) preserve |= 0xFFFFFF00u;
    if (psm == 20u) preserve |= 0xFFFFFFF0u;
    if (psm == 2u || psm == 10u) preserve |= 0xFFFF0000u;
    write_word(at, value << shift, ~preserve << shift, unknown << shift, conditional);
    if (!conditional) ++pixel_writes_; // possible writes are not claimed as proven pixels
}
void GsVram::write_word(unsigned at, std::uint32_t value, std::uint32_t mask,
                        std::uint32_t unknown, bool conditional) {
    if(gpu_){
        // A source upload may not replace bytes an earlier queued primitive
        // still needs. Disjoint CPU writes are independent of that queue.
        if(pending_reads_[at/2048u])submit_gpu();
        synchronize_page(at/2048u);
        locations_[at/2048u]=Location::Cpu;knowledge_cached_[at/2048u]=false;
    }
    const auto next = (words_[at] & ~mask) | (value & mask);
    auto knowledge = (known_[at] & ~mask) | (mask & ~unknown);
    // Join draw/skip branches. Only bits proved equal in both remain known.
    if (conditional) knowledge &= known_[at] & ~(words_[at] ^ next);
    words_[at] = next; known_[at] = knowledge; ++revision_;
}
std::uint32_t GsVram::depth_address(std::uint32_t base, unsigned width, unsigned psm,
                                  unsigned x, unsigned y) {
    if (psm == 0u || psm == 1u) {
        (void)address(base, width, 0, x, y);
        // XOR the block number, before adding the buffer base.
        return (base + (address(0, width, 0, x, y) ^ 6144u)) & (kBytes - 1u);
    }
    if (psm != 2u && psm != 10u) throw DeviceRefusal("GS depth format is not modeled");
    const auto color=address(base,width,psm,x,y);
    // Z16 uses the same page/column mapping with block bits 3/4 inverted.
    // Subtract the base before the permutation, including non-page bases.
    return (base+((color-base)^6144u))&(kBytes-1u);
}
std::uint32_t GsVram::read_depth(std::uint32_t base, unsigned width, unsigned psm,
                               unsigned x, unsigned y) const {
    const auto address = depth_address(base, width, psm, x, y);
    if(gpu_)synchronize_page(address/kPageBytes);
    const auto shift = (address & 2u) * 8u;
    const auto mask = psm == 0u ? 0xFFFFFFFFu : psm == 1u ? 0xFFFFFFu : 0xFFFFu;
    if (((known_[address / 4u] >> shift) & mask) != mask)
        throw DeviceRefusal("GS depth read: pixel contains unwritten bits");
    return (words_[address / 4u] >> shift) & mask;
}
void GsVram::write_depth(std::uint32_t base, unsigned width, unsigned psm,
                         unsigned x, unsigned y, std::uint32_t value, bool conditional) {
    const auto address = depth_address(base, width, psm, x, y);
    const auto shift = (address & 2u) * 8u;
    const auto mask = (psm == 0u ? 0xFFFFFFFFu : psm == 1u ? 0xFFFFFFu : 0xFFFFu) << shift;
    write_word(address / 4u, value << shift, mask, 0u, conditional);
}
ObservedWord GsVram::observe_word(std::uint32_t byte_address) const {
    if (byte_address >= kBytes || byte_address % 4u) throw DeviceRefusal("GS VRAM raw address out of range");
    if(gpu_)synchronize_page(byte_address/kPageBytes);
    return {words_[byte_address/4u],~known_[byte_address/4u]};
}
std::uint32_t GsVram::word_at(std::uint32_t byte_address) const {
    const auto word=observe_word(byte_address);
    if(word.unknown)throw DeviceRefusal("GS VRAM read: word contains unwritten bits");
    return word.value;
}
} // namespace ps2native
