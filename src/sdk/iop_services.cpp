// SPDX-License-Identifier: GPL-3.0-only
#include "iop_services.hpp"
#include "ps2native/memory.hpp"
#include "ps2native/ee.hpp"
#include "ps2native/iop_aot.hpp"
#include "ps2native/sha256.hpp"
#include "ps2x/iop/ps2_path.h"
#include "kernel_interface.hpp"
#include "module_factories.h"
#include <cstdlib>
namespace ps2_stubs {
std::int32_t nativeMemoryCard(ps2x::iop::IopHost&,const ps2x::iop::MemoryCardRequest&,std::shared_ptr<void>&);
std::int32_t nativePad(ps2x::iop::IopHost&,const ps2x::iop::PadRequest&,std::shared_ptr<void>&);
}

namespace ps2native {
using namespace ps2x::iop;
namespace {
struct NativeCall {unsigned& depth;explicit NativeCall(unsigned& d):depth(d){++depth;}~NativeCall(){--depth;}};
}
Ps2RecompIopServices::Ps2RecompIopServices(Memory& memory,EEState& state)
    :iop_(const_cast<Iop&>(memory.iop())),consoleMemory_(memory),eeState_(state),memory_(iop_) {
    emulator_=std::make_unique<detail::IopEmulator>(*this);
#ifdef _WIN32
    const auto* data=_wgetenv(L"PS2N_DATA_DIR");
    if(data && *data)memoryCardRoot_=std::filesystem::path(data)/L"memorycards"/L"mc0";
    else if(const auto* app=_wgetenv(L"APPDATA");app && *app)
        memoryCardRoot_=std::filesystem::path(app)/L"ps2-native"/L"ps2-native"/L"memorycards"/L"mc0";
#else
    if(const auto* data=std::getenv("PS2N_DATA_DIR");data && *data)memoryCardRoot_=std::filesystem::path(data)/"memorycards"/"mc0";
    else if(const auto* home=std::getenv("HOME");home && *home) {
#ifdef __APPLE__
        memoryCardRoot_=std::filesystem::path(home)/"Library"/"Application Support"/"ps2-native"/"ps2-native"/"memorycards"/"mc0";
#else
        const auto* xdg=std::getenv("XDG_DATA_HOME");
        memoryCardRoot_=(xdg && *xdg?std::filesystem::path(xdg):std::filesystem::path(home)/".local"/"share")/"ps2-native"/"ps2-native"/"memorycards"/"mc0";
#endif
    }
#endif

}
std::uint32_t sdk_iop_bus_read(Iop& iop,std::uint32_t address,unsigned bytes) {return Ps2RecompIopServices::read_device(iop,address,bytes);}
void configure_hle_console(Memory& memory,const ConsoleHardware* hardware,std::shared_ptr<const NvmImage> nvm) {
    Ps2RecompIopServices::configure_console(memory,hardware,std::move(nvm));
}
void configure_hle_spu2_quiescent_reset(Memory& memory) {
    Ps2RecompIopServices::configure_spu2_quiescent_reset(memory);
}
void Ps2RecompIopServices::configure_spu2_quiescent_reset(Memory& memory) {
    auto& iop=const_cast<Iop&>(memory.iop());const auto* services=iop.hle_system_;
    if(!iop.hle_services_ || !services || iop.cycle_ || services->eeState_.cycles() ||
       iop.spu2_control_[0] || iop.spu2_control_[1] || iop.spu2_clock_active_)
        throw DeviceRefusal("Diagnostic SPU2 reset requires the unused HLE session before audio configuration");
    iop.diagnostic_spu2_quiescent_=true;
}
void Ps2RecompIopServices::configure_console(Memory& memory,const ConsoleHardware* hardware,std::shared_ptr<const NvmImage> nvm) {
    auto& iop=const_cast<Iop&>(memory.iop());auto* services=iop.hle_system_;
    if(!iop.hle_services_ || !services || iop.cycle_ || services->eeState_.cycles())throw DeviceRefusal("HLE console inputs require the unused SDK session");
    if(hardware && (!services->bios_ || hardware->romver!=services->bios_->version.romver))throw DeviceRefusal("Console profile and HLE BIOS ROMVER differ");
    if(nvm && (!hardware || !hardware->mechacon || nvm->model_name!=hardware->model || nvm->layout!=hardware->mechacon->layout))
        throw DeviceRefusal("HLE NVM does not match its console profile");
    services->consoleNvm_=std::move(nvm);
    memory.registers_.hardware_=hardware;
    iop.hardware_=hardware;iop.nvm_=services->consoleNvm_.get();
}
void sdk_iop_bus_write(Iop& iop,std::uint32_t address,unsigned bytes,std::uint32_t value) {Ps2RecompIopServices::write_device(iop,address,bytes,value);}
std::uint32_t Ps2RecompIopServices::read_device(Iop& iop,std::uint32_t address,unsigned bytes) {
    struct ReadScope {
        std::optional<std::string>& undefined;std::uint32_t& known;std::uint64_t& epoch;
        std::optional<std::string> previous;std::uint32_t previousKnown;std::uint64_t previousEpoch;
        ~ReadScope(){undefined=std::move(previous);known=previousKnown;epoch=previousEpoch;}
    } scope{iop.undefined_load_,iop.undefined_known_,iop.undefined_sio2_epoch_,std::move(iop.undefined_load_),iop.undefined_known_,iop.undefined_sio2_epoch_};
    iop.undefined_load_.reset();iop.undefined_known_=0;iop.undefined_sio2_epoch_=0;
    try {
        const auto value=iop.io_read(address,bytes,"SDK IOP service read");
        const auto required=bytes==4u?~0u:(1u<<(bytes*8u))-1u;
        if(iop.undefined_load_ && (iop.undefined_known_&required)!=required)
            throw DeviceRefusal("SDK IOP service consumes undefined device bits: "+*iop.undefined_load_);
        return value&required;
    }catch(const IopExecutionStop& error){throw DeviceRefusal(error.reason);}
}
void Ps2RecompIopServices::write_device(Iop& iop,std::uint32_t address,unsigned bytes,std::uint32_t value) {
    try {iop.io_write(address,bytes,value,"SDK IOP service write");}
    catch(const IopExecutionStop& error){throw DeviceRefusal(error.reason);}
}
void Ps2RecompIopServices::initialize_memory() {
    // Called only after the console validates the unused HLE reset boundary.
    std::fill(iop_.ram_.begin(),iop_.ram_.end(),0u);
    std::fill(iop_.ram_known_.begin(),iop_.ram_known_.end(),0xFFu);
    reset_interrupts();
}
void Ps2RecompIopServices::reset_interrupts() {
    // Explicit IopMemory::reset fields, at the validated fresh/reboot boundary.
    write_device(iop_,0x1F801070u,4u,0u);
    memory_.setInterruptMask(0u);memory_.setInterruptControl(1u);
}
void Ps2RecompIopServices::load_cpu(const detail::IopCpuState& cpu) {
    for(unsigned r=0;r<32;++r)iop_.gpr_[r]=cpu.gpr[r].value;
    iop_.gpr_[0]=0;iop_.gpr_defined_=1u;
    for(unsigned r=1;r<32;++r){iop_.gpr_known_[r]=~cpu.gpr[r].unknown;if(!cpu.gpr[r].unknown)iop_.gpr_defined_|=1u<<r;}
    for(unsigned r=0;r<32;++r)iop_.gpr_sio2_epoch_[r]=cpu.gpr[r].epoch;
    iop_.hi_=cpu.hiUnknown?std::nullopt:std::optional{cpu.hi};
    iop_.lo_=cpu.loUnknown?std::nullopt:std::optional{cpu.lo};
    iop_.pc_=cpu.pc;iop_.in_delay_slot_=cpu.branchPending;
    iop_.branch_target_=cpu.branchPending?std::optional{cpu.branchTarget}:std::nullopt;
    iop_.pending_load_.reset();
    if(cpu.pendingLoad)iop_.pending_load_=Iop::PendingLoad{cpu.pendingLoadReg,cpu.pendingLoadValue,
        !cpu.pendingLoadUnknown,"SDK native context",~cpu.pendingLoadUnknown,cpu.pendingLoadEpoch};
    iop_.write_.reset();iop_.undefined_load_.reset();
    for(unsigned r=0;r<16;++r)iop_.cop0_[r]=r==15u?(iop_.hardware_?iop_.hardware_->iop_prid:std::nullopt):
        (cpu.cop0Defined&(1u<<r))?std::optional{cpu.cop0[r]}:std::nullopt;
}
void Ps2RecompIopServices::save_cpu(detail::IopCpuState& cpu) const {
    for(unsigned r=0;r<32;++r)cpu.gpr[r].value=iop_.gpr_[r];
    cpu.gpr[0]=0;
    for(unsigned r=0;r<32;++r)cpu.gpr[r].unknown=iop_.diagnostic_gpr(r).unknown;
    for(unsigned r=0;r<32;++r)cpu.gpr[r].epoch=iop_.gpr_sio2_epoch_[r];
    cpu.hi=iop_.hi_.value_or(0);cpu.hiUnknown=iop_.hi_?0u:~0u;
    cpu.lo=iop_.lo_.value_or(0);cpu.loUnknown=iop_.lo_?0u:~0u;
    cpu.pc=iop_.pc_;cpu.branchPending=iop_.in_delay_slot_;cpu.branchTarget=iop_.branch_target_.value_or(0);
    cpu.pendingLoad=bool(iop_.pending_load_);
    if(iop_.pending_load_){const auto& l=*iop_.pending_load_;cpu.pendingLoadReg=l.reg;cpu.pendingLoadValue=l.value;
        cpu.pendingLoadUnknown=l.defined?0u:~l.known;cpu.pendingLoadEpoch=l.sio2_epoch;}
    cpu.cop0Defined=0;
    for(unsigned r=0;r<16;++r)if(iop_.cop0_[r]){cpu.cop0[r]=*iop_.cop0_[r];cpu.cop0Defined|=1u<<r;}
}
void Ps2RecompIopServices::nativePrepareImport(detail::IopCpuState& cpu) {
    load_cpu(cpu);
    // A functional import consumes arguments after entering its service body.
    // Complete the caller's delayed load through the existing R3000 pipeline.
    if(iop_.pending_load_)iop_.commit(*iop_.pending_load_);
    iop_.pending_load_.reset();save_cpu(cpu);
}
bool Ps2RecompIopServices::nativeInterrupt(detail::IopCpuState& cpu) {
    load_cpu(cpu);
    try {const bool interrupted=!iop_.begin_instruction();save_cpu(cpu);return interrupted;}
    catch(const IopExecutionStop& error){throw DeviceRefusal(error.reason);}
}
std::uint32_t Ps2RecompIopServices::nativePendingInterrupts(std::uint32_t interested) const {
    if(!interested)return 0;
    if(!nativeInterruptsEnabled())return 0;
    const auto* mask=iop_.latch(0x1F801074u);
    if(!mask || !*mask)throw DeviceRefusal("SDK native IRQ mask is unknown");
    const auto enabled=interested&**mask;
    if(const auto unknown=enabled&~iop_.modeled_irqs(enabled))
        throw DeviceRefusal("SDK native IRQ source state is unknown: mask="+std::to_string(unknown));
    return iop_.istat_&enabled;
}
bool Ps2RecompIopServices::nativeInterruptsEnabled() const {
    const auto* control=iop_.latch(0x1F801078u);
    if(!control || !*control)throw DeviceRefusal("SDK native IRQ control is unknown");
    return (**control&1u)!=0;
}
void Ps2RecompIopServices::nativeAcknowledgeInterrupts(std::uint32_t handled) {
    write_device(iop_,0x1F801070u,4u,~handled);
}
bool Ps2RecompIopServices::nativeStep(detail::IopCpuState& cpu) {
    load_cpu(cpu);
    try {
        const auto word=iop_.fetch(iop_.pc_);
        if(!execute_iop_aot(iop_,word))throw DeviceRefusal(iop_.code_identity(cpu.pc)+": "+missing_iop_aot(cpu.pc,word));
        ++iop_.native_instructions_;save_cpu(cpu);return true;
    } catch(const IopExecutionStop& error){save_cpu(cpu);throw DeviceRefusal(error.reason);}
}
bool Ps2RecompIopServices::nativeSif0Available() const {
    if(!emulator_->nativeHasRpc())return !iop_.hle_events_pending();
    // An unfinished callback does not occupy either physical SIF DMA source.
    const auto* status=iop_.latch(0x1F801574u);
    return status && *status && iop_.sif0_fifo_.empty() && !(iop_.iop_sif0_[2]&0x01000000u);
}
bool Ps2RecompIopServices::nativeSif0Complete() const {
    if(iop_.iop_sif0_[2]&0x01000000u)return false;
    const auto* status=iop_.latch(0x1F801574u);
    if(!status || !*status)throw DeviceRefusal("SIF0 DMA completion flags are unknown");
    return !(**status&(1u<<26u));
}
void Ps2RecompIopServices::nativeStartSif0(std::uint32_t address) {
    if(!nativeSif0Available())throw DeviceRefusal("SDK SIF0 submission requires the shared transport to be idle");
    write_device(iop_,0x1F801524u,4u,0x20u);
    write_device(iop_,0x1F80152Cu,4u,address);
    write_device(iop_,0x1F801528u,4u,0x01000701u);
}
std::uint64_t Ps2RecompIopServices::nativeCycles() const {return iop_.cycle_;}
void Ps2RecompIopServices::nativeTick(std::uint64_t cycle) {
    if(!nativeDepth_)throw DeviceRefusal("IOP native tick outside its SDK execution scope");
    if(cycle>UINT64_MAX/kEeCyclesPerIopCycle)throw DeviceRefusal("IOP native clock overflow");
    const auto eeCycle=cycle*kEeCyclesPerIopCycle;
    if(eeCycle>eeState_.cycles())eeState_.stall(eeCycle-eeState_.cycles());
    iop_.run_until(cycle,&consoleMemory_.gs());
    if(iop_.stopped())throw DeviceRefusal(*iop_.stop_reason());
}
void Ps2RecompIopServices::run_until(std::uint64_t cycle) {
    if(nativeDepth_ || !emulator_)return;
    if(bootPending_){bootPending_=false;boot_modules();}
    if(cycle<=emulator_->cycles())return;
    NativeCall call(nativeDepth_);
    emulator_->runUntilNative(cycle);
}
void Ps2RecompIopServices::nativeModuleLoaded(std::uint32_t base,std::uint32_t size,std::uint32_t entry) {
    if(!size || base>=kIopRamBytes || size>kIopRamBytes-base || entry<base || entry-base>=size || nativeModules_.size()>=64u)
        throw DeviceRefusal("SDK native IRX module bounds/capacity invalid");
    nativeModules_.emplace_back(base,size);
}
void Ps2RecompIopServices::nativeModuleReleased(std::uint32_t base,std::uint32_t size) {
    if(padModuleBase_>=base && padModuleBase_-base<size) {
        padService_.reset();padState_.reset();padModuleBase_=0;
        for(auto& endpoint:padEndpoints_) {
            if(endpoint.descriptor)memory_.freeAllocation(endpoint.descriptor);
            if(endpoint.buffer)memory_.freeAllocation(endpoint.buffer);
            endpoint={};
        }
    }
    if(memoryCardServerBase_>=base && memoryCardServerBase_-base<size) {
        memoryCardService_.reset();memoryCardServerBase_=0;
        if(memoryCardServer_)memory_.freeAllocation(memoryCardServer_);
        if(memoryCardBuffer_)memory_.freeAllocation(memoryCardBuffer_);
        memoryCardServer_=memoryCardBuffer_=0;
    }
    if(memoryCardManagerBase_>=base && memoryCardManagerBase_-base<size) {
        if(memoryCardService_)throw DeviceRefusal("Cannot unload MemoryCard manager while MCSERV is active");
        memoryCardState_.reset();memoryCardManagerBase_=0;memoryCardManagerVersion_=0;
    }
    std::erase_if(nativeModules_,[&](const auto& range){return range.first>=base && range.first-base<size;});
}
std::optional<std::pair<std::uint32_t,std::uint32_t>> Ps2RecompIopServices::module_range(std::uint32_t pc) const {
    pc&=0x1FFFFFFFu;
    for(const auto& region:nativeModules_)if(pc>=region.first && pc-region.first<region.second)return region;
    return std::nullopt;
}
std::optional<std::uint32_t> Ps2RecompIopServices::nativeStartModule(std::string_view name,std::uint16_t version,
    std::uint32_t base,std::uint32_t size,std::int32_t argc) {
    // Sony IRX identifiers can carry space padding (for example "mc2_d ").
    while(name.ends_with(' '))name.remove_suffix(1u);
    if(name=="padman" || name=="xpadman" || name=="ds2u_d") {
        if(argc<0 || !version || !size)throw DeviceRefusal("PADMAN module lifecycle/version unsupported");
        if(padModuleBase_ && padModuleBase_!=base)throw DeviceRefusal("SDK PADMAN is already active");
        if(!padService_) {
            auto service=detail::createPadmanService(*this,version);service->reset();
            std::array<PadEndpoint,4> endpoints{};
            try {
                unsigned index=0;
                for(const auto sid:service->sids()) {
                    if(index>=endpoints.size())throw DeviceRefusal("SDK PADMAN endpoint capacity exceeded");
                    RpcRequest existing;existing.sid=sid;
                    if(emulator_->nativeRpcServer(existing,false))throw DeviceRefusal("SDK PADMAN conflicts with an active native RPC server");
                    auto& endpoint=endpoints[index++];endpoint.sid=sid;
                    endpoint.descriptor=memory_.allocate(64u,16u);endpoint.buffer=memory_.allocate(128u,64u);
                    if(!endpoint.descriptor || !endpoint.buffer)throw DeviceRefusal("SDK PADMAN endpoint allocation failed");
                    memory_.zeroRam(endpoint.descriptor,64u);memory_.zeroRam(endpoint.buffer,128u);
                    memory_.write32(endpoint.descriptor,sid);memory_.write32(endpoint.descriptor+8u,endpoint.buffer);
                }
            } catch(...) {
                for(const auto& endpoint:endpoints){if(endpoint.descriptor)memory_.freeAllocation(endpoint.descriptor);if(endpoint.buffer)memory_.freeAllocation(endpoint.buffer);}
                padState_.reset();throw;
            }
            padEndpoints_=endpoints;padService_=std::move(service);
        }
        padModuleBase_=base;
        log(LogLevel::Info,"SDK PADMAN HLE active for IRX "+std::string(name)+" version="+std::to_string(version));
        return 0u;
    }
    const bool manager=name=="mcman" || name=="mcman_tool" || name=="xmcman" || name=="mc2_d";
    const bool server=name=="mcserv" || name=="xmcserv" || name=="mc2_s";
    if(!manager && !server)return std::nullopt;
    if(argc<0 || !size || !version)throw DeviceRefusal("MemoryCard HLE module lifecycle/version unsupported");
    if(manager) {
        if(memoryCardManagerBase_ && memoryCardManagerBase_!=base)throw DeviceRefusal("MemoryCard manager is already active");
        if(!memoryCardManagerBase_ && memoryCard({MemoryCardOperation::Init})<0)throw DeviceRefusal("SDK MemoryCard manager initialization failed");
        memoryCardManagerBase_=base;memoryCardManagerVersion_=version;
    } else {
        if(!memoryCardManagerBase_)throw DeviceRefusal("MCSERV requires an active SDK MemoryCard manager");
        if(memoryCardServerBase_ && memoryCardServerBase_!=base)throw DeviceRefusal("MCSERV is already active");
        if(!memoryCardService_) {
            auto service=detail::createMcservService(*this,version,memoryCardManagerVersion_);service->reset();
            const auto descriptor=memory_.allocate(64u,16u),buffer=memory_.allocate(2048u,64u);
            if(!descriptor || !buffer) {
                if(descriptor)memory_.freeAllocation(descriptor);if(buffer)memory_.freeAllocation(buffer);
                throw DeviceRefusal("SDK MCSERV endpoint allocation failed");
            }
            memory_.zeroRam(descriptor,64u);memory_.zeroRam(buffer,2048u);
            memory_.write32(descriptor,0x80000400u);memory_.write32(descriptor+8u,buffer);
            memoryCardServer_=descriptor;memoryCardBuffer_=buffer;memoryCardService_=std::move(service);
        }
        memoryCardServerBase_=base;
    }
    log(LogLevel::Info,"SDK MemoryCard HLE active for IRX "+std::string(name)+" version="+std::to_string(version));
    return 0u; // A real SDK provider now owns this resident module's services.
}
bool Ps2RecompIopServices::rpc_server(RpcRequest& request,bool byAddress) const {
    if(memoryCardService_ && (byAddress?request.serverAddress==memoryCardServer_:request.sid==0x80000400u)) {
        if(memory_.read32(memoryCardServer_)!=0x80000400u || memory_.read32(memoryCardServer_+8u)!=memoryCardBuffer_)
            throw DeviceRefusal("SDK MCSERV endpoint was modified outside its service lifecycle");
        request.sid=0x80000400u;request.serverAddress=memoryCardServer_;request.serverBuffer=memoryCardBuffer_;return true;
    }
    if(padService_)for(const auto& endpoint:padEndpoints_)if(byAddress?request.serverAddress==endpoint.descriptor:request.sid==endpoint.sid) {
        if(memory_.read32(endpoint.descriptor)!=endpoint.sid || memory_.read32(endpoint.descriptor+8u)!=endpoint.buffer)
            throw DeviceRefusal("SDK PADMAN endpoint changed outside its lifecycle");
        request.sid=endpoint.sid;request.serverAddress=endpoint.descriptor;request.serverBuffer=endpoint.buffer;return true;
    }
    return emulator_->nativeRpcServer(request,byAddress);
}
ModuleLoadResult Ps2RecompIopServices::load_irx(std::string_view path,std::span<const std::uint8_t> image) {
    NativeCall call(nativeDepth_);
    return emulator_->loadModuleImage(path,image);
}
void Ps2RecompIopServices::boot_modules() {
    for(const auto* name:{"DMACMAN","CDVDMAN","CDVDFSV","MODLOAD","LOADFILE"}) {
    const auto extended=std::string("X")+name;
    const auto* module=reboot_?reboot_->find(name):nullptr;
    if(!module && reboot_)module=reboot_->find(extended);
    const bool fromReboot=module!=nullptr;
    if(!module && bios_)module=bios_->find(name);
    if(!module && bios_)module=bios_->find(extended);
    // Partial diagnostic images can omit module services; unresolved imports/binds still stop explicitly.
    if(!module && (std::string_view(name)=="MODLOAD" || std::string_view(name)=="LOADFILE"))continue;
    if(!module)throw DeviceRefusal(std::string("IOP native boot image has no ")+name+" module");
    const auto bytes=fromReboot?std::span<const std::uint8_t>(reboot_->bytes):std::span<const std::uint8_t>(bios_->rom0);
    const auto image=bytes.subspan(module->offset,module->size);
    log(LogLevel::Info,"SDK native IRX boot "+module->name+" sha256="+sha256_hex(image));
    const auto result=load_irx(module->name,image);
    if(!result.handled || result.moduleId<=0 || (result.startResult!=0 && result.startResult!=2))
        throw DeviceRefusal("SDK IRX loader/start refused "+module->name);
    if(std::string_view(name)=="DMACMAN")iop_.initialize_hle_sif();
    }
}
std::uint32_t Ps2RecompIopServices::nativeCdvdStatus() const {
    if(!iop_.disc_)throw DeviceRefusal("SDK CDVD status requires mounted canonical media");
    if(iop_.stopped())throw DeviceRefusal("SDK CDVD status after IOP stop: "+*iop_.stop_reason());
    return iop_.cdvd_status_;
}
bool Ps2RecompIopServices::has_cdvd() const noexcept {return bool(iop_.disc_);}
std::vector<std::string> Ps2RecompIopServices::launch_arguments() const {
    return has_cdvd()?std::vector<std::string>{"cdrom0:"+iop_.disc_->boot_path()}:std::vector<std::string>{};
}
std::uint32_t Ps2RecompIopServices::allocate_heap(std::uint32_t bytes) {return memory_.allocate(bytes,64u);}
std::optional<std::pair<std::uint32_t,std::uint32_t>> Ps2RecompIopServices::rpc_call(RpcRequest request) {
    if(executing_cpu() || !rpc_server(request,true))throw DeviceRefusal("SDK native RPC requires a registered server at a scheduler boundary");
    request.send.address=request.serverBuffer;
    if(request.receive.size>kIopRamBytes-15u)throw DeviceRefusal("SDK native RPC response exceeds IOP RAM");
    NativeCall call(nativeDepth_);
    RpcResult result;
    detail::IopService* service=request.sid==0x80000400u?memoryCardService_.get():nullptr;
    const bool padRequest=padService_ && std::any_of(padEndpoints_.begin(),padEndpoints_.end(),[&](const auto& endpoint){return endpoint.sid==request.sid;});
    if(padRequest)service=padService_.get();
    if(service) {
        const auto capacity=padRequest?128u:2048u;
        if(request.send.size>capacity || request.receive.size>capacity)throw DeviceRefusal("SDK HLE RPC exceeds its bounded endpoint");
        if(rpcResponse_ && !memory_.freeAllocation(rpcResponse_))throw DeviceRefusal("SDK MCSERV previous response allocation lost");
        rpcResponse_=memory_.allocate(2048u,16u);
        if(!rpcResponse_ || !memory_.zeroRam(rpcResponse_,2048u))throw DeviceRefusal("SDK MCSERV response allocation failed");
        request.receive.address=rpcResponse_;
        auto& active=padRequest?padRpc_:memoryCardRpc_;
        struct Scope{bool& active;~Scope(){active=false;}} scope{active};active=true;
        result=service->handleRpc(request);
    } else result=emulator_->handleRpc(request);
    if(!result.handled)throw DeviceRefusal("SDK native RPC handler refused its registered request");
    if(!service && !result.signalCompletion)return std::nullopt;
    return stage_rpc_response(result,request.receive.size);
}
std::optional<std::pair<std::uint32_t,std::uint32_t>> Ps2RecompIopServices::rpc_poll(RpcRequest* completed,bool transport) {
    if(executing_cpu())return std::nullopt;
    NativeCall call(nativeDepth_);RpcRequest request;
    const auto result=emulator_->pollNativeRpc(request,RpcPacketToken,transport?RpcPacketToken:0u);
    if(!result.handled)return std::nullopt;
    if(completed)*completed=request;
    return stage_rpc_response(result,request.receive.size);
}
std::pair<std::uint32_t,std::uint32_t> Ps2RecompIopServices::stage_rpc_response(const RpcResult& result,std::uint32_t receiveSize) {
    const auto size=(receiveSize+15u)&~15u;
    if(!size)return {0,0};
    std::vector<std::uint8_t> bytes(size,0);
    if(!memory_.readRam(result.resultAddress,bytes.data(),receiveSize))throw DeviceRefusal("SDK native RPC response outside IOP RAM");
    if(rpcResponse_ && !memory_.freeAllocation(rpcResponse_))throw DeviceRefusal("SDK native RPC response allocation lost");
    rpcResponse_=memory_.allocate(size,16u);
    if(!rpcResponse_ || !memory_.writeRam(rpcResponse_,bytes.data(),bytes.size()))throw DeviceRefusal("SDK native RPC response staging allocation failed");
    return {rpcResponse_,size};
}
int Ps2RecompIopServices::free_heap(std::uint32_t address) {return memory_.freeAllocation(address)?0:-1;}
void Ps2RecompIopServices::configure_bios(std::shared_ptr<const BiosImage> bios) {
    if(!bios || bios->rom0.size()!=4u*1024u*1024u || bios->sha256.empty() || iop_.cycle_ || eeState_.cycles() || bios_)
        throw DeviceRefusal("PS2Recomp IOP BIOS dependency requires one validated image before execution");
    const bool boot=bios->find("CDVDFSV") || bios->find("XCDVDFSV");
    if(boot) {
        if(iop_.iop_sif1_[2]!=0x41000300u || iop_.sif_smcom_!=0x1000u ||
           (iop_.iop_sif0_[2]&(1u<<24u)) || (iop_.ee_sif1_chcr_&0x100u) || !iop_.sif1_fifo_.empty() || !iop_.sif0_fifo_.empty() || iop_.sif1_have_tag_ || iop_.sif1_words_)
            throw DeviceRefusal("SDK IRX bootstrap requires an unused SIF transport");
    }
    bios_=std::move(bios);bootPending_=boot;
    if(bootPending_) {
        // Undo only our unused reset-profile receiver; DMACMAN initializes first.
        iop_.iop_sif1_[2]=0u;iop_.sif_smcom_.reset();iop_.sif_smflg_&=~0x30000u;
    }
}
std::array<std::uint32_t,3> Ps2RecompIopServices::cdvd_versions() const {
    const auto find=[&](const char* first,const char* extended){
        const auto* module=reboot_?reboot_->find(first):bios_?bios_->find(first):nullptr;
        if(!module)module=reboot_?reboot_->find(extended):bios_?bios_->find(extended):nullptr;
        if(!module || !module->version)throw DeviceRefusal("PS2Recomp CDVD version metadata is unavailable in the selected BIOS");
        return std::uint32_t{*module->version};
    };
    return {find("CDVDFSV","XCDVDFSV"),find("CDVDMAN","XCDVDMAN"),0u}; // HLE verbose logging is off.
}
std::string Ps2RecompIopServices::reset(std::string_view arguments,std::uint32_t mode) {
    if(nativeDepth_)throw DeviceRefusal("IOP reset must wait until SDK native execution yields");
    if(mode || !bios_ || !has_cdvd())throw DeviceRefusal("PS2Recomp IOP reset requires mode 0, selected BIOS and mounted media");
    const auto separator=arguments.find(' ');
    if(separator==arguments.npos || arguments.find(' ',separator+1u)!=arguments.npos)
        throw DeviceRefusal("PS2Recomp IOP reset requires a loader and one reboot image");
    const auto loader=parsePs2Path(arguments.substr(0,separator)),image=parsePs2Path(arguments.substr(separator+1u));
    if(loader.device!=Ps2PathDevice::Rom0 || ps2PathLeafKey(loader)!="udnl" || !bios_->find("UDNL") || image.device!=Ps2PathDevice::Cdrom)
        throw DeviceRefusal("PS2Recomp IOP reset loader/image path unsupported");
    // Keep the requested ISO version: upstream path normalization intentionally strips it.
    const auto imagePath=arguments.substr(separator+1u),suffix=imagePath.substr(imagePath.find(':')+1u);
    std::shared_ptr<const IopRebootImage> prepared;
    try {
        if(iop_.disc_->file_size(suffix)>kIopRamBytes)throw DeviceRefusal("IOP reboot image exceeds 2 MiB preparation limit");
        prepared=std::make_shared<const IopRebootImage>(load_iop_reboot_image(iop_.disc_->read(suffix)));
    } catch(const std::exception& error) {throw DeviceRefusal(std::string("IOP reboot image rejected: ")+error.what());}
    // IopEmulator::reset lifecycle, reusing its kernel/CDVD with canonical RAM/time.
    auto identity=prepared->sha256;
    padService_.reset();padState_.reset();padEndpoints_={};padModuleBase_=0;
    memoryCardService_.reset();memoryCardState_.reset();memoryCardManagerVersion_=0;memoryCardManagerBase_=memoryCardServerBase_=memoryCardServer_=memoryCardBuffer_=0;
    memory_.resetAllocations();rpcResponse_=0;emulator_->reset();nativeModules_.clear();bootPending_=false;reboot_=std::move(prepared);
    reset_interrupts();
    return identity;
}
std::uint32_t Ps2RecompIopServices::cdvd_init(std::uint32_t mode) {
    if(!has_cdvd() || iop_.stopped())throw DeviceRefusal("PS2Recomp CDVD initialization requires mounted canonical media");
    ps2x::iop::detail::IopCpuState arguments;arguments.gpr[4]=mode;
    if(!emulator_->dispatchNativeImport("cdvdman",4,arguments))unsupported("CDVD initialization");
    return arguments.gpr[2];
}
void Ps2RecompIopServices::unsupported(const char* service) {throw DeviceRefusal(std::string("PS2Recomp IOP host adapter unavailable: ")+service);}
bool Ps2RecompIopServices::normalizeGuestAddress(std::uint32_t address,std::uint32_t& normalized) const {
    if(!memoryCardIo_ && !memoryCardRpc_ && !padIo_ && !padRpc_)unsupported("EE address outside MemoryCard HLE or PADMAN HLE");
    normalized=address>=0x80000000u && address<0xC0000000u?address&0x1FFFFFFFu:address;
    return normalized<Memory::kMainRamBytes;
}
bool Ps2RecompIopServices::validateGuestOutput(std::uint32_t address,std::size_t size) const {
    std::uint32_t normalized;
    return normalizeGuestAddress(address,normalized) && size<=Memory::kMainRamBytes-normalized &&
        SdkMemory(consoleMemory_).validateGuestRange(0xA0000000u|normalized,size,true);
}
bool Ps2RecompIopServices::readGuest(std::uint32_t address,void* output,std::size_t size) const {
    std::uint32_t normalized;
    if(!normalizeGuestAddress(address,normalized) || size>Memory::kMainRamBytes-normalized || (!output && size))return false;
    auto* bytes=static_cast<std::uint8_t*>(output);
    for(std::size_t i=0;i<size;++i) {
        const auto value=consoleMemory_.load8_observed((0xA0000000u|normalized)+static_cast<std::uint32_t>(i));
        if(value.unknown)throw DeviceRefusal("MemoryCard HLE consumed unknown EE data");
        bytes[i]=static_cast<std::uint8_t>(value.value);
    }
    return true;
}
bool Ps2RecompIopServices::writeGuest(std::uint32_t address,const void* input,std::size_t size) {
    if(!size)return true;
    if(!input || !validateGuestOutput(address,size))return false;
    std::uint32_t normalized;normalizeGuestAddress(address,normalized);
    const auto* bytes=static_cast<const std::uint8_t*>(input);
    for(std::size_t i=0;i<size;++i)consoleMemory_.write8((0xA0000000u|normalized)+static_cast<std::uint32_t>(i),bytes[i]);
    return true;
}
bool Ps2RecompIopServices::readRpcInput(std::uint32_t address,void* output,std::size_t size) const {
    if(!memoryCardRpc_ && !padRpc_)unsupported("RPC input outside active SDK HLE");
    return memory_.readRam(address,output,size);
}
bool Ps2RecompIopServices::writeRpcOutput(std::uint32_t address,const void* input,std::size_t size) {
    if(!memoryCardRpc_ && !padRpc_)unsupported("RPC output outside active SDK HLE");
    return memory_.writeRam(address,input,size);
}
bool Ps2RecompIopServices::zeroGuest(std::uint32_t address,std::size_t size) {
    if(size>1024u*1024u)return false;
    const std::vector<std::uint8_t> zeros(size);
    return writeGuest(address,zeros.data(),size);
}
void Ps2RecompIopServices::configure_memory_card_root(std::filesystem::path root) {
    if(memoryCardState_ || memoryCardIo_ || !root.is_absolute() || root==root.root_path())
        throw DeviceRefusal("MemoryCard save root requires an unused service and an absolute dedicated directory");
    memoryCardRoot_=std::move(root);
}
std::uint32_t Ps2RecompIopServices::allocateIopHandle(IopHandleKind) {unsupported("IOP handle");}
std::uint32_t Ps2RecompIopServices::allocateGuest(std::uint32_t,std::uint32_t) {unsupported("EE allocation");}
void Ps2RecompIopServices::freeGuest(std::uint32_t) {unsupported("EE free");}
void Ps2RecompIopServices::audioCommand(std::uint32_t,std::uint32_t,GuestBuffer,GuestBuffer) {unsupported("audio");}
std::string Ps2RecompIopServices::hostPath(HostPathKind kind) const {
    if(kind==HostPathKind::MemoryCardRoot) {
        if(memoryCardRoot_.empty() || !memoryCardRoot_.is_absolute())unsupported("MemoryCard save root");
        const auto text=memoryCardRoot_.u8string();return {text.begin(),text.end()};
    }
    if(kind==HostPathKind::CdRoot)return {}; // No synthetic directory-backed ISO.
    if(kind==HostPathKind::CdImage)return has_cdvd()?"cdrom0:":"";
    unsupported("host path");
}
std::string Ps2RecompIopServices::translateGuestPath(std::string_view) const {unsupported("host path translation");}
std::uint64_t Ps2RecompIopServices::openHostFile(std::string_view path) {
    if(path!="cdrom0:" || !has_cdvd())unsupported("unmounted disc image");
    return 1; // One handle onto the already-mounted, read-only DiscImage.
}
bool Ps2RecompIopServices::hostFileSize(std::uint64_t handle,std::uint64_t& size) const {
    if(handle!=1 || !has_cdvd())return false;
    size=std::uint64_t{iop_.disc_->user_sector_count()}*2048u;return true;
}
bool Ps2RecompIopServices::readHostFile(std::uint64_t handle,std::uint64_t offset,void* output,std::size_t size,std::size_t& read) {
    read=0;std::uint64_t capacity=0;
    if(!hostFileSize(handle,capacity) || offset>capacity || size>capacity-offset || (!output && size))return false;
    auto* destination=static_cast<std::uint8_t*>(output);std::array<std::uint8_t,2048> sector;
    while(read<size) {
        iop_.disc_->read_sector(static_cast<std::uint32_t>(offset/2048u),sector);
        const auto within=static_cast<std::size_t>(offset%2048u),count=std::min(size-read,2048u-within);
        std::copy_n(sector.data()+within,count,destination+read);read+=count;offset+=count;
    }
    return true;
}
void Ps2RecompIopServices::closeHostFile(std::uint64_t handle) {if(handle!=1)unsupported("disc handle");}
std::int32_t Ps2RecompIopServices::memoryCard(const MemoryCardRequest& request) {
    if(memoryCardIo_)throw DeviceRefusal("Reentrant MemoryCard HLE request");
    struct Scope{bool& active;~Scope(){active=false;}} scope{memoryCardIo_};memoryCardIo_=true;
    return ps2_stubs::nativeMemoryCard(*this,request,memoryCardState_);
}
std::int32_t Ps2RecompIopServices::pad(const PadRequest& request) {
    if(padIo_)throw DeviceRefusal("Reentrant SDK pad backend request");
    struct Scope{bool& active;~Scope(){active=false;}} scope{padIo_};padIo_=true;
    return ps2_stubs::nativePad(*this,request,padState_);
}
bool Ps2RecompIopServices::readPadInput(unsigned port,unsigned slot,std::uint8_t* data,std::size_t size) const {
    if(!padIo_)unsupported("pad input outside SDK backend");
    if(slot || port>=2u || !data || size!=32u)return false;
    const auto input=iop_.poll_pad_input(port);if(!input)return false;
    std::fill_n(data,32u,0u);data[2]=static_cast<std::uint8_t>(input->buttons);data[3]=static_cast<std::uint8_t>(input->buttons>>8u);
    data[4]=input->rx;data[5]=input->ry;data[6]=input->lx;data[7]=input->ly;
    constexpr std::array<unsigned,12> bits{5u,7u,4u,6u,12u,13u,14u,15u,10u,11u,8u,9u};
    for(unsigned i=0;i<bits.size();++i)data[8u+i]=(input->pressureValid&(1u<<i))?input->pressure[i]:(input->buttons&(1u<<bits[i]))?0u:0xFFu;
    return true;
}
bool Ps2RecompIopServices::setPadRumble(unsigned port,unsigned slot,std::uint8_t smallMotor,std::uint8_t largeMotor) {
    return !slot && port<2u && iop_.emit_pad_output(port,smallMotor,largeMotor);
}
void Ps2RecompIopServices::on_vblank() {
    if(!padService_)return;
    if(padRpc_ || padIo_)throw DeviceRefusal("Reentrant PADMAN video update");
    struct Scope{bool& active;~Scope(){active=false;}} scope{padRpc_};padRpc_=true;
    padService_->onVblank();
}
bool Ps2RecompIopServices::hasGuestFunction(std::uint32_t) const {return false;}
bool Ps2RecompIopServices::invokeGuestFunction(std::uint64_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t*) {unsupported("unprepared native guest callback");}
void Ps2RecompIopServices::log(LogLevel,std::string_view text) {iop_.events_.push_back({iop_.cycle_,std::string(text)});}
}
