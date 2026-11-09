#include "ps2native/frontend/native_module.hpp"
#include "ps2native/frontend/session.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"
#include "ps2_runtime.h"
#include "sdk/iop_module_interface.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include "../tools/dev_process.hpp"
using namespace ps2native;
namespace fs=std::filesystem;
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    try{operation();}catch(const std::exception& error){require(std::string(error.what()).find(reason)!=std::string::npos,error.what());return;}
    throw std::runtime_error(std::string("Expected refusal: ")+reason);
}
int main(int argc,char** argv) try {
    if(argc==2 && std::string_view(argv[1])=="--child-wait") {std::this_thread::sleep_for(std::chrono::seconds(60));return 0;}
    if(argc==2 && std::string_view(argv[1])=="--child-log") {
        for(unsigned n=0;n<2048;++n)std::cout<<std::string(1024,'x')<<std::flush;
        return 0;
    }
    require(argc==6,"Expected EE/IOP valid/incompatible modules and ELF");
    const auto binary=fs::absolute(argv[1]),incompatible=fs::absolute(argv[2]);
    const auto image=parse_ee_elf(read_file(argv[3]));const auto hash=sha256_hex(image.bytes);
    const auto receipt=binary.parent_path()/"test-receipt.json";
    require(!file_exists(receipt),"Refusing to overwrite an existing test receipt");
    struct Cleanup {fs::path path;~Cleanup(){std::error_code error;fs::remove(path,error);}} cleanup{receipt};
    {
        const auto input=binary.parent_path()/"hash-input.bin";
        require(!file_exists(input),"Refusing to overwrite an existing hash fixture");Cleanup hash_cleanup{input};
        (void)write_text_if_changed(input,"abc");
        require(sha256_file(input)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","File SHA-256 known vector");
        std::vector<std::uint8_t> bytes(1024u*1024u+65u);
        for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<std::uint8_t>(i*37u+11u);
        for(const auto size:{0u,55u,56u,63u,64u,65u,1024u*1024u+65u}) {
            const auto data=std::span(bytes).first(size);(void)write_binary_if_changed(input,data);
            require(sha256_file(input)==sha256_hex(data),"File and portable SHA-256 agree across padding and I/O boundaries");
        }
    }
    nlohmann::json info{{"schema","ps2n-native-module-v1"},{"runtime_abi",native_runtime_abi()},
        {"input_sha256",hash},{"elf_sha256",hash},{"binary",path_utf8(binary.filename())},{"binary_sha256",sha256_file(binary)}};
    const auto save=[&]{(void)write_text_if_changed(receipt,info.dump());};save();
    {
        frontend::LoadedNativeModule loaded(receipt,hash,hash);
        frontend::SessionSupervisor supervisor;
        auto* cpu=&supervisor.ee();auto* memory=&supervisor.memory();auto* gs=&memory->gs();auto* iop=&memory->iop();
        supervisor.runtime().prepare_direct_elf(image,&loaded.module());cpu->set_reg32_sign_extended(31,0xFFFFFFF0u);
        BootOptions options;options.max_instructions=100;options.stop_at=0xFFFFFFF0u;
        const auto report=supervisor.run(&loaded.module(),options);
        require(report.outcome==BootOutcome::StopAddress && report.retired==report.aot_instructions &&
            !report.interpreted_instructions && cpu->reg32(2)==37 && memory->read32(0x40)==37,"Loaded module executes real native code");
        require(cpu==&supervisor.ee() && memory==&supervisor.memory() && gs==&memory->gs() && iop==&memory->iop(),
            "Module execution preserves the host-owned CPU, memory and devices");
    }
    {
        const auto root=binary.parent_path()/"iop-lifetime";
        require(!file_exists(root),"Refusing to overwrite existing IOP module fixtures");ps2native::create_directories(root);
        struct CleanupDirectory {fs::path path;~CleanupDirectory(){std::error_code error;fs::remove_all(path,error);}} cleanup_iop{root};
        const std::array words{ObservedWord{0x2402002Au,0u}};
        const auto capture=encode_iop_aot_image(0x1000u,words);const auto source=sha256_hex(capture);
        const auto code=root/native_iop_cache_key(source,native_runtime_abi());ps2native::create_directories(code);
        const auto iop_binary=fs::absolute(argv[4]),bad_iop_binary=fs::absolute(argv[5]);
        fs::copy_file(native_io_path(iop_binary),native_io_path(code/iop_binary.filename()));
        fs::copy_file(native_io_path(bad_iop_binary),native_io_path(code/bad_iop_binary.filename()));
        const auto iop_receipt=code/"native-iop-prepare.json";
        nlohmann::json iop_info{{"schema","ps2n-native-iop-module-v1"},{"runtime_abi",native_runtime_abi()},
            {"image_sha256",source},{"binary",path_utf8(iop_binary.filename())},{"binary_sha256",sha256_file(iop_binary)}};
        const auto save_iop=[&]{(void)write_text_if_changed(iop_receipt,iop_info.dump());};save_iop();
        const auto case_dir=root/"main";ps2native::create_directories(case_dir);
        fs::copy_file(native_io_path(binary),native_io_path(case_dir/binary.filename()));
        const auto main_receipt=case_dir/"native-prepare.json";
        auto main_info=info;main_info["iop_modules"]=nlohmann::json::array({{{"image_sha256",source}}});
        (void)write_text_if_changed(main_receipt,main_info.dump());
        const auto capture_file=root/"iop-regions"/(source+".iop");write_file_atomically(capture_file,capture);
        const auto step=[&] {
            auto cpu=std::make_unique<Iop>();const std::array<std::uint8_t,4> bytes{0x2A,0,2,0x24};
            cpu->load_program(bytes,0x1000u,0x1000u);cpu->enable_aot(true);cpu->require_aot(true);cpu->run_until(1u);
            return cpu;
        };
        {
            frontend::LoadedNativeModule loaded(main_receipt,hash,hash);const auto cpu=step();
            require(!cpu->stopped() && cpu->native_instructions()==1u && !cpu->interpreted_instructions() && cpu->gpr(2)==42u,
                "Verified IOP code module executes through the same native module receipt and canonical IOP");
        }
        const auto unloaded=step();
        require(unloaded->stopped() && unloaded->stop_reason()->find("missing IOP AOT")!=std::string::npos,
            "Unloading a code module removes its native entries instead of leaving dangling function pointers");
        iop_info["runtime_abi"]="stale";save_iop();
        rejects([&]{frontend::NativeIopCode loaded(iop_receipt,source);},"stale or incompatible");
        iop_info["runtime_abi"]=native_runtime_abi();iop_info["binary_sha256"]="wrong";save_iop();
        rejects([&]{frontend::NativeIopCode loaded(iop_receipt,source);},"SHA-256 mismatch");
        iop_info["binary"]=path_utf8(bad_iop_binary.filename());iop_info["binary_sha256"]=sha256_file(bad_iop_binary);save_iop();
        rejects([&]{frontend::NativeIopCode loaded(iop_receipt,source);},"ABI or image mismatch");
        const auto rejected=step();
        require(rejected->stopped() && rejected->stop_reason()->find("missing IOP AOT")!=std::string::npos,"Rejected IOP images publish no native entries");
    }
    rejects([&]{frontend::LoadedNativeModule loaded(receipt,"other",hash);},"stale or incompatible");
    rejects([&]{frontend::LoadedNativeModule loaded(receipt,hash,"other");},"stale or incompatible");
    info["runtime_abi"]="old";save();
    rejects([&]{frontend::LoadedNativeModule loaded(receipt,hash,hash);},"stale or incompatible");
    info["runtime_abi"]=native_runtime_abi();info["binary_sha256"]="wrong";save();
    rejects([&]{frontend::LoadedNativeModule loaded(receipt,hash,hash);},"SHA-256 mismatch");
    info["binary"]="../escape.dll";save();
    rejects([&]{frontend::LoadedNativeModule loaded(receipt,hash,hash);},"filename");
    info["binary"]=path_utf8(binary.filename());info["binary_sha256"]=sha256_file(binary);
    info["execution_alias"]=0x180000000ull;save();
    rejects([&]{frontend::LoadedNativeModule loaded(receipt,hash,hash);},"execution alias");
    info.erase("execution_alias");
    info["overlays"]=nlohmann::json::array();
    for(unsigned i=0;i<9;++i)info["overlays"].push_back({{"image_sha256",std::string(64,'0')}});
    save();rejects([&]{frontend::LoadedNativeModule loaded(receipt,hash,hash);},"overlay list");
    info["overlays"]=nlohmann::json::array({{{"image_sha256","../escape"}}});
    save();rejects([&]{frontend::LoadedNativeModule loaded(receipt,hash,hash);},"overlay hash");
    info.erase("overlays");
    info["binary"]=path_utf8(incompatible.filename());info["binary_sha256"]=sha256_file(incompatible);save();
    rejects([&]{frontend::LoadedNativeModule loaded(receipt,hash,hash);},"ABI mismatch");
    const auto cancellation_log=binary.parent_path()/"cancellation.log",limit_log=binary.parent_path()/"log-limit.log";
    require(!file_exists(cancellation_log) && !file_exists(limit_log),"Process test logs already exist");
    Cleanup cancel_cleanup{cancellation_log},limit_cleanup{limit_log};
    const auto start=std::chrono::steady_clock::now();
    const auto cancelled=dev::process(fs::absolute(argv[0]),{"--child-wait"},cancellation_log,10,1024,
        [&]{return std::chrono::steady_clock::now()-start>std::chrono::milliseconds(50);});
    require(cancelled.cancelled && !cancelled.timed_out && cancelled.exit_code==130 && cancelled.milliseconds<3000,
        "Native compilation cancellation must stop the owned child promptly");
    rejects([&]{(void)dev::process(fs::absolute(argv[0]),{"--child-log"},limit_log,10,1024);},"process log exceeded");
    require(regular_file_size(limit_log)<=65536,"Process evidence must remain bounded");
    std::cout<<"Native module execution, shared console ownership and hash/ABI negative controls passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
