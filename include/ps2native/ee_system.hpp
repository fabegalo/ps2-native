#pragma once
// Shared EE system semantics. AOT fixes opcode and word at compile time;
// only the separate validation interpreter decodes guest instructions.
#include "ps2native/ee_semantics.hpp"
#include "ps2native/interpreter.hpp"
#include "ps2native/ee_fpu.hpp"
#include "ps2native/hle.hpp"
#include "ps2native/vu_float.hpp"
#include <algorithm>
#include <bit>
#include <iomanip>
#include <sstream>
namespace ps2native {
namespace ee_system_detail {
inline std::string hex32(std::uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return stream.str();
}

inline const char* cop0_name(unsigned reg) {
    static constexpr const char* kNames[32] = {
        "Index", "Random", "EntryLo0", "EntryLo1", "Context", "PageMask", "Wired", "reserved7",
        "BadVAddr", "Count", "EntryHi", "Compare", "Status", "Cause", "EPC", "PRid",
        "Config", "reserved17", "reserved18", "reserved19", "reserved20", "reserved21", "reserved22", "BadPAddr",
        "Debug", "Perf", "reserved26", "reserved27", "TagLo", "TagHi", "ErrorEPC", "reserved31"};
    return kNames[reg & 31u];
}

class SystemOps {
public:
    SystemOps(ExecutionContext& ctx, SystemEventObserver* events)
        : s_(ctx.state), m_(ctx.memory), events_(events) {}

    [[nodiscard]] bool wants_events() const noexcept {return events_ && events_->wants_system_events();}
    void event(const std::string& text) {
        if (events_ != nullptr) events_->on_system_event(text);
    }
    [[noreturn]] void stop(StopKind kind, const std::string& what) {
        throw GuestStop(kind, what + " at " + hex32(s_.instruction_pc));
    }

    void apply_status() {
        const auto status = s_.cp0(Cop0::Status);
        const bool exception_level = (status & (kStatusEXL | kStatusERL)) != 0u;
        m_.set_cpu_context(status, s_.cp0(Cop0::EntryHi));
        if (!exception_level && (status & kStatusKSU) == kStatusKSU)
            stop(StopKind::Undefined, "Status.KSU selects reserved privilege mode");
        if (!exception_level && (status & kStatusIE) && (status & kStatusEIE) &&
            (status & kStatusIM7) && !s_.compare_written)
            stop(StopKind::NotModeled, "COP0 timer interrupt enabled with undefined Compare");
    }

    void mfc0(unsigned rt, unsigned rd) {
        switch (static_cast<Cop0>(rd)) {
            case Cop0::TagLo:
                s_.set_reg64_observed(rt,ee::sx32(s_.cop0[rd]),
                    (s_.taglo_unknown&0x80000000u) ? (0xFFFFFFFF00000000ull|s_.taglo_unknown) : s_.taglo_unknown);
                return;
            case Cop0::Random:
                stop(StopKind::NotModeled, "MFC0 Random: its per-cycle decrement is not modeled");
            case Cop0::Count:
                s_.set_reg32_sign_extended(rt, s_.count());
                return;
            case Cop0::Debug: case Cop0::Perf:
                s_.set_reg64_observed(rt,ee::sx32(s_.cop0[rd]),
                    rd==static_cast<unsigned>(Cop0::Debug) ? s_.bpc_unknown : s_.pccr_unknown);
                return;
            case Cop0::Index: case Cop0::EntryLo0: case Cop0::EntryLo1: case Cop0::Context:
            case Cop0::PageMask: case Cop0::Wired: case Cop0::BadVAddr: case Cop0::EntryHi:
            case Cop0::Compare: case Cop0::Status: case Cop0::Cause: case Cop0::Epc: case Cop0::PRid:
            case Cop0::Config: case Cop0::BadPAddr: case Cop0::TagHi: case Cop0::ErrorEpc:
                s_.set_reg32_sign_extended(rt, s_.cop0[rd]);
                return;
        }
        stop(StopKind::Undefined, std::string("MFC0 from reserved COP0 register ") + cop0_name(rd));
    }

    void mtc0(unsigned rt, unsigned rd) {
        if(rd==static_cast<unsigned>(Cop0::TagLo)) {
            s_.taglo_unknown=static_cast<std::uint32_t>(s_.unknown_gpr_bits[rt]);
            s_.cop0[rd]=static_cast<std::uint32_t>(ee::raw_gpr64(s_,rt))&~s_.taglo_unknown;
            return;
        }
        if(rd==static_cast<unsigned>(Cop0::Debug) || rd==static_cast<unsigned>(Cop0::Perf)) {
            const bool debug=rd==static_cast<unsigned>(Cop0::Debug);
            const auto value=static_cast<std::uint32_t>(ee::raw_gpr64(s_,rt));
            const auto unknown=static_cast<std::uint32_t>(s_.unknown_gpr_bits[rt]);
            const auto enables=debug ? 0xE0000000u : 0x80000000u;
            if((value|unknown)&enables)
                stop(StopKind::NotModeled,debug ? "BPC breakpoint enable is active or unknown" : "PCCR counter enable is active or unknown");
            const auto mask=debug ? 0xF7BF8007u : 0x800FFBFEu;
            s_.cop0[rd]=value&mask&~unknown;
            (debug ? s_.bpc_unknown : s_.pccr_unknown)=unknown&mask;
            if(wants_events())event(std::string("MTC0 ")+cop0_name(rd)+" disabled control saved (undefined fields preserved)");
            return;
        }
        const auto value = s_.reg32(rt);
        const auto old = s_.cop0[rd];
        std::uint32_t stored = value;
        switch (static_cast<Cop0>(rd)) {
            case Cop0::Index: stored = (old & 0x80000000u) | (value & 0x3Fu); break;
            case Cop0::EntryLo0: stored = value & 0x83FFFFFFu; break;
            case Cop0::EntryLo1: stored = value & 0x03FFFFFFu; break;
            case Cop0::Context: stored = (value & 0xFF800000u) | (old & 0x007FFFFFu); break;
            case Cop0::PageMask: stored = value & 0x01FFE000u; break;
            case Cop0::Wired:
                stored = value & 0x3Fu;
                s_.set_cp0(Cop0::Random, 47u); // Writing Wired resets Random to its upper bound.
                break;
            case Cop0::EntryHi: stored = value & 0xFFFFE0FFu; break;
            case Cop0::Count:
                s_.count_anchor = s_.cycles();
                break;
            case Cop0::Compare:
                s_.compare_written = true;
                s_.set_cp0(Cop0::Cause, s_.cp0(Cop0::Cause) & ~kStatusIM7);
                break;
            case Cop0::Epc: case Cop0::ErrorEpc:
            case Cop0::BadPAddr: case Cop0::TagLo: case Cop0::TagHi:
                break;
            case Cop0::Status: stored = value & kStatusWritable; break;
            case Cop0::Config: stored = (value & ~0x00000FC0u) | (old & 0x00000FC0u); break; // IC/DC sizes are fixed.
            case Cop0::Cause:
                if (value != 0u)
                    stop(StopKind::NotModeled, "MTC0 Cause " + hex32(value) + " (software-set cause bits)");
                stored = old;
                break;
            case Cop0::Random: case Cop0::BadVAddr: case Cop0::PRid:
                stored = old; // Read-only: the write has no effect.
                break;
            case Cop0::Debug: case Cop0::Perf:
                stop(StopKind::NotModeled, std::string("MTC0 ") + cop0_name(rd) + " (breakpoint/performance counters)");
            default:
                stop(StopKind::Undefined, std::string("MTC0 to reserved COP0 register ") + cop0_name(rd));
        }
        s_.cop0[rd] = stored;
        if(rd==static_cast<unsigned>(Cop0::Config)) m_.set_cache_config(stored);
        if(wants_events())event(std::string("MTC0 ") + cop0_name(rd) + " <- " + hex32(value) +
              (stored != value ? " (stored " + hex32(stored) + ")" : std::string()));
        if (rd == static_cast<unsigned>(Cop0::Status) || rd == static_cast<unsigned>(Cop0::EntryHi)) apply_status();
    }

    void tlb_write_indexed() {
        const auto index = s_.cp0(Cop0::Index) & 0x3Fu;
        if (index >= Memory::kTlbEntries)
            stop(StopKind::Undefined, "TLBWI with Index " + std::to_string(index) + " (only 48 entries)");
        const TlbEntry entry{s_.cp0(Cop0::PageMask), s_.cp0(Cop0::EntryHi), s_.cp0(Cop0::EntryLo0),
                             s_.cp0(Cop0::EntryLo1)};
        m_.tlb_write(index, entry);
        if(wants_events())event("TLBWI " + std::to_string(index) + ": PageMask " + hex32(entry.page_mask) + " EntryHi " +
              hex32(entry.entry_hi) + " EntryLo0 " + hex32(entry.entry_lo0) + " EntryLo1 " + hex32(entry.entry_lo1));
    }

    void tlb_read() {
        const auto index = s_.cp0(Cop0::Index) & 0x3Fu;
        if (index >= Memory::kTlbEntries)
            stop(StopKind::Undefined, "TLBR with Index " + std::to_string(index) + " (only 48 entries)");
        const auto& entry = m_.tlb_entry(index);
        // G reads back as set only when both halves are global.
        const auto global = entry.entry_lo0 & entry.entry_lo1 & 1u;
        s_.set_cp0(Cop0::PageMask, entry.page_mask);
        s_.set_cp0(Cop0::EntryHi, entry.entry_hi & ~entry.page_mask);
        s_.set_cp0(Cop0::EntryLo0, (entry.entry_lo0 & ~1u) | global);
        s_.set_cp0(Cop0::EntryLo1, (entry.entry_lo1 & ~1u) | global);
        if(wants_events())event("TLBR " + std::to_string(index));
        apply_status();
    }

    void tlb_probe() {
        const auto match = m_.tlb_probe(s_.cp0(Cop0::EntryHi));
        s_.set_cp0(Cop0::Index, match ? *match : 0x80000000u);
        if(wants_events())event("TLBP EntryHi " + hex32(s_.cp0(Cop0::EntryHi)) + " -> " +
              (match ? "entry " + std::to_string(*match) : std::string("no match")));
    }

    void eret() {
        auto status = s_.cp0(Cop0::Status);
        if (status & kStatusERL) {
            s_.pc = s_.cp0(Cop0::ErrorEpc);
            status &= ~kStatusERL;
        } else {
            s_.pc = s_.cp0(Cop0::Epc);
            status &= ~kStatusEXL;
        }
        s_.set_cp0(Cop0::Status, status);
        if(wants_events())event("ERET -> " + hex32(s_.pc) + ", Status " + hex32(status));
        apply_status();
    }

    void interrupt_enable(bool enable) {
        auto status = s_.cp0(Cop0::Status);
        // Effective in kernel mode, at exception/error level, or with Status.EDI.
        if ((status & (kStatusEDI | kStatusEXL | kStatusERL)) || (status & kStatusKSU) == 0u)
            status = enable ? status | kStatusEIE : status & ~kStatusEIE;
        s_.set_cp0(Cop0::Status, status);
        if(wants_events())event(std::string(enable ? "EI" : "DI") + ", Status " + hex32(status));
        apply_status();
    }

    // No cache is modeled: guest memory is always coherent with what the cache
    // would hold, so invalidation and write-back have no observable effect.
    // Operations that read or write cache contents/tags are refused.
    void cache(std::uint32_t word) {
        const auto op = ee::rt(word);
        const auto address = ee::effective_address(s_, word);
        if(m_.data_cache_enabled() && op>=0x10u) {
            ObservedWord result;
            try {result=m_.data_cache_operation(op,address,{s_.cp0(Cop0::TagLo),s_.taglo_unknown});}
            catch(const DeviceRefusal& error) {stop(StopKind::NotModeled,error.what());}
            if(op==0x10 || op==0x11) {
                s_.set_cp0(Cop0::TagLo,result.value);
                s_.taglo_unknown=result.unknown;
            }
            if (events_ && events_->wants_system_events())
                event("CACHE data operation "+hex32(op)+" "+hex32(address));
            return;
        }
        const char* name = nullptr;
        switch (op) {
            case 0x07: name = "IXIN"; break;
            case 0x0C: name = "BFH"; break;
            case 0x14: name = "DXWBIN"; break;
            case 0x16: name = "DXIN"; break;
            case 0x18: name = "DHWBIN"; break;
            case 0x1A: name = "DHIN"; break;
            case 0x1C: name = "DHWOIN"; break;
            case 0x04: case 0x12:
                // Index Store Tag with a zero TagLo writes an invalid line: an invalidation.
                if (s_.cp0(Cop0::TagLo) != 0u || s_.taglo_unknown)
                    stop(StopKind::NotModeled, "CACHE Index Store Tag with TagLo " + hex32(s_.cp0(Cop0::TagLo)) +
                         " (cache tags are not modeled)");
                name = op == 0x04 ? "IXSTG (TagLo 0)" : "DXSTG (TagLo 0)";
                break;
            default:
                stop(StopKind::NotModeled, "CACHE operation " + hex32(op) + " (cache contents are not modeled)");
        }
        // Only hit operations use the address's translation/tag. Index
        // operations select a cache set/way directly, including unmapped VA 0.
        if (op == 0x18 || op == 0x1A || op == 0x1C) (void)m_.translate(address, false);
        if (events_ && events_->wants_system_events())
            event(std::string("CACHE ") + name + " " + hex32(address) + " (no cache model: memory coherent)");
    }

    // LWC1/SWC1 move a raw 32-bit word between memory and an FPU register.
    void require_fpr_known(unsigned index,const char* operation) {
        if(s_.unknown_fpr_bits[index])stop(StopKind::NotModeled,std::string(operation)+" consumes unknown FPR bits");
    }
    FpuAffine fpu_relation(unsigned index) {
        const auto value=s_.fpr[index]&~s_.unknown_fpr_bits[index],unknown=s_.unknown_fpr_bits[index];
        auto& relation=s_.fpr_affine[index];
        if(unknown && (relation.candidates || relation.ranges) && relation.origin && relation.value==value && relation.unknown==unknown)return relation;
        if(!unknown || (unknown&0xFF800000u) || !(value&0x7F800000u)){relation={};return {};}
        if(!relation.origin || relation.value!=value || relation.unknown!=unknown)
            relation={m_.allocate_fpu_origin(),0,(value>>23u)&255u,false,value,unknown,value,value|unknown,{}};
        return relation;
    }
    static void transform_candidates(FpuAffine& relation,bool absolute=false) {
        if(relation.ranges) {
            auto ranges=std::make_shared<std::vector<FloatBounds>>(*relation.ranges);
            for(auto& range:*ranges)for(auto& value:range)value=absolute ? value&0x7FFFFFFFu : value^0x80000000u;
            std::sort(ranges->begin(),ranges->end());
            relation.minimum=ranges->front()[0];relation.maximum=0;
            for(const auto& range:*ranges)relation.maximum=std::max(relation.maximum,range[1]);
            relation.ranges=std::move(ranges);
        }
        if(!relation.candidates)return;
        auto values=std::make_shared<std::vector<std::uint32_t>>(*relation.candidates);
        for(auto& value:*values)value=absolute ? value&0x7FFFFFFFu : value^0x80000000u;
        std::sort(values->begin(),values->end());values->erase(std::unique(values->begin(),values->end()),values->end());
        relation.minimum=values->front();relation.maximum=values->back();relation.candidates=std::move(values);
    }
    static std::vector<std::uint32_t> fpu_candidates(ObservedWord word,const FpuAffine& relation,unsigned limit=64) {
        if(!word.unknown)return {word.value};
        if(relation.candidates)return *relation.candidates;
        if(relation.ranges) {
            std::vector<std::uint32_t> values;
            for(const auto& range:*relation.ranges) {
                if(std::uint64_t{range[1]}-range[0]+1u>limit-values.size())return {};
                for(std::uint64_t value=range[0];value<=range[1];++value)
                    if(!((value^word.value)&~word.unknown))values.push_back(static_cast<std::uint32_t>(value));
            }
            return values;
        }
        const auto lo=relation.origin ? relation.minimum : word.value&~word.unknown;
        const auto hi=relation.origin ? relation.maximum : word.value|word.unknown;
        std::vector<std::uint32_t> values;
        if(hi>=lo && hi-lo<limit) {
            for(std::uint64_t value=lo;value<=hi;++value)if(!((value^word.value)&~word.unknown))values.push_back(static_cast<std::uint32_t>(value));
        } else if(std::popcount(word.unknown)<=std::countr_zero(limit)) {
            auto subset=word.unknown;
            for(;;) {
                const auto value=(word.value&~word.unknown)|subset;
                if(value>=lo && value<=hi)values.push_back(value);
                if(!subset)break;
                subset=(subset-1u)&word.unknown;
            }
        }
        return values;
    }
    static std::optional<VuFloatResult> finite_fpu(MipsOpcode op,ObservedWord a,ObservedWord b,
        const FpuAffine& ar,const FpuAffine& br,bool identical,std::shared_ptr<const std::vector<std::uint32_t>>& candidates,bool diagnostic) {
        profiling::Scope profile(profiling::Phase::Bounds);
        const auto av=fpu_candidates(a,ar,op==MipsOpcode::DivS ? 512u : 64u),bv=fpu_candidates(b,br,op==MipsOpcode::DivS ? 512u : 64u);
        if(av.empty() || bv.empty() || av.size()*(identical ? 1u : bv.size())>4096u)return {};
        std::vector<std::uint32_t> values;std::optional<std::uint32_t> common_flags;
        const auto append=[&](std::uint32_t value,std::uint32_t flags) {
            if(common_flags && *common_flags!=flags)return false;
            common_flags=flags;
            // Many input pairs collapse to the same value after alignment.
            // Bound live storage, not the number of duplicate observations.
            if(values.size()==512u) {
                std::sort(values.begin(),values.end());values.erase(std::unique(values.begin(),values.end()),values.end());
                if(values.size()==512u)return std::binary_search(values.begin(),values.end(),value);
            }
            values.push_back(value);return true;
        };
        for(const auto x:av)for(unsigned i=0;i<(identical ? 1u : bv.size());++i) {
            const auto y=identical ? (op==MipsOpcode::SubS ? x^0x80000000u : x) : bv[i];
            if(op==MipsOpcode::DivS) {
                std::uint32_t flags=0;const auto value=ee_fpu_divide(x,y,flags);
                if(!append(value,flags))return {};
            } else if(op!=MipsOpcode::MulS) {
                std::uint32_t flags=0;const auto value=ee_fpu_add(x,y,flags);
                if(!append(value,flags))return {};
            } else {
                // Known input pairs may still have uncertified Booth low bits.
                // Enumerate every value in their proven result interval; never
                // replace it with one guessed product or a host float.
                const auto product=vu_float_product({x,0},{y,0},diagnostic ? CoarseEarlyDigits : NoCarryExperiment,{},{},true);
                if(!product)return {};
                const auto low=product->bounded ? product->bounds[0] : product->word.value;
                const auto high=product->bounded ? product->bounds[1] : product->word.value;
                if(high<low || high-low>16u)return {};
                for(std::uint64_t value=low;value<=high;++value)if(!append(static_cast<std::uint32_t>(value),product->flags))return {};
            }
        }
        if(values.empty())return {};
        std::sort(values.begin(),values.end());values.erase(std::unique(values.begin(),values.end()),values.end());
        std::uint32_t all=~0u,any=0;for(const auto value:values){all&=value;any|=value;}
        const VuFloatResult result{{all,all^any},*common_flags,{values.front(),values.back()},true};
        if(result.word.unknown)candidates=std::make_shared<const std::vector<std::uint32_t>>(std::move(values));
        return result;
    }
    static ObservedWord interval_word(FloatBounds range) {
        const auto different=range[0]^range[1];
        const auto mask=different ? (1u<<std::bit_width(different))-1u : 0u;
        return {range[0]&~mask,mask};
    }
    static std::vector<FloatBounds> fpu_ranges(ObservedWord word,const FpuAffine& relation) {
        if(!word.unknown)return {{word.value,word.value}};
        if(relation.ranges)return *relation.ranges;
        if(relation.candidates) {
            std::array<std::optional<FloatBounds>,512> classes{};
            for(const auto value:*relation.candidates) {
                auto& range=classes[value>>23u];
                if(!range)range=FloatBounds{value,value};
                else {(*range)[0]=std::min((*range)[0],value);(*range)[1]=std::max((*range)[1],value);}
            }
            std::vector<FloatBounds> ranges;
            for(const auto& range:classes)if(range)ranges.push_back(*range);
            return ranges;
        }
        if(word.unknown&0xFF800000u) {
            // Opaque raw transfers can lose affine provenance while retaining
            // useful known bits. Partition the at most 512 sign/exponent
            // classes; each fraction interval conservatively includes every
            // encoding admitted by that class. Downstream arithmetic must
            // still prove a common exception class across all intervals.
            std::vector<FloatBounds> ranges;
            const auto high_unknown=word.unknown&0xFF800000u;
            auto subset=high_unknown;
            for(;;) {
                const auto low=(word.value&~word.unknown)|subset;
                ranges.push_back({low,low|(word.unknown&0x7FFFFFu)});
                if(!subset)break;
                subset=(subset-1u)&high_unknown;
            }
            return ranges;
        }
        return {{relation.origin ? relation.minimum : word.value,relation.origin ? relation.maximum : word.value|word.unknown}};
    }
    static std::optional<VuFloatResult> range_fpu(MipsOpcode op,ObservedWord a,ObservedWord b,
        const FpuAffine& ar,const FpuAffine& br,std::shared_ptr<const std::vector<FloatBounds>>& ranges,bool diagnostic) {
        profiling::Scope profile(profiling::Phase::Bounds);
        std::array<std::optional<FloatBounds>,512> classes{};std::optional<std::uint32_t> flags;
        const auto append=[&](std::uint32_t lo,std::uint32_t hi,std::uint32_t next_flags) {
            if((flags && *flags!=next_flags) || lo>hi || ((lo^hi)&0x80000000u))return false;
            flags=next_flags;
            for(unsigned index=lo>>23u;index<=(hi>>23u);++index) {
                const auto begin=std::max(lo,index<<23u),end=std::min(hi,(index<<23u)|0x7FFFFFu);
                auto& range=classes[index];
                if(!range)range=FloatBounds{begin,end};
                else {(*range)[0]=std::min((*range)[0],begin);(*range)[1]=std::max((*range)[1],end);}
            }
            return true;
        };
        const auto ac=fpu_candidates(a,ar),bc=fpu_candidates(b,br);
        if(op==MipsOpcode::DivS) {
            if(b.unknown || (b.value&0x7FFFFFu) || !(b.value&0x7F800000u)) {
                const auto av=fpu_candidates(a,ar,65536u),bv=fpu_candidates(b,br,65536u);
                if(av.empty() || bv.empty() || std::uint64_t{av.size()}*bv.size()>67108864u)return {};
                // SRT endpoint monotonicity is not established; fold every
                // concrete quotient into ranges instead of storing it.
                for(const auto x:av)for(const auto y:bv) {
                    std::uint32_t next_flags=0;const auto value=ee_fpu_divide(x,y,next_flags);
                    if(!append(value,value,next_flags))return {};
                }
            } else {
                // A normalized power-of-two divisor leaves every significand bit
                // unchanged in the 25-digit SRT circuit. All 2^23 significands
                // were checked against that circuit; exponent adjustment, signed
                // zero and clamps remain in the same scalar implementation.
                const auto av=fpu_ranges(a,ar);if(av.empty())return {};
                for(const auto& x:av) {
                    if(x[0]>x[1] || ((x[0]^x[1])&0xFF800000u))return {};
                    std::uint32_t first_flags=0,last_flags=0;
                    const auto first=ee_fpu_divide(x[0],b.value,first_flags),last=ee_fpu_divide(x[1],b.value,last_flags);
                    if(first_flags!=last_flags || !append(std::min(first,last),std::max(first,last),first_flags))return {};
                }
            }
        } else if(op!=MipsOpcode::MulS && !ac.empty() && !bc.empty() && ac.size()*bc.size()<=16384u) {
            // Folding exact scalar sums directly into intervals bounds live
            // storage even when thousands of distinct outputs are possible.
            for(const auto x:ac)for(const auto y:bc) {
                std::uint32_t next_flags=0;const auto value=ee_fpu_add(x,y,next_flags);
                if(!append(value,value,next_flags))return {};
            }
        } else {
            const auto av=fpu_ranges(a,ar),bv=fpu_ranges(b,br);
            if(av.empty() || bv.empty() || std::uint64_t{av.size()}*bv.size()>262144u)return {};
            for(const auto& x:av)for(const auto& y:bv) {
                if(x[0]>x[1] || y[0]>y[1] || ((x[0]^x[1])&0xFF800000u) || ((y[0]^y[1])&0xFF800000u))return {};
                // Each input pair has fixed signs/exponents. Existing integer
                // bounds prove every result and flag. Overlapping cancellation
                // needs the separate guard-column proof below.
                const auto result=op==MipsOpcode::MulS ? vu_float_product(interval_word(x),interval_word(y),diagnostic ? CoarseEarlyDigits : NoCarryExperiment,x,y,true) :
                    vu_float_add(interval_word(x),interval_word(y),x,y,true);
                const auto common=std::max((x[0]>>23u)&255u,(y[0]>>23u)&255u);
                if(!result && op!=MipsOpcode::MulS && ((x[0]^y[0])&0x80000000u) && common>=25u) {
                    // The integer adder retains one guard column: every
                    // nonzero sum is at least 2^(common-151). At common>=25
                    // that is normal, while opposite signs cannot overflow.
                    // Thus an interior zero cannot hide an underflow flag.
                    std::uint32_t positive=0,negative=0;
                    for(const auto a:x)for(const auto b:y) {
                        std::uint32_t next_flags=0;const auto value=ee_fpu_add(a,b,next_flags);
                        if(next_flags)return {};
                        auto& bound=(value&0x80000000u) ? negative : positive;
                        bound=std::max(bound,value&0x7FFFFFFFu);
                    }
                    const auto minimum=(common-24u)<<23u;
                    if(!append(0,0,0) || (positive && !append(minimum,positive,0)) ||
                       (negative && !append(minimum|0x80000000u,negative|0x80000000u,0)))return {};
                    continue;
                }
                if(!result)return {};
                const auto lo=result->bounded ? result->bounds[0] : result->word.value;
                const auto hi=result->bounded ? result->bounds[1] : result->word.value;
                if(!append(lo,hi,result->flags))return {};
            }
        }
        std::vector<FloatBounds> output;std::uint32_t all=~0u,any=0;
        for(const auto& range:classes)if(range) {
            output.push_back(*range);const auto word=interval_word(*range);
            all&=word.value;any|=word.value|word.unknown;
        }
        if(output.empty())return {};
        const VuFloatResult result{{all,all^any},*flags,{output.front()[0],output.back()[1]},true};
        if(result.word.unknown)ranges=std::make_shared<const std::vector<FloatBounds>>(std::move(output));
        return result;
    }
    void fpu_binary(MipsOpcode op,std::uint32_t word) {
        if(!(s_.cp0(Cop0::Status)&kStatusCU1))
            stop(StopKind::GuestException,"FPU arithmetic with Status.CU1 = 0; coprocessor unusable vectoring is not modeled");
        const auto diagnostic=m_.begin_fpu_arithmetic();
        const auto fs=ee::rd(word),ft=ee::rt(word),fd=ee::sa(word);
        const ObservedWord a{s_.fpr[fs]&~s_.unknown_fpr_bits[fs],s_.unknown_fpr_bits[fs]};
        ObservedWord b{s_.fpr[ft]&~s_.unknown_fpr_bits[ft],s_.unknown_fpr_bits[ft]};
        auto ar=fpu_relation(fs),br=fpu_relation(ft);
        // MOV.S and certified affine copies preserve the same concrete input,
        // even when it lives in two registers. Equal hulls alone prove nothing.
        const bool identical_inputs=fs==ft || (ar.origin && ar.origin==br.origin && ar.exponent==br.exponent &&
            ar.offset==br.offset && ar.negated==br.negated && a.value==b.value && a.unknown==b.unknown);
        if(op==MipsOpcode::SubS) {
            b.value=(b.value^0x80000000u)&~b.unknown;
            br.negated=!br.negated;br.offset=-br.offset;
            br.minimum^=0x80000000u;br.maximum^=0x80000000u;
            transform_candidates(br);
        }
        // Shared EE/VU normal-value bounds. The low bits remain unknown;
        // neither a host product nor one endpoint substitutes for hardware.
        std::optional<VuFloatResult> result;
        std::shared_ptr<const std::vector<std::uint32_t>> candidates;
        std::shared_ptr<const std::vector<FloatBounds>> ranges;
        FpuAffine relation;
        if(op==MipsOpcode::DivS) {
            result=finite_fpu(op,a,b,ar,br,identical_inputs,candidates,diagnostic);
        } else if(op!=MipsOpcode::MulS && ar.origin && ar.origin==br.origin && ar.exponent==br.exponent && ar.negated!=br.negated) {
            std::uint32_t flags=0;
            result=VuFloatResult{{ee_fpu_scaled_integer(ar.offset+br.offset,ar.exponent,flags),0},flags};
        } else {
            const auto ab=ar.origin ? std::optional<FloatBounds>{{ar.minimum,ar.maximum}} : std::nullopt;
            const auto bb=br.origin ? std::optional<FloatBounds>{{br.minimum,br.maximum}} : std::nullopt;
            result=op==MipsOpcode::MulS ? vu_float_product(a,b,diagnostic ? CoarseEarlyDigits : NoCarryExperiment,ab,bb) : vu_float_add(a,b,ab,bb);
        }
        if(!result || (result->word.unknown && (ar.candidates || br.candidates ||
           (result->bounded && result->bounds[1]-result->bounds[0]>64u)))) {
            const auto finite=finite_fpu(op,a,b,ar,br,identical_inputs,candidates,diagnostic);
            if(finite)result=finite;
        }
        if(!result)result=range_fpu(op,a,b,ar,br,ranges,diagnostic);
        if(!result)stop(StopKind::NotModeled,"FPU arithmetic cannot certify value/exception bounds; FS="+hex32(a.value)+", FT="+hex32(b.value));
        if(op!=MipsOpcode::MulS && op!=MipsOpcode::DivS && result->word.unknown && (a.unknown!=0u)!=(b.unknown!=0u)) {
            const auto source=a.unknown ? a : b,constant=a.unknown ? b : a;
            const auto inherited=a.unknown ? ar : br;
            const auto exponent=(source.value>>23u)&255u;
            const auto signed_mantissa=[](std::uint32_t bits) {
                const auto m=static_cast<std::int64_t>((bits&0x7FFFFFu)|0x800000u);
                return bits&0x80000000u ? -m : m;
            };
            if(inherited.origin && inherited.exponent && !(source.unknown&0xFF800000u) && !(result->word.unknown&0xFF800000u) &&
               ((constant.value>>23u)&255u)<=exponent &&
               ((result->word.value>>23u)&255u)==exponent && !((source.value^result->word.value)&0x80000000u)) {
                std::uint32_t flags=0;const auto anchor=ee_fpu_add(inherited.minimum,constant.value,flags);
                relation=inherited;relation.offset+=signed_mantissa(anchor)-signed_mantissa(inherited.minimum);
            }
        }
        if(result->word.unknown && !relation.origin)
            relation={m_.allocate_fpu_origin(),0,(result->word.unknown&0xFF800000u) ? 0u : (result->word.value>>23u)&255u,false,0,0,0,0,{}};
        relation.value=result->word.value;relation.unknown=result->word.unknown;
        relation.minimum=result->bounded ? result->bounds[0] : result->word.value;
        relation.maximum=result->bounded ? result->bounds[1] : result->word.value|result->word.unknown;
        relation.candidates=std::move(candidates);
        relation.ranges=std::move(ranges);
        s_.fpr[fd]=result->word.value;s_.unknown_fpr_bits[fd]=result->word.unknown;
        s_.fpr_affine[fd]=relation;
        s_.fcr31=(s_.fcr31&~(op==MipsOpcode::DivS ? 0x30000u : 0xC000u))|result->flags;
    }
    void cop1_transfer(bool store, std::uint32_t word) {
        if ((s_.cp0(Cop0::Status) & kStatusCU1) == 0u)
            stop(StopKind::GuestException, std::string(store ? "SWC1" : "LWC1") +
                 " with Status.CU1 = 0: coprocessor unusable exception; vectoring is not modeled");
        const auto address = ee::effective_address(s_, word);
        const auto index=ee::rt(word);auto& fpr = s_.fpr[index];
        if (store) {
            const auto relation=fpu_relation(index);const ObservedWord value{fpr,s_.unknown_fpr_bits[index]};
            m_.store32_observed(address,value);m_.remember_fpu_relation(address,value,relation);
        } else {const auto value=m_.load32_observed(address);fpr=value.value;s_.unknown_fpr_bits[index]=value.unknown;s_.fpr_affine[index]=m_.fpu_relation(address,value);}
    }
    void cop1_register(MipsOpcode op,std::uint32_t word) {
        if(!(s_.cp0(Cop0::Status)&kStatusCU1))
            stop(StopKind::GuestException,"COP1 register transfer with Status.CU1 = 0; coprocessor unusable vectoring is not modeled");
        const auto rt=ee::rt(word),fs=ee::rd(word);
        if(op==MipsOpcode::Mfc1) {
            const auto mask=s_.unknown_fpr_bits[fs];
            if(mask) {
                const auto values=fpu_candidates({s_.fpr[fs],mask},fpu_relation(fs));
                if(!values.empty()) {
                    std::vector<std::uint64_t> words;words.reserve(values.size());
                    for(const auto value:values)words.push_back(ee::sx32(value));
                    s_.set_reg64_candidates(rt,std::move(words));return;
                }
            }
            s_.set_reg64_observed(rt,ee::sx32(s_.fpr[fs]),std::uint64_t{mask}|((mask&0x80000000u) ? 0xFFFFFFFF00000000ull : 0ull));
        } else if(op==MipsOpcode::Mtc1) {
            s_.fpr_affine[fs]={};
            s_.unknown_fpr_bits[fs]=static_cast<std::uint32_t>(s_.unknown_gpr_bits[rt]);
            s_.fpr[fs]=static_cast<std::uint32_t>(s_.gpr[rt].low)&~s_.unknown_fpr_bits[fs];
            if(s_.unknown_fpr_bits[fs])if(const auto values=s_.reg64_candidates(rt))
                s_.fpr_affine[fs]=ee::finite_word_relation(m_,{s_.fpr[fs],s_.unknown_fpr_bits[fs]},*values);
        }
        else {
            if(fs!=31)stop(StopKind::NotModeled,"FPU control register "+std::to_string(fs)+" not modeled");
            if(op==MipsOpcode::Cfc1)s_.set_reg32_sign_extended(rt,s_.fcr31);
            else s_.fcr31=(s_.reg32(rt)&0x0083C078u)|0x01000001u;
        }
    }
    void mula(std::uint32_t word) {
        if(!(s_.cp0(Cop0::Status)&kStatusCU1))
            stop(StopKind::GuestException,"MULA.S with Status.CU1 = 0; coprocessor unusable vectoring is not modeled");
        require_fpr_known(ee::rd(word),"MULA.S");require_fpr_known(ee::rt(word),"MULA.S");
        auto flags=s_.fcr31;
        const auto product=ee_fpu_multiply(s_.fpr[ee::rd(word)],s_.fpr[ee::rt(word)],flags,
            m_.begin_fpu_arithmetic() ? CoarseEarlyDigits : NoCarryExperiment);
        if(!product)stop(StopKind::NotModeled,"MULA.S product is not certified; uncertain accumulator state is not modeled");
        s_.fpu_acc=*product;s_.fcr31=flags;
    }
    void multiply_accumulate(MipsOpcode op,std::uint32_t word) {
        const bool subtract=op==MipsOpcode::MsubaS || op==MipsOpcode::MsubS;
        const char* operation=op==MipsOpcode::MsubaS ? "MSUBA.S" : op==MipsOpcode::MsubS ? "MSUB.S" :
            op==MipsOpcode::MaddaS ? "MADDA.S" : "MADD.S";
        if(!(s_.cp0(Cop0::Status)&kStatusCU1))
            stop(StopKind::GuestException,std::string(operation)+" with Status.CU1 = 0; coprocessor unusable vectoring is not modeled");
        const auto fs=ee::rd(word),ft=ee::rt(word);
        if((s_.fpu_acc&0x7F800000u)==0x7F800000u)
            stop(StopKind::NotModeled,std::string(operation)+" accumulator overflow state not modeled");
        const auto diagnostic=m_.begin_fpu_arithmetic();
        std::shared_ptr<const std::vector<std::uint32_t>> candidates;
        std::optional<VuFloatResult> product;
        if(!s_.unknown_fpr_bits[fs] && !s_.unknown_fpr_bits[ft]) {
            std::uint32_t flags=0;
            if(const auto value=ee_fpu_multiply(s_.fpr[fs],s_.fpr[ft],flags,diagnostic ? CoarseEarlyDigits : NoCarryExperiment))
                product=VuFloatResult{{*value,0},flags};
        }
        if(!product)product=finite_fpu(MipsOpcode::MulS,{s_.fpr[fs],s_.unknown_fpr_bits[fs]},
            {s_.fpr[ft],s_.unknown_fpr_bits[ft]},fpu_relation(fs),fpu_relation(ft),fs==ft,candidates,diagnostic);
        if(!product)stop(StopKind::NotModeled,std::string(operation)+" product is not certified");
        if(product->flags&0xC000u)
            stop(StopKind::NotModeled,std::string(operation)+" nonnormal intermediate state is not modeled");
        std::optional<std::uint32_t> result,result_flags;
        const auto consume=[&](std::uint32_t value) {
            if((value&0x7F800000u)==0x7F800000u)
                stop(StopKind::NotModeled,std::string(operation)+" nonnormal intermediate state is not modeled");
            auto flags=(s_.fcr31&~0xC000u)|product->flags;
            // Retain ADD.S's guard column and signed-zero rules, including
            // a zero product, rather than host FMA or fused rounding.
            const auto sum=ee_fpu_add(s_.fpu_acc,value^(subtract ? 0x80000000u : 0u),flags);
            if((flags&0xC000u) || (sum&0x7F800000u)==0x7F800000u)
                stop(StopKind::NotModeled,std::string(operation)+" nonnormal accumulation state is not modeled");
            if((result && *result!=sum) || (result_flags && *result_flags!=flags))
                stop(StopKind::NotModeled,std::string(operation)+" result is not certified across all product candidates");
            result=sum;result_flags=flags;
        };
        if(candidates)for(const auto value:*candidates)consume(value);
        else consume(product->word.value);
        if(op==MipsOpcode::MsubaS || op==MipsOpcode::MaddaS)s_.fpu_acc=*result;
        else {s_.fpr[ee::sa(word)]=*result;s_.unknown_fpr_bits[ee::sa(word)]=0;s_.fpr_affine[ee::sa(word)]={};}
        s_.fcr31=*result_flags;
    }
    void adda(std::uint32_t word) {
        if(!(s_.cp0(Cop0::Status)&kStatusCU1))
            stop(StopKind::GuestException,"ADDA.S with Status.CU1 = 0; coprocessor unusable vectoring is not modeled");
        require_fpr_known(ee::rd(word),"ADDA.S");require_fpr_known(ee::rt(word),"ADDA.S");
        const auto fs=s_.fpr[ee::rd(word)],ft=s_.fpr[ee::rt(word)];
        const auto a=fs&0x7F800000u,b=ft&0x7F800000u;
        if(a && b)stop(StopKind::NotModeled,"ADDA.S nonzero addends: R5900 adder rounding not modeled");
        if(a==0x7F800000u || b==0x7F800000u)
            stop(StopKind::NotModeled,"ADDA.S exponent-255 arithmetic not modeled");
        s_.fpu_acc=a?fs:b?ft:(fs&ft&0x80000000u);
        s_.fcr31&=~0x0000C000u;
    }

    EEState& s_;
    Memory& m_;
    SystemEventObserver* events_;
};
} // namespace ee_system_detail

template<MipsOpcode Op,std::int64_t Word=-1,bool Observe=false>
void execute_ee_system(ExecutionContext& ctx,std::uint32_t dynamic_word=0,SystemEventObserver* events=nullptr) {
    static_assert(Op>=MipsOpcode::Mfc0,"integer operations use ee::exec directly");
    using ee_system_detail::hex32;
    auto& state=ctx.state;auto& memory=ctx.memory;
    const auto pc=state.pc;
    const std::uint32_t word=Word<0 ? dynamic_word : static_cast<std::uint32_t>(Word);
    const MipsInstruction instruction{pc,word,Op,static_cast<std::uint8_t>((word>>21u)&31u),
        static_cast<std::uint8_t>((word>>16u)&31u),static_cast<std::uint8_t>((word>>11u)&31u),static_cast<std::uint8_t>((word>>6u)&31u)};
    ee_system_detail::SystemOps cpu(ctx,events ? events : dynamic_cast<SystemEventObserver*>(ctx.observer));
    const auto status = state.cp0(Cop0::Status);
    if (((word >> 26u) == 0x10u || instruction.opcode == MipsOpcode::Cache) &&
        instruction.opcode != MipsOpcode::Ei && instruction.opcode != MipsOpcode::Di &&
        !(status & (kStatusEXL | kStatusERL | (1u << 28u))) && (status & kStatusKSU))
        cpu.stop(StopKind::GuestException, "COP0 unusable in supervisor/user mode with Status.CU0 = 0; vectoring is not modeled");
    switch (Op) {
        case MipsOpcode::Addi: case MipsOpcode::Add: case MipsOpcode::Sub:
        case MipsOpcode::Daddi: case MipsOpcode::Dadd: case MipsOpcode::Dsub: {
            const auto op=instruction.opcode;
            const bool wide=op==MipsOpcode::Daddi || op==MipsOpcode::Dadd || op==MipsOpcode::Dsub;
            const bool immediate=op==MipsOpcode::Addi || op==MipsOpcode::Daddi;
            const bool subtract=op==MipsOpcode::Sub || op==MipsOpcode::Dsub;
            const std::uint64_t a=wide ? state.reg64(instruction.rs) : state.reg32(instruction.rs);
            const std::uint64_t b=immediate ? ee::simm64(word) :
                (wide ? state.reg64(instruction.rt) : state.reg32(instruction.rt));
            // Unsigned host arithmetic avoids UB even for the 64-bit limits.
            const auto result=subtract ? a-b : a+b;
            const auto sign=std::uint64_t{1}<<(wide ? 63u : 31u);
            const bool overflow=((a^result)&(subtract ? (a^b) : ~(a^b))&sign)!=0;
            if constexpr(Observe)if (ctx.observer != nullptr) ctx.observer->on_instruction(pc, word, ExecutionEngine::Interpreter);
            if (overflow) {
                enter_ee_exception(state, 12u, pc, state.delay_pending);
                memory.set_cpu_context(state.cp0(Cop0::Status), state.cp0(Cop0::EntryHi)); memory.set_cache_config(state.cp0(Cop0::Config));
                if(cpu.wants_events())cpu.event(std::string(mips_opcode_name(op))+" overflow exception -> " + hex32(state.pc));
                ++ctx.executed;
                ++state.retired_instructions;
            } else {
                const auto frame = ee::begin(state, pc);
                const auto target=immediate ? instruction.rt : instruction.rd;
                if (wide) state.set_reg64(target,result);
                else state.set_reg32_sign_extended(target,static_cast<std::uint32_t>(result));
                (void)ee::retire(ctx, frame);
            }
            return;
        }
        case MipsOpcode::Syscall: case MipsOpcode::Break: {
            if constexpr(Op==MipsOpcode::Syscall)if(auto* hle=memory.hle_kernel()) {
                if(state.delay_pending)cpu.stop(StopKind::NotModeled,"HLE syscall in a branch delay slot");
                flush_interrupt_cache(ctx);
                // Validation precedes retirement and any thread context switch.
                if constexpr(Observe)if(ctx.observer)ctx.observer->on_instruction(pc,word,ExecutionEngine::Interpreter);
                hle->dispatch_syscall(ctx);
                const auto frame=ee::begin(state,pc);
                (void)ee::retire(ctx,frame);
                hle->finish_syscall();
                return;
            }
            if constexpr(Observe)if (ctx.observer != nullptr) ctx.observer->on_instruction(pc, word, ExecutionEngine::Interpreter);
            enter_ee_exception(state, instruction.opcode == MipsOpcode::Syscall ? 8u : 9u, pc, state.delay_pending);
            memory.set_cpu_context(state.cp0(Cop0::Status), state.cp0(Cop0::EntryHi)); memory.set_cache_config(state.cp0(Cop0::Config));
            if(cpu.wants_events())cpu.event(std::string(instruction.opcode == MipsOpcode::Syscall ? "SYSCALL" : "BREAK") +
                      " exception -> " + hex32(state.pc) + " EPC=" + hex32(state.cp0(Cop0::Epc)) +
                      " Cause=" + hex32(state.cp0(Cop0::Cause)));
            // Exception entry replaces normal retirement: retire() would apply
            // the cancelled branch target when the trap was in its delay slot.
            ++ctx.executed;
            ++state.retired_instructions;
            return;
        }
        case MipsOpcode::Eret:
            if (state.delay_pending) cpu.stop(StopKind::Undefined, "ERET in a branch delay slot");
            break;
        default:
            break;
    }
    if constexpr(Observe)if (ctx.observer != nullptr) ctx.observer->on_instruction(pc, word, ExecutionEngine::Interpreter);
    const auto frame = ee::begin(state, pc);
    switch (Op) {
        case MipsOpcode::Mfc0: cpu.mfc0(instruction.rt, instruction.rd); break;
        case MipsOpcode::Mtc0: cpu.mtc0(instruction.rt, instruction.rd); break;
        case MipsOpcode::Tlbwi: cpu.tlb_write_indexed(); break;
        case MipsOpcode::Tlbwr: cpu.stop(StopKind::NotModeled, "TLBWR (Random register timing is not modeled)");
        case MipsOpcode::Tlbr: cpu.tlb_read(); break;
        case MipsOpcode::Tlbp: cpu.tlb_probe(); break;
        case MipsOpcode::Eret: cpu.eret(); break;
        case MipsOpcode::Ei: cpu.interrupt_enable(true); break;
        case MipsOpcode::Di: cpu.interrupt_enable(false); break;
        case MipsOpcode::Cache: cpu.cache(word); break;
        case MipsOpcode::Pref: break;
        case MipsOpcode::Lwc1: cpu.cop1_transfer(false, word); break;
        case MipsOpcode::Swc1: cpu.cop1_transfer(true, word); break;
        case MipsOpcode::Mfc1: case MipsOpcode::Mtc1: case MipsOpcode::Cfc1: case MipsOpcode::Ctc1:
            cpu.cop1_register(instruction.opcode,word);break;
        case MipsOpcode::CvtWS: case MipsOpcode::CvtSW: {
            if(!(state.cp0(Cop0::Status)&kStatusCU1))
                cpu.stop(StopKind::GuestException,"FPU conversion with Status.CU1 = 0; coprocessor unusable vectoring is not modeled");
            const auto input=state.fpr[ee::rd(word)];
            std::uint32_t result=0,result_unknown=0;FpuAffine result_relation;
            if(instruction.opcode==MipsOpcode::CvtWS) {
                const auto convert=[](std::uint32_t bits) {
                    const int exponent=static_cast<int>((bits>>23u)&255u)-127;
                    if(exponent>=31)return (bits&0x80000000u) ? 0x80000000u : 0x7FFFFFFFu;
                    if(exponent<0)return 0u;
                    const auto mantissa=(bits&0x7FFFFFu)|0x800000u;
                    const auto magnitude=exponent>=23 ? mantissa<<(exponent-23) : mantissa>>(23-exponent);
                    return (bits&0x80000000u) ? 0u-magnitude : magnitude;
                };
                const auto unknown=state.unknown_fpr_bits[ee::rd(word)];
                result=convert(input&~unknown);
                if(unknown) {
                    const auto relation=cpu.fpu_relation(ee::rd(word));
                    std::vector<std::uint32_t> values;
                    const auto append=[&](std::uint32_t value) {
                        const auto at=std::lower_bound(values.begin(),values.end(),value);
                        if(at!=values.end() && *at==value)return;
                        if(values.size()==512)cpu.stop(StopKind::NotModeled,"FPU conversion exceeds the bounded integer domain");
                        values.insert(at,value);
                    };
                    if(relation.candidates)for(const auto value:*relation.candidates)append(convert(value));
                    else for(const auto& range:cpu.fpu_ranges({input&~unknown,unknown},relation)) {
                        // Truncation/saturation is monotone for each fixed
                        // sign/exponent class. Retain every integer between
                        // the converted endpoints, including both sides of
                        // zero/normalization boundaries in separate classes.
                        auto lo=convert(range[0]),hi=convert(range[1]);if(lo>hi)std::swap(lo,hi);
                        if(std::uint64_t{hi}-lo>=512)cpu.stop(StopKind::NotModeled,"FPU conversion exceeds the bounded integer domain");
                        for(std::uint64_t value=lo;value<=hi;++value)append(static_cast<std::uint32_t>(value));
                    }
                    if(values.empty())cpu.stop(StopKind::NotModeled,"FPU conversion has no certified input domain");
                    std::uint32_t all=~0u,any=0;for(const auto value:values){all&=value;any|=value;}
                    result=all;result_unknown=all^any;
                    if(result_unknown)result_relation={memory.allocate_fpu_origin(),0,0,false,result,result_unknown,values.front(),values.back(),
                        std::make_shared<const std::vector<std::uint32_t>>(std::move(values))};
                }
            } else {
                cpu.require_fpr_known(ee::rd(word),"FPU conversion");
                const auto magnitude=(input&0x80000000u) ? 0u-input : input;
                if(magnitude) {
                    const auto exponent=31u-std::countl_zero(magnitude);
                    const auto mantissa=exponent<=23 ? magnitude<<(23u-exponent) : magnitude>>(exponent-23u);
                    result=(input&0x80000000u)|((exponent+127u)<<23u)|(mantissa&0x7FFFFFu);
                }
            }
            state.fpr[ee::sa(word)]=result;state.unknown_fpr_bits[ee::sa(word)]=result_unknown;state.fpr_affine[ee::sa(word)]=std::move(result_relation);break;
        }
        case MipsOpcode::AddS: case MipsOpcode::SubS:
            cpu.fpu_binary(instruction.opcode,word);break;
        case MipsOpcode::AbsS: case MipsOpcode::NegS:
            if(!(state.cp0(Cop0::Status)&kStatusCU1))
                cpu.stop(StopKind::GuestException,"FPU sign operation with Status.CU1 = 0; coprocessor unusable vectoring is not modeled");
            // Sign operations preserve all magnitude bits, including denormals
            // and exponent 255. They clear O/U causes, not their sticky history.
            {
            auto relation=cpu.fpu_relation(ee::rd(word));
            const bool uncertain_abs=instruction.opcode==MipsOpcode::AbsS && (state.unknown_fpr_bits[ee::rd(word)]&0x80000000u);
            if(instruction.opcode==MipsOpcode::NegS || (state.fpr[ee::rd(word)]&0x80000000u)) {
                relation.negated=!relation.negated;relation.offset=-relation.offset;
                relation.minimum^=0x80000000u;relation.maximum^=0x80000000u;
                cpu.transform_candidates(relation,instruction.opcode==MipsOpcode::AbsS);
            }
            if(uncertain_abs){cpu.transform_candidates(relation,true);relation.origin=memory.allocate_fpu_origin();relation.offset=0;relation.negated=false;}
            state.unknown_fpr_bits[ee::sa(word)]=state.unknown_fpr_bits[ee::rd(word)]&(instruction.opcode==MipsOpcode::AbsS ? 0x7FFFFFFFu : 0xFFFFFFFFu);
            state.fpr[ee::sa(word)]=(instruction.opcode==MipsOpcode::AbsS ?
                state.fpr[ee::rd(word)]&0x7FFFFFFFu : state.fpr[ee::rd(word)]^0x80000000u);
            state.fpr[ee::sa(word)]&=~state.unknown_fpr_bits[ee::sa(word)];
            relation.value=state.fpr[ee::sa(word)];relation.unknown=state.unknown_fpr_bits[ee::sa(word)];state.fpr_affine[ee::sa(word)]=relation;
            }
            state.fcr31&=~0xC000u;break;
        case MipsOpcode::MovS:
            if(!(state.cp0(Cop0::Status)&kStatusCU1))
                cpu.stop(StopKind::GuestException,"MOV.S with Status.CU1 = 0; coprocessor unusable vectoring is not modeled");
            {
            const auto relation=cpu.fpu_relation(ee::rd(word));
            state.fpr[ee::sa(word)]=state.fpr[ee::rd(word)];state.unknown_fpr_bits[ee::sa(word)]=state.unknown_fpr_bits[ee::rd(word)];state.fpr_affine[ee::sa(word)]=relation;break;
            }
        case MipsOpcode::DivS:
            if(!(state.cp0(Cop0::Status)&kStatusCU1))
                cpu.stop(StopKind::GuestException,"DIV.S with Status.CU1 = 0; coprocessor unusable vectoring is not modeled");
            if(state.unknown_fpr_bits[ee::rd(word)] || state.unknown_fpr_bits[ee::rt(word)]){cpu.fpu_binary(MipsOpcode::DivS,word);break;}
            state.fpr[ee::sa(word)]=ee_fpu_divide(state.fpr[ee::rd(word)],state.fpr[ee::rt(word)],state.fcr31);state.unknown_fpr_bits[ee::sa(word)]=0;state.fpr_affine[ee::sa(word)]={};break;
        case MipsOpcode::SqrtS: {
            if(!(state.cp0(Cop0::Status)&kStatusCU1))
                cpu.stop(StopKind::GuestException,"SQRT.S with Status.CU1 = 0; coprocessor unusable vectoring is not modeled");
            // EE instruction manual p.376: the operand is FT (FS is zero).
            // Negative nonzero inputs use sqrt(abs(FT)), set I/SI, and do
            // not trap. I/D causes are replaced; O/U and all stickies survive.
            const auto ft=ee::rt(word),fd=ee::sa(word);
            const auto eval=[](std::uint32_t input,std::uint32_t& flags)->std::optional<std::uint32_t> {
                const auto exponent=input&0x7F800000u;
                const auto root=ee_fpu_sqrt(input&0x7FFFFFFFu);
                if(!root)return std::nullopt;
                flags=(exponent && (input&0x80000000u)) ? 0x20040u : 0u;
                return exponent ? *root : (input&0x80000000u);
            };
            if(!state.unknown_fpr_bits[ft]) {
                std::uint32_t flags=0;
                const auto root=eval(state.fpr[ft],flags);
                if(!root)cpu.stop(StopKind::NotModeled,"SQRT.S nonnegative SRT root unavailable");
                state.fpr[fd]=*root;
                state.unknown_fpr_bits[fd]=0;
                state.fpr_affine[fd]={};
                state.fcr31=(state.fcr31&~0x30000u)|flags;
                break;
            }
            const ObservedWord b{state.fpr[ft]&~state.unknown_fpr_bits[ft],state.unknown_fpr_bits[ft]};
            const auto br=cpu.fpu_relation(ft);
            if(const auto bc=cpu.fpu_candidates(b,br,512u); !bc.empty()) {
                std::vector<std::uint32_t> values;std::optional<std::uint32_t> common_flags;
                bool ok=true;
                for(const auto x:bc) {
                    std::uint32_t flags=0;const auto root=eval(x,flags);
                    if(!root || (common_flags && *common_flags!=flags)){ok=false;break;}
                    common_flags=flags;values.push_back(*root);
                }
                if(ok && !values.empty()) {
                    std::sort(values.begin(),values.end());values.erase(std::unique(values.begin(),values.end()),values.end());
                    std::uint32_t all=~0u,any=0;for(const auto v:values){all&=v;any|=v;}
                    const auto unknown=all^any;
                    FpuAffine relation;
                    if(unknown) {
                        relation={memory.allocate_fpu_origin(),0,(unknown&0xFF800000u) ? 0u : (all>>23u)&255u,false,
                            all,unknown,values.front(),values.back(),
                            std::make_shared<const std::vector<std::uint32_t>>(std::move(values))};
                    }
                    state.fpr[fd]=all;state.unknown_fpr_bits[fd]=unknown;state.fpr_affine[fd]=std::move(relation);
                    state.fcr31=(state.fcr31&~0x30000u)|*common_flags;
                    break;
                }
            }
            const auto bv=cpu.fpu_ranges(b,br);
            std::array<std::optional<FloatBounds>,512> classes{};std::optional<std::uint32_t> common_flags;
            bool ok=!bv.empty();
            for(const auto& x:bv) {
                if(x[0]>x[1] || ((x[0]^x[1])&0xFF800000u)){ok=false;break;}
                std::uint32_t first_flags=0,last_flags=0;
                const auto first=eval(x[0],first_flags),last=eval(x[1],last_flags);
                if(!first || !last || first_flags!=last_flags || (common_flags && *common_flags!=first_flags)){ok=false;break;}
                common_flags=first_flags;
                const auto lo=std::min(*first,*last),hi=std::max(*first,*last);
                if((lo^hi)&0x80000000u){ok=false;break;}
                for(unsigned index=lo>>23u;index<=(hi>>23u);++index) {
                    const auto begin=std::max(lo,index<<23u),end=std::min(hi,(index<<23u)|0x7FFFFFu);
                    auto& range=classes[index];
                    if(!range)range=FloatBounds{begin,end};
                    else {(*range)[0]=std::min((*range)[0],begin);(*range)[1]=std::max((*range)[1],end);}
                }
            }
            std::vector<FloatBounds> output;std::uint32_t all=~0u,any=0;
            if(ok)for(const auto& range:classes)if(range) {
                output.push_back(*range);const auto w=cpu.interval_word(*range);
                all&=w.value;any|=w.value|w.unknown;
            }
            if(!ok || output.empty())
                cpu.stop(StopKind::NotModeled,"SQRT.S cannot certify value/exception bounds; FT="+hex32(b.value));
            const auto unknown=all^any;
            FpuAffine relation;
            if(unknown) {
                relation={memory.allocate_fpu_origin(),0,(unknown&0xFF800000u) ? 0u : (all>>23u)&255u,false,
                    all,unknown,output.front()[0],output.back()[1],{},
                    std::make_shared<const std::vector<FloatBounds>>(std::move(output))};
            }
            state.fpr[fd]=all;state.unknown_fpr_bits[fd]=unknown;state.fpr_affine[fd]=std::move(relation);
            state.fcr31=(state.fcr31&~0x30000u)|*common_flags;
            break;
        }
        case MipsOpcode::MulS: cpu.fpu_binary(instruction.opcode,word);break;
        case MipsOpcode::MaddS: case MipsOpcode::MsubaS: case MipsOpcode::MaddaS: case MipsOpcode::MsubS:
            cpu.multiply_accumulate(instruction.opcode,word);break;
        case MipsOpcode::MulaS: cpu.mula(word);break;
        case MipsOpcode::AddaS: cpu.adda(word);break;
        case MipsOpcode::Addi: case MipsOpcode::Add: case MipsOpcode::Sub:
        case MipsOpcode::Daddi: case MipsOpcode::Dadd: case MipsOpcode::Dsub:
        case MipsOpcode::Syscall: case MipsOpcode::Break: break; // Handled above.
        default:throw std::logic_error("non-system opcode passed to static EE system execution");
    }
    (void)ee::retire(ctx, frame);
}
} // namespace ps2native
