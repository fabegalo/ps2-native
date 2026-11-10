#include "iop_aot_cli.hpp"
#include "boot_checks.hpp"
#include "ps2native/iop_ops.hpp"
#include "ps2native/sha256.hpp"
#include <memory>
using namespace ps2native;
using namespace boot_checks;
template<auto Words> auto native_entries() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
        return std::array{IopAotEntry{4u*static_cast<unsigned>(I),Words[I],[](Iop& iop){iop.execute_native<Words[I]>();}}...};
    }(std::make_index_sequence<Words.size()>{});
}
template<class C> int run(int argc,C** argv) try {
    if(argc!=2)return 2;
    constexpr std::array<std::uint32_t,6> words{0x2401000Cu,0x00200008u,0,0x24020003u,0x08000004u,0};
    std::vector<std::uint8_t> program;
    for(const auto word:words)for(unsigned b=0;b<4;++b)program.push_back(static_cast<std::uint8_t>(word>>(8u*b)));
    const auto native=native_entries<words>();const auto source=sha256_hex(program),identity=iop_aot_identity(source,native);
    const IopAotModule module{source,identity,native};const IopAotRegistration registration(module);
    auto memory=std::make_unique<Memory>();auto& iop=const_cast<Iop&>(memory->iop());
    iop.load_program(program,0,0);
    cli::IopAotOptions capture;capture.directory=std::filesystem::path(argv[1]);capture.attach(*memory,{});
    iop.run_until(4);capture.finish();
    require(!iop.stopped() && iop.gpr(2)==3,"reference observation preserves instruction effects");
    require(capture.captured.size()==1 && capture.captured.begin()->second.contains(12),
        "sequential-looking JR target is recorded as an offline entry");
    const auto hash=capture.identity;
    const auto image=read_file(capture.directory/("iop_"+hash+".iop"));
    const std::uint32_t incomplete_roots[]{0},complete_roots[]{0,4,8,12};
    require(generate_iop_aot_sources(image,0,incomplete_roots,"negative").dispatcher.find("::at_12}")==std::string::npos,
        "negative control reproduces indirect-target coverage hole");
    require(generate_iop_aot_sources(image,0,complete_roots,"complete").dispatcher.find("::at_12}")!=std::string::npos,
        "observed indirect target is emitted by the existing offline generator");
    iop.ee_write(12,4,0x24020009u);iop.ee_write(16,4,0x08000003u);
    constexpr auto changed=[] {auto result=words;result[3]=0x24020009u;result[4]=0x08000003u;return result;}();
    const auto changed_native=native_entries<changed>();std::vector<std::uint8_t> changed_program;
    for(const auto word:changed)for(unsigned b=0;b<4;++b)changed_program.push_back(static_cast<std::uint8_t>(word>>(8u*b)));
    const auto changed_source=sha256_hex(changed_program),changed_identity=iop_aot_identity(changed_source,changed_native);
    const IopAotModule changed_module{changed_source,changed_identity,changed_native};const IopAotRegistration changed_registration(changed_module);
    iop.run_until(7);capture.finish();
    require(!iop.stopped() && iop.gpr(2)==9 && capture.captured.size()==2 && capture.identity!=hash,
        "changed loaded code gets a new image identity and retains the old artifact");
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return run(argc,argv);}
#else
int main(int argc,char** argv){return run(argc,argv);}
#endif
