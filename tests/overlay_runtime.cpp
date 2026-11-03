#include "direct_program.hpp"
#include "boot_checks.hpp"
#include "ps2native/boot.hpp"
#include "ps2native/sha256.hpp"
#include <iostream>
namespace ps2native { const AotModule& overlay_aot_module(); }
int main(int argc,char** argv) {
    using namespace ps2native;using namespace boot_checks;
    try {
        require(argc==3,"dynamic ELF and overlay ELF");
        const auto elf=parse_ee_elf(read_file(argv[1])),overlay=parse_ee_elf(read_file(argv[2]));
        const auto program=direct_test::build("dynamic");
        std::vector<std::string> traces[2];
        for(unsigned native=0;native<2;++native) {
            Memory memory;EEState state;prepare_direct_elf(elf,native?&aot_module():nullptr,state,memory);
            BootOptions o;o.stop_at=program.stop;o.max_instructions=1000;o.trace=TraceLevel::Full;
            const auto before=run_ee_boot(state,memory,native?&aot_module():nullptr,o);
            require(before.outcome==BootOutcome::StopAddress,"guest copied code before checkpoint");
            const auto code=direct_test::dynamic_code();
            const auto snapshot=snapshot_ee_code(memory,direct_test::dynamic_address,12,sha256_hex(code));
            require(snapshot.bytes==overlay.bytes,"offline snapshot stable ELF identity");
            rejects([&]{(void)snapshot_ee_code(memory,direct_test::dynamic_address,12,std::string(64,'0'));},"SHA-256");
            rejects([&]{(void)snapshot_ee_code(memory,direct_test::dynamic_address+1,12,sha256_hex(code));},"range");
            auto verified=VerifiedAotOverlay::verify(snapshot,overlay_aot_module(),memory);
            Memory other;
            rejects([&]{(void)verified.module_for(other);},"different runtime");
            o.stop_at=program.stop+16u;
            if(native)o.aot_overlays.push_back(verified);
            const auto after=run_ee_boot(state,memory,native?&aot_module():nullptr,o);
            require(after.outcome==BootOutcome::StopAddress && state.reg32(2)==42,"dynamic function executed");
            if(native)require(after.interpreted_instructions==0 && after.aot_instructions==7,"offline overlay executes entirely in AOT");
            auto full=normalized(before.trace);const auto tail=normalized(after.trace);full.insert(full.end(),tail.begin(),tail.end());traces[native]=std::move(full);
            memory.write32(direct_test::dynamic_address,0x2402002Bu);
            rejects([&]{(void)VerifiedAotOverlay::verify(snapshot,overlay_aot_module(),memory);},"differ from loaded RAM");
            state.pc=direct_test::dynamic_address;
            o.aot_overlays={verified};
            const auto changed=run_ee_boot(state,memory,nullptr,o);
            require(changed.outcome==BootOutcome::BlockPoint && changed.reason.find("code modified")!=std::string::npos,"stale compiled overlay invalidated");
        }
        require(traces[0]==traces[1],"dynamic loader and overlay event equivalence");
        std::cout<<"verified dynamic EE code: snapshot SHA-256, offline AOT, runtime identity and invalidation passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
