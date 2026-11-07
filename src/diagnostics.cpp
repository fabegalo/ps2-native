#include "ps2native/diagnostics.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"
#include "ps2native/control_flow.hpp"
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <charconv>

namespace ps2native {
std::string json_string(std::string_view value) {
    std::string out = "\"";
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += digits[c >> 4]; out += digits[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
namespace {
std::string hex(std::uint64_t n) {
    std::ostringstream out; out << "0x" << std::hex << n; return out.str();
}
template<class Range> void numbers(std::ostream& out, const Range& values) {
    out << '['; bool comma = false;
    for (auto n : values) { if (comma) out << ','; comma = true; out << n; }
    out << ']';
}
void atomic_text(const std::filesystem::path& path, const std::string& text) {
    write_file_atomically(path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}
// Fixed-size event records avoid formatting/allocation on every instruction.
struct Event {
    enum class Kind { Instruction, Bus, System } kind{};
    std::uint64_t sequence{}, cycle{}, a{}, b{}, c{}, d{};
    std::uint32_t pc{}, word{}, address{}, physical{};
    unsigned bytes{}, region{};
    bool write{};
    std::string text;
};
std::string encode(const Event& e) {
    std::ostringstream out;
    out << "{\"event\":" << e.sequence << ",\"cycle\":" << e.cycle << ",\"pc\":" << e.pc;
    if (e.kind == Event::Kind::Instruction)
        out << ",\"kind\":\"instruction-attempt\",\"word\":" << e.word
            << ",\"rs\":" << e.a << ",\"rs_unknown\":" << e.b
            << ",\"rt\":" << e.c << ",\"rt_unknown\":" << e.d;
    else if (e.kind == Event::Kind::Bus)
        out << ",\"kind\":\"bus\",\"write\":" << (e.write ? "true" : "false")
            << ",\"bytes\":" << e.bytes << ",\"address\":" << e.address << ",\"physical\":" << e.physical
            << ",\"region\":" << json_string(bus_region_name(static_cast<BusRegion>(e.region)))
            << ",\"value\":" << e.a << ",\"unknown\":" << e.b;
    else out << ",\"kind\":\"system\",\"text\":" << json_string(e.text);
    return out.str() + '}';
}
constexpr std::string_view stream_header = "{\"schema\":\"ps2n-events-v1\",\"scope\":\"complete\"}";
class JsonSyntax {
public:
    explicit JsonSyntax(std::string_view text) : text_(text) {}
    bool valid() { return value(0) && position_==text_.size(); }
private:
    bool consume(char c) { if(position_==text_.size() || text_[position_]!=c)return false;++position_;return true; }
    bool string() {
        if(!consume('"'))return false;
        while(position_<text_.size()) {
            const auto c=static_cast<unsigned char>(text_[position_++]);if(c=='"')return true;if(c<32)return false;
            if(c=='\\') {
                if(position_==text_.size())return false;const auto escape=text_[position_++];
                if(escape=='u') {for(unsigned i=0;i<4;++i){if(position_==text_.size())return false;const auto h=text_[position_++];if(!((h>='0'&&h<='9')||(h>='a'&&h<='f')||(h>='A'&&h<='F')))return false;}}
                else if(std::string_view("\"\\/bfnrt").find(escape)==std::string_view::npos)return false;
            }
        }
        return false;
    }
    bool value(unsigned depth) {
        if(depth>32 || position_==text_.size())return false;
        if(text_[position_]=='"')return string();
        if(consume('{')) {
            if(consume('}'))return true;
            do{if(!string()||!consume(':')||!value(depth+1))return false;}while(consume(','));return consume('}');
        }
        if(consume('[')) {if(consume(']'))return true;do{if(!value(depth+1))return false;}while(consume(','));return consume(']');}
        for(const auto word:{"true","false","null"})if(text_.substr(position_).starts_with(word)){position_+=std::char_traits<char>::length(word);return true;}
        (void)consume('-');const auto begin=position_;
        if(!consume('0'))while(position_<text_.size() && text_[position_]>='0' && text_[position_]<='9')++position_;
        if(position_==begin)return false;
        if(consume('.')){const auto start=position_;while(position_<text_.size()&&text_[position_]>='0'&&text_[position_]<='9')++position_;if(start==position_)return false;}
        if(consume('e')||consume('E')){if(!consume('+'))(void)consume('-');const auto start=position_;while(position_<text_.size()&&text_[position_]>='0'&&text_[position_]<='9')++position_;if(start==position_)return false;}
        return true;
    }
    std::string_view text_;std::size_t position_{};
};
std::uint64_t record_number(std::string_view line,std::string_view prefix) {
    if(!line.starts_with(prefix))throw std::runtime_error("invalid diagnostic record");
    line.remove_prefix(prefix.size());std::uint64_t n{};
    const auto result=std::from_chars(line.data(),line.data()+line.size(),n);
    if(result.ec!=std::errc{} || result.ptr==line.data() || result.ptr==line.data()+line.size() || (*result.ptr!=',' && *result.ptr!='}'))
        throw std::runtime_error("invalid diagnostic sequence");
    return n;
}
bool read_record(std::istream& input,std::string& line) {
    std::array<char,65538> buffer;input.getline(buffer.data(),static_cast<std::streamsize>(buffer.size()));
    const auto n=input.gcount();
    if(input.bad() || (input.fail()&&!input.eof()))throw std::runtime_error("diagnostic record exceeds 64 KiB or cannot be read");
    if(n==0 && input.eof())return false;
    const auto length=static_cast<std::size_t>(n)-(input.eof() ? 0u : 1u);
    if(length>65536)throw std::runtime_error("diagnostic record exceeds 64 KiB");
    line.assign(buffer.data(),length);return true;
}
}
struct DiagnosticRecorder::Impl {
    explicit Impl(std::size_t capacity) : ring(capacity) {
        if (capacity == 0 || capacity > 1'000'000) throw std::runtime_error("diagnostic ring must contain 1..1000000 events");
    }
    std::vector<Event> ring;
    std::size_t next{}, size{};
    std::uint64_t total{}, bytes{}, max_bytes{};
    std::ofstream output;
    bool truncated{};
    bool stream_error{};
    std::vector<std::pair<std::uint32_t,std::size_t>> ram;
    void line(const std::string& text) {
        if (!output.is_open() || truncated) return;
        if (bytes + text.size() + 65 > max_bytes) { truncated = true; return; }
        output << text << '\n'; bytes += text.size() + 1;
        if (!output) { stream_error = true; truncated = true; }
    }
    void add(Event e) {
        e.sequence = ++total;
        if (output.is_open()) line(encode(e));
        ring[next] = std::move(e); next = (next + 1) % ring.size();
        if (size < ring.size()) ++size;
    }
};
DiagnosticRecorder::DiagnosticRecorder(std::size_t capacity) : impl_(std::make_unique<Impl>(capacity)) {}
DiagnosticRecorder::~DiagnosticRecorder() = default;
std::uint64_t DiagnosticRecorder::total_events() const noexcept { return impl_->total; }
void DiagnosticRecorder::stream(const std::filesystem::path& path, std::uint64_t max_bytes) {
    if (impl_->total || impl_->output.is_open() || max_bytes < 1024) throw std::runtime_error("open diagnostic stream before execution (at least 1024 bytes)");
    ps2native::create_directories(path.parent_path());
    impl_->output.open(native_io_path(path), std::ios::binary | std::ios::trunc);
    if (!impl_->output) throw std::runtime_error("cannot open diagnostic event stream");
    impl_->max_bytes = max_bytes; impl_->line(std::string(stream_header));
}
void DiagnosticRecorder::instruction(const EEState& s, std::uint32_t pc, std::uint32_t word) {
    const auto rs = (word >> 21) & 31u, rt = (word >> 16) & 31u;
    Event e; e.kind = Event::Kind::Instruction; e.cycle = s.cycles(); e.pc = pc; e.word = word;
    e.a = s.gpr[rs].low; e.b = s.unknown_gpr_bits[rs]; e.c = s.gpr[rt].low; e.d = s.unknown_gpr_bits[rt];
    impl_->add(std::move(e));
}
void DiagnosticRecorder::bus(const EEState& s, const BusAccess& access) {
    Event e; e.kind = Event::Kind::Bus; e.cycle = s.cycles(); e.pc = s.instruction_pc;
    e.address = access.virtual_address; e.physical = access.physical_address; e.write = access.write;
    e.bytes = access.bytes; e.region = static_cast<unsigned>(access.region); e.a = access.value; e.b = access.unknown_bits;
    impl_->add(std::move(e));
}
void DiagnosticRecorder::system(const EEState& s, std::string_view event) {
    Event e; e.kind = Event::Kind::System; e.cycle = s.cycles(); e.pc = s.instruction_pc; e.text = event;
    impl_->add(std::move(e));
}
void DiagnosticRecorder::watch_ram(std::uint32_t address, std::size_t bytes) {
    if (!bytes || bytes > 4096 || address > Memory::kMainRamBytes || bytes > Memory::kMainRamBytes-address || impl_->ram.size() >= 16)
        throw std::runtime_error("RAM inspection requires up to 16 physical windows, each 1..4096 bytes inside EE RAM");
    impl_->ram.emplace_back(address,bytes);
}
void DiagnosticRecorder::write(const std::filesystem::path& directory, const BootReport& r,
                               const EEState& s, const Memory& m) {
    ps2native::create_directories(directory);
    std::ostringstream snapshot;
    bool comma = false;
    snapshot << "{\"schema\":\"ps2n-state-v1\",\"pc\":" << s.pc << ",\"cycles\":" << s.cycles()
             << ",\"delay_pending\":" << (s.delay_pending ? "true" : "false")
             << ",\"delay_target\":" << s.delay_target << ",\"gpr\":[";
    for (unsigned i = 0; i < 32; ++i) {
        if (i) snapshot << ',';
        snapshot << "{\"low\":" << s.gpr[i].low << ",\"high\":" << s.gpr[i].high
                 << ",\"unknown_low\":" << s.unknown_gpr_bits[i] << ",\"unknown_high\":" << s.unknown_gpr_high_bits[i] << '}';
    }
    snapshot << "],\"cop0\":"; numbers(snapshot, s.cop0);
    snapshot << ",\"cop0_count\":" << s.count() << ",\"fpr\":"; numbers(snapshot, s.fpr);
    snapshot << ",\"fpr_unknown\":"; numbers(snapshot, s.unknown_fpr_bits);
    snapshot << ",\"fcr31\":" << s.fcr31 << ",\"fpu_acc\":" << s.fpu_acc
             << ",\"lo\":" << s.lo << ",\"hi\":" << s.hi << ",\"lo1\":" << s.lo1 << ",\"hi1\":" << s.hi1
             << ",\"iop\":{\"pc\":" << m.iop().pc() << ",\"cycles\":" << m.iop().cycle() << ",\"gpr\":[";
    for (unsigned i = 0; i < 32; ++i) {
        if (i) snapshot << ','; const auto value = m.iop().diagnostic_gpr(i);
        snapshot << "{\"value\":" << value.value << ",\"unknown\":" << value.unknown << '}';
    }
    snapshot << "],\"cop0\":["; comma = false;
    for (const auto& value : m.iop().diagnostic_cop0()) { if (comma) snapshot << ','; comma = true; snapshot << (value ? std::to_string(*value) : "null"); }
    snapshot << "],\"istat_modeled_sources\":" << m.iop().diagnostic_istat()
             << ",\"sif0_fifo_words\":" << m.iop().diagnostic_sif0_fifo_words()
             << ",\"sif1_fifo_words\":" << m.iop().diagnostic_sif1_fifo_words()
             << ",\"ee_dma\":[";
    comma = false;
    for (const auto& channel : m.iop().diagnostic_ee_dma()) {
        if (comma) snapshot << ','; comma = true; snapshot << '['; bool field = false;
        for (const auto& value : channel) { if (field) snapshot << ','; field = true; snapshot << (value ? std::to_string(*value) : "null"); }
        snapshot << ']';
    }
    snapshot << "],\"stop\":" << (r.iop_stop ? json_string(*r.iop_stop) : "null") << "},\"vu0_vectors\":[";
    for (unsigned i = 0; i < 32; ++i) {
        if (i) snapshot << ',';
        const auto v = m.observe_vu_vector(i);
        snapshot << "{\"low\":" << v.low.value << ",\"high\":" << v.high.value
                 << ",\"unknown_low\":" << v.low.unknown << ",\"unknown_high\":" << v.high.unknown << '}';
    }
    snapshot << "],\"vif\":[";
    for (unsigned i = 0; i < 2; ++i) {
        if (i) snapshot << ','; const auto v = m.diagnostic_vif_status(i != 0);
        snapshot << "{\"value\":" << v.value << ",\"unknown\":" << v.unknown << '}';
    }
    snapshot << "],\"gs_pixel_writes\":" << m.gs().vram().pixel_writes()
             << ",\"gs_revision\":" << m.gs().vram().revision()
             << ",\"sif1_bytes\":" << m.iop().sif1_metrics().bytes
             << ",\"sif0_bytes\":" << m.iop().sif0_metrics().bytes << ",\"ram_windows\":[";
    comma = false;
    for (const auto& [address,bytes] : impl_->ram) {
        if (comma) snapshot << ','; comma = true;
        snapshot << "{\"address\":" << address << ",\"bytes\":[";
        for (std::size_t i = 0; i < bytes; ++i) {
            if (i) snapshot << ',';
            const auto value = m.diagnostic_ram(address + static_cast<std::uint32_t>(i),1);
            snapshot << "{\"value\":" << value.value << ",\"unknown\":" << value.unknown << '}';
        }
        snapshot << "]}";
    }
    snapshot << "],\"frame\":";
    try {
        const auto frame = m.gs().scanout(s.retired_instructions);
        save_framebuffer_bmp(directory / "frame.bmp",frame);
        snapshot << "{\"width\":" << frame.width << ",\"height\":" << frame.height
                 << ",\"rgba_sha256\":" << json_string(sha256_hex(frame.rgba)) << '}';
    } catch (const DeviceRefusal& e) { snapshot << "{\"unavailable\":" << json_string(e.what()) << '}'; }
    snapshot << "}\n";
    const auto state = snapshot.str();
    atomic_text(directory / "state.json", state);
    const auto state_hash = sha256_hex({reinterpret_cast<const std::uint8_t*>(state.data()),state.size()});
    const auto end = "{\"end\":true,\"total\":" + std::to_string(impl_->total) + ",\"pc\":" + std::to_string(r.pc) +
        ",\"outcome\":" + json_string(boot_outcome_name(r.outcome)) + ",\"reason\":" + json_string(r.reason) +
        ",\"state_sha256\":" + json_string(state_hash) + "}";
    if(impl_->output.is_open() && impl_->bytes+end.size()+1>impl_->max_bytes)impl_->truncated=true;
    const auto decoded = r.word ? try_decode_r5900(r.pc, *r.word) : std::nullopt;
    std::ostringstream summary;
    summary << "{\"schema\":\"ps2n-diagnostic-v1\",\"outcome\":" << json_string(boot_outcome_name(r.outcome))
            << ",\"category\":" << json_string(r.category) << ",\"reason\":" << json_string(r.reason)
            << ",\"pc\":" << r.pc << ",\"opcode\":" << (r.word ? std::to_string(*r.word) : "null")
            << ",\"disassembly\":" << (decoded ? json_string(mips_opcode_name(decoded->opcode)) : "null")
            << ",\"delay_slot\":" << (r.in_delay_slot ? "true" : "false")
            << ",\"retired\":" << r.retired << ",\"aot\":" << r.aot_instructions
            << ",\"interpreted\":" << r.interpreted_instructions << ",\"events_total\":" << impl_->total
            << ",\"events_retained\":" << impl_->size << ",\"stream_truncated\":" << (impl_->truncated ? "true" : "false")
            << ",\"state_sha256\":" << json_string(state_hash)
            << ",\"operand_registers\":[";
    if(r.word) {
        for(unsigned field=0;field<2;++field) {
            if(field)summary<<',';const auto index=(*r.word>>(field ? 16u : 21u))&31u;
            summary<<"{\"gpr\":"<<index<<",\"low\":"<<s.gpr[index].low<<",\"high\":"<<s.gpr[index].high
                   <<",\"unknown_low\":"<<s.unknown_gpr_bits[index]<<",\"unknown_high\":"<<s.unknown_gpr_high_bits[index]<<'}';
        }
    }
    summary<<"],\"inspection_scope\":\"EE/COP0/FPU, IOP registers, VU0 vectors, VIF, DMA/SIF/GS counters and explicit RAM windows; not a save-state\"}\n";
    atomic_text(directory / "report.json", summary.str());
    std::ostringstream human;
    human << boot_outcome_name(r.outcome) << " at " << hex(r.pc) << " after " << r.retired << " instructions\n"
          << r.category << ": " << r.reason << '\n';
    if (decoded) human << mips_opcode_name(decoded->opcode) << " rs=" << unsigned(decoded->rs)
                       << " rt=" << unsigned(decoded->rt) << " rd=" << unsigned(decoded->rd) << '\n';
    if(r.word) {
        human<<"opcode "<<hex(*r.word);
        for(const auto index:{(*r.word>>21u)&31u,(*r.word>>16u)&31u})
            human<<" r"<<index<<'='<<hex(s.gpr[index].low)<<"/unknown="<<hex(s.unknown_gpr_bits[index]);
        human<<'\n';
    }
    human << "IOP " << hex(r.iop_pc) << ": " << (r.iop_stop ? *r.iop_stop : "running")
          << "\nEvents retained " << impl_->size << '/' << impl_->total << "; diagnostic bundle is not a complete .ps2ncp checkpoint\n";
    atomic_text(directory / "report.txt", human.str());
    std::ostringstream events;
    const auto start = impl_->total - impl_->size;
    events << "{\"schema\":\"ps2n-events-v1\",\"scope\":\"ring\",\"discarded\":" << start << "}\n";
    const auto first = (impl_->next + impl_->ring.size() - impl_->size) % impl_->ring.size();
    for (std::size_t i = 0; i < impl_->size; ++i) events << encode(impl_->ring[(first + i) % impl_->ring.size()]) << '\n';
    // Final state is an observation too. Engine counters/timings are deliberately
    // outside semantic comparison, permitting AOT/interpreter comparisons.
    events << end << '\n'; atomic_text(directory / "events.ndjson", events.str());
    if (impl_->output.is_open()) {
        // A truncated stream is explicitly incomplete and cannot compare equal.
        impl_->output << (impl_->truncated ? "{\"truncated\":true}" : end) << '\n';
        impl_->output.close();
        if (!impl_->output || impl_->stream_error) throw std::runtime_error("host diagnostic stream write/close failed");
    }
    atomic_text(directory / "complete",state_hash);
}
std::vector<std::uint32_t> discover_ee_entries(const ElfImage& image, const std::filesystem::path& events) {
    std::string why;
    (void)compare_diagnostics(events,events,why);
    std::ifstream input(native_io_path(events),std::ios::binary);
    if(!input)throw std::runtime_error("cannot read observed instructions");
    const auto graph=analyze_control_flow(image,{}, {},true);
    std::set<std::uint32_t> candidates;
    std::optional<std::uint32_t> previous;
    std::string line;
    while(read_record(input,line)) {
        if(line.find(",\"kind\":\"instruction-attempt\",")==std::string::npos)continue;
        const auto field=[&](std::string_view key) {
            const auto at=line.find(key);
            if(at==std::string::npos || line.find(key,at+key.size())!=std::string::npos)
                throw std::runtime_error("invalid observed instruction field");
            const auto n=record_number(std::string_view(line).substr(at),key);
            if(n>UINT32_MAX)throw std::runtime_error("observed instruction field exceeds 32 bits");
            return static_cast<std::uint32_t>(n);
        };
        const auto pc=field(",\"pc\":"),word=field(",\"word\":");
        if(image.instruction_at(pc)!=word)throw std::runtime_error("observed instruction bytes differ from input image");
        if((!previous || pc!=std::uint64_t{*previous}+4u) && !graph.instructions.contains(pc))candidates.insert(pc);
        previous=pc;
        if(candidates.size()>65536)throw std::runtime_error("observed entry limit exceeded");
    }
    if(input.bad())throw std::runtime_error("observed instruction read failed");
    return {candidates.begin(),candidates.end()};
}
int compare_diagnostics(const std::filesystem::path& a, const std::filesystem::path& b, std::string& why) {
    std::ifstream first(native_io_path(a), std::ios::binary), second(native_io_path(b), std::ios::binary);
    if (!first || !second) throw std::runtime_error("cannot read diagnostic event files");
    // Validate the entire streams even when a divergence is early: a truncated
    // producer output must never be reported as a complete comparison.
    std::string x, y, difference; std::uint64_t line = 0, sequence_a=0, sequence_b=0; bool end_a = false, end_b = false;
    for (;;) {
        const bool has_a = read_record(first,x);
        const bool has_b = read_record(second,y);
        if (!has_a && !has_b) break;
        ++line;
        auto validate = [&](bool has, const std::string& value, bool& ended,std::uint64_t& sequence) {
            if (!has) return;
            if (ended || value.empty() || !JsonSyntax(value).valid()) throw std::runtime_error("invalid diagnostic stream");
            if (line == 1) {
                if(value==stream_header)sequence=0;
                else sequence=record_number(value,"{\"schema\":\"ps2n-events-v1\",\"scope\":\"ring\",\"discarded\":");
            } else if (value.starts_with("{\"end\":true,")) {
                if(record_number(value,"{\"end\":true,\"total\":")!=sequence || value.find("\"state_sha256\":\"")==std::string::npos)
                    throw std::runtime_error("incomplete diagnostic sequence/end state");
                ended=true;
            } else {
                if(!value.starts_with("{\"event\":"))throw std::runtime_error("incomplete or invalid diagnostic stream");
                if(record_number(value,"{\"event\":")!=sequence+1)throw std::runtime_error("incomplete diagnostic sequence");
                ++sequence;
            }
        };
        validate(has_a, x, end_a,sequence_a); validate(has_b, y, end_b,sequence_b);
        if (line == 1 && x != y) throw std::runtime_error("incompatible event coverage (complete/ring or different discarded prefix)");
        if (difference.empty() && (has_a != has_b || x != y))
            difference = "first recorded divergence at line " + std::to_string(line) + "\nA: " + (has_a ? x : "<end>") + "\nB: " + (has_b ? y : "<end>");
    }
    if (first.bad() || second.bad() || !end_a || !end_b) throw std::runtime_error("incomplete diagnostic stream (missing end marker)");
    why = difference.empty() ? "recorded events and inspected final state are identical; unobserved device state is not certified" : difference;
    return difference.empty() ? 0 : 1;
}
} // namespace ps2native
