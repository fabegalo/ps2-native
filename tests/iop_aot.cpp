#include "iop_aot_fixture.hpp"
#include "boot_checks.hpp"
#include "ps2native/iop.hpp"
#include "ps2native/iop_aot.hpp"
#include <iostream>
#include <memory>
using namespace ps2native;
using namespace boot_checks;
int main() try {
    const auto bytes=iop_aot_fixture::image();
    auto native=std::make_unique<Iop>(),reference=std::make_unique<Iop>();
    native->load_program(bytes,0,0x1000);reference->load_program(bytes,0,0x1000);
    native->require_aot(true);reference->enable_aot(false);
    for(unsigned cycle=1;cycle<=20;++cycle) {
        native->run_until(cycle);reference->run_until(cycle);
        require(!native->stopped() && !reference->stopped(),"native and reference continue");
        require(native->cycle()==reference->cycle() && native->pc()==reference->pc(),"PC, exception vector and cycles match");
        for(unsigned r=0;r<32;++r) {
            require(native->gpr(r)==reference->gpr(r),"GPR/load and branch delays match");
            if(r<16)require(native->diagnostic_cop0(r)==reference->diagnostic_cop0(r),"exception EPC, cause, SR and RFE match");
        }
        require(native->diagnostic_ram32(0x1080)==reference->diagnostic_ram32(0x1080),"native store bytes match");
    }
    require(native->gpr(3)==5 && native->gpr(4)==1 && native->gpr(5)==10 && native->gpr(6)==7 && native->gpr(8)==8,
        "independent expected results for load delay, branch delay and exception return");
    require(native->native_instructions()==20 && native->interpreted_instructions()==0 && reference->interpreted_instructions()==20,
        "compiled IOP artifact ran without interpreter fallback");
    auto changed=bytes;changed[0x1000]^=1;
    native->load_program(changed,0,0x1000);native->run_until(1);
    require(native->stopped() && native->stop_reason()->find("missing IOP AOT")!=std::string::npos && native->gpr(1)==0,
        "changed executable bytes fail before guest effects");
    const std::uint32_t roots[]{0x1000,0x80,0x1028};
    auto sources=generate_iop_aot_sources(bytes,0,roots,"fixture",256);
    auto again=generate_iop_aot_sources(bytes,0,roots,"fixture",256);
    require(sources.dispatcher==again.dispatcher && sources.partitions.size()==again.partitions.size(),"deterministic partitioned offline generation");
    for(unsigned n=0;n<sources.partitions.size();++n)require(sources.partitions[n].text==again.partitions[n].text,"stable partition bytes");
    auto changed_data=bytes;changed_data[0x1F0]=1;
    const auto data_sources=generate_iop_aot_sources(changed_data,0,roots,"another-captured-image",256);
    require(sources.dispatcher!=data_sources.dispatcher && sources.partitions.size()==data_sources.partitions.size(),
        "source identity still changes when non-executable image bytes change");
    for(unsigned n=0;n<sources.partitions.size();++n)
        require(sources.partitions[n].name==data_sources.partitions[n].name,"unchanged native code is reusable across loaded snapshots");
    const std::uint32_t expanded_roots[]{0x1000,0x80,0x1028,0x200};
    const auto expanded=generate_iop_aot_sources(bytes,0,expanded_roots,"fixture",256);
    require(sources.partitions.front().name==expanded.partitions.front().name && sources.dispatcher!=expanded.dispatcher,
        "adding a root reuses unrelated compiled partitions and changes the module identity");
    std::vector<ObservedWord> observed;
    for(unsigned at=0;at<bytes.size();at+=4) {
        std::uint32_t word=0;for(unsigned b=0;b<4;++b)word|=std::uint32_t{bytes[at+b]}<<(b*8u);
        observed.push_back({word,0});
    }
    observed[0x1000/4]={0,~0u};
    const auto capture=encode_iop_aot_image(0,observed);
    const auto sparse=generate_iop_aot_sources(capture,0,roots,"sparse",256);
    require(sources.dispatcher.find("::at_4096}")!=std::string::npos && sparse.dispatcher.find("::at_4096}")==std::string::npos,
        "unwritten instruction bits cannot become native code during offline preparation");
    rejects([&]{(void)generate_iop_aot_sources(capture,4,roots,"wrong-base");},"mismatch");
    std::cout<<"IOP AOT: load/branch delays, exceptions/RFE, stores, cycles, identity and offline partitions passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
