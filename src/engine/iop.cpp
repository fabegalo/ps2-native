// CPU: isolated GPL-3.0 PS2Recomp component. Loader/formatter adapt exoteric33 a50471a (MIT).
#include "ps2native/engine/console.hpp"
#include "ps2native/io.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <format>

namespace ps2native::engine {
std::uint32_t Console::iop_read(std::uint32_t address,unsigned bytes) const {
    if (bytes!=1 && bytes!=2 && bytes!=4) throw std::runtime_error("Invalid IOP access width");
    const auto p=address&0x1fffffff;
    if (p<0x800000) {
        const auto offset=p&0x1fffff;
        if (bytes>iop_ram.size()-offset) throw std::runtime_error("IOP RAM access crosses its end");
        std::uint32_t value=0; std::memcpy(&value,iop_ram.data()+offset,bytes); return value;
    }
    throw std::runtime_error(std::format("Unsupported IOP MMIO read {:08X}",address));
}
void Console::iop_write(std::uint32_t address,std::uint32_t value,unsigned bytes) {
    if (bytes!=1 && bytes!=2 && bytes!=4) throw std::runtime_error("Invalid IOP access width");
    const auto p=address&0x1fffffff;
    if (p<0x800000) {
        const auto offset=p&0x1fffff;
        if (bytes>iop_ram.size()-offset) throw std::runtime_error("IOP RAM access crosses its end");
        std::memcpy(iop_ram.data()+offset,&value,bytes); return;
    }
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
        char buffer[512]; int size=-1;
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
void Console::iop_import(const std::string& library,std::uint32_t ordinal) {
    if (library=="stdio" && ordinal==4) {
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
        if (ordinal==27) { iop.gpr[2]=static_cast<std::uint32_t>(iop_string(a,65536).size()); return; }
    }
    throw std::runtime_error(std::format("Unsupported IOP import {}:{}",library,ordinal));
}
void Console::step_iop() {
    if (!iop_callback_active && !sif.iop_commands.empty() && (!iop_active || (!iop.branchPending && !iop_import_slot))) {
        auto command=std::move(sif.iop_commands.front()); sif.iop_commands.pop_front();
        iop_suspended=iop; iop_suspended_active=iop_active;
        iop={}; iop_callback_active=true; iop_active=true;
        const auto buffer=sif.command_buffer+0x400;
        std::copy(command.second.begin(),command.second.end(),iop_ram.begin()+buffer);
        iop.pc=command.first.entry; iop.gpr[4]=buffer; iop.gpr[5]=command.first.argument;
        iop.gpr[28]=command.first.gp; iop.gpr[29]=0x1deff0; iop.gpr[31]=0xffffffe0;
    }
    if (!iop_instructions) iop_first_tick=ticks;
    iop_last_tick=ticks;
    if (iop.pc==0xfffffff0) { iop_active=false; iop_result=static_cast<std::int32_t>(iop.gpr[2]); return; }
    if (iop_instructions++>=10000000) throw std::runtime_error("IOP instruction limit reached");
    const auto before=iop.pc;
    if (iop_imports.contains(before)) {
        if (iop_import_slot) throw std::runtime_error("Nested unresolved IOP import");
        iop_import_slot=before+4;
    }
    (void)iop_core.executeInstruction(iop);
    if (iop.exception) throw std::runtime_error(std::format("IOP exception {} EPC={:08X}",(iop.cop0[13]>>2)&31,iop.cop0[14]));
    if (iop_import_slot && before==iop_import_slot) {
        const auto imported=iop_imports.at(iop_import_slot-4);
        iop_import_slot=0;
        iop_import(imported.first,imported.second);
    }
    if (iop.pc==0xffffffe0 && iop_callback_active) {
        iop=iop_suspended; iop_active=iop_suspended_active; iop_callback_active=false;
    } else if (iop.pc==0xfffffff0) { iop_active=false; iop_result=static_cast<std::int32_t>(iop.gpr[2]); }

}
void Console::run_iop() {
    while(iop_active || !sif.iop_commands.empty()) advance_to(iop_next_tick);
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
