// SPDX-License-Identifier: GPL-3.0-only
// Host interfaces for the directly compiled PS2Recomp kernel. See licenses/PS2Recomp.
#include "ps2native/hle.hpp"
#include "ps2_runtime.h"
#include "sdk_kernel_common.hpp"
#include "sdk/iop_services.hpp"
#include "Dispatcher.h"
#include <deque>
#include <limits>

namespace {
using namespace ps2native;
struct GuestView {std::uint32_t address;bool output;bool publish{};std::vector<std::uint8_t> bytes;};
thread_local Ps2SdkKernelState* activeKernel{};
thread_local ps2native::Memory* activeMemory{};
thread_local std::deque<GuestView>* activeViews{};
thread_local std::array<std::shared_ptr<void>,2>* activeSlots{};
struct KernelScope {
    Ps2SdkKernelState* previousKernel=activeKernel;
    ps2native::Memory* previousMemory=activeMemory;
    std::deque<GuestView>* previousViews=activeViews;
    std::array<std::shared_ptr<void>,2>* previousSlots=activeSlots;
    KernelScope(Ps2SdkKernelState& kernel,ps2native::Memory& memory,std::deque<GuestView>& views,std::array<std::shared_ptr<void>,2>& slots) {
        activeKernel=&kernel;activeMemory=&memory;activeViews=&views;activeSlots=&slots;
    }
    ~KernelScope(){activeKernel=previousKernel;activeMemory=previousMemory;activeViews=previousViews;activeSlots=previousSlots;}
};
void initializeKernelTlb(EEState& state,Memory& memory) {
    // 32-MiB EE kernel layout documented by ps2sdk ee/kernel/src/tlbfunc.c.
    // Construct address translations here; device behavior still belongs to Memory.
    unsigned index=0;
    const auto map=[&](std::uint32_t address,std::uint32_t physical,std::uint32_t page,
                       unsigned even,unsigned odd) {
        memory.tlb_write(index++,{(page-1u)*2u & 0x01FFE000u,address,
            (physical>>6u)|even,((physical+page)>>6u)|odd});
    };
    memory.tlb_write(index++,{0,0x70000000u,0x80000007u,7});
    map(0xFFFF8000u,0x00078000u,0x4000u,0x1Fu,0x1Fu);
    for(unsigned page=0;page<16;page+=2) {
        const auto physical=0x10000000u+page*0x1000u;
        // The kernel's individual register pages retain their write permissions.
        map(physical,physical,0x1000u,page==12?0x13u:0x17u,page==0?0x13u:0x17u);
    }
    for(const auto address:{0x11000000u,0x12000000u})map(address,address,0x10000u,0x17u,0x15u);
    map(0x1E000000u,0x1E000000u,0x1000000u,0x17u,0x17u);
    const auto ram=[&](std::uint32_t alias,unsigned flags,std::uint32_t start) {
        for(auto address=start;address<Memory::kMainRamBytes;) {
            const auto page=address<0x200000u?0x40000u:address<0x800000u?0x100000u:0x400000u;
            map(alias+address,address,page,flags,flags);address+=page*2u;
        }
    };
    ram(0,0x1Fu,0x80000u);ram(0x20000000u,0x17u,0x80000u);
    state.set_cp0(Cop0::Wired,index);state.set_cp0(Cop0::Random,47);
    ram(0x30000000u,0x3Fu,0x100000u);
    for(;index<Memory::kTlbEntries;++index)
        memory.tlb_write(index,{0,0xE0000000u+(index<<13u),0,0});
}
}
Ps2SdkKernelState& ps2n_sdk_kernel_state() {
    if(!activeKernel)throw std::runtime_error("PS2Recomp kernel state is not bound to a console");
    return *activeKernel;
}
namespace ps2native {
std::shared_ptr<void>& sdk_kernel_service_slot(unsigned index) {
    if(!activeSlots || index>=activeSlots->size())throw std::runtime_error("Unbound SDK service storage");
    return activeSlots->at(index);
}
void sdk_kernel_return(std::int64_t value) {
    if(value>=0 && activeViews)for(auto& view:*activeViews)if(view.output)view.publish=true;
}
Memory& sdk_kernel_memory() {
    if(!activeMemory)throw std::runtime_error("PS2Recomp memory interface is not bound");
    return *activeMemory;
}
void SdkMemory::syncGuestWrite(std::uint32_t address,unsigned bytes) {
    if(activeMemory!=&memory_ || !activeViews)return;
    const auto written=memory_.resolve(address,bytes,false,"SDK output write");
    if(written.region!=BusRegion::MainRam && written.region!=BusRegion::Scratchpad)return;
    for(auto& view:*activeViews)for(std::size_t offset=0;offset<view.bytes.size();) {
        const auto guest=view.address+static_cast<std::uint32_t>(offset);
        const auto chunk=std::min(view.bytes.size()-offset,std::size_t{4096u-(guest&4095u)});
        const auto target=memory_.resolve(guest,1,false,"SDK output alias");
        if(target.region==written.region) {
            const auto begin=std::max<std::uint64_t>(target.physical,written.physical);
            const auto end=std::min<std::uint64_t>(target.physical+chunk,std::uint64_t{written.physical}+bytes);
            for(auto physical=begin;physical<end;++physical) {
                const auto index=offset+static_cast<std::size_t>(physical-target.physical);
                const auto value=memory_.load8_observed(view.address+static_cast<std::uint32_t>(index));
                if(!value.unknown)view.bytes[index]=static_cast<std::uint8_t>(value.value);
            }
        }
        offset+=chunk;
    }
}
}
void* ps2n_sdk_guest_view(std::uint32_t address,std::size_t size,bool output,std::uint64_t inputWords) {
    if(!activeMemory || !activeViews)throw std::runtime_error("PS2Recomp guest access outside a syscall transaction");
    std::uint32_t offset{};bool scratch{};
    if(!address || !resolveEeGuestRange(address,size,offset,scratch))return nullptr;
    if(!ps2native::SdkMemory(*activeMemory).validateGuestRange(address,size,output))return nullptr;
    if(size>1024u*1024u || activeViews->size()>=256u)throw std::runtime_error("PS2Recomp guest transaction exceeds its bounded interface capacity");
    for(auto& view:*activeViews)if(view.address==address && view.bytes.size()==size && view.output==output)return view.bytes.data();
    GuestView view{address,output,false,std::vector<std::uint8_t>(size)};
    for(std::size_t i=0;i<size;++i) {
        if(output)view.bytes[i]=static_cast<std::uint8_t>(activeMemory->load8_observed(address+static_cast<std::uint32_t>(i)).value);
        else if(i/4u>=64u || (inputWords&(1ull<<(i/4u)))) {
            const auto byte=activeMemory->load8_observed(address+static_cast<std::uint32_t>(i));
            if(byte.unknown)throw std::runtime_error("PS2Recomp HLE descriptor contains unknown bits");
            view.bytes[i]=static_cast<std::uint8_t>(byte.value);
        }
        else view.bytes[i]=static_cast<std::uint8_t>(activeMemory->load8_observed(address+static_cast<std::uint32_t>(i)).value);
    }
    activeViews->push_back(std::move(view));return activeViews->back().bytes.data();
}
namespace ps2native {
struct HleKernel::Impl {
    PS2Runtime& runtime;
    EEState& state;
    Memory& memory;
    Ps2SdkKernelState globals;
    std::array<std::shared_ptr<void>,2> serviceSlots;
    SdkMemory memoryInterface;
    EeScheduler scheduler;
    std::unique_ptr<Ps2RecompIopServices> iopServices;
    std::deque<GuestView> views;
    EEState* syscallContext{};
    std::uint32_t resumePc{};
    std::uint64_t calls{},accountedCycles{};
    std::uint64_t videoGeneration{UINT64_MAX},videoStart{UINT64_MAX},videoEnd{UINT64_MAX};
    std::uint32_t heapBase{},heapLimit{0x01F00000u},callbackTop{0x00078000u};
    std::optional<std::uint32_t> interruptStatus;
    std::vector<PS2Runtime::EeExitHandlerRegistration> exitHandlers;
    explicit Impl(PS2Runtime& owner)
        :runtime(owner),state(owner.cpu()),memory(owner.memory()),memoryInterface(memory),scheduler(runtime),
         iopServices(std::make_unique<Ps2RecompIopServices>(memory,state)) {}
    void syncTime() {
        auto elapsed=state.cycles()-accountedCycles;
        while(elapsed) {
            const auto step=static_cast<std::uint32_t>(std::min<std::uint64_t>(elapsed,UINT32_MAX));
            scheduler.accountCycles(step);elapsed-=step;
        }
        accountedCycles=state.cycles();
    }
    void commit() {
        for(const auto& view:views)if(view.output && view.publish)for(std::size_t i=0;i<view.bytes.size();++i)
            memory.write8(view.address+static_cast<std::uint32_t>(i),view.bytes[i]);
        views.clear();
    }
    void postVideoEvents() {
        const auto now=state.cycles();
        if(videoGeneration!=memory.gs().timing_generation()) {
            videoGeneration=memory.gs().timing_generation();videoStart=videoEnd=UINT64_MAX;
            const auto clock=memory.gs().vertical_clock();
            if(clock.state==GsRegisters::VerticalClock::State::Running && now!=UINT64_MAX) {
                videoStart=clock.next_edge(now+1u,false);videoEnd=clock.next_edge(now+1u,true);
            }
        }
        if(now<std::min(videoStart,videoEnd))return;
        const auto clock=memory.gs().vertical_clock();
        for(unsigned count=0;std::min(videoStart,videoEnd)<=now;++count) {
            if(count==64u)throw std::runtime_error("SDK video event observation exceeded its bounded batch");
            const bool end=videoEnd<videoStart;
            auto& edge=end?videoEnd:videoStart;
            scheduler.postEvent({end?EeEventType::VBlankEnd:EeEventType::VBlankStart,0,0});
            if(!end)iopServices->on_vblank();
            edge=edge==UINT64_MAX?UINT64_MAX:clock.next_edge(edge+1u,end);
        }
    }
    void boundary() {
        KernelScope scope(globals,memory,views,serviceSlots);
        if(syscallContext)*syscallContext=state;
        syncTime();
        postVideoEvents();
        scheduler.finishDispatch();
        auto* next=scheduler.prepareDispatch();
        if(!next)throw std::runtime_error("PS2Recomp scheduler stopped");
        auto restored=*next;
        restored.cop0=state.cop0;restored.retired_instructions=state.retired_instructions;
        restored.stall_cycles=state.stall_cycles;restored.count_anchor=state.count_anchor;
        restored.compare_written=state.compare_written;restored.bpc_unknown=state.bpc_unknown;
        restored.pccr_unknown=state.pccr_unknown;restored.taglo_unknown=state.taglo_unknown;
        restored.instruction_pc=state.instruction_pc;
        restored.instruction_in_delay_slot=state.instruction_in_delay_slot;
        state=std::move(restored);
        const auto* current=scheduler.currentThread();
        const bool interrupt=current && !current->invocations.empty() && current->invocations.back().kind==GuestInvocationKind::Interrupt;
        if(interrupt && !interruptStatus)interruptStatus=state.cp0(Cop0::Status);
        if(interrupt)state.set_cp0(Cop0::Status,state.cp0(Cop0::Status)|kStatusEXL);
        else if(interruptStatus){state.set_cp0(Cop0::Status,*interruptStatus);interruptStatus.reset();}
        memory.set_cpu_context(state.cp0(Cop0::Status),state.cp0(Cop0::EntryHi));
        accountedCycles=state.cycles();
        syscallContext=nullptr;
    }
};
HleKernel::HleKernel(PS2Runtime& runtime) {
    auto& state=runtime.cpu();auto& memory=runtime.memory();
    if(memory.hle_kernel_)throw std::runtime_error("A console already owns an HLE kernel");
    if(memory.mapping()!=Memory::Mapping::DirectElf)throw std::runtime_error("HLE attachment requires an explicitly prepared ELF session");
    if(!memory.data_cache_enabled()) {
        memory.diagnostic_data_cache();
        // HLE kernel software initializes invalid tags; hardware reset still leaves PFNs unknown.
        for(unsigned index=0;index<64u;++index)for(unsigned way=0;way<2u;++way)
            (void)memory.data_cache_operation(0x12u,index*64u+way,{0u,0u});
    }
    p_=std::make_unique<Impl>(runtime);memory.hle_kernel_=this;memory.hle_used_=true;
    state.set_cp0(Cop0::Status,state.cp0(Cop0::Status)|kStatusCU1|(1u<<30)|kStatusIE|kStatusEIE|kStatusIM2|kStatusIM3);
    memory.set_clock(&state.retired_instructions,&state.stall_cycles);
    initializeKernelTlb(state,memory);
    p_->memoryInterface.initializeKernelDevices();
    p_->runtime.initializeEeKernelState(nullptr);
    // Native SDK rdram is an opaque console token; all reads/writes use checked bus views.
    p_->scheduler.bindMainContextForSyscall(state,reinterpret_cast<std::uint8_t*>(&memory));
    p_->syncTime();
    p_->postVideoEvents();
    try {const_cast<Iop&>(memory.iop()).start_hle_services(p_->iopServices.get());}
    catch(...) {memory.hle_kernel_=nullptr;throw;}
}
HleKernel::~HleKernel() {
    if(p_){const_cast<Iop&>(p_->memory.iop()).stop_hle_services();p_->memory.hle_kernel_=nullptr;}
}
void HleKernel::dispatch_syscall(ExecutionContext& context) {
    if(&context.state!=&p_->state || &context.memory!=&p_->memory)throw std::runtime_error("HLE syscall belongs to another console");
    check_running();p_->syncTime();KernelScope scope(p_->globals,p_->memory,p_->views,p_->serviceSlots);
    auto* saved=p_->scheduler.currentContext();
    if(!saved)throw std::runtime_error("PS2Recomp scheduler has no current context");
    *saved=p_->state;saved->pc+=4u;
    const auto callingThread=p_->scheduler.currentThreadId();
    const auto depth=p_->scheduler.currentThread()->invocations.size();
    const auto before=*saved;
    p_->syscallContext=saved;
    const auto number=sdk_gpr32(saved,3);
    try {
        if(!ps2_syscalls::dispatchNumericSyscall(number,reinterpret_cast<std::uint8_t*>(&p_->memory),saved,&p_->runtime))
            throw std::runtime_error("unsupported PS2Recomp HLE syscall "+std::to_string(number));
    } catch(const EeDispatcherTransfer&) {}
    catch(...) {p_->views.clear();p_->syscallContext=nullptr;throw;}
    p_->commit();
    auto* thread=p_->scheduler.thread(callingThread);
    saved=thread?(depth?&thread->invocations.at(depth-1u).context:&thread->context):nullptr;
    p_->syscallContext=saved;
    const auto result=saved?*saved:before;
    p_->resumePc=result.pc;
    const auto pc=p_->state.pc;p_->state=result;p_->state.pc=pc;++p_->calls;
}
void HleKernel::finish_syscall() {p_->state.pc=p_->resumePc;p_->boundary();}
bool HleKernel::complete_syscall_return() {
    if(p_->state.pc)return false;
    p_->syscallContext=p_->scheduler.currentContext();p_->boundary();return true;
}
bool HleKernel::dispatch_interrupt(std::uint32_t lines) {
    check_running();auto* current=p_->scheduler.currentContext();*current=p_->state;
    p_->syscallContext=current;
    if(!(lines&(kStatusIM2|kStatusIM3)) || (lines&kStatusIM7))return false;
    p_->state.set_cp0(Cop0::Epc,p_->state.delay_pending?p_->state.pc-4u:p_->state.pc);
    if(p_->state.delay_pending){p_->state.pc-=4u;p_->state.delay_pending=false;p_->state.delay_indirect=false;}
    p_->runtime.pollCanonicalInterrupts();
    p_->boundary();return true;
}
void HleKernel::configure_bios(std::shared_ptr<const BiosImage> bios) {
    if(p_->state.retired_instructions)throw DeviceRefusal("HLE BIOS dependency cannot change during execution");
    p_->iopServices->configure_bios(std::move(bios));
}
bool HleKernel::poll_execution(ExecutionContext& context) {
    p_->postVideoEvents();
    const auto elapsed=p_->state.cycles()-p_->accountedCycles;
    if(!elapsed)return false;
    const auto due=p_->scheduler.checkpointDue(static_cast<std::uint32_t>(std::min<std::uint64_t>(elapsed,UINT32_MAX)));
    p_->accountedCycles=p_->state.cycles();
    if(!due || p_->state.delay_pending)return false;
    flush_interrupt_cache(context);
    p_->syscallContext=p_->scheduler.currentContext();p_->boundary();
    return true;
}
std::uint64_t HleKernel::next_deadline() const {return std::min({p_->scheduler.nextNativeDeadline(),p_->videoStart,p_->videoEnd});}
void HleKernel::check_running() const {
    if(!p_->scheduler.currentContext())throw std::runtime_error("PS2Recomp scheduler has no runnable thread");
}
std::uint64_t HleKernel::syscall_count() const noexcept {return p_->calls;}
int HleKernel::current_thread_id() const noexcept {return p_->scheduler.currentThreadId();}
}
EeScheduler& PS2Runtime::eeScheduler(){return memory().hle_kernel()->p_->scheduler;}
std::vector<std::string> PS2Runtime::launchArguments() const {
    return const_cast<PS2Runtime*>(this)->memory().hle_kernel()->p_->iopServices->launch_arguments();
}
ps2native::SdkMemory& PS2Runtime::kernelMemory(){return memory().hle_kernel()->p_->memoryInterface;}
bool PS2Runtime::hasFunction(std::uint32_t address) const {
    if(!address || (address&3u))return false;
    (void)const_cast<PS2Runtime*>(this)->memory().fetch32(address);return true;
}
void PS2Runtime::configureGuestHeap(std::uint32_t base,std::uint32_t limit) {
    if(base>=limit || limit>ps2native::Memory::kMainRamBytes)throw std::runtime_error("PS2Recomp heap lies outside canonical EE RAM");
    auto& kernel=*memory().hle_kernel()->p_;kernel.heapBase=base;kernel.heapLimit=limit;
}
std::uint32_t PS2Runtime::guestHeapBase() const {return const_cast<PS2Runtime*>(this)->memory().hle_kernel()->p_->heapBase;}
std::uint32_t PS2Runtime::guestHeapEnd() const {return guestHeapBase();}
std::uint32_t PS2Runtime::guestHeapLimit() const {return const_cast<PS2Runtime*>(this)->memory().hle_kernel()->p_->heapLimit;}
std::uint32_t PS2Runtime::guestMalloc(std::uint32_t,std::uint32_t) {throw std::runtime_error("PS2Recomp EE allocation interface is unavailable");}
void PS2Runtime::guestFree(std::uint32_t) {throw std::runtime_error("PS2Recomp EE free interface is unavailable");}
std::uint32_t PS2Runtime::reserveAsyncCallbackStack(std::uint32_t bytes,std::uint32_t alignment) {
    if(!bytes || !alignment || (alignment&(alignment-1u)))return 0;
    auto& top=memory().hle_kernel()->p_->callbackTop;
    if(bytes>top || top-bytes<0x20000u)return 0;
    const auto end=top;top=(top-bytes)&~(alignment-1u);
    if(top<0x20000u)throw std::runtime_error("PS2Recomp callback stack reserve exhausted");
    return 0x80000000u|end;
}
void PS2Runtime::advanceIopEeCycles(std::uint64_t cycles) {cpu().stall(cycles);memory().sync_iop();}
std::uint64_t PS2Runtime::nextCanonicalEventCycle() const {
    auto& owner=*const_cast<PS2Runtime*>(this);
    return owner.memory().idle_execution_horizon().value_or(owner.cpu().cycles()+8u);
}
std::vector<PS2Runtime::EeExitHandlerRegistration> PS2Runtime::takeEeExitHandlers(int) {
    auto& handlers=memory().hle_kernel()->p_->exitHandlers;auto result=std::move(handlers);handlers.clear();return result;
}
void PS2Runtime::removeEeExitHandlers(int) {memory().hle_kernel()->p_->exitHandlers.clear();}
bool PS2Runtime::findEeSyscallOverride(std::uint32_t number,std::uint32_t& handler) {
    const auto signedNumber=static_cast<std::int32_t>(number);
    const auto index=signedNumber<0?-static_cast<std::int64_t>(signedNumber):signedNumber;
    handler=index<256?memory().read32(0x80011F80u+static_cast<std::uint32_t>(index)*4u):0u;
    return handler!=0;
}
void PS2Runtime::setEeSyscallOverride(std::uint8_t*,std::uint32_t number,std::uint32_t handler) {
    if(number>=256u)throw std::runtime_error("PS2Recomp syscall table index is out of range");
    if(handler)(void)memory().fetch32(handler);
    memory().write32(0x80011F80u+number*4u,handler);
}
void PS2Runtime::initializeEeKernelState(std::uint8_t*) {
    for(unsigned i=0;i<256;++i)memory().write32(0x80011F80u+i*4u,0u);
    memory().write32(0x800002F0u,0x8001u);memory().write32(0x800002F8u,0x1F80u);
}

void PS2Runtime::pollCanonicalInterrupts() {
    auto& kernel=*memory().hle_kernel()->p_;
    KernelScope scope(kernel.globals,kernel.memory,kernel.views,kernel.serviceSlots);
    kernel.syncTime();kernel.postVideoEvents();kernel.scheduler.pollNativeEvents();
    memory().sync_iop();
    if(memory().intc_interrupt_pending()) {
        const auto mask=memory().read32(0xB000F010u)&0x7FFFu;
        const auto observed=memory().load32_observed(0xB000F000u);
        if(observed.unknown&mask)throw std::runtime_error("HLE interrupt source contains unknown unmasked state");
        const auto pending=static_cast<std::uint32_t>(observed.value)&mask;
        for(unsigned cause=0;cause<15;++cause)if(pending&(1u<<cause))eeScheduler().dispatchIrq(false,cause);
        memory().write32(0xB000F000u,pending);
    }
    if(memory().dmac_interrupt_pending()) {
        const auto observed=memory().load32_observed(0xB000E010u);
        if(observed.unknown&0x03FF03FFu)throw std::runtime_error("HLE DMA interrupt source contains unknown unmasked state");
        const auto status=static_cast<std::uint32_t>(observed.value),pending=status&(status>>16u)&0x3FFu;
        for(unsigned cause=0;cause<10;++cause)if(pending&(1u<<cause))eeScheduler().dispatchIrq(true,cause);
        memory().write32(0xB000E010u,pending);
    }
}
