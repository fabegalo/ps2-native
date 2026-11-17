// CPU: isolated GPL-3.0 PS2Recomp component. Loader/formatter adapt exoteric33 a50471a (MIT).
#include "ps2native/engine/console.hpp"
#include "ps2native/io.hpp"
#include "iop_cdvd.hpp"
#include "engine_audio.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <format>

namespace ps2native::engine {
std::uint32_t Console::iop_read(std::uint32_t address,unsigned bytes) const {
    if (bytes!=1 && bytes!=2 && bytes!=4) throw std::runtime_error("Invalid IOP access width");
    const auto p=address&0x1fffffff;
    if(p>=0x1f900000 && p<0x1f910000)return const_cast<Console*>(this)->audio_read(p,bytes);
    if (p<0x800000) {
        const auto offset=p&0x1fffff;
        if (bytes>iop_ram.size()-offset) throw std::runtime_error("IOP RAM access crosses its end");
        std::uint32_t value=0; std::memcpy(&value,iop_ram.data()+offset,bytes); return value;
    }
    if(const auto it=iop_bus_registers.find(p);it!=iop_bus_registers.end() && bytes==4)return it->second;
    const std::uint32_t timer_bases[]={0x1f801100,0x1f801110,0x1f801120,0x1f801480,0x1f801490,0x1f8014a0};
    for(unsigned k=0;k<6;++k)if(p>=timer_bases[k] && p<timer_bases[k]+12){
        if((bytes!=2 && bytes!=4) || (p&3))throw std::runtime_error("Invalid IOP timer access");
        const auto reg=p-timer_bases[k];return reg==0?iop_timer_counter(k):reg==4?(iop_timers[k].mode&65535):iop_timers[k].compare;
    }
    if(p>=0x1f808200 && p<=0x1f808280)return const_cast<Console*>(this)->sio2_read(p,bytes);
    if((p>=0x1f402000 && p<0x1f402020) || cdvd->dma.contains(p&~3u))return cdvd->read(p,bytes);
    throw std::runtime_error(std::format("Unsupported IOP MMIO read {:08X}",address));
}
void Console::iop_write(std::uint32_t address,std::uint32_t value,unsigned bytes) {
    if (bytes!=1 && bytes!=2 && bytes!=4) throw std::runtime_error("Invalid IOP access width");
    const auto p=address&0x1fffffff;

    if(p>=0x1f900000 && p<0x1f910000){audio_write(p,value,bytes);return;}
    if (p<0x800000) {
        const auto offset=p&0x1fffff;
        if (bytes>iop_ram.size()-offset) throw std::runtime_error("IOP RAM access crosses its end");
        std::memcpy(iop_ram.data()+offset,&value,bytes); return;
    }
    // Bus timing/base latches reused from ps2-native before b2959c8; unwritten values remain unknown.
    if(bytes==4 && !(p&3) && ((p>=0x1f801000 && p<=0x1f801020) || (p>=0x1f801400 && p<=0x1f801420) || p==0x1f801060 || p==0x1f801450 || p==0x1ffe0130)) {iop_bus_registers[p]=value;return;}
    if(p>=0x1f808200 && p<=0x1f808280){sio2_write(p,value,bytes);return;}
    if((p>=0x1f402000 && p<0x1f402020) || cdvd->dma.contains(p&~3u)){cdvd->write(p,value,bytes);return;}
    throw std::runtime_error(std::format("Unsupported IOP MMIO write {:08X}",address));
}
std::string Console::iop_string(std::uint32_t address,std::size_t limit) const {
    std::string text;
    for (std::size_t i=0;i<limit;++i) {
        const auto ch=static_cast<char>(iop_read(address+static_cast<std::uint32_t>(i),1));
        if (!ch) return text;
        text+=ch;
    }
    throw std::runtime_error("IOP string exceeds its bound");
}
std::uint32_t Console::iop_arg(unsigned index) const {
    return index<4 ? iop.gpr[4+index] : iop_read(iop.gpr[29]+16+(index-4)*4,4);
}
void Console::tty_write(bool from_iop,std::string_view text) {
    if (text.size()>1u<<20 || output.size()+iop_output.size()>(1u<<20)-text.size()) throw std::runtime_error("Console TTY output limit reached");
    (from_iop ? iop_output : output).append(text);
}
std::string Console::iop_format(std::uint32_t address,unsigned first) {
    const auto fmt=iop_string(address);
    std::string text;
    auto next=first;
    for (std::size_t i=0;i<fmt.size();++i) {
        if (fmt[i]!='%') { text+=fmt[i]; continue; }
        std::string spec="%"; ++i;
        while(i<fmt.size() && std::strchr("-+ #0",fmt[i])) spec+=fmt[i++];
        while(i<fmt.size() && (std::isdigit(static_cast<unsigned char>(fmt[i])) || fmt[i]=='.')) spec+=fmt[i++];
        if (i==fmt.size() || spec.size()>24) throw std::runtime_error("Invalid IOP printf format");
        if (fmt[i]=='l' || fmt[i]=='h') throw std::runtime_error("IOP printf length modifier is not implemented");
        char buffer[65537]; int size=-1;
        switch(fmt[i]) {
        case 'd': case 'i': size=std::snprintf(buffer,sizeof(buffer),(spec+"d").c_str(),static_cast<std::int32_t>(iop_arg(next++))); break;
        case 'u': case 'x': case 'X': case 'o': size=std::snprintf(buffer,sizeof(buffer),(spec+fmt[i]).c_str(),iop_arg(next++)); break;
        case 'p': size=std::snprintf(buffer,sizeof(buffer),(spec+"x").c_str(),iop_arg(next++)); break;
        case 'c': size=std::snprintf(buffer,sizeof(buffer),(spec+"c").c_str(),static_cast<int>(iop_arg(next++))); break;
        case 's': size=std::snprintf(buffer,sizeof(buffer),(spec+"s").c_str(),iop_string(iop_arg(next++)).c_str()); break;
        case '%': buffer[0]='%'; size=1; break;
        default: throw std::runtime_error("Unsupported IOP printf conversion");
        }
        if (size<0 || size>=static_cast<int>(sizeof(buffer)) || text.size()+size>65536) throw std::runtime_error("IOP printf output exceeds bound");
        text.append(buffer,size);
    }
    return text;
}
void Console::iop_import(const std::string& library,std::uint32_t ordinal,std::uint32_t version) {
    if (iop_services_import(library,ordinal) || iop_rpc_import(library,ordinal) || iop_loader_import(library,ordinal,version) || iop_thread_import(library,ordinal)) return;
    if ((library=="stdio" && ordinal==4) || (library=="sysmem" && ordinal==14)) {
        const auto text=iop_format(iop_arg(0),1);
        tty_write(true,text); iop.gpr[2]=static_cast<std::uint32_t>(text.size()); return;
    }
    if (library=="sifcmd") {
        const auto a=iop_arg(0),b=iop_arg(1),c=iop_arg(2);
        if (ordinal==4) {
            if (!sif.command_buffer) throw std::runtime_error("SIF CMD service is not initialized");
            iop.gpr[2]=0; return;
        }
        if (ordinal==6 || ordinal==7) {
            if (a>=sif.iop_sregs.size()) throw std::runtime_error("Invalid IOP SIF sreg");
            if (ordinal==7) { sif.iop_sregs[a]=b; if (!a) sif.rpc_ready=(b!=0); }
            iop.gpr[2]=sif.iop_sregs[a]; return;
        }
        if (ordinal==10) {
            if ((b&3) || (b&0x1fffffff)>=iop_ram.size()) throw std::runtime_error("IOP SIF handler outside RAM");
            sif.iop_handlers[a]={b,c,iop.gpr[28]}; iop.gpr[2]=0; return;
        }
        if (ordinal==11) { sif.iop_handlers.erase(a); iop.gpr[2]=0; return; }
        if (ordinal==12 || ordinal==13) {
            iop_send_command(a,b,c,iop_arg(3),iop_arg(4),iop_arg(5)); iop.gpr[2]=sif.transfer_id; return;
        }
    }
    if (library=="sysclib") {
        const auto a=iop_arg(0), b=iop_arg(1), count=iop_arg(2);
        const auto span=[&](std::uint32_t address,std::size_t length) {
            const auto physical=address&0x1fffffff, offset=physical&0x1fffff;
            if (physical>=0x800000 || length>iop_ram.size()-offset) throw std::runtime_error("IOP service span outside RAM");
            return std::span(iop_ram).subspan(offset,length);
        };
        if(ordinal==6 || ordinal==7){
            iop.gpr[2]=ordinal==6 && a>='a' && a<='z'?a-32:ordinal==7 && a>='A' && a<='Z'?a+32:a;return;
        }
        if (ordinal==12 || ordinal==13) {
            const auto to=span(a,count),from=span(b,count);
            std::memmove(to.data(),from.data(),count); iop.gpr[2]=a; return;
        }
        if (ordinal==14 || ordinal==17) {
            const auto length=ordinal==17 ? b : count;
            std::memset(span(a,length).data(),ordinal==17 ? 0 : static_cast<int>(b),length); iop.gpr[2]=a; return;
        }
        if (ordinal==19) {
            const auto text=iop_format(b,2);
            std::memcpy(span(a,text.size()+1).data(),text.c_str(),text.size()+1); iop.gpr[2]=static_cast<std::uint32_t>(text.size()); return;
        }
        if(ordinal==36){
            if(count && (count<2 || count>36))throw std::runtime_error("Invalid IOP strtol base");
            if(b)(void)iop_read(b,4);
            const auto text=iop_string(a,4096);char* end{};
            const auto parsed=std::strtoll(text.c_str(),&end,static_cast<int>(count));
            if(b)iop_write(b,a+static_cast<std::uint32_t>(end-text.c_str()),4);
            iop.gpr[2]=static_cast<std::uint32_t>(std::clamp(parsed,-2147483648ll,2147483647ll));return;
        }
        if (ordinal==35) {
            auto cursor=a ? a : iop_strtok_next;
            const auto delimiters=b ? iop_string(b,256) : std::string{};
            const auto delimiter=[&](std::uint32_t ch) { return ch && delimiters.find(static_cast<char>(ch))!=std::string::npos; };
            while (cursor && delimiter(iop_read(cursor,1))) ++cursor;
            if (!cursor || !iop_read(cursor,1)) { iop_strtok_next=0; iop.gpr[2]=0; return; }
            const auto token=cursor;
            while(iop_read(cursor,1) && !delimiter(iop_read(cursor,1))) ++cursor;
            if (iop_read(cursor,1)) { iop_write(cursor,0,1); iop_strtok_next=cursor+1; }
            else iop_strtok_next=cursor;
            iop.gpr[2]=token; return;
        }
        if (ordinal==21 || ordinal==25 || ordinal==26 || ordinal==32) {
            const auto text=iop_string(a,65536);
            const auto ch=static_cast<char>(b);
            const auto position=ch==0 ? text.size() :
                (ordinal==26 || ordinal==32 ? text.rfind(ch) : text.find(ch));
            iop.gpr[2]=position==std::string::npos ? 0 : a+static_cast<std::uint32_t>(position);return;
        }
        if (ordinal==27) { iop.gpr[2]=static_cast<std::uint32_t>(iop_string(a,65536).size()); return; }
    }
    throw std::runtime_error(std::format("Unsupported IOP import {}:{} args={:08X},{:08X},{:08X},{:08X}",library,ordinal,iop_arg(0),iop_arg(1),iop_arg(2),iop_arg(3)));
}
// PS2SDK threadman alarm callbacks execute in interrupt context and repeat from their previous deadline.
bool Console::iop_alarm_step(){
    if(iop_alarm_active && iop.pc==0xffffff98){
        const auto repeat=iop.gpr[2];auto it=iop_alarms.find(iop_alarm_active);
        if(it!=iop_alarms.end()){
            if(!repeat)iop_alarms.erase(it);
            else {const auto delta=std::max<std::uint64_t>(repeat,iop_alarm_min)*8;if(it->second.target>~0ull-delta)throw std::runtime_error("IOP repeating alarm clock overflow");it->second.target+=delta;it->second.pending=false;}
        }
        iop_alarm_active=0;iop=iop_suspended;iop_active=iop_suspended_active;iop_callback_active=false;
        if(iop_reschedule)iop_schedule();return true;
    }
    if(iop_callback_active || !iop_intr_enabled || iop.branchPending || iop_import_slot)return false;
    auto first=iop_alarms.end();
    for(auto it=iop_alarms.begin();it!=iop_alarms.end();++it)if(it->second.pending && (first==iop_alarms.end() || std::pair(it->second.target,it->first)<std::pair(first->second.target,first->first)))first=it;
    if(first==iop_alarms.end())return false;
    const auto a=first->second;iop_alarm_active=first->first;iop_suspended=iop;iop_suspended_active=iop_active;iop={};iop_callback_active=iop_active=true;
    iop.pc=a.function;iop.gpr[4]=a.argument;iop.gpr[28]=a.gp;iop.gpr[29]=0x1dbff0;iop.gpr[31]=0xffffff98;return false;
}
void Console::step_iop() {
    for (auto& [id,t] : iop_threads)
        if ((t.status&4) && t.wait==2 && t.deadline<=ticks) iop_ready(id);
    if ((!iop_active || (iop_reschedule && !iop.branchPending && !iop_import_slot)) && !iop_callback_active) iop_schedule();
    if(!iop_callback_active && iop_intr_enabled && !iop.branchPending && !iop_import_slot){
        if(iop_vblank_pending){
            const auto pending=iop_vblank_pending;iop_vblank_pending=0;
            for(const auto& h:iop_vblank_handlers)if(pending&(1u<<h.edge))iop_vblank_jobs.push_back(h);
            std::stable_sort(iop_vblank_jobs.begin(),iop_vblank_jobs.end(),[](const auto& a,const auto& b){return std::pair(a.edge,std::pair(a.priority,a.order))<std::pair(b.edge,std::pair(b.priority,b.order));});
            for(auto& [id,t]:iop_threads)if((t.status&4) && t.wait==8 && (pending&(1u<<t.wait_id)))iop_ready(id);
        }
        if(!iop_vblank_jobs.empty()){
            const auto h=iop_vblank_jobs.front();iop_vblank_jobs.pop_front();iop_vblank_active=h;
            iop_suspended=iop;iop_suspended_active=iop_active;iop={};iop_active=iop_callback_active=true;
            iop.pc=h.function;iop.gpr[4]=h.argument;iop.gpr[28]=h.gp;iop.gpr[29]=0x1dcff0;iop.gpr[31]=0xffffff9c;
        }
    }
    if(iop.pc==0xffffff9c && iop_vblank_active){
        const auto h=*iop_vblank_active;if(!iop.gpr[2])std::erase_if(iop_vblank_handlers,[&](const auto& old){return old.edge==h.edge && old.function==h.function;});
        iop_vblank_active.reset();iop=iop_suspended;iop_active=iop_suspended_active;iop_callback_active=false;
        if(iop_reschedule)iop_schedule();return;
    }
    if(iop.pc==0xffffff90 && iop_callback_active){
        iop=iop_suspended;iop_active=iop_suspended_active;iop_callback_active=false;
        if(iop_reschedule)iop_schedule();return;
    }
    if(!iop_callback_active && iop_intr_enabled && !sif.dma_callbacks.empty() && !iop.branchPending && !iop_import_slot){
        const auto callback=sif.dma_callbacks.front();sif.dma_callbacks.pop_front();
        iop_suspended=iop;iop_suspended_active=iop_active;iop={};iop_active=iop_callback_active=true;
        iop.pc=callback.entry;iop.gpr[4]=callback.argument;iop.gpr[28]=callback.gp;iop.gpr[29]=0x1ddff0;iop.gpr[31]=0xffffff90;
    }
    if (!iop_callback_active && !sif.iop_commands.empty() && (!iop_active || (!iop.branchPending && !iop_import_slot))) {
        auto command=std::move(sif.iop_commands.front()); sif.iop_commands.pop_front();
        iop_suspended=iop; iop_suspended_active=iop_active;
        iop={}; iop_callback_active=true; iop_active=true;
        const auto buffer=sif.command_buffer+0x400;
        std::copy(command.second.begin(),command.second.end(),iop_ram.begin()+buffer);
        iop.pc=command.first.entry; iop.gpr[4]=buffer; iop.gpr[5]=command.first.argument;
        iop.gpr[28]=command.first.gp; iop.gpr[29]=0x1deff0; iop.gpr[31]=0xffffffe0;
    }
    if(!iop_callback_active && !iop_import_slot && !iop.branchPending && pending_iop_irq()!=~0u){
        iop_irq_cause=pending_iop_irq();
        if(iop_irq_cause>=32){const auto ch=iop_irq_cause<39?iop_irq_cause-32:iop_irq_cause-33;auto& value=cdvd->dma.at(ch<7?0x1f8010f4:0x1f801574);value&=~(1u<<(24+(ch<7?ch:ch-7)));cdvd->update_dicr();}
        else iop_pending_irqs&=~(1u<<iop_irq_cause);
        const auto& irq=iop_irqs.at(iop_irq_cause);iop_suspended=iop;iop_suspended_active=iop_active;iop_callback_active=true;iop_irq_active=true;iop_active=true;
        iop={};iop.pc=irq.function;iop.gpr[4]=irq.argument;iop.gpr[28]=irq.gp;iop.gpr[29]=0x1ddff0;iop.gpr[31]=0xffffffa4;
    }
    if(iop.pc==0xffffffa4 && iop_irq_active){const auto keep=iop.gpr[2];iop=iop_suspended;iop_active=iop_suspended_active;iop_callback_active=iop_irq_active=false;constexpr unsigned timer_irq[]={4,5,6,14,15,16};
        for(unsigned k=0;k<6;++k)if(timer_irq[k]==iop_irq_cause && iop_timers[k].setup && iop_timers[k].handler){
            auto& t=iop_timers[k];t.mode&=~0x800u;
            if(!keep){t.count=iop_timer_counter(k);t.base=ticks;t.mode=0;t.due=~0ull;iop_irqs.erase(timer_irq[k]);}
            else {t.compare=k<3?keep&65535:keep;refresh_iop_timer(k);}
        }
        if(!keep && iop_irqs.contains(iop_irq_cause))iop_irqs.at(iop_irq_cause).enabled=false;if(iop_reschedule)iop_schedule();return;}
    if(iop_alarm_step())return;
    if (!iop_active) return;
    if (!iop_instructions) iop_first_tick=ticks;
    iop_last_tick=ticks;
    if (iop.pc==0xfffffff0) { iop_active=false; iop_result=static_cast<std::int32_t>(iop.gpr[2]); return; }
    // One IOP instruction per eight EE ticks; long sessions share the Console run bound.
    if (iop_instructions++>=execution_limit/8+1) throw std::runtime_error("IOP instruction limit reached");
    if(iop.pc==0xffffffa0){
        auto& stack=iop_native_calls.at(iop_current);auto call=std::move(stack.back());stack.pop_back();const auto result=iop.gpr[2];iop=call.saved;call.finish(result);return;
    }
    if(iop.pc==0xffffffb0){rpc_finish();return;}
    if(iop.pc==0xffffffb4){rpc_next(iop.gpr[4]);if(iop_reschedule)iop_schedule();return;}
    const auto before=iop.pc;
    if (iop_imports.contains(before)) {
        if (iop_import_slot) throw std::runtime_error("Nested unresolved IOP import");
        iop_import_slot=before+4;
    }
    (void)iop_core.executeInstruction(iop);
    if(iop.exception){
        const auto fault=iop.cop0[14];
        if(std::getenv("PS2N_ENGINE_STATE_HASH")){
            for(unsigned n=0;n<32;++n)std::fprintf(stderr,"[iop-fault-reg] r%u=%08X\n",n,iop.gpr[n]);
            if(fault>=64 && (fault&0x1fffffff)<=iop_ram.size()-16)for(unsigned n=0;n<20;++n)std::fprintf(stderr,"[iop-fault-code] %08X %08X\n",fault-64+n*4,iop_read(fault-64+n*4,4));
        }
        throw std::runtime_error(std::format("IOP exception {} EPC={:08X} BadVAddr={:08X} v0={:08X} v1={:08X} ra={:08X} sp={:08X}",(iop.cop0[13]>>2)&31,fault,iop.cop0[8],iop.gpr[2],iop.gpr[3],iop.gpr[31],iop.gpr[29]));
    }
    if (iop_import_slot && before==iop_import_slot) {
        const auto imported=iop_imports.at(iop_import_slot-4);
        const auto slot=iop_import_slot-4;iop_import_slot=0;
        const auto lib=iop_libraries.find(imported.first);
        if(lib!=iop_libraries.end()) {
            if(imported.second>=lib->second.exports.size())throw std::runtime_error("Missing native IOP export ordinal; HLE cannot replace a registered library");
            if(const auto version=iop_import_versions.find(slot);version!=iop_import_versions.end() && ((version->second&0xff00)!=(lib->second.version&0xff00) || (version->second&0xff)>(lib->second.version&0xff)))throw std::runtime_error("IOP import/export ABI version mismatch");
            iop.pc=lib->second.exports[imported.second];iop.branchPending=false;
        } else iop_import(imported.first,imported.second,(iop_import_versions.contains(slot)?iop_import_versions.at(slot):0));
    }
    if (iop.pc==0xffffffe0 && iop_callback_active) {
        iop=iop_suspended; iop_active=iop_suspended_active; iop_callback_active=false;
    } else if (iop.pc==0xfffffff0 || iop.pc==0xffffffd0) {
        if (iop.pc==0xfffffff0) {iop_module_return();iop_result=static_cast<std::int32_t>(iop.gpr[2]);iop_module_returned=true;}
        if (iop_current) iop_threads.at(iop_current).status=16;
        iop_active=false;iop_reschedule=true;
    }
    if (!iop_callback_active && iop_reschedule && !iop_import_slot && !iop.branchPending) iop_schedule();

}
void Console::run_iop() {
    while(iop_work_pending() || std::any_of(iop_timers.begin(),iop_timers.end(),[](const auto& t){return t.due!=~0ull;}) || iop_boot_active || cdvd->due!=~0ull || iop_sio2_dma_due[0]!=~0ull || iop_sio2_dma_due[1]!=~0ull || std::any_of(iop_alarms.begin(),iop_alarms.end(),[](const auto& item){return !item.second.pending;}) || (audio && (audio->dmaBusy(0) || audio->dmaBusy(1)))) advance_to(next_event_tick());
    for (const auto& [id,t] : iop_threads)
        if (t.status!=16 && !iop_module_returned) throw std::runtime_error("IOP scheduler deadlock: waiting without an event");
}
}
namespace ps2x::iop::detail {
std::uint8_t IopMemory::read8(std::uint32_t a) { return static_cast<std::uint8_t>(console_.iop_read(a,1)); }
std::uint16_t IopMemory::read16(std::uint32_t a) { return static_cast<std::uint16_t>(console_.iop_read(a,2)); }
std::uint32_t IopMemory::read32(std::uint32_t a) { return console_.iop_read(a,4); }
void IopMemory::write8(std::uint32_t a,std::uint8_t v) { console_.iop_write(a,v,1); }
void IopMemory::write16(std::uint32_t a,std::uint16_t v) { console_.iop_write(a,v,2); }
void IopMemory::write32(std::uint32_t a,std::uint32_t v) { console_.iop_write(a,v,4); }
}
