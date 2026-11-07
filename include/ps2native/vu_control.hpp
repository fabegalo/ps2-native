#pragma once
#include "ps2native/device_timing.hpp"
#include "ps2native/ee_fpu.hpp"
#include "ps2native/vu_float.hpp"
#include "ps2native/profile.hpp"
    // HOST_INPUT_BEGIN
#include "ps2native/vu_aot_runtime.hpp"
#include <functional>
    // HOST_INPUT_END
#include <array>
#include <optional>
#include <vector>

namespace ps2native {
class Gif;
class GsRegisters;
// VU User's Manual 6.0, sections 1.3 and 5.1.3: control transfers and
// reset of idle VUs and a shared, clocked microinstruction engine. Unsupported
// instructions stop with their real micro PC; they never finish successfully.
class VuControl {
    friend struct CheckpointAccess;
public:
    VuControl() { reset(); }
    void reset() {
    // HOST_INPUT_BEGIN
        invalidate_vu1_aot();vu1_aot_stats_={};
    // HOST_INPUT_END
        diagnostic_power_two_carry_=false;
        micro_={};
        path1_.reset();
        queued_path1_.reset();
        pending_q_.reset();
        pending_q1_.reset();
        registers_ = {};
        unknown_ = {};
        vector_bounds_={};accumulator_bounds_={};q_bounds_.reset();vu1_vector_bounds_={};
        registers_[0] = 0;
        for (unsigned index=1;index<16;++index) {registers_[index]=0;unknown_[index]=0xFFFFu;}
        for (const auto index : {16u,17u,18u,28u,29u}) registers_[index] = 0;
        for(const auto index : {20u,21u,22u,27u}) {
            registers_[index]=0;
            unknown_[index]=index==20u ? 0x7FFFFFu : index==27u ? 0xFFFFu : ~0u;
        }
        vu1_integers_.fill({0,0xFFFFu}); vu1_integers_[0]={};
        vu1_controls_.fill({0,~0u});
        for(unsigned index=0;index<3;++index) vu1_controls_[index]={};
        data_.assign(1024u,{0,~0u});data_bounds_.assign(1024u,std::nullopt);
        for(auto& vector : vectors_) vector.fill({0,~0u});
        vectors_[0]={ObservedWord{},ObservedWord{},ObservedWord{},ObservedWord{0x3F800000u,0}};
        vu1_vectors_=vectors_;
        accumulator_.fill({0,~0u});
        accumulator1_=accumulator_;
        micro0_.assign(1024u,{0,~0u}); micro1_.assign(4096u,{0,~0u}); data1_.assign(4096u,{0,~0u});data1_bounds_.assign(4096u,std::nullopt);
    }
    [[nodiscard]] std::uint32_t read(unsigned index) const {
        const auto word=observe(index);
        if (word.unknown) throw DeviceRefusal("VU register read consumes unknown initial bits");
        return word.value;
    }
    [[nodiscard]] ObservedWord observe(unsigned index) const {
        if (index >= registers_.size() || !registers_[index])
            throw DeviceRefusal("VU control register is reserved or undefined before write");
        const auto value=*registers_[index];
        if(index==29u)return {value|(pending_q_ ? 1u<<5u : 0u)|(pending_q1_ ? 1u<<13u : 0u)|
            (path1_ ? 1u<<12u : 0u),unknown_[index]};
        return {value,unknown_[index]};
    }
    void write(unsigned index, std::uint32_t value) {
        write_observed(index,{value,0});
    }
    void write_observed(unsigned index, ObservedWord word) {
        const auto value=word.value&~word.unknown;
        if (index == 0) return; // VI00 is the constant zero register.
        if (index < 16) { registers_[index] = value & 0xFFFFu; unknown_[index]=word.unknown&0xFFFFu; return; }
        // VU manual 5.1.3: STATUS writes affect only the six sticky flags;
        // CLIP holds 24 bits. Reserved/input-only bits consume no uncertainty.
        if (index == 16) {
            registers_[16]=(*registers_[16]&0x3Fu)|(value&0xFC0u);
            unknown_[16]=(unknown_[16]&0x3Fu)|(word.unknown&0xFC0u);
            return;
        }
        if (index == 18) {registers_[18]=value&0xFFFFFFu;unknown_[18]=word.unknown&0xFFFFFFu;return;}
        if (index==20u || index==21u || index==22u || index==27u) {
            // R is 23 bits; I/Q hold raw 32-bit floating-point encodings.
            // CMSAR0 stores a 16-bit start address and does not start execution.
            const auto mask=index==20u ? 0x7FFFFFu : index==27u ? 0xFFFFu : ~0u;
            registers_[index]=value&mask;unknown_[index]=word.unknown&mask;
            if(index==22u)q_bounds_.reset();
            return;
        }
        if (word.unknown) throw DeviceRefusal("VU control write consumes unknown bits");
        if (index != 28)
            throw DeviceRefusal("VU control register write is not modeled");
        if (value & ~0x0F0Fu)
            throw DeviceRefusal("VU FBRST reserved bits set");
        if (value & 0x0101u)
            throw DeviceRefusal("VU force break transition is not modeled");
        // FB/RS are commands that always read zero; DE/TE retain settings.
        // VU0 reset initializes its flag registers, not the integer registers.
        if (value & 2u) {
            reset_micro(0);
            pending_q_.reset();
            for (const auto flag : {16u,17u,18u}) {registers_[flag] = 0;unknown_[flag]=0;}
        }
        if (value & 0x200u) {
            reset_micro(1);
            pending_q1_.reset();
            for(unsigned index=0;index<3;++index) vu1_controls_[index]={};
        }
        registers_[28] = value & 0x0C0Cu;
        registers_[29] = (micro_[0].running ? 1u : 0u)|(micro_[1].running ? 256u : 0u);
    }
    // HOST_INPUT_BEGIN
    template<std::uint32_t Low,std::uint32_t Upper> void execute_micro_native(Gif* gif,GsRegisters* gs);
    void enable_vu1_aot(bool enabled) noexcept {vu1_aot_enabled_=enabled;}
    [[nodiscard]] const VuAotStats& vu1_aot_stats() const noexcept {return vu1_aot_stats_;}
    [[nodiscard]] VuAotFallback vu1_aot_fallback() const noexcept {return vu1_aot_fallback_;}
    [[nodiscard]] VuAotImage capture_vu1_micro() const;
    [[nodiscard]] bool vu1_diagnostic_power_two_carry() const noexcept {return diagnostic_power_two_carry_;}
    // Optional passive capture at genuine MSCAL/MSCNT boundaries.
    using Vu1Capture=std::function<void(const VuControl&,std::uint32_t)>;
    void set_vu1_capture(Vu1Capture capture) {vu1_capture_=std::move(capture);vu1_capture_dirty_=true;}
    void invalidate_vu1_aot() noexcept {vu1_aot_checked_=false;vu1_aot_selected_=nullptr;vu1_capture_dirty_=true;}
    // HOST_INPUT_END
    void start_micro(unsigned unit,std::uint32_t instruction_address,std::uint64_t ee_cycle,
                     ObservedWord itop={0,0x3FFu},ObservedWord top={0,0x3FFu});
    void advance_micro(std::uint64_t ee_cycle,Gif* gif=nullptr,GsRegisters* gs=nullptr);
    void resume_micro(unsigned unit,std::uint64_t ee_cycle,ObservedWord itop={0,0x3FFu},ObservedWord top={0,0x3FFu}) {
        const auto& previous=micro_.at(unit);
        if(previous.running || !previous.instructions)throw DeviceRefusal("VU MSCNT requires an established Ready termination PC");
        start_micro(unit,previous.pc/8u,ee_cycle,itop,top);
    }
    [[nodiscard]] bool micro_running(unsigned unit) const {return micro_.at(unit).running;}
    void diagnostic_power_two_carry() {
        for(const auto& unit:micro_)if(unit.running || unit.instructions)throw DeviceRefusal("VU carry experiment must precede micro execution");
        if(pending_q_ || pending_q1_ || path1_)throw DeviceRefusal("VU carry experiment requires idle hardware");
        diagnostic_power_two_carry_=true;
    // HOST_INPUT_BEGIN
        invalidate_vu1_aot();
    // HOST_INPUT_END
    }
    [[nodiscard]] bool path1_transfer_pending() const noexcept {return path1_.has_value();}
    [[nodiscard]] bool path1_end_on_kick() const noexcept {return (path1_ && path1_->end_on_kick) || (queued_path1_ && queued_path1_->end_on_kick);}
    [[nodiscard]] std::optional<std::uint64_t> next_micro_cycle() const noexcept {
        std::optional<std::uint64_t> result;
        for(const auto& unit:micro_)if(unit.running && (!result || unit.next<*result))result=unit.next;
        if(path1_ && (!result || path1_->next<*result))result=path1_->next;
        return result;
    }
    // RUNTIME_HELPERS_BEGIN
    // Q completion is independent of a microprogram's running state. The
    // synchronization fast path must include both pipelines after E/Ready.
    [[nodiscard]] bool micro_event_due(std::uint64_t cycle) const noexcept {
        return (micro_[0].running && micro_[0].next<=cycle) ||
               (micro_[1].running && micro_[1].next<=cycle) ||
               (path1_ && path1_->next<=cycle) ||
               (pending_q_ && pending_q_->ready<=cycle) ||
               (pending_q1_ && pending_q1_->ready<=cycle);
    }
    // RUNTIME_HELPERS_END
    [[nodiscard]] std::uint32_t diagnostic_micro_pc(unsigned unit) const {return micro_.at(unit).pc;}
    [[nodiscard]] std::uint64_t diagnostic_micro_instructions(unsigned unit) const {return micro_.at(unit).instructions;}
    // VU Mem0 uses qword addresses in VISWR. VU1's integer registers live at
    // byte addresses 0x4200..0x42F0; only their low 16 bits are writable.
    [[nodiscard]] ObservedWord observe_data(std::uint32_t address) const {
        if (address & 3u) throw DeviceRefusal("unaligned VU data word");
        if (address < 0x1000u) return data_[address/4u];
        if (address>=0x4000u && address<0x4200u)
            return vu1_vectors_[(address-0x4000u)/16u][(address&15u)/4u];
        if (address>=0x4200u && address<0x4300u)
            return (address&15u) ? ObservedWord{} : vu1_integers_[(address-0x4200u)/16u];
        if (address>=0x4300u && address<0x43B0u) {
            const auto index=(address-0x4300u)/16u;
            if (index<3u || (index>=4u && index<=7u) || index==10u)
                return (address&15u) ? ObservedWord{} : vu1_controls_[index];
        }
        throw DeviceRefusal("VU data/register window is not modeled");
    }
    void integer_store(unsigned source, unsigned base, unsigned mask) {
        if (source>=16u || base>=16u) throw DeviceRefusal("VISWR requires integer registers VI00..VI15");
        if (!mask) return;
        const auto address=read(base)*16u;
        // Check the entire destination before any store can change it.
        for (unsigned lane=0;lane<4u;++lane)
            if (mask&(8u>>lane)) check_data_write(address+lane*4u);
        const auto word=observe(source);
        for (unsigned lane=0;lane<4u;++lane)
            if (mask&(8u>>lane)) store_data(address+lane*4u,word);
    }
    [[nodiscard]] ObservedQuadword observe_vector(unsigned index) const {
        const auto& v=vectors_.at(index);
        return {{std::uint64_t{v[0].value}|(std::uint64_t{v[1].value}<<32),
                 std::uint64_t{v[0].unknown}|(std::uint64_t{v[1].unknown}<<32)},
                {std::uint64_t{v[2].value}|(std::uint64_t{v[3].value}<<32),
                 std::uint64_t{v[2].unknown}|(std::uint64_t{v[3].unknown}<<32)}};
    }
    [[nodiscard]] std::optional<FloatBounds> observe_vector_bounds(unsigned index,unsigned lane) const {
        return vector_bounds_.at(index).at(lane);
    }
    void restore_vector_bounds(unsigned index,unsigned lane,FloatBounds bounds) {
        if(index==0)return;
        const auto word=vectors_.at(index).at(lane);
        if(bounds[0]>bounds[1] || bounds[0]<(word.value&~word.unknown) || bounds[1]>(word.value|word.unknown) ||
           ((bounds[0]^word.value)&~word.unknown) || ((bounds[1]^word.value)&~word.unknown))
            throw DeviceRefusal("VU transfer bounds differ from the loaded word");
        vector_bounds_.at(index).at(lane)=bounds;
    }
    void write_vector(unsigned index, ObservedQuadword word) {
        if(index==0) return; // VF00 = (0,0,0,1).
        auto& v=vectors_.at(index);
        for(auto& bounds:vector_bounds_.at(index))bounds.reset();
        for(unsigned lane=0;lane<4;++lane) {
            const auto half=lane<2 ? word.low : word.high;
            const auto shift=(lane&1u)*32u;
            v[lane]={static_cast<std::uint32_t>((half.value&~half.unknown)>>shift),
                     static_cast<std::uint32_t>(half.unknown>>shift)};
        }
    }
    [[nodiscard]] std::uint64_t q_ready_cycle() const noexcept { return pending_q_ ? pending_q_->ready : 0; }
    void advance_q(std::uint64_t cycle) {
        if(!pending_q_ || cycle<pending_q_->ready)return;
        registers_[22]=pending_q_->value;unknown_[22]=pending_q_->unknown;q_bounds_=pending_q_->bounds;
        const auto flags=pending_q_->flags;
        registers_[16]=(*registers_[16]&~0x30u)|flags|(flags<<6u);
        unknown_[16]&=~(0x30u|(flags<<6u));
        pending_q_.reset();
    }
    void issue_q(std::uint64_t cycle,unsigned left,unsigned right,unsigned left_lane,unsigned right_lane,unsigned q_op) {
        profiling::Scope profile(profiling::Phase::Vu);
        if(pending_q_)throw DeviceRefusal("VU FDIV pipeline is still occupied");
        const auto a=vectors_.at(left)[left_lane],b=vectors_.at(right)[right_lane];
        const bool square_root = (q_op == 1u);
        const bool rsqrt = (q_op == 2u);
        if(q_op == 0u && (b.unknown || a.unknown))throw DeviceRefusal("VU Q consumes unknown operands");
        std::uint32_t value,flags=0,unknown=0;
        std::optional<FloatBounds> bounds;
        if(square_root) {
            if(b.unknown) {
                const auto root=vu_float_sqrt(b,vector_bounds_.at(right)[right_lane]);
                if(!root)throw DeviceRefusal("VU Q consumes unknown operands with unproven SQRT fraction/flags");
                value=root->word.value;unknown=root->word.unknown;
                if(root->bounded)bounds=root->bounds;
            } else {
                const auto root=ee_fpu_sqrt(b.value);
                if(!root)throw DeviceRefusal("VU SQRT negative input flags are not modeled");
                value=*root;
            }
        } else if(rsqrt) {
    // RUNTIME_HELPERS_BEGIN
            if(b.unknown && !a.unknown) {
                const auto result=vu_float_rsqrt(a.value,b,vector_bounds_.at(right)[right_lane]);
                if(!result)throw DeviceRefusal("VU RSQRT consumes unknown operands with unproven fraction/flags");
                flags=((result->flags>>13u)&0x10u)|((result->flags>>11u)&0x20u);
                pending_q_=PendingQ{cycle+13u,result->word.value,flags,result->word.unknown,
                    result->bounded ? std::optional<FloatBounds>{result->bounds} : std::nullopt};
                return;
            }
    // RUNTIME_HELPERS_END
            if(b.unknown || a.unknown)throw DeviceRefusal("VU Q consumes unknown operands");
            const auto root=ee_fpu_sqrt(b.value);
            if(!root)throw DeviceRefusal("VU RSQRT negative input flags are not modeled");
            value=ee_fpu_divide(a.value,*root,flags);
            flags=((flags>>13u)&0x10u)|((flags>>11u)&0x20u);
        } else {
            value=ee_fpu_divide(a.value,b.value,flags);
            flags=((flags>>13u)&0x10u)|((flags>>11u)&0x20u);
        }
        pending_q_=PendingQ{cycle+(rsqrt ? 13u : 7u),value,flags,unknown,bounds};
    }
    void add(unsigned destination,unsigned left,unsigned right,unsigned mask,unsigned broadcast=4,bool subtract=false,bool to_accumulator=false) {
        profiling::Scope profile(profiling::Phase::Vu);
        auto result=to_accumulator ? accumulator_ : vectors_.at(destination);
        std::array<std::optional<FloatBounds>,4> bounds{};
        std::uint32_t mac=0,status=0;
        for(unsigned lane=0;lane<4;++lane) {
            const auto bit=8u>>lane;
            if(!(mask&bit))continue;
            const auto a=vectors_.at(left)[lane];
            const auto b=broadcast==5u ? observe(22) : vectors_.at(right)[broadcast<4 ? broadcast : lane];
            std::uint32_t flags=0;
            ObservedWord value;
            if(a.unknown || b.unknown) {
                const auto sum=vu_float_add(a,{b.value^(subtract ? 0x80000000u : 0u),b.unknown},
                    vector_bounds_.at(left)[lane],signed_bounds(broadcast==5u ? q_bounds_ :
                        vector_bounds_.at(right)[broadcast<4 ? broadcast : lane],subtract));
                if(!sum)throw DeviceRefusal("VU ADD consumes unknown operands with unproven fraction/flags");
                value=sum->word;flags=sum->flags;
                if(sum->bounded)bounds[lane]=sum->bounds;
            } else value={ee_fpu_add(a.value,b.value^(subtract ? 0x80000000u : 0u),flags),0};
            if(to_accumulator && (flags&0xC000u))throw DeviceRefusal("VU ADD exceptional result/ACC metadata is not modeled");
            result[lane]=value;
            if(vu_float_zero(value,bounds[lane])){mac|=bit;status|=1u;}
            if(value.value&0x80000000u){mac|=bit<<4u;status|=2u;}
            if(flags&0x4000u){mac|=bit<<8u;status|=4u;}
            if(flags&0x8000u){mac|=bit<<12u;status|=8u;}
        }
        // Commit only after all selected operands have been validated.
        if(to_accumulator) {
            accumulator_=result;
            for(unsigned lane=0;lane<4u;++lane)if(mask&(8u>>lane))accumulator_bounds_[lane]=bounds[lane];
        } else if(destination){
            vectors_.at(destination)=result;
            for(unsigned lane=0;lane<4u;++lane)if(mask&(8u>>lane))vector_bounds_.at(destination)[lane]=bounds[lane];
        }
        registers_[17]=mac;unknown_[17]=0;
        registers_[16]=(*registers_[16]&~0xFu)|status|(status<<6u);
        unknown_[16]&=~(0xFu|(status<<6u));
    }
    void product(unsigned destination,unsigned left,unsigned right,unsigned mask,
                 unsigned broadcast,bool accumulate,bool to_accumulator,bool outer=false) {
        profiling::Scope profile(profiling::Phase::Vu);
        auto result=to_accumulator ? accumulator_ : vectors_.at(destination);
        std::array<std::optional<FloatBounds>,4> bounds{};
        std::uint32_t mac=0,status=0;
        for(unsigned lane=0;lane<4;++lane) {
            const auto bit=8u>>lane;
            if(!(mask&bit))continue;
            const auto a=vectors_.at(left)[outer ? (lane+1u)%3u : lane];
            const auto b=broadcast==5u ? observe(22) : vectors_.at(right)[outer ? (lane+2u)%3u : broadcast<4 ? broadcast : lane];
            std::uint32_t flags=0;
            const auto multiplied=a.unknown || b.unknown ? std::nullopt :
                ee_fpu_multiply(a.value,b.value,flags,diagnostic_power_two_carry_);
            ObservedWord value;
            if(multiplied)value={*multiplied,0};
            else {
                const auto range=vu_float_product(a,b,diagnostic_power_two_carry_,
                    vector_bounds_.at(left)[outer ? (lane+1u)%3u : lane],
                    broadcast==5u ? q_bounds_ : vector_bounds_.at(right)[outer ? (lane+2u)%3u : broadcast<4 ? broadcast : lane]);
                if(!range)throw DeviceRefusal("VU multiply consumes unknown operands or ambiguous fraction/flags");
                value=range->word;flags=range->flags;
                if(range->bounded)bounds[lane]=range->bounds;
            }
            // Ordinary VMUL does not read or write ACC. Its certified clipped
            // result and MAC/status flags need no hidden accumulator state.
            // Exceptional ACC writes/accumulations remain explicit refusals.
            if((flags&0xC000u) && (accumulate || to_accumulator))
                throw DeviceRefusal("VU multiply exception/ACC state is not modeled");
            if(accumulate) {
                const auto acc=accumulator_[lane];
                if(acc.unknown || value.unknown) {
                    const auto sum=vu_float_add(acc,{value.value^(outer ? 0x80000000u : 0u),value.unknown},
                        accumulator_bounds_[lane],signed_bounds(bounds[lane],outer));
                    if(!sum)throw DeviceRefusal("VU multiply-add consumes unknown ACC or unproven fraction/flags");
                    value=sum->word;flags=sum->flags;
                    bounds[lane]=sum->bounded ? std::optional<FloatBounds>{sum->bounds} : std::nullopt;
                } else {value={ee_fpu_add(acc.value,value.value^(outer ? 0x80000000u : 0u),flags),0};bounds[lane].reset();}
                if(flags&0xC000u)throw DeviceRefusal("VU accumulation exception/ACC state is not modeled");
            }
            result[lane]=value;
            if(vu_float_zero(value,bounds[lane])){mac|=bit;status|=1u;}
            if(value.value&0x80000000u){mac|=bit<<4u;status|=2u;}
            if(flags&0x4000u){mac|=bit<<8u;status|=4u;}
            if(flags&0x8000u){mac|=bit<<12u;status|=8u;}
        }
        if(to_accumulator){
            accumulator_=result;
            for(unsigned lane=0;lane<4u;++lane)if(mask&(8u>>lane))accumulator_bounds_[lane]=bounds[lane];
        } else if(destination){
            vectors_.at(destination)=result;
            for(unsigned lane=0;lane<4u;++lane)if(mask&(8u>>lane))vector_bounds_.at(destination)[lane]=bounds[lane];
        }
        registers_[17]=mac;unknown_[17]=0;
        registers_[16]=(*registers_[16]&~0xFu)|status|(status<<6u);
        unknown_[16]&=~(0xFu|(status<<6u));
    }
    void rotate(unsigned destination,unsigned source,unsigned mask) {
        profiling::Scope profile(profiling::Phase::Vu);
        const auto input=vectors_.at(source); // alias-safe, including unknown bits
        const auto bounds=vector_bounds_.at(source);
        if(destination)for(unsigned lane=0;lane<4;++lane)
            if(mask&(8u>>lane)){
                vectors_.at(destination)[lane]=input[(lane+1u)&3u];
                vector_bounds_.at(destination)[lane]=bounds[(lane+1u)&3u];
            }
    }
    void subtract(unsigned destination,unsigned left,unsigned right,unsigned mask) {
        add(destination,left,right,mask,4,true);
    }
    void vector_store_increment(unsigned source,unsigned base,unsigned mask) {
        if(base>=16u) throw DeviceRefusal("VSQI base must be VI00..VI15");
        const auto qword=read(base),address=qword*16u;
        for(unsigned lane=0;lane<4;++lane)
            if(mask&(8u>>lane)) check_data_write(address+lane*4u);
        const auto value=vectors_.at(source); // Snapshot before any aliased writes.
        for(unsigned lane=0;lane<4;++lane)
            if(mask&(8u>>lane)) store_data(address+lane*4u,value[lane]);
        write(base,(qword+1u)&0xFFFFu);
    }
    void add_integer(unsigned destination,unsigned left,unsigned right) {
        if(destination>=16u || left>=16u || right>=16u)
            throw DeviceRefusal("VIADD requires integer registers VI00..VI15");
        // VU manual 6.0, IADD/VIADD: 16-bit wrapping result, no flag changes.
        // Read both operands before changing a possibly aliased destination.
        const auto result=read(left)+read(right);
        write(destination,result);
    }
    [[nodiscard]] ObservedDoubleword read_memory(std::uint32_t physical,std::size_t bytes) const {
        ObservedDoubleword result;
        for(std::size_t byte=0;byte<bytes;++byte) {
            const auto address=physical+static_cast<std::uint32_t>(byte);
            const auto word=memory_word(address&~3u);
            const auto shift=(address&3u)*8u;
            result.value|=std::uint64_t{(word.value>>shift)&0xFFu}<<(byte*8u);
            result.unknown|=std::uint64_t{(word.unknown>>shift)&0xFFu}<<(byte*8u);
        }
        return result;
    }
    void restore_data_bounds(std::uint32_t physical,FloatBounds bounds) {
        if((physical&3u) || !word_bounds_match(memory_word(physical),bounds))throw DeviceRefusal("VU data certificate differs from the stored word");
        if(physical>=0x11004000u && physical<0x11005000u)data_bounds_[(physical-0x11004000u)/4u]=bounds;
        else if(physical>=0x1100C000u && physical<0x11010000u)data1_bounds_[(physical-0x1100C000u)/4u]=bounds;
        else throw DeviceRefusal("VU data certificate is outside data RAM");
    }
    void write_memory(std::uint32_t physical,std::size_t bytes,ObservedDoubleword value) {
        for(std::size_t byte=0;byte<bytes;++byte)
            (void)memory_word((physical+static_cast<std::uint32_t>(byte))&~3u);
        for(std::size_t byte=0;byte<bytes;++byte) {
            const auto address=physical+static_cast<std::uint32_t>(byte);
    // HOST_INPUT_BEGIN
            if(address>=0x11008000u && address<0x1100C000u)invalidate_vu1_aot();
    // HOST_INPUT_END
            if(address>=0x11004000u && address<0x11005000u)data_bounds_[(address-0x11004000u)/4u].reset();
            if(address>=0x1100C000u && address<0x11010000u)data1_bounds_[(address-0x1100C000u)/4u].reset();
            auto& word=memory_word(address&~3u);
            const auto shift=(address&3u)*8u,mask=0xFFu<<shift;
            word.value=(word.value&~mask)|(static_cast<std::uint32_t>((value.value&~value.unknown)>>(byte*8u))<<shift&mask);
            word.unknown=(word.unknown&~mask)|(static_cast<std::uint32_t>(value.unknown>>(byte*8u))<<shift&mask);
        }
    }
private:
    struct MicroIntegerWrite {unsigned index{};ObservedWord word{};std::uint64_t ready{};bool memory_load{},flag_check{};};
    struct MicroVectorWrite {
        unsigned index{},mask{};
        std::array<ObservedWord,4> words{};
        std::uint64_t ready{};
        std::array<std::optional<FloatBounds>,4> bounds{};
    };
    struct MicroFlagWrite {std::uint32_t mac{},status{};std::uint64_t ready{};};
    struct MicroFmac {MicroVectorWrite output{};std::uint32_t mac{},status{};bool active{},hazard{},changes_flags{};};
    struct MicroState {
        bool running{},end_delay{};
        std::uint32_t pc{};
        std::uint64_t next{},instructions{};
        ObservedWord itop{0,0x3FFu},top{0,0x3FFu};
        std::optional<std::uint32_t> branch;
        std::optional<std::uint64_t> drain;
        std::array<MicroIntegerWrite,4> pending{};
        unsigned writes{};
        std::array<MicroVectorWrite,8> vectors{};
        unsigned vector_writes{};
        std::array<MicroFlagWrite,4> flags{};
        unsigned flag_writes{};
        std::array<MicroVectorWrite,4> stores{}; // index is a qword address here
        unsigned store_writes{};
        std::optional<std::string> failure;
    };
    void reset_micro(unsigned unit);
    void step_micro(unsigned unit,Gif* gif,GsRegisters* gs);
    void step_path1(Gif* gif,GsRegisters* gs);
    void advance_micro_q(std::uint64_t cycle);
    [[nodiscard]] ObservedWord micro_integer(unsigned unit,unsigned index,bool forwarded,unsigned bypass_delay=1) const;
    void retire_micro_integers(unsigned unit);
    [[nodiscard]] MicroFmac evaluate_micro_upper(unsigned unit,std::uint32_t instruction) const;
    // HOST_INPUT_BEGIN
    template<int Unit,std::int64_t Instruction> [[nodiscard]] MicroFmac evaluate_micro_upper_shared(unsigned unit,std::uint32_t instruction) const;
    template<int Unit,std::int64_t Low,std::int64_t Upper> void issue_micro_shared(unsigned unit,std::uint32_t low,std::uint32_t upper,Gif* gif,GsRegisters* gs);
    bool try_vu1_aot(Gif* gif,GsRegisters* gs);
    bool vu1_aot_enabled_{true},vu1_aot_checked_{},vu1_capture_dirty_{true};
    const VuAotModule* vu1_aot_selected_{};
    VuAotStats vu1_aot_stats_{};
    VuAotFallback vu1_aot_fallback_{VuAotFallback::None};
    Vu1Capture vu1_capture_;
    // HOST_INPUT_END
    std::array<MicroState,2> micro_{};
    struct Path1 {
        std::uint32_t address{};
        std::uint64_t next{},qwords{};
        bool end_on_kick{};
        std::optional<std::string> failure;
    };
    std::optional<Path1> path1_;
    std::optional<Path1> queued_path1_;
    struct PendingQ { std::uint64_t ready; std::uint32_t value,flags; std::uint32_t unknown{}; std::optional<FloatBounds> bounds; };
    std::optional<PendingQ> pending_q_;
    std::optional<PendingQ> pending_q1_;
    ObservedWord& memory_word(std::uint32_t address) {
        if(address>=0x11000000u && address<0x11001000u) return micro0_[(address-0x11000000u)/4u];
        if(address>=0x11004000u && address<0x11005000u) return data_[(address-0x11004000u)/4u];
        if(address>=0x11008000u && address<0x1100C000u) return micro1_[(address-0x11008000u)/4u];
        if(address>=0x1100C000u && address<0x11010000u) return data1_[(address-0x1100C000u)/4u];
        throw DeviceRefusal("unimplemented VU memory aperture");
    }
    const ObservedWord& memory_word(std::uint32_t address) const {
        return const_cast<VuControl*>(this)->memory_word(address);
    }
    void check_data_write(std::uint32_t address) const {
        (void)observe_data(address);
        if((address&~15u)==0x4310u || (address&~15u)==0x43A0u)
            throw DeviceRefusal("VU store to read-only VU1 control register is not modeled");
    }
    void store_data(std::uint32_t address,ObservedWord word) {
        if(address<0x1000u) {data_[address/4u]=word;data_bounds_[address/4u].reset();return;}
        const auto lane=(address&15u)/4u;
        if(address<0x4200u) {
            const auto index=(address-0x4000u)/16u;
            if(index) {vu1_vectors_[index][lane]=word;vu1_vector_bounds_[index][lane].reset();}
        } else if(address<0x4300u) {
            const auto index=(address-0x4200u)/16u;
            if(index && !lane) vu1_integers_[index]={word.value&0xFFFFu,word.unknown&0xFFFFu};
        } else if(!lane) {
            const auto index=(address-0x4300u)/16u;
            const auto writable=index==0u ? 0xFC0u : index==2u ? 0xFFFFFFu : ~0u;
            auto& destination=vu1_controls_[index];
            destination.value=(destination.value&~writable)|(word.value&writable);
            destination.unknown=(destination.unknown&~writable)|(word.unknown&writable);
        }
    }
    std::array<std::optional<std::uint32_t>,32> registers_{};
    bool diagnostic_power_two_carry_{};
    std::array<std::uint32_t,32> unknown_{};
    std::array<ObservedWord,16> vu1_integers_{};
    std::array<ObservedWord,16> vu1_controls_{};
    std::vector<ObservedWord> data_;
    std::vector<std::optional<FloatBounds>> data_bounds_,data1_bounds_;
    std::array<std::array<ObservedWord,4>,32> vectors_{};
    // Certificates belong to the current value, never to a matching bit hull.
    static std::optional<FloatBounds> signed_bounds(std::optional<FloatBounds> bounds,bool negate) {
        if(bounds && negate)for(auto& endpoint:*bounds)endpoint^=0x80000000u;
        return bounds;
    }
    std::array<std::array<std::optional<FloatBounds>,4>,32> vector_bounds_{};
    std::array<std::optional<FloatBounds>,4> accumulator_bounds_{};
    std::array<std::array<std::optional<FloatBounds>,4>,33> vu1_vector_bounds_{};
    std::optional<FloatBounds> q_bounds_;
    std::array<ObservedWord,4> accumulator_{};
    std::array<ObservedWord,4> accumulator1_{};
    std::array<std::array<ObservedWord,4>,32> vu1_vectors_{};
    std::vector<ObservedWord> micro0_,micro1_,data1_;
};
} // namespace ps2native
