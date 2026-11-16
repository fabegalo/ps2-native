// Thread/import contracts informed by ps2autotests and exoteric33 a50471a (MIT).
// API reference: ps2dev/ps2sdk iop/system/threadman/src/thbase.c (behavior only, no SDK source imported).
#include "ps2native/engine/console.hpp"
#include "iop_cdvd.hpp"
#include <algorithm>
#include <limits>

namespace ps2native::engine {
bool Console::iop_work_pending() const {
    if(iop_intr_enabled && (iop_vblank_pending || !iop_vblank_jobs.empty()))return true;
    if(cdvd->irq_ready())return true;
    if (iop_active || !sif.iop_commands.empty()) return true;
    for (const auto& [id,t] : iop_threads)
        if (t.status==2 || ((t.status&4) && t.wait==2 && t.deadline!=~0ull)) return true;
    return false;
}
void Console::iop_ready(std::uint32_t id,std::int32_t result) {
    auto& t=iop_threads.at(id);
    t.context.gpr[2]=static_cast<std::uint32_t>(result);
    t.status=(t.status&8) ? 8 : 2;t.wait=0;t.wait_id=0;t.deadline=~0ull;t.order=++iop_order;
    for (auto* objects : {&iop_semas,&iop_events,&iop_mailboxes})
        for (auto& [object,o] : *objects) std::erase(o.waiters,id);
    iop_reschedule=true;
}
void Console::iop_block(std::uint32_t kind,std::uint32_t object) {
    if (!iop_current || iop_callback_active || iop_dispatch_depth) throw std::runtime_error("IOP blocking call outside dispatchable thread context");
    auto& t=iop_threads.at(iop_current);t.status=4;t.wait=kind;t.wait_id=object;iop_reschedule=true;
}
void Console::iop_schedule() {
    if (iop_callback_active || iop_dispatch_depth) return;
    if (iop_current) {
        auto it=iop_threads.find(iop_current);
        if (it!=iop_threads.end()) {
            it->second.context=iop;
            if (it->second.status==1) it->second.status=2;
            if (it->second.delete_on_exit) iop_threads.erase(it);
        }
    }
    auto best=iop_threads.end();
    for (auto it=iop_threads.begin();it!=iop_threads.end();++it)
        if (it->second.status==2 && (best==iop_threads.end() ||
            std::pair(it->second.priority,it->second.order)<std::pair(best->second.priority,best->second.order))) best=it;
    iop_reschedule=false;
    if (best!=iop_threads.end()) {
        iop_current=best->first;best->second.status=1;iop=best->second.context;iop_active=true;
    } else {
        iop_current=0;iop_active=false;
        auto wake=~0ull;
        for (const auto& [id,t] : iop_threads) if ((t.status&4) && t.wait==2) wake=std::min(wake,t.deadline);
        if (wake!=~0ull) iop_next_tick=ticks+((std::max(ticks+8,wake)-ticks+7)/8)*8-8;
    }
}
bool Console::iop_thread_import(const std::string& library,std::uint32_t ordinal) {
    if (library!="thbase" && library!="thsemap" && library!="thevent" && library!="thmsgbx") return false;
    if(library=="thbase") {
        switch(ordinal) {
        case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 12: case 13:
        case 14: case 15: case 16: case 17: case 18: case 19: case 20: case 22: case 23:
        case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31: case 32:
        case 33: case 34: case 39: case 40: case 42: case 43:break;
        default:return false;
        }
    } else if(library=="thevent") {
        if(ordinal<4 || ordinal>14)return false;
    } else if(ordinal<4 || ordinal>12 || ordinal==10)return false;
    const auto a=iop_arg(0),b=iop_arg(1),c=iop_arg(2),d=iop_arg(3);
    auto result=[&](std::int32_t value) { iop.gpr[2]=static_cast<std::uint32_t>(value);return true; };
    if (library=="thbase") {
        const auto id=(ordinal==24 || ordinal==33 || ordinal==42) ? iop_current : a ? a : iop_current;
        auto it=iop_threads.find(id);
        if (ordinal==4) {
            const auto attr=iop_read(a,4),option=iop_read(a+4,4),entry=iop_read(a+8,4),size=iop_read(a+12,4),priority=iop_read(a+16,4);
            if (size>0x50000) return result(-400);
            if (attr&~0x03000008u) return result(-401);
            if (entry&3) return result(-402);
            if (!priority || priority>=127) return result(-403);
            if (size<0x130) return result(-404);
            const auto rounded=(size+0xff)&~0xffu;
            auto stack=0x180000u;
            std::vector<std::pair<std::uint32_t,std::uint32_t>> occupied;
            for (const auto& [tid,t] : iop_threads) if (t.stack>=0x180000 && t.stack<0x1d0000) occupied.emplace_back(t.stack,t.size);
            std::sort(occupied.begin(),occupied.end());
            for (const auto& [base,bytes] : occupied) { if (stack+rounded<=base) break;stack=std::max(stack,base+bytes); }
            if (rounded>0x1d0000-stack) return result(-400);
            IopThread t;t.attr=attr;t.option=option;t.entry=entry;t.size=rounded;t.stack=stack;t.gp=iop.gpr[28];t.initial_priority=priority;
            std::fill_n(iop_ram.data()+stack,rounded-0x30,0xff);
            const auto created=iop_next_thread++;iop_threads.emplace(created,t);return result(created);
        }
        if (ordinal==20) return result(iop_current);
        if (ordinal==12) { const auto old=iop_dispatch_depth++;return result(old); }
        if (ordinal==13) { if (!iop_dispatch_depth) return result(-1);--iop_dispatch_depth;iop_reschedule=true;return result(0); }
        if (ordinal==33) {
            if (it==iop_threads.end()) return result(-407);
            result(0);iop_block(2);it->second.deadline=ticks+std::max<std::uint64_t>(8,(std::uint64_t{a}*36864/1000)*8);return true;
        }
        if (ordinal==34 || ordinal==43) {
            const auto clock=ticks/8;
            if (ordinal==43) return result(clock);
            iop_write(a,clock,4);iop_write(a+4,clock>>32,4);return result(0);
        }
        if (ordinal==39) { const auto clock=std::uint64_t{a}*36864/1000;iop_write(b,clock,4);iop_write(b+4,clock>>32,4);return result(0); }
        if (ordinal==40) { const auto clock=std::uint64_t{iop_read(a,4)}|(std::uint64_t{iop_read(a+4,4)}<<32);const auto us=clock*1000/36864;iop_write(b,us/1000000,4);iop_write(c,us%1000000,4);return result(0); }
        if (ordinal==8 || ordinal==9) {
            if (!iop_current) throw std::runtime_error("IOP ExitThread without current context");
            auto& t=iop_threads.at(iop_current);t.status=16;t.delete_on_exit=ordinal==9;iop_reschedule=true;return result(0);
        }
        if ((ordinal==5 || ordinal==6 || ordinal==7 || ordinal==10 || ordinal==11 || ordinal==18 || ordinal==19 || (ordinal>=25 && ordinal<=32)) && !a && ordinal!=27 && ordinal!=28) return result(-406);
        if (ordinal==16 || ordinal==17) {
            for(auto& [tid,q] : iop_threads) if (tid==iop_current && (!a || q.priority==a)) q.order=++iop_order;
            iop_reschedule=true;return result(0);
        }
        if (it==iop_threads.end()) return result(-407);
        auto& t=it->second;
        switch(ordinal) {
        case 5:
            if (t.status!=16 || id==iop_current) return result(-414);
            iop_threads.erase(it);return result(0);
        case 6: case 7:
            if (t.status!=16) return result(-414);
            t.context={};t.context.pc=t.entry;t.context.gpr[28]=t.gp;t.context.gpr[29]=t.stack+t.size-16;t.context.gpr[31]=0xffffffd0;
            t.context.gpr[4]=b;t.context.gpr[5]=c;t.priority=t.initial_priority;t.wakeups=0;
            if (ordinal==7) {
                if (b>t.size-32) return result(-400);
                const auto sp=(t.context.gpr[29]-b)&~15u;
                for(std::uint32_t n=0;n<b;++n)iop_write(sp+n,iop_read(c+n,1),1);
                t.context.gpr[29]=sp-16;t.context.gpr[5]=sp;
            }
            iop_ready(id);return result(0);
        case 10: case 11:
            if (id==iop_current) return result(-406);
            if (t.status==16) return result(-413);
            iop_ready(id);t.status=16;iop_reschedule=true;return result(0);
        case 14: case 15:
            if (b>=127) return result(-403);
            t.priority=b ? b : t.initial_priority;iop_reschedule=true;return result(0);
        case 18: case 19:
            if (id==iop_current) return result(-406);
            if (!(t.status&4)) return result(-416);
            iop_ready(id,-418);return result(0);
        case 22: case 23: {
            const std::uint32_t info[]={t.attr,t.option,t.status,t.entry,t.stack,t.size,t.gp,t.initial_priority,t.priority,t.wait,t.wait_id,t.wakeups};
            if (b) for(unsigned n=0;n<std::size(info);++n)iop_write(b+4*n,info[n],4);
            return result(0);
        }
        case 24:
            if(t.wakeups) {--t.wakeups;return result(0);}result(0);iop_block(1);return true;
        case 25: case 26:
            if(t.status==16) return result(-413);
            if((t.status&4) && t.wait==1) iop_ready(id);else ++t.wakeups;
            return result(0);
        case 27: case 28: { const auto count=t.wakeups;t.wakeups=0;return result(count); }
        case 29: case 30:
            if(t.status==16) return result(-413);
            if(t.status&8) return result(-1);
            t.status=(t.status&4) ? 12 : 8;iop_reschedule=true;return result(0);
        case 31: case 32:
            if(!(t.status&8)) return result(-415);
            t.status=(t.status&4) ? 4 : 2;iop_reschedule=true;return result(0);
        case 42:return result(iop_threads.at(iop_current).priority);
        default:return false;
        }
    }
    auto& objects=library=="thsemap" ? iop_semas : library=="thevent" ? iop_events : iop_mailboxes;
    const auto kind=library=="thsemap" ? 3u : library=="thevent" ? 4u : 5u;
    if(ordinal==4) {
        if(objects.size()>=4096)return result(-400);
        IopObject o;o.attr=iop_read(a,4);o.option=iop_read(a+4,4);
        if(kind==3) {o.initial=o.value=iop_read(a+8,4);o.maximum=iop_read(a+12,4);if(!o.maximum || o.value>o.maximum)return result(-400);}
        if(kind==4) o.initial=o.value=iop_read(a+8,4);
        const auto created=iop_next_object++;objects.emplace(created,o);return result(created);
    }
    const auto object=kind==5 && (ordinal==8 || ordinal==9) ? b : a;
    auto found=objects.find(object);
    if(found==objects.end()) return result(kind==3 ? -408 : kind==4 ? -409 : -410);
    auto& o=found->second;
    if(ordinal==5) {const auto waiting=o.waiters;for(auto id:waiting)iop_ready(id,-425);objects.erase(found);return result(0);}
    if((kind==3 && (ordinal==11 || ordinal==12)) || (kind==4 && (ordinal==13 || ordinal==14)) || (kind==5 && (ordinal==11 || ordinal==12))) {
        std::vector<std::uint32_t> info;
        if(kind==3)info={o.attr,o.option,o.initial,o.maximum,o.value,static_cast<std::uint32_t>(o.waiters.size())};
        if(kind==4)info={o.attr,o.option,o.initial,o.value,static_cast<std::uint32_t>(o.waiters.size())};
        if(kind==5)info={o.attr,o.option,static_cast<std::uint32_t>(o.waiters.size()),static_cast<std::uint32_t>(o.messages.size()),o.messages.empty() ? 0u : o.messages.front()};
        for(unsigned n=0;n<info.size();++n)iop_write(b+4*n,info[n],4);
        return result(0);
    }
    const auto waiter=[&] {
        auto id=o.waiters.front();
        if(o.attr&1)id=*std::min_element(o.waiters.begin(),o.waiters.end(),[&](auto x,auto y){return iop_threads.at(x).priority<iop_threads.at(y).priority;});
        return id;
    };
    if(kind==3) {
        if(ordinal==6 || ordinal==7) {
            if(!o.waiters.empty()) {
                auto id=waiter();
                iop_ready(id);
            } else {if(o.value==o.maximum)return result(-420);++o.value;}return result(0);
        }
        if(ordinal==8 || ordinal==9) {
            if(o.value) {--o.value;return result(0);}if(ordinal==9)return result(-419);
            result(0);iop_block(kind,object);o.waiters.push_back(iop_current);return true;
        }
    }
    if(kind==4) {
        const auto ready=[&](std::uint32_t bits,std::uint32_t mode) {return mode&1 ? (o.value&bits)!=0 : (o.value&bits)==bits;};
        if(ordinal==6 || ordinal==7) {
            o.value|=b;const auto waiting=o.waiters;
            for(auto id:waiting) {auto& t=iop_threads.at(id);if(!ready(t.wait_bits,t.wait_mode))continue;
                if(t.wait_pointer)iop_write(t.wait_pointer,o.value,4);if(t.wait_mode&0x10)o.value=0;iop_ready(id);}
            return result(0);
        }
        if(ordinal==8 || ordinal==9) {o.value&=b;return result(0);}
        if(ordinal==10 || ordinal==11 || ordinal==12) {
            if(!b)return result(-423);
            if(c&~0x11u)return result(-400);
            if(!o.waiters.empty() && (ordinal!=10 || !(o.attr&2)))return result(-422);
            if(ready(b,c)) {if(d)iop_write(d,o.value,4);if(c&0x10)o.value=0;return result(0);}
            if(ordinal!=10)return result(-421);
            result(0);iop_block(kind,object);auto& t=iop_threads.at(iop_current);t.wait_bits=b;t.wait_mode=c;t.wait_pointer=d;o.waiters.push_back(iop_current);return true;
        }
    }
    if(kind==5) {
        if(ordinal==6 || ordinal==7) {
            if(!o.waiters.empty()) {const auto id=waiter();const auto pointer=iop_threads.at(id).wait_pointer;if(pointer)iop_write(pointer,b,4);iop_ready(id);}
            else {
                if(std::find(o.messages.begin(),o.messages.end(),b)!=o.messages.end())throw std::runtime_error("IOP mailbox packet already queued");
                if(o.attr&4) {
                    const auto priority=iop_read(b+4,1);
                    const auto at=std::find_if(o.messages.begin(),o.messages.end(),[&](auto msg){return iop_read(msg+4,1)>priority;});o.messages.insert(at,b);
                } else o.messages.push_back(b);
                for(std::size_t n=0;n<o.messages.size();++n)iop_write(o.messages[n],o.messages[(n+1)%o.messages.size()],4);
            }
            return result(0);
        }
        if(ordinal==8 || ordinal==9) {
            if(!o.messages.empty()) {if(a)iop_write(a,o.messages.front(),4);o.messages.pop_front();return result(0);}
            if(ordinal==9)return result(-424);
            result(0);iop_block(kind,object);iop_threads.at(iop_current).wait_pointer=a;o.waiters.push_back(iop_current);return true;
        }
    }
    return false;
}
}
