#include "boot_checks.hpp"
#include "direct_program.hpp"
#include "ps2native/diagnostics.hpp"
#include "../tools/dev_profile.hpp"
#include <chrono>
#include <iostream>

namespace {
using namespace ps2native;
using namespace boot_checks;
using namespace ps2native::dev;
void hashes(const fs::path& root) {
    std::vector<std::uint8_t> data(2*1024*1024+113);
    for(std::size_t i=0;i<data.size();++i)data[i]=static_cast<std::uint8_t>(i*17u);
    const auto reference=sha256_hex(data);
    for(const auto step:{1u,63u,64u,65u,8192u}) {
        Sha256 hash;
        for(std::size_t i=0;i<data.size();i+=step)hash.update(std::span(data).subspan(i,std::min<std::size_t>(step,data.size()-i)));
        require(hash.hex_digest()==reference,"incremental SHA matches whole buffer, across block boundaries");
        require(hash.hex_digest()==reference,"finalizing hash does not consume state");
    }
    const auto file=root / "hash.bin";(void)write_binary_if_changed(file,data);
    require(sha256_file(file)==reference,"streamed file SHA includes bytes after 1 MiB");
    const std::string abc="abc";Sha256 hash;hash.update({reinterpret_cast<const std::uint8_t*>(abc.data()),3});
    require(hash.hex_digest()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","FIPS SHA-256 vector");
}
void history() {
    DiagnosticHistory<unsigned> ring; unsigned count=0;
    ring.configure(3,[&](unsigned){++count;});
    for(unsigned i=0;i<20;++i)ring.push_back(i);
    require(ring.items()==std::vector<unsigned>({17,18,19}) && count==20,"bounded device history retains chronological tail and observes every event");
    ring.push_back(20);require(ring.items()==std::vector<unsigned>({18,19,20}),"reading the ring preserves subsequent rotation");
    ring.configure(0,{});ring.push_back(21);require(ring.items().size()==4,"legacy history remains unlimited by default");
}
void discovery(const fs::path& root) {
    const auto image=parse_ee_elf(direct_test::elf(direct_test::cpu()));
    const auto path=root / "observed.ndjson";
    const auto pc=direct_test::text+0x200u;
    auto stream=[&](std::uint32_t address,std::uint32_t word) {
        return "{\"schema\":\"ps2n-events-v1\",\"scope\":\"ring\",\"discarded\":17}\n"
            "{\"event\":18,\"cycle\":0,\"pc\":"+std::to_string(address)+",\"kind\":\"instruction-attempt\",\"word\":"+std::to_string(word)+"}\n"
            "{\"end\":true,\"total\":18,\"state_sha256\":\"test\"}\n";
    };
    save_text(path,stream(pc,image.instruction_at(pc)));
    // This callback may already be statically proven; both cases remain safe.
    const auto roots=discover_ee_entries(image,path);
    for(const auto entry:roots)require(entry==pc,"only validated observed addresses become roots");
    save_text(path,stream(pc,image.instruction_at(pc)^1u));
    rejects([&]{(void)discover_ee_entries(image,path);},"bytes differ");
    save_text(path,stream(pc+1,image.instruction_at(pc)));
    rejects([&]{(void)discover_ee_entries(image,path);},"aligned");
    save_text(path,stream(0x70000000u,0));
    rejects([&]{(void)discover_ee_entries(image,path);},"executable");
    save_text(path,"{\"schema\":\"ps2n-events-v1\",\"scope\":\"complete\"}\n{\"truncated\":true}\n");
    rejects([&]{(void)discover_ee_entries(image,path);},"incomplete");
}
void recordings(const fs::path& root,const fs::path& rom) {
    const auto bios=load_bios_rom0(read_file(rom));
    for(unsigned engine=0;engine<2;++engine) {
        Memory memory;EEState state;const auto* module=engine ? &aot_module() : nullptr;
        prepare_ee_boot(bios,module,state,memory);
        DiagnosticRecorder recorder(8);recorder.stream(root / std::to_string(engine) / "full.ndjson",1000000);
        recorder.watch_ram(0,32);
        BootOptions options;options.max_instructions=10000;options.diagnostics=&recorder;options.max_trace_events=8;
        const auto r=run_ee_boot(state,memory,module,options);
        require(r.outcome==BootOutcome::BlockPoint,"synthetic BIOS reaches genuine unsupported device");
        require(r.trace.size()<=9 && r.retired>8,"text trace is bounded independently of instruction budget");
        recorder.write(root / std::to_string(engine),r,state,memory);
        require(read_text(root / std::to_string(engine) / "report.json").find("\"events_retained\":8")!=std::string::npos,"diagnostic ring bounded");
        require(read_text(root / std::to_string(engine) / "state.json").find("ram_windows")!=std::string::npos,"passive backing RAM captured");
    }
    std::string why;
    require(compare_diagnostics(root / "0/full.ndjson",root / "1/full.ndjson",why)==0,"AOT/interpreter observed events and final state match: "+why);
    require(compare_diagnostics(root / "0/events.ndjson",root / "1/events.ndjson",why)==0,"same-coverage circular histories compare");
    auto changed=read_text(root / "0/full.ndjson");const auto site=changed.find("\"word\":");
    require(site!=std::string::npos,"instruction event exists");changed.insert(site+7,"1");save_text(root / "different.ndjson",changed);
    require(compare_diagnostics(root / "0/full.ndjson",root / "different.ndjson",why)==1 && why.find("first recorded divergence")!=std::string::npos,"first observed divergence detected");
    save_text(root / "incomplete.ndjson",changed.substr(0,changed.rfind("{\"end\":")));
    rejects([&]{(void)compare_diagnostics(root / "0/full.ndjson",root / "incomplete.ndjson",why);},"incomplete");
    rejects([&]{(void)compare_diagnostics(root / "0/full.ndjson",root / "0/events.ndjson",why);},"coverage");
    Memory memory;EEState state;prepare_ee_boot(bios,nullptr,state,memory);
    DiagnosticRecorder limited(4);limited.stream(root / "truncated.ndjson",1024);BootOptions options;options.diagnostics=&limited;
    const auto r=run_ee_boot(state,memory,nullptr,options);limited.write(root / "limited",r,state,memory);
    rejects([&]{(void)compare_diagnostics(root / "truncated.ndjson",root / "truncated.ndjson",why);},"incomplete");
    require(regular_file_size(root / "truncated.ndjson")<=1024,"stream byte budget includes truncation marker");
    save_text(root / "malformed.ndjson","{\"schema\":\"ps2n-events-v1\",\"scope\":\"complete\"}\n{\"event\":false}\n");
    rejects([&]{(void)compare_diagnostics(root / "malformed.ndjson",root / "malformed.ndjson",why);},"sequence");
    rejects([&]{limited.watch_ram(Memory::kMainRamBytes,1);},"RAM inspection");
}
}
template<class C> int test_main(int argc,C** argv) {
    try {
        require(argc==3,"output root and synthetic ROM");const auto root=fs::path(argv[1]);ps2native::create_directories(root);
        hashes(root);history();discovery(root);recordings(root,fs::path(argv[2]));
        std::cout<<"diagnostics: bounded histories, SHA, shared engine equivalence, divergence and incomplete-stream checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return test_main(argc,argv);}
#else
int main(int argc,char** argv){return test_main(argc,argv);}
#endif
