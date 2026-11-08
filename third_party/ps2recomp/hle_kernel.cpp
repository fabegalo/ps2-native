// SPDX-License-Identifier: GPL-3.0-only
// Adapted from PS2Recomp by its contributors, revision
// e5636981a1c9de368f6b71e819c3dc188dfedbf4:
// ps2xRuntime/src/lib/Kernel/{EeScheduler.cpp,Syscalls/{Thread,Sync,Dispatcher,System}.cpp}
// and Syscalls/Helpers/State.h. See LICENSE in this directory.
// Adaptation: existing EEState/Memory, validated guest accesses, guest clocks,
// syscall-boundary scheduling; no second CPU runtime or host-clock VBLANK.
#include "ps2native/hle.hpp"
#include "ps2native/boot.hpp"
#include <algorithm>
#include <deque>
#include <limits>
#include <map>
#include <tuple>

namespace ps2native {
namespace {
constexpr int KE_OK=0, KE_ERROR=-1, KE_ILLEGAL_PRIORITY=-403, KE_ILLEGAL_MODE=-405;
constexpr int KE_ILLEGAL_THID=-406, KE_UNKNOWN_THID=-407, KE_UNKNOWN_SEMID=-408, KE_UNKNOWN_EVFID=-409;
constexpr int KE_DORMANT=-413, KE_NOT_DORMANT=-414, KE_SEMA_ZERO=-419, KE_SEMA_OVF=-420;
constexpr int KE_EVF_COND=-421, KE_EVF_MULTI=-422, KE_EVF_ILPAT=-423, KE_WAIT_DELETE=-425;
constexpr std::uint32_t WEF_OR=1, WEF_CLEAR=0x10, WEF_CLEAR_ALL=0x20, EA_MULTI=2;
template<typename Map> int allocatePositiveId(int& nextId,const Map& objects) {
    const int first=std::max(1,nextId);
    int candidate=first;
    do {
        if(!objects.contains(candidate)) {
            nextId=candidate==std::numeric_limits<int>::max()?1:candidate+1;
            return candidate;
        }
        candidate=candidate==std::numeric_limits<int>::max()?1:candidate+1;
    } while(candidate!=first);
    return 0;
}
bool eventCondition(std::uint32_t current,std::uint32_t requested,std::uint32_t mode) {
    return (mode&WEF_OR)!=0 ? (current&requested)!=0 : (current&requested)==requested;
}
enum class Status {Running,Ready,Waiting,Dormant};
enum class Wait {None,Sleep,Semaphore,Event};
struct Thread {
    EEState context;
    Status status{Status::Dormant};
    Wait wait{Wait::None};
    int priority{64},initialPriority{64},waitId{};
    std::uint32_t entry{},stack{},stackSize{},gp{},attr{},option{},wakeups{},bits{},mode{},output{};
    std::uint64_t order{};
};
struct Semaphore {
    int count{},maxCount{},initCount{};
    std::uint32_t attr{},option{};
    std::deque<int> waiters;
};
struct EventFlag {
    std::uint32_t bits{},initBits{},attr{},option{};
    std::deque<int> waiters;
};
}

struct HleKernel::Impl {
    EEState& state;
    Memory& memory;
    std::map<int,Thread> threads;
    std::map<int,Semaphore> semaphores;
    std::map<int,EventFlag> events;
    int current{1},nextSemaphore{1},nextEvent{1};
    std::uint64_t order{},calls{};
    bool preempt{};
    // PS2Recomp reserves the final MiB from its default HLE heap.
    std::uint32_t heapBase{},heapLimit{0x01F00000u};
    explicit Impl(EEState& s,Memory& m):state(s),memory(m) {
        auto& main=threads[1];main.context=s;main.status=Status::Running;main.entry=s.pc;
        main.priority=main.initialPriority=0; // PS2Recomp main-thread bootstrap priority
        main.gp=s.reg32(28);main.stack=s.reg32(29);
    }
    static void result(EEState& s,int value) {s.set_reg32_sign_extended(2,static_cast<std::uint32_t>(value));}
    bool buffer(std::uint32_t address,std::size_t bytes,bool write) const {
        if(!address || (address&3u) || bytes>UINT32_MAX || bytes>UINT32_MAX-address)return false;
        try {
            const auto target=memory.resolve(address,bytes,write,"HLE guest buffer");
            return target.region==BusRegion::MainRam || target.region==BusRegion::Scratchpad;
        } catch(const BusError&) {return false;}
    }
    template<std::size_t N> std::array<std::uint32_t,N> read(std::uint32_t address,std::uint32_t fields=UINT32_MAX) const {
        std::array<std::uint32_t,N> data{};
        for(unsigned i=0;i<N;++i) {
            if(!(fields&(1u<<i)))continue;
            const auto value=memory.load32_observed(address+4u*i);
            if(value.unknown)throw std::runtime_error("HLE descriptor contains unknown bits");
            data[i]=static_cast<std::uint32_t>(value.value);
        }
        return data;
    }
    template<std::size_t N> void write(std::uint32_t address,const std::array<std::uint32_t,N>& data) {
        for(unsigned i=0;i<N;++i)memory.write32(address+4u*i,data[i]);
    }
    void ready(Thread& thread,int value) {
        thread.status=Status::Ready;thread.wait=Wait::None;thread.waitId=0;
        thread.order=++order;result(thread.context,value);
    }
    void block(Wait kind,int id=0) {
        auto& self=threads.at(current);self.status=Status::Waiting;self.wait=kind;self.waitId=id;
    }
    void restore(const Thread& target) {
        // Kernel/clock/exception bookkeeping belongs to the one hardware CPU.
        auto registers=target.context;
        registers.cop0=state.cop0;registers.retired_instructions=state.retired_instructions;
        registers.stall_cycles=state.stall_cycles;registers.count_anchor=state.count_anchor;
        registers.compare_written=state.compare_written;registers.bpc_unknown=state.bpc_unknown;
        registers.pccr_unknown=state.pccr_unknown;registers.taglo_unknown=state.taglo_unknown;
        registers.instruction_pc=state.instruction_pc;
        registers.instruction_in_delay_slot=state.instruction_in_delay_slot;
        state=std::move(registers);
    }
    void finish() {
        auto& outgoing=threads.at(current);outgoing.context=state;
        auto best=threads.end();
        for(auto it=threads.begin();it!=threads.end();++it)if(it->second.status==Status::Ready &&
            (best==threads.end() || std::tie(it->second.priority,it->second.order)<std::tie(best->second.priority,best->second.order)))best=it;
        if(outgoing.status==Status::Running && (!preempt || best==threads.end() || best->second.priority>=outgoing.priority))return;
        if(outgoing.status==Status::Running){outgoing.status=Status::Ready;outgoing.order=++order;}
        if(best==threads.end()) {
            current=0;
            throw std::runtime_error("HLE scheduler has no runnable thread; device-event wakeup is not implemented");
        }
        current=best->first;best->second.status=Status::Running;restore(best->second);
    }
    int createThread(std::uint32_t address) {
        if(!buffer(address,0x24,false))return KE_ERROR;
        const auto p=read<9>(address,0x1BEu); // status/current priority are output-only
        const auto priority=static_cast<int>(p[5]);
        if(priority<1 || priority>=128)return KE_ILLEGAL_PRIORITY;
        if((p[1]&3u) || !buffer(p[1],4,false) || !p[2] || !p[3] || !buffer(p[2],p[3],true))return KE_ERROR;
        int id=2;while(id<=255 && threads.contains(id))++id;
        if(id>255)return KE_ERROR;
        Thread thread;thread.entry=p[1];thread.stack=p[2];thread.stackSize=p[3];thread.gp=p[4];
        thread.priority=thread.initialPriority=priority;thread.attr=p[7];thread.option=p[8];
        threads.emplace(id,std::move(thread));return id;
    }
    int startThread(int id,std::uint32_t argument) {
        auto it=threads.find(id);if(it==threads.end())return KE_UNKNOWN_THID;
        auto& target=it->second;if(target.status!=Status::Dormant)return KE_NOT_DORMANT;
        const auto gp=target.gp ? target.gp : state.reg32(28);
        target.context={};target.context.pc=target.entry;target.wakeups=0;target.wait=Wait::None;
        target.context.set_reg32(4,argument);target.context.set_reg32(28,gp);
        target.context.set_reg32(29,(target.stack+target.stackSize)&~0xFu);
        ready(target,0);return KE_OK;
    }
    int deleteThread(int id) {
        if(id<=1)return KE_ILLEGAL_THID;
        auto it=threads.find(id);if(it==threads.end())return KE_UNKNOWN_THID;
        if(it->second.status!=Status::Dormant)return KE_NOT_DORMANT;
        threads.erase(it);return KE_OK;
    }
    int wakeupThread(int id) {
        if(id==0 || id==current)return KE_ILLEGAL_THID;
        auto it=threads.find(id);if(it==threads.end())return KE_UNKNOWN_THID;
        auto& target=it->second;if(target.status==Status::Dormant)return KE_DORMANT;
        if(target.status==Status::Waiting && target.wait==Wait::Sleep)ready(target,KE_OK);
        else if(target.wakeups<INT32_MAX)++target.wakeups;
        else return KE_ERROR;
        return KE_OK;
    }
    int createSemaphore(std::uint32_t address) {
        if(!buffer(address,0x18,false))return KE_ERROR;
        const auto p=read<6>(address,0x36u);const auto initial=static_cast<std::int32_t>(p[2]),maximum=static_cast<std::int32_t>(p[1]);
        if(maximum<=0 || initial<0 || initial>maximum)return KE_ERROR;
        // Bound host allocation; this is an implementation limit, not PS2 hardware capacity.
        if(semaphores.size()>=4096)return KE_ERROR;
        const int id=allocatePositiveId(nextSemaphore,semaphores);if(!id)return KE_ERROR;
        semaphores.emplace(id,Semaphore{initial,maximum,initial,p[4],p[5],{}});return id;
    }
    int deleteSemaphore(int id) {
        auto it=semaphores.find(id);if(it==semaphores.end())return KE_UNKNOWN_SEMID;
        for(int waiter:it->second.waiters)ready(threads.at(waiter),KE_WAIT_DELETE);
        semaphores.erase(it);return id;
    }
    int signalSemaphore(int id) {
        auto it=semaphores.find(id);if(it==semaphores.end())return KE_UNKNOWN_SEMID;
        auto& object=it->second;
        if(!object.waiters.empty()) {
            const int waiter=object.waiters.front();object.waiters.pop_front();ready(threads.at(waiter),id);return id;
        }
        if(object.count==object.maxCount)return KE_SEMA_OVF;
        ++object.count;return id;
    }
    int pollSemaphore(int id,bool wait) {
        auto it=semaphores.find(id);if(it==semaphores.end())return KE_UNKNOWN_SEMID;
        auto& object=it->second;
        if(object.count){--object.count;return id;}
        if(!wait)return KE_SEMA_ZERO;
        object.waiters.push_back(current);block(Wait::Semaphore,id);return 0;
    }
    int createEvent(std::uint32_t address) {
        if(!buffer(address,12,false))return KE_ERROR;
        if(events.size()>=4096)return KE_ERROR;
        const auto p=read<3>(address);const int id=allocatePositiveId(nextEvent,events);if(!id)return KE_ERROR;
        events.emplace(id,EventFlag{p[2],p[2],p[0],p[1],{}});return id;
    }
    int deleteEvent(int id) {
        auto it=events.find(id);if(it==events.end())return KE_UNKNOWN_EVFID;
        for(int waiter:it->second.waiters)ready(threads.at(waiter),KE_WAIT_DELETE);
        events.erase(it);return KE_OK;
    }
    static void consume(EventFlag& flag,std::uint32_t bits,std::uint32_t mode) {
        if(mode&WEF_CLEAR_ALL)flag.bits=0;
        else if(mode&WEF_CLEAR)flag.bits&=~bits;
    }
    int setEvent(int id,std::uint32_t bits) {
        auto it=events.find(id);if(it==events.end())return KE_UNKNOWN_EVFID;
        auto& flag=it->second;
        for(int waiter:flag.waiters)if(auto address=threads.at(waiter).output;address && !buffer(address,4,true))
            throw std::runtime_error("HLE event waiter output is no longer writable");
        flag.bits|=bits;
        for(auto waiter=flag.waiters.begin();waiter!=flag.waiters.end();) {
            auto& thread=threads.at(*waiter);
            if(!eventCondition(flag.bits,thread.bits,thread.mode)){++waiter;continue;}
            if(thread.output)memory.write32(thread.output,flag.bits);
            consume(flag,thread.bits,thread.mode);ready(thread,KE_OK);waiter=flag.waiters.erase(waiter);
        }
        return KE_OK;
    }
    int pollEvent(int id,std::uint32_t bits,std::uint32_t mode,std::uint32_t output,bool wait) {
        auto it=events.find(id);if(it==events.end())return KE_UNKNOWN_EVFID;
        if(!bits)return KE_EVF_ILPAT;
        if((mode&~(WEF_OR|WEF_CLEAR|WEF_CLEAR_ALL)) || ((mode&(WEF_CLEAR|WEF_CLEAR_ALL))==(WEF_CLEAR|WEF_CLEAR_ALL)))return KE_ILLEGAL_MODE;
        if(output && !buffer(output,4,true))return KE_ERROR;
        auto& flag=it->second;
        if(!(flag.attr&EA_MULTI) && !flag.waiters.empty())return KE_EVF_MULTI;
        if(eventCondition(flag.bits,bits,mode)) {
            if(output)memory.write32(output,flag.bits);
            consume(flag,bits,mode);return KE_OK;
        }
        if(!wait)return KE_EVF_COND;
        auto& self=threads.at(current);self.bits=bits;self.mode=mode;self.output=output;
        flag.waiters.push_back(current);block(Wait::Event,id);return 0;
    }
    int referSemaphore(int id,std::uint32_t address) {
        auto it=semaphores.find(id);if(it==semaphores.end())return KE_UNKNOWN_SEMID;
        if(!buffer(address,24,true))return KE_ERROR;
        const auto& s=it->second;
        write(address,std::array<std::uint32_t,6>{static_cast<std::uint32_t>(s.count),static_cast<std::uint32_t>(s.maxCount),
            static_cast<std::uint32_t>(s.initCount),static_cast<std::uint32_t>(s.waiters.size()),s.attr,s.option});return KE_OK;
    }
    int referEvent(int id,std::uint32_t address) {
        auto it=events.find(id);if(it==events.end())return KE_UNKNOWN_EVFID;
        if(!buffer(address,28,true))return KE_ERROR;
        const auto& f=it->second;
        write(address,std::array<std::uint32_t,7>{f.attr,f.option,f.initBits,f.bits,static_cast<std::uint32_t>(f.waiters.size()),0,0});return KE_OK;
    }
    int referThread(int id,std::uint32_t address) {
        if(!id)id=current;
        auto it=threads.find(id);if(it==threads.end())return KE_UNKNOWN_THID;
        if(!buffer(address,48,true))return KE_ERROR;
        const auto& t=it->second;
        const std::uint32_t status=t.status==Status::Running?1:t.status==Status::Ready?2:t.status==Status::Waiting?4:16;
        write(address,std::array<std::uint32_t,12>{status,t.entry,t.stack,t.stackSize,t.gp,
            static_cast<std::uint32_t>(t.initialPriority),static_cast<std::uint32_t>(t.priority),t.attr,t.option,
            static_cast<std::uint32_t>(t.wait),static_cast<std::uint32_t>(t.waitId),t.wakeups});return KE_OK;
    }
    void dispatch() {
        const auto number=static_cast<std::int32_t>(state.reg32(3));
        const auto arg=[&](unsigned i){return state.reg32(i);};
        preempt=number>=0;
        int value=0;
        switch(number) {
            case 0x20:value=createThread(arg(4));break;
            case 0x21:value=deleteThread(static_cast<int>(arg(4)));break;
            case 0x22:{const auto id=arg(4),argument=arg(5);value=startThread(static_cast<int>(id),argument);break;}
            case 0x23:threads.at(current).status=Status::Dormant;break;
            case 0x29:case -0x2A:{
                const int id=arg(4)?static_cast<int>(arg(4)):current,priority=static_cast<int>(arg(5));
                auto it=threads.find(id);
                if(priority<1 || priority>=128)value=KE_ILLEGAL_PRIORITY;
                else if(it==threads.end())value=KE_UNKNOWN_THID;
                else {value=it->second.priority;it->second.priority=priority;}break;
            }
            case 0x2B:case -0x2C:{
                const int priority=arg(4)?static_cast<int>(arg(4)):threads.at(current).priority;
                if(priority<1 || priority>=128)value=KE_ILLEGAL_PRIORITY;
                else if(priority==threads.at(current).priority){threads.at(current).status=Status::Ready;threads.at(current).order=++order;}
                else {
                    auto first=threads.end();
                    for(auto it=threads.begin();it!=threads.end();++it)if(it->second.status==Status::Ready && it->second.priority==priority &&
                        (first==threads.end() || it->second.order<first->second.order))first=it;
                    if(first!=threads.end())first->second.order=++order;
                }break;
            }
            case 0x2F:case -0x2F:value=current;break;
            case 0x30:case -0x31:{const auto id=arg(4),address=arg(5);value=referThread(static_cast<int>(id),address);break;}
            case 0x32:{auto& self=threads.at(current);if(self.wakeups)--self.wakeups;else block(Wait::Sleep);break;}
            case 0x33:case -0x34:value=wakeupThread(static_cast<int>(arg(4)));break;
            case 0x35:case -0x36:{
                const int id=arg(4)?static_cast<int>(arg(4)):current;auto it=threads.find(id);
                if(it==threads.end())value=KE_UNKNOWN_THID;
                else {value=static_cast<int>(it->second.wakeups);it->second.wakeups=0;}break;
            }
            case 0x3C:{
                // PS2Recomp SetupThread convention, with range/overflow validation.
                const auto gp=arg(4),stack=arg(5),size=arg(6),oldSp=arg(29);
                if(static_cast<std::int32_t>(size)<0){value=KE_ERROR;break;}
                std::uint32_t sp=oldSp,base=stack;
                if(stack==UINT32_MAX){
                    if(size>=Memory::kMainRamBytes){value=KE_ERROR;break;}
                    sp=static_cast<std::uint32_t>(Memory::kMainRamBytes)-size;base=sp;
                } else if(stack){
                    if(!buffer(stack,size?size:4,true)){value=KE_ERROR;break;}
                    sp=stack+size;
                } else base=oldSp;
                sp&=~0xFu;
                if(sp<16 || !buffer(sp-16,16,true)){value=KE_ERROR;break;}
                auto& self=threads.at(current);self.stack=base;self.stackSize=size;
                self.gp=gp?gp:arg(28);if(gp)state.set_reg32(28,gp);
                value=static_cast<int>(sp);break;
            }
            case 0x3D:{
                const auto base=arg(4),size=arg(5);
                const auto aligned=(std::uint64_t{base}+15u)&~std::uint64_t{15};
                auto limit=std::uint64_t{0x01F00000u};
                const auto stack=threads.at(current).stack;
                if(stack && stack<limit)limit=stack;
                if(size && size!=UINT32_MAX)limit=std::min(limit,aligned+size);
                if(aligned>=limit || !buffer(static_cast<std::uint32_t>(aligned),static_cast<std::size_t>(limit-aligned),true)) {
                    value=KE_ERROR;break;
                }
                heapBase=static_cast<std::uint32_t>(aligned);heapLimit=static_cast<std::uint32_t>(limit);
                value=static_cast<int>(heapBase);break;
            }
            case 0x3E:value=static_cast<int>(heapLimit);break;
            case 0x40:value=createSemaphore(arg(4));break;
            case 0x41:case -0x49:value=deleteSemaphore(static_cast<int>(arg(4)));break;
            case 0x42:case -0x43:value=signalSemaphore(static_cast<int>(arg(4)));break;
            case 0x44:case 0x45:case -0x46:value=pollSemaphore(static_cast<int>(arg(4)),number==0x44);break;
            case 0x47:case -0x48:{const auto id=arg(4),address=arg(5);value=referSemaphore(static_cast<int>(id),address);break;}
            case 0x50:value=createEvent(arg(4));break;
            case 0x51:value=deleteEvent(static_cast<int>(arg(4)));break;
            case 0x52:case -0x53:{const auto id=arg(4),bits=arg(5);value=setEvent(static_cast<int>(id),bits);break;}
            case 0x54:case -0x55:{
                const int id=static_cast<int>(arg(4));const auto mask=arg(5);auto it=events.find(id);
                if(it==events.end())value=KE_UNKNOWN_EVFID;else it->second.bits&=mask;break;
            }
            case 0x56:case 0x57:case -0x58:{
                const auto id=arg(4),bits=arg(5),mode=arg(6),output=arg(7);
                value=pollEvent(static_cast<int>(id),bits,mode,output,number==0x56);break;
            }
            case 0x59:case -0x5A:{const auto id=arg(4),address=arg(5);value=referEvent(static_cast<int>(id),address);break;}
            case 0x64:case -0x68:memory.flush_hle_cache(arg(4));break;
            case 0x7F:value=static_cast<int>(Memory::kMainRamBytes);break;
            default:throw std::runtime_error("unsupported PS2Recomp HLE syscall "+std::to_string(number));
        }
        result(state,value);++calls;
    }
};

HleKernel::HleKernel(EEState& state,Memory& memory) {
    if(memory.hle_kernel_)throw std::runtime_error("A console already owns an HLE kernel");
    if(memory.mapping()!=Memory::Mapping::DirectElf)throw std::runtime_error("HLE attachment requires an explicitly prepared ELF session");
    p_=std::make_unique<Impl>(state,memory);memory.hle_kernel_=this;memory.hle_used_=true;
    // HLE ELF launch grants COP1/COP2 access; firmware/reset paths keep their own Status.
    state.set_cp0(Cop0::Status,state.cp0(Cop0::Status)|kStatusCU1|(1u<<30));
}
HleKernel::~HleKernel(){if(p_)p_->memory.hle_kernel_=nullptr;}
void HleKernel::dispatch_syscall(ExecutionContext& context) {
    if(&context.state!=&p_->state || &context.memory!=&p_->memory)throw std::runtime_error("HLE syscall belongs to another console");
    check_running();p_->dispatch();
}
void HleKernel::finish_syscall(){p_->finish();}
void HleKernel::check_running() const {
    if(!p_->current)throw std::runtime_error("HLE scheduler has no runnable thread; device-event wakeup is not implemented");
}
std::uint64_t HleKernel::syscall_count() const noexcept {return p_->calls;}
int HleKernel::current_thread_id() const noexcept {return p_->current;}
} // namespace ps2native
