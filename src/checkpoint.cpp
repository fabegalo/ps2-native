// Original, field-based runtime serialization. No raw objects or host handles.
#include "ps2native/checkpoint.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"
#include "checkpoint_build.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <deque>
#include <map>
#include <typeindex>
#include <unordered_map>
#include <limits>
#include <tuple>

namespace ps2native {
namespace checkpoint_detail {
constexpr std::uint64_t maximum_bytes=128ull*1024u*1024u;
struct Any {template<class T> operator T() const;};
template<class T,std::size_t... I> constexpr bool initializes(std::index_sequence<I...>) {
    return requires {T{(static_cast<void>(I),Any{})...};};
}
template<class T,std::size_t N=0> consteval std::size_t arity() {
    if constexpr(N==64)return N;
    else if constexpr(initializes<T>(std::make_index_sequence<N+1>{}))return arity<T,N+1>();
    else return N;
}
template<class A,class T> void aggregate(A& archive,T& value) {
    static_assert(std::is_aggregate_v<T>,"checkpoint requires explicit nonaggregate fields");
    constexpr auto n=arity<T>();
#include "checkpoint_aggregate.inc"
}
template<class T> struct Optional:std::false_type{};
template<class T> struct Optional<std::optional<T>>:std::true_type{using element=T;};
template<class T> struct Shared:std::false_type{};
template<class T> struct Shared<std::shared_ptr<T>>:std::true_type{using element=T;};
template<class T> struct Array:std::false_type{};
template<class T,std::size_t N> struct Array<std::array<T,N>>:std::true_type{using element=T;};
template<class T> struct Vector:std::false_type{};
template<class T,class Alloc> struct Vector<std::vector<T,Alloc>>:std::true_type{using element=T;};
template<class T> struct Deque:std::false_type{};
template<class T,class Alloc> struct Deque<std::deque<T,Alloc>>:std::true_type{using element=T;};
template<class T> struct Map:std::false_type{};
template<class K,class V,class C,class Alloc> struct Map<std::map<K,V,C,Alloc>>:std::true_type{using key=K;using mapped=V;};
template<class K,class V,class H,class E,class Alloc> struct Map<std::unordered_map<K,V,H,E,Alloc>>:std::true_type{using key=K;using mapped=V;};
template<class T> struct Pair:std::false_type{};
template<class X,class Y> struct Pair<std::pair<X,Y>>:std::true_type{};
template<class T> T empty_value() {
    if constexpr(std::is_default_constructible_v<T>)return T{};
    else if constexpr(std::is_same_v<T,RdramChain>)return T{0};
    else if constexpr(std::is_constructible_v<T,std::uint8_t>)return T{std::uint8_t{0}};
    else static_assert(sizeof(T)==0,"checkpoint needs a safe value constructor");
}
template<bool Reading> class Archive;
}

struct CheckpointAccess {
#include "checkpoint_fields.inc"
    template<class A,class T> static void fields(A& a,T& value){checkpoint_detail::aggregate(a,value);}
    static std::string bindings(const Memory& memory,std::string_view expected_disc);
    static void validate_boundary(const EEState& state,const Memory& memory);
    static void refine_scalar(const EEState& state,Memory& memory) {
        if(memory.cache_clock_!=&state.retired_instructions || memory.fpu_origin_ || !memory.fpu_relations_.empty() ||
           std::any_of(state.unknown_fpr_bits.begin(),state.unknown_fpr_bits.end(),[](auto bits){return bits!=0;}))
            throw DeviceRefusal("scalar checkpoint refinement requires bound, fully certified numeric history");
        // Every uncertain scalar result allocates or inherits a monotonic
        // origin. Zero origins prove that all earlier results were certified.
        // The optional coarse carry subtracts 2^15 within the strict low-mass
        // bounds; it agrees with every formerly certified result and flag.
        // Keep the arithmetic-started guard: this is a reviewed refinement,
        // not permission to reconfigure arbitrary partially evaluated state.
        memory.diagnostic_ee_early_digit_carry_=true;
    }
    static void refine_texture(Memory& memory,unsigned modes) {
        auto& gs=memory.registers_.gs_;
        if(!modes || (modes&~63u) || (gs.diagnostic_texture_precision_&modes)!=gs.diagnostic_texture_precision_)
            throw DeviceRefusal("checkpoint texture refinement requires valid additive diagnostic modes");
        gs.diagnostic_texture_precision_=modes;
    }
    template<class T> static void prepare_read(T&){}
    // Staging copies the destination, including warm host translation hints.
    // The archive replaces the TLB, so none of those hints may survive it.
    static void prepare_read(Memory& memory){memory.reset_tlb_cache();}
    static void prepare_read(GsVram& v){
        v.detach_gpu();v.pending_gpu_.clear();v.gpu_.reset();v.gpu_primitives_=v.cpu_fallbacks_=0;
        v.locations_={};v.pending_pages_={};v.knowledge_cached_={};v.fallback_reasons_={};
    }
    static void prepare_read(VuControl& v){v.invalidate_vu1_aot();v.vu1_aot_stats_={};v.vu1_capture_={};}
    static void prepare_read(Iop& i){i.pad_poll_source_={};}
    template<class T> static void prepare_read(DiagnosticHistory<T>& history){history.sink_=nullptr;}
    template<class T> static void validate(const T&){}
    static void demand(bool condition){if(!condition)throw std::runtime_error("checkpoint modeled state has invalid storage or queue bounds");}
    template<class T> static void validate(const DiagnosticHistory<T>& h){demand((!h.limit_ || h.data_.size()<=h.limit_) && (h.next_==0 || (h.next_<h.data_.size() && h.data_.size()==h.limit_)));}
    static void validate(const Memory& m){demand(m.ram_.size()==Memory::kMainRamBytes && m.scratchpad_.size()==Memory::kScratchpadBytes);}
    static void validate(const Iop& i){demand(i.ram_.size()==kIopRamBytes && i.ram_known_.size()==kIopRamBytes && (i.spu2_ram_.empty() || i.spu2_ram_.size()==0x100000u) && i.spu2_ram_written_.size()==i.spu2_ram_.size());for(const auto& core:i.spu2_voices_)for(const auto& v:core)demand(v.phase<=4 && v.next<0x100000u && (!v.loop || (*v.loop<0x100000u && !(*v.loop&7u))) && v.envelope>=0 && v.envelope<=32767 && v.previous>=-32768 && v.previous<=32767 && v.older>=-32768 && v.older<=32767 && v.output>=-32768 && v.output<=32767 && v.envelope_counter<0x8000u && v.write>=v.read && v.write-v.read<=32 && v.pitch_counter<4096u);}
    static void validate(const GsVram& v){demand(v.words_.size()==GsVram::kBytes/4 && v.known_.size()==v.words_.size());}
    static void validate(const GsRegisters& g){demand(g.triangle_vertices_count_<=g.triangle_vertices_.size());}
    static void validate(const Gif& g){demand(g.path_>=1 && g.path_<=3 && g.nreg_<=16 && g.format_<=3 && (!g.in_packet_ || (g.nreg_ && (g.loops_left_ ? g.reg_index_<g.nreg_ : (!g.end_of_packet_ && g.reg_index_==0u)))));}
    static void validate(const Vif& v){demand(v.direct_words_<=4 && v.input_count_<=4 && v.input_index_<=v.input_count_ && v.fifo_.size()<=(v.unit_==Vif::Unit::Vu1?16u:8u));}
    static void validate(const VuControl& v){demand(v.data_.size()==1024 && v.micro0_.size()==1024 && v.micro1_.size()==4096 && v.data1_.size()==4096);for(const auto& m:v.micro_)demand(m.writes<=m.pending.size() && m.vector_writes<=m.vectors.size() && m.flag_writes<=m.flags.size() && m.store_writes<=m.stores.size());}
    static void bind(Memory& memory,EEState& state,bool clock,bool stalls) {
        memory.set_clock(clock ? &state.retired_instructions : nullptr,stalls ? &state.stall_cycles : nullptr);
        memory.registers_.attach_ee_ram(memory.ram_,&memory.unknown_words_,memory.scratchpad_);
        memory.observer_=nullptr;
    }
    static bool clock(const Memory& m){return m.cache_clock_!=nullptr;}
    static bool stalls(const Memory& m){return m.registers_.stall_clock_!=nullptr;}
#ifndef PS2N_CHECKPOINT_SAVE_ONLY
    static void own_strings(Memory& m,std::vector<std::shared_ptr<const std::string>> strings){m.checkpoint_origins_=std::move(strings);}
#endif
    static constexpr std::size_t cache_clock_offset(){return offsetof(Memory,cache_clock_);}
};
#ifdef PS2N_CHECKPOINT_SAVE_ONLY
extern "C" std::size_t ps2n_checkpoint_cache_clock_offset(){return CheckpointAccess::cache_clock_offset();}
#endif

namespace checkpoint_detail {
template<bool Reading> class Archive {
public:
    static constexpr bool reading=Reading;
    bool legacy_scalar_flags{};
    bool legacy_spu2{};
    bool legacy_vertex_st{};
    bool legacy_from_spr{};
    bool legacy_voice_readback{};
    bool legacy_vif_payload{};
    bool legacy_micro_bounds{};
    explicit Archive(std::span<const std::uint8_t> input={}):input_(input){}
    std::vector<std::uint8_t> output;
    std::vector<std::shared_ptr<const std::string>> strings;
    std::size_t position{};
    std::size_t remaining()const {return input_.size()-position;}
    template<class... T> void operator()(T&... values){(value(values),...);}
    void raw(void* data,std::size_t bytes) {
        if(!bytes)return;
        if constexpr(Reading) {
            if(bytes>remaining())throw std::runtime_error("checkpoint payload is truncated");
            std::memcpy(data,input_.data()+position,bytes);position+=bytes;
        } else {
            if(bytes>maximum_bytes-output.size())throw std::runtime_error("checkpoint exceeds the 128 MiB state bound");
            const auto* first=static_cast<const std::uint8_t*>(data);output.insert(output.end(),first,first+bytes);
        }
    }
    std::uint64_t count(std::uint64_t actual) {
        value(actual);
        if constexpr(Reading)if(actual>remaining() || actual>maximum_bytes)throw std::runtime_error("checkpoint container length is invalid");
        return actual;
    }
    template<class T> void value(T& v) {
        using U=std::remove_cv_t<T>;
        if constexpr(std::is_same_v<U,bool>) {
            std::uint8_t n=v ? 1u : 0u;scalar(n);
            if constexpr(Reading){if(n>1u)throw std::runtime_error("checkpoint boolean is invalid");v=n!=0;}
        } else if constexpr(std::is_integral_v<U>)scalar(v);
        else if constexpr(std::is_enum_v<U>) {
            auto n=static_cast<std::underlying_type_t<U>>(v);value(n);if constexpr(Reading)v=static_cast<U>(n);
        } else if constexpr(std::is_same_v<U,std::string>) {
            const auto n=count(v.size());if constexpr(Reading){claim(n,1);v.resize(static_cast<std::size_t>(n));}raw(v.data(),static_cast<std::size_t>(n));
        } else if constexpr(std::is_same_v<U,std::string_view>) {
            static_assert(!Reading,"immutable host string views are bindings, not restored state");
            auto text=std::string(v);value(text);
        } else if constexpr(std::is_same_v<U,const char*>) {
            bool present=v!=nullptr;value(present);
            if(present) {
                std::string text;if constexpr(!Reading)text=v;value(text);
                if constexpr(Reading){auto owner=std::make_shared<const std::string>(std::move(text));v=owner->c_str();strings.push_back(std::move(owner));}
            } else if constexpr(Reading)v=nullptr;
        } else if constexpr(Optional<U>::value) {
            bool present=v.has_value();value(present);
            if constexpr(Reading){if(present && !v)v.emplace(empty_value<typename Optional<U>::element>());if(!present)v.reset();}
            if(present)value(*v);
        } else if constexpr(Shared<U>::value)shared(v);
        else if constexpr(Array<U>::value)sequence(v);
        else if constexpr(Vector<U>::value) {
            const auto n=count(v.size());if constexpr(Reading){claim(n,sizeof(typename Vector<U>::element));v.resize(static_cast<std::size_t>(n));}
            if constexpr(std::is_same_v<typename Vector<U>::element,bool>) {
                for(std::size_t i=0;i<v.size();++i){bool b=v[i];value(b);if constexpr(Reading)v[i]=b;}
            } else sequence(v);
        } else if constexpr(Deque<U>::value) {
            const auto n=count(v.size());
            if constexpr(Reading){claim(n,sizeof(typename Deque<U>::element));v.clear();for(std::uint64_t i=0;i<n;++i){auto item=empty_value<typename Deque<U>::element>();value(item);v.push_back(std::move(item));}}
            else for(auto& item:v)value(item);
        } else if constexpr(Map<U>::value)mapping(v);
        else if constexpr(Pair<U>::value){value(v.first);value(v.second);}
        else {
            if constexpr(Reading)CheckpointAccess::prepare_read(v);
            else if constexpr(std::is_same_v<U,GsVram>)v.synchronize();
            CheckpointAccess::fields(*this,v);if constexpr(Reading)CheckpointAccess::validate(v);
        }
    }
private:
    void claim(std::uint64_t n,std::uint64_t bytes) {
        if(n>allocation_remaining_/bytes)throw std::runtime_error("checkpoint allocation budget exceeded");
        allocation_remaining_-=n*bytes;
    }
    template<class T> void scalar(T& v) {
        using U=std::make_unsigned_t<std::remove_cv_t<T>>;
        U n=static_cast<U>(v);
        std::array<std::uint8_t,sizeof(T)> bytes{};
        if constexpr(!Reading)for(unsigned i=0;i<sizeof(T);++i)bytes[i]=static_cast<std::uint8_t>(n>>(i*8u));
        raw(bytes.data(),bytes.size());
        if constexpr(Reading){n=0;for(unsigned i=0;i<sizeof(T);++i)n|=static_cast<U>(bytes[i])<<(i*8u);v=static_cast<T>(n);}
    }
    template<class C> void sequence(C& v) {
        using E=typename C::value_type;
        if constexpr(std::is_integral_v<E> && !std::is_same_v<E,bool> && std::endian::native==std::endian::little)raw(v.data(),v.size()*sizeof(E));
        else for(auto& item:v)value(item);
    }
    template<class C> void mapping(C& v) {
        using K=typename Map<C>::key;using V=typename Map<C>::mapped;
        const auto n=count(v.size());
        if constexpr(Reading){claim(n,sizeof(K)+sizeof(V)+32);v.clear();for(std::uint64_t i=0;i<n;++i){K key{};auto item=empty_value<V>();value(key);value(item);if(!v.emplace(std::move(key),std::move(item)).second)throw std::runtime_error("checkpoint map has duplicate keys");}}
        else {
            std::vector<K> keys;keys.reserve(v.size());for(const auto& entry:v)keys.push_back(entry.first);std::sort(keys.begin(),keys.end());
            for(auto& key:keys){value(key);value(v.at(key));}
        }
    }
    struct Object {std::shared_ptr<void> pointer;std::type_index type{typeid(void)};};
    template<class P> void shared(P& v) {
        using E=typename Shared<P>::element;using Mutable=std::remove_const_t<E>;
        if constexpr(Reading) {
            std::uint64_t id{};value(id);if(!id){v.reset();return;}
            const auto found=objects_.find(id);
            if(found!=objects_.end()) {
                if(found->second.type!=std::type_index(typeid(Mutable)))throw std::runtime_error("checkpoint shared domain type mismatch");
                v=std::static_pointer_cast<E>(found->second.pointer);return;
            }
            if(id!=objects_.size()+1)throw std::runtime_error("checkpoint shared domain reference is invalid");
            auto owner=std::make_shared<Mutable>();objects_.emplace(id,Object{owner,std::type_index(typeid(Mutable))});value(*owner);v=std::move(owner);
        } else {
            if(!v){std::uint64_t zero=0;value(zero);return;}
            const auto [it,added]=references_.emplace(v.get(),references_.size()+1);
            auto id=it->second;value(id);if(added)value(const_cast<Mutable&>(*v));
        }
    }
    std::span<const std::uint8_t> input_;
    std::uint64_t allocation_remaining_{maximum_bytes*2};
    std::unordered_map<const void*,std::uint64_t> references_;
    std::unordered_map<std::uint64_t,Object> objects_;
};
using Writer=Archive<false>;using Reader=Archive<true>;
bool digest_valid(std::string_view value){return value.size()==64 && std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
void validate_context(const CheckpointContext& c) {
    if(!digest_valid(c.scenario_sha256))throw std::runtime_error("checkpoint needs an explicit lowercase SHA-256 scenario identity");
    if(c.host_metadata.size()>65536)throw std::runtime_error("checkpoint host metadata exceeds 64 KiB");
}
void metadata(auto& a,CheckpointInfo& i){a(i.schema_sha256,i.runtime_sha256,i.bindings_sha256,i.scenario_sha256,i.host_metadata,i.payload_sha256,i.retired,i.cycles,i.payload_bytes,i.pc);}
struct Parsed {CheckpointInfo info;std::vector<std::uint8_t> file;std::size_t payload{};};
Parsed parse(const std::filesystem::path& path) {
    if(regular_file_size(path)>maximum_bytes)throw std::runtime_error("checkpoint file exceeds the 128 MiB bound");
    Parsed p;p.file=read_file(path);
    constexpr std::string_view magic="PS2NCP01";
    if(p.file.size()<8 || std::memcmp(p.file.data(),magic.data(),8))throw std::runtime_error("not a ps2-native complete checkpoint");
    Reader a(std::span<const std::uint8_t>(p.file).subspan(8));metadata(a,p.info);p.payload=8+a.position;
    if(p.info.payload_bytes!=p.file.size()-p.payload || p.info.host_metadata.size()>65536 || !digest_valid(p.info.schema_sha256) || !digest_valid(p.info.runtime_sha256) || !digest_valid(p.info.bindings_sha256) || !digest_valid(p.info.scenario_sha256))
        throw std::runtime_error("checkpoint envelope is invalid");
    if(sha256_hex(std::span<const std::uint8_t>(p.file).subspan(p.payload))!=p.info.payload_sha256)throw std::runtime_error("checkpoint payload checksum mismatch");
    return p;
}
}

std::string CheckpointAccess::bindings(const Memory& m,std::string_view expected_disc) {
    checkpoint_detail::Writer a;
    auto rom=sha256_hex(m.rom_),rom1=sha256_hex(m.rom1_);
    auto irom=sha256_hex(m.registers_.iop_.rom_),irom1=sha256_hex(m.registers_.iop_.rom1_);
    a(rom,rom1,irom,irom1);
    for(const auto* hardware:{m.registers_.hardware_,m.registers_.iop_.hardware_}) {
        bool present=hardware!=nullptr;a(present);if(hardware)a(const_cast<ConsoleHardware&>(*hardware));
    }
    const auto* nvm=m.registers_.iop_.nvm_;bool has_nvm=nvm!=nullptr;a(has_nvm);
    if(nvm){auto hash=sha256_hex(nvm->bytes);auto layout=nvm->layout;auto name=nvm->model_name;a(hash,layout,name);}
    const auto& disc=m.registers_.iop_.disc_;bool has_disc=disc!=nullptr;a(has_disc);
    if(disc){
        if(!checkpoint_detail::digest_valid(expected_disc))throw std::runtime_error("checkpoint requires the pinned SHA-256 of the mounted disc");
        auto hash=sha256_file(disc->file_.path());if(hash!=expected_disc)throw std::runtime_error("checkpoint mounted disc changed from its pinned identity");
        auto size=disc->image_size();a(hash,size);
    }
    if(m.registers_.iop_.rtc_epoch_) {
        // Preserve prior binding hashes when no RTC input was supplied.
        // A provided epoch is immutable and must be supplied again on load;
        // the restored IOP cycle determines all subsequent calendar ticks.
        std::string tag="RTC-UTC-at-IOP-reset-v1";auto epoch=*m.registers_.iop_.rtc_epoch_;a(tag,epoch);
    }
    return sha256_hex(a.output);
}
void CheckpointAccess::validate_boundary(const EEState& s,const Memory& m) {
    if(m.observer_)throw std::runtime_error("checkpoint requires a returned execution boundary, without an active bus observer");
    if((m.cache_clock_ && m.cache_clock_!=&s.retired_instructions) ||
       m.registers_.clock_!=m.cache_clock_ || (m.registers_.stall_clock_ && m.registers_.stall_clock_!=&s.stall_cycles))
        throw std::runtime_error("checkpoint has an unbound or foreign EE clock");
    if(m.registers_.iop_.ee_ram_.data()!=m.ram_.data() || m.registers_.iop_.ee_ram_unknown_!=&m.unknown_words_)
        throw std::runtime_error("checkpoint has foreign DMA RAM bindings");
}
std::string checkpoint_state_sha256(const EEState& state,const Memory& memory) {
    checkpoint_detail::Writer a;auto clock=CheckpointAccess::clock(memory),stalls=CheckpointAccess::stalls(memory);
    a(clock,stalls,const_cast<EEState&>(state),const_cast<Memory&>(memory));return sha256_hex(a.output);
}
void refine_checkpoint_scalar_carry(const EEState& state,Memory& memory) {
    CheckpointAccess::validate_boundary(state,memory);CheckpointAccess::refine_scalar(state,memory);
}
void refine_checkpoint_texture_precision(const EEState& state,Memory& memory,unsigned modes) {
    CheckpointAccess::validate_boundary(state,memory);
    CheckpointAccess::refine_texture(memory,modes);
}
void save_checkpoint(const std::filesystem::path& path,const EEState& state,const Memory& memory,
                     const BootReport& boundary,const CheckpointContext& context) {
    using namespace checkpoint_detail;validate_context(context);CheckpointAccess::validate_boundary(state,memory);
    if((boundary.outcome!=BootOutcome::BudgetExhausted && boundary.outcome!=BootOutcome::StopAddress) || boundary.pc!=state.pc)throw std::runtime_error("refused/partial instructions are not checkpoint boundaries; keep the previous successful checkpoint");
    if(iop_execution_refused(memory))
        throw std::runtime_error("IOP refusal is not a checkpoint boundary; keep the previous successful checkpoint: "+*memory.iop().stop_reason());
    Writer payload;auto clock=CheckpointAccess::clock(memory),stalls=CheckpointAccess::stalls(memory);
    payload(clock,stalls,const_cast<EEState&>(state),const_cast<Memory&>(memory));
    CheckpointInfo info{PS2N_CHECKPOINT_SCHEMA,PS2N_CHECKPOINT_RUNTIME,CheckpointAccess::bindings(memory,context.expected_disc_sha256),context.scenario_sha256,context.host_metadata,sha256_hex(payload.output),state.retired_instructions,state.cycles(),payload.output.size(),state.pc};
    Writer file;const char magic[]="PS2NCP01";file.raw(const_cast<char*>(magic),8);metadata(file,info);file.raw(payload.output.data(),payload.output.size());
    if(file_exists(path)) {
        InputFile previous(path);std::array<std::uint8_t,8> prefix{};
        if(previous.read_at(0,prefix)!=8 || std::memcmp(prefix.data(),magic,8))throw std::runtime_error("checkpoint output must not overwrite existing non-checkpoint user data");
        if(inspect_checkpoint(path).scenario_sha256!=context.scenario_sha256)throw std::runtime_error("checkpoint output belongs to a different scenario");
    }
    write_file_atomically(path,file.output);
}
CheckpointInfo inspect_checkpoint(const std::filesystem::path& path){return checkpoint_detail::parse(path).info;}
#ifndef PS2N_CHECKPOINT_SAVE_ONLY
CheckpointInfo load_checkpoint(const std::filesystem::path& path,EEState& state,Memory& memory,const CheckpointContext& context) {
    using namespace checkpoint_detail;validate_context(context);CheckpointAccess::validate_boundary(state,memory);auto parsed=parse(path);const auto& info=parsed.info;
    // Audited byte-identical v1 transition: execution-only ee_semantics.hpp
    // moved from the state fingerprint to the runtime fingerprint. Pin both
    // ends so a future state-field change cannot silently reuse this bridge.
    const bool reviewed_v1=context.allow_runtime_change &&
        info.schema_sha256=="9199cae351d7ea3f43526fa1dd800d9f6810d80f6c0370974f190695aee3e2cc" &&
        std::string_view(PS2N_CHECKPOINT_SCHEMA)=="771b628039a2b4c73a7c836b022c56d167cf8cd22310d8fe75803cb7934d3f88";
    // The decoder-only MADD extension added no serialized fields. Moving
    // mips.hpp to the runtime fingerprint retains the audited scalar bridge;
    // both destination fingerprints stay pinned against future field changes.
    const bool reviewed_scalar_transition=context.allow_runtime_change &&
        (std::string_view(PS2N_CHECKPOINT_SCHEMA)=="72d8f630ea9a0befdba0e33bbc9d80394b6a2c29db31cf0a402bd6299876a61e" ||
         std::string_view(PS2N_CHECKPOINT_SCHEMA)=="4e87d58b722e40020caec94e9928c087e3f9d86c7e33f5116a76628648e4dc4f" ||
         std::string_view(PS2N_CHECKPOINT_SCHEMA)=="830377c10823d2223579db00dc995da1a05a0fcc433a85be4014e50ea96ac772" ||
         std::string_view(PS2N_CHECKPOINT_SCHEMA)=="3613da6d23479169c5d84e8eea6a68ed75b7acc10e86c5c324238a5cbccf495f" ||
         // SQC2/LQC2 now retain existing numeric metadata. The new methods
         // add no fields to VU, memory or the checkpoint wire format.
         std::string_view(PS2N_CHECKPOINT_SCHEMA)=="06f3fe6fbdb88d6864fb5e3b18139b95e13d22312976051c0cbcd9406931d695" ||
         // Host profiling adds no hardware fields; boot options/reports now
         // participate only in the runtime fingerprint, never the wire schema.
         std::string_view(PS2N_CHECKPOINT_SCHEMA)=="b32d8b0d82c1d14b0c3f8e5fb8912a2a8495f113256e122b4e924fd34f4e10e2" ||
         std::string_view(PS2N_CHECKPOINT_SCHEMA)=="4d32f9c6e62746ff39398d75d0efb1ae80a7a653a7abb8fb4f4c3a6933ede047" ||
         std::string_view(PS2N_CHECKPOINT_SCHEMA)=="c649550347ab1c003a8b8e441e3241a8917c893acb09a3a5262c2ad2a6fc2ee6" ||
         std::string_view(PS2N_CHECKPOINT_SCHEMA)=="5662ca9c89ac26516224d7aa8f3e777e8d72ec24255364cfedd3481e13db1882" ||
         std::string_view(PS2N_CHECKPOINT_SCHEMA)=="fe6e8c07cc00203aa30906e155d9511c731237aa3919857a36a7098ea4409051" ||
         std::string_view(PS2N_CHECKPOINT_SCHEMA)=="85deaf45287f093ffa1045d13df0e972f5a47193781fadac5e1b73d8b2936111" ||
         std::string_view(PS2N_CHECKPOINT_SCHEMA)=="b36ce4ca5c0bf510d302b10b002b27d736cb1fbc4e258e8a2b8961950e2d18e3");
    const bool legacy_scalar_flags=reviewed_scalar_transition &&
        (info.schema_sha256=="9199cae351d7ea3f43526fa1dd800d9f6810d80f6c0370974f190695aee3e2cc" ||
         info.schema_sha256=="771b628039a2b4c73a7c836b022c56d167cf8cd22310d8fe75803cb7934d3f88");
    const bool same_scalar_fields=reviewed_scalar_transition &&
        (info.schema_sha256=="4553168c0bd7fd198259cc9a8f5ccc7572a3b789e7dca3495ac548162a6768da" ||
         info.schema_sha256=="72d8f630ea9a0befdba0e33bbc9d80394b6a2c29db31cf0a402bd6299876a61e" ||
         info.schema_sha256=="cd1aa8bf7755b3926af94afc8385147b755b5de6b335faa25049d9f719960ecd" ||
         info.schema_sha256=="4e87d58b722e40020caec94e9928c087e3f9d86c7e33f5116a76628648e4dc4f" ||
         info.schema_sha256=="830377c10823d2223579db00dc995da1a05a0fcc433a85be4014e50ea96ac772" ||
         info.schema_sha256=="3d2c143346ea6328bb9cb49c48abfc09bfe426f4cabb88a2051625384ed54b1f" ||
         info.schema_sha256=="3613da6d23479169c5d84e8eea6a68ed75b7acc10e86c5c324238a5cbccf495f" ||
         info.schema_sha256=="06f3fe6fbdb88d6864fb5e3b18139b95e13d22312976051c0cbcd9406931d695");
    // v1 has no numerical voices. Preserve its abstract zero-gain/release
    // proofs verbatim; no active decoder history is reconstructed or guessed.
    const bool bounds_transition=std::string_view(PS2N_CHECKPOINT_SCHEMA)=="b36ce4ca5c0bf510d302b10b002b27d736cb1fbc4e258e8a2b8961950e2d18e3";
    const bool vif_transition=bounds_transition || std::string_view(PS2N_CHECKPOINT_SCHEMA)=="85deaf45287f093ffa1045d13df0e972f5a47193781fadac5e1b73d8b2936111";
    const bool voice_transition=std::string_view(PS2N_CHECKPOINT_SCHEMA)=="fe6e8c07cc00203aa30906e155d9511c731237aa3919857a36a7098ea4409051" || vif_transition;
    const bool spr_transition=std::string_view(PS2N_CHECKPOINT_SCHEMA)=="5662ca9c89ac26516224d7aa8f3e777e8d72ec24255364cfedd3481e13db1882" || voice_transition;
    const bool st_transition=std::string_view(PS2N_CHECKPOINT_SCHEMA)=="c649550347ab1c003a8b8e441e3241a8917c893acb09a3a5262c2ad2a6fc2ee6" || spr_transition;
    const bool synthesis_transition=std::string_view(PS2N_CHECKPOINT_SCHEMA)=="4d32f9c6e62746ff39398d75d0efb1ae80a7a653a7abb8fb4f4c3a6933ede047" || st_transition;
    const bool legacy_spu2=context.allow_runtime_change &&
        ((synthesis_transition && info.schema_sha256=="b32d8b0d82c1d14b0c3f8e5fb8912a2a8495f113256e122b4e924fd34f4e10e2") ||
         reviewed_v1 || legacy_scalar_flags || same_scalar_fields);
    const bool same_spu2_fields=context.allow_runtime_change && synthesis_transition &&
        (info.schema_sha256=="6a3a20754386bdabd4cc27966faaf222a335b6b7111cab9aa951b06a3739d098" ||
         info.schema_sha256=="efced2750855b215c91be106204dd31881755498213aed8c78b38806f5ac2e35" ||
         info.schema_sha256=="baa51041ae4e48bb415e3af985bca1f2451485a3ebd5db633d7e8c45e6b83781");
    // The old Vertex codec did not retain ST uncertainty. Every old field
    // is reserialized and checked below; retained ST is conservatively
    // unknown, rather than inventing certainty for a newly supported path.
    const bool legacy_vertex_st=context.allow_runtime_change && st_transition &&
        (info.schema_sha256=="4d32f9c6e62746ff39398d75d0efb1ae80a7a653a7abb8fb4f4c3a6933ede047" ||
         reviewed_v1 || legacy_scalar_flags || same_scalar_fields || legacy_spu2 || same_spu2_fields);
    // Older engines could not start ch-8. Retain every old field and use its
    // idle EE bus anchor for the newly introduced source; verify old bytes below.
    const bool legacy_from_spr=context.allow_runtime_change && spr_transition &&
        (info.schema_sha256=="c649550347ab1c003a8b8e441e3241a8917c893acb09a3a5262c2ad2a6fc2ee6" ||
         reviewed_v1 || legacy_scalar_flags || same_scalar_fields || legacy_spu2 || same_spu2_fields || legacy_vertex_st);
    const bool legacy_voice_readback=context.allow_runtime_change && voice_transition &&
        (info.schema_sha256=="5662ca9c89ac26516224d7aa8f3e777e8d72ec24255364cfedd3481e13db1882" || legacy_from_spr);
    const bool legacy_vif_payload=context.allow_runtime_change && vif_transition &&
        (info.schema_sha256=="fe6e8c07cc00203aa30906e155d9511c731237aa3919857a36a7098ea4409051" || legacy_voice_readback);
    const bool same_micro_bounds=context.allow_runtime_change && bounds_transition && info.schema_sha256=="fbaf0fe92b5a983a351765c0af5dd263161c6e4a62fa18eae651d1f365c16460";
    const bool legacy_micro_bounds=context.allow_runtime_change && bounds_transition &&
        (info.schema_sha256=="85deaf45287f093ffa1045d13df0e972f5a47193781fadac5e1b73d8b2936111" || legacy_vif_payload);
    if(info.schema_sha256!=PS2N_CHECKPOINT_SCHEMA && !reviewed_v1 && !legacy_scalar_flags && !same_scalar_fields && !legacy_spu2 && !same_spu2_fields && !legacy_vertex_st && !legacy_from_spr && !legacy_voice_readback && !legacy_vif_payload && !legacy_micro_bounds && !same_micro_bounds)
        throw std::runtime_error("checkpoint state schema differs; no partial/guessed migration is permitted");
    if(info.runtime_sha256!=PS2N_CHECKPOINT_RUNTIME && !context.allow_runtime_change)throw std::runtime_error("checkpoint runtime changed; explicit reviewed allow-runtime-change is required");
    if(info.scenario_sha256!=context.scenario_sha256)throw std::runtime_error("checkpoint scenario identity differs");
    if(info.bindings_sha256!=CheckpointAccess::bindings(memory,context.expected_disc_sha256))throw std::runtime_error("checkpoint firmware/profile/NVM/disc identity differs");
    EEState staged_state;Memory staged_memory=memory;
    Reader a(std::span<const std::uint8_t>(parsed.file).subspan(parsed.payload));a.legacy_scalar_flags=legacy_scalar_flags;a.legacy_spu2=legacy_spu2;a.legacy_vertex_st=legacy_vertex_st;
    a.legacy_from_spr=legacy_from_spr;a.legacy_voice_readback=legacy_voice_readback;a.legacy_vif_payload=legacy_vif_payload;a.legacy_micro_bounds=legacy_micro_bounds;
    bool clock{},stalls{};a(clock,stalls,staged_state,staged_memory);
    if(a.remaining() || staged_state.retired_instructions!=info.retired || staged_state.pc!=info.pc || staged_state.cycles()!=info.cycles)
        throw std::runtime_error("checkpoint state/envelope disagree");
    if(CheckpointAccess::bindings(staged_memory,context.expected_disc_sha256)!=info.bindings_sha256)throw std::runtime_error("checkpoint changed immutable firmware bindings");
    if(iop_execution_refused(staged_memory))
        throw std::runtime_error("checkpoint contains an IOP refusal; inspect it without resuming partial execution: "+*staged_memory.iop().stop_reason());
    if(legacy_scalar_flags || legacy_spu2 || legacy_vertex_st || legacy_from_spr || legacy_voice_readback || legacy_vif_payload || legacy_micro_bounds) {
        Writer original;original.legacy_scalar_flags=legacy_scalar_flags;original.legacy_spu2=legacy_spu2;original.legacy_vertex_st=legacy_vertex_st;original.legacy_from_spr=legacy_from_spr;original.legacy_voice_readback=legacy_voice_readback;original.legacy_vif_payload=legacy_vif_payload;original.legacy_micro_bounds=legacy_micro_bounds;original(clock,stalls,staged_state,staged_memory);
        if(sha256_hex(original.output)!=info.payload_sha256)throw std::runtime_error("checkpoint scalar migration changed legacy fields");
    }
    CheckpointAccess::own_strings(staged_memory,std::move(a.strings));
    state=std::move(staged_state);memory=std::move(staged_memory);CheckpointAccess::bind(memory,state,clock,stalls);
    return info;
}
#endif
} // namespace ps2native
