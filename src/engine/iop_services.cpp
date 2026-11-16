// Kernel/device registration adapts exoteric33 a50471a libs.cpp (MIT).
#include "ps2native/engine/console.hpp"
#include <algorithm>
#include "iop_cdvd.hpp"
namespace ps2native::engine {
void Console::iop_native_call(std::uint32_t function,std::uint32_t argument,std::function<void(std::uint32_t)> finish){
    if((function&3) || function>=iop_ram.size())throw std::runtime_error("Native IOP callback outside RAM");
    auto& stack=iop_native_calls[iop_current];if(stack.size()>=16)throw std::runtime_error("Native IOP callback depth limit");
    stack.push_back({iop,std::move(finish)});iop.pc=function;iop.gpr[4]=argument;iop.gpr[31]=0xffffffa0;iop.branchPending=iop.pendingLoad=false;
}
void Console::iop_vblank_edge(unsigned edge){
    const bool idle=!iop_work_pending();iop_in_vblank=edge==0;
    iop_vblank_pending|=1u<<edge;
    if(idle)iop_next_tick=std::max(iop_next_tick,ticks+8);
}
std::uint32_t Console::iop_timer_counter(unsigned index) const {
    const auto& t=iop_timers.at(index);const auto value=std::uint64_t{t.count}+(ticks-t.base)/(8ull*t.prescale);
    return static_cast<std::uint32_t>(value)&(index<3?0xffffu:0xffffffffu);
}
bool Console::iop_services_import(const std::string& lib,std::uint32_t ordinal){
    const auto a=iop_arg(0),b=iop_arg(1),c=iop_arg(2);const auto result=[&](std::int32_t v){iop.gpr[2]=static_cast<std::uint32_t>(v);return true;};
    if(lib=="vblank"){
        if(ordinal==8 || ordinal==9){
            if(iop_callback_active)return result(-100);
            const auto edge=a?1u:0u;
            if(ordinal==9){const auto n=std::erase_if(iop_vblank_handlers,[&](const auto& h){return h.edge==edge && h.function==b;});return result(n?0:-105);}
            if(!c || (c&3) || c>=iop_ram.size())throw std::runtime_error("VBlank callback outside IOP RAM");
            if(std::any_of(iop_vblank_handlers.begin(),iop_vblank_handlers.end(),[&](const auto& h){return h.edge==edge && h.function==c;}))return result(-104);
            if(iop_vblank_handlers.size()>=14)return result(-400);
            iop_vblank_handlers.push_back({edge,b,c,iop_arg(3),iop.gpr[28],++iop_order});return result(0);
        }
        if(ordinal>=4 && ordinal<=7){
            if((ordinal==6 && iop_in_vblank) || (ordinal==7 && !iop_in_vblank))return result(0);
            result(0);iop_block(8,ordinal==5 || ordinal==7?1:0);return true;
        }
    }
    if(lib=="timrman"){
        if(!((ordinal>=4 && ordinal<=12) || (ordinal>=16 && ordinal<=18)))return false;
        constexpr std::uint32_t addresses[]={0xbf801100,0xbf801110,0xbf801120,0xbf801480,0xbf801490,0xbf8014a0};
        constexpr unsigned sources[]={11,13,1,5,1,1},widths[]={16,16,16,32,32,32},limits[]={1,1,8,1,256,256},irq[]={4,5,6,14,15,16};
        const auto id=[&](unsigned k){return ((k+1)<<28)|(addresses[k]>>4);};
        if(ordinal==4 || ordinal==5){
            if(iop_callback_active)return result(-100);
            constexpr unsigned allocation_order[]={2,5,4,3,0,1};
            for(unsigned n=0;n<6;++n){const auto k=ordinal==4?allocation_order[n]:n;auto& t=iop_timers[k];
                if(!(sources[k]&a) || widths[k]!=b)continue;
                if(ordinal==4){if(t.users || !c || c>limits[k])continue;t={};t.users=1;t.base=ticks;return result(id(k));}
                if(t.users && t.mode && (t.mode&iop_arg(3))==c){++t.users;return result(id(k));}
            }
            return result(-150);
        }
        const auto k=(a>>28)-1;
        if(k>=6 || a!=id(k) || !iop_timers[k].users)return result(-151);
        auto& t=iop_timers[k];
        if(ordinal==6){if(!--t.users)t={};return result(0);}
        if(ordinal==7){
            if(b&0x13f)throw std::runtime_error("IOP gated/external/IRQ timer mode is not integrated");
            t.mode=b;t.count=0;t.base=ticks;t.prescale=k<3?((b&0x200)?8:1):((b&0x6000)==0x6000?256:(b&0x6000)==0x4000?16:(b&0x6000)==0x2000?8:1);return true;
        }
        if(ordinal==8 || ordinal==17)return result(t.mode);
        if(ordinal==9){t.count=b;t.base=ticks;return true;}
        if(ordinal==10)return result(iop_timer_counter(k));
        if(ordinal==11){t.compare=b;return true;}
        if(ordinal==12)return result(t.compare);
        if(ordinal==16)return result(irq[k]);
        if(ordinal==18){
            const auto at=0x1e0800u+k*16;const std::uint32_t code[]={0x3c020000u|(addresses[k]>>16),0x8c420000u|(addresses[k]&65535),0x03e00008,0};
            for(unsigned n=0;n<4;++n)iop_write(at+n*4,code[n],4);return result(at);
        }
        return false;
    }
    // Shared-register adapter reused from patch 0062, following the PS2SDK DMACMAN ABI.
    if(lib=="dmacman" && (ordinal==28 || ordinal==32)){
        if(a>=13 || (ordinal==28 && a==6))return result(0);
        const auto base=a<7?0x1f801080u+a*16:0x1f801500u+(a-7)*16;
        if(!cdvd->dma.contains(base+8))throw std::runtime_error("IOP slice DMA channel is not integrated");
        if(ordinal==32){iop_write(base+8,iop_read(base+8,4)|0x1000000,4);return true;}
        const auto direction=iop_arg(4);iop_write(base,b&0xffffff,4);iop_write(base+4,(c&65535)|(iop_arg(3)<<16),4);
        iop_write(base+8,0x200|(direction&1)|(direction==0?0x40000000u:0),4);return result(1);
    }
    if(lib=="dmacman" && ordinal>=33 && ordinal<=35){
        if(a>=13)throw std::runtime_error("IOP DMA channel not integrated");
        const auto reg=a<7?0x1f8010f0u:0x1f801570u,shift=4*(a<7?a:a-7);
        auto value=iop_read(reg,4);
        if(ordinal==33)value=(value&~(7u<<shift))|((b&7)<<shift);
        else if(ordinal==34)value|=8u<<shift;else value&=~(8u<<shift);
        iop_write(reg,value,4);return true;
    }
    if(lib=="intrman"){
        if(ordinal==4){if(a>=64 || !c || (c&3) || c>=iop_ram.size())return result(-1);if(iop_irqs.contains(a))return result(-1);iop_irqs.emplace(a,IopIrq{c,iop_arg(3),iop.gpr[28],false});return result(0);}
        if(ordinal==5)return result(iop_irqs.erase(a)?0:-1);
        if(ordinal==6 || ordinal==7){const auto it=iop_irqs.find(a);if(it==iop_irqs.end())return result(-1);if(ordinal==7 && b)iop_write(b,it->second.enabled,4);it->second.enabled=ordinal==6;
            if((a>=32 && a<39) || (a>=40 && a<46)){
                const auto reg=a<39?0x1f8010f4u:0x1f801574u,bit=1u<<(16+(a<39?a-32:a-40));
                auto value=iop_read(reg,4)&0xffffff;if(ordinal==6)value|=bit;else value&=~bit;iop_write(reg,value,4);
                if(ordinal==6)iop_write(reg,(iop_read(reg,4)&0xffffff)|0x800000,4);
            }
            return result(0);}
        if(ordinal==8 || ordinal==9){iop_intr_enabled=ordinal==9;return result(0);}
        if(ordinal==17){const auto old=iop_intr_enabled;if(a)iop_write(a,old,4);iop_intr_enabled=false;return result(old?0:-102);}
        if(ordinal==18){iop_intr_enabled=a!=0;return result(0);}
        if(ordinal==23)return result(iop_callback_active?1:0);
    }
    if(lib=="ioman" && (ordinal==20 || ordinal==21)){
        if(ordinal==21){const auto name=iop_string(a,256);const auto it=iop_drivers.find(name);if(it==iop_drivers.end())return result(-1);const auto device=it->second;const auto function=iop_read(iop_read(device+16,4)+4,4);iop_native_call(function,device,[this,name](std::uint32_t r){if(static_cast<std::int32_t>(r)>=0)iop_drivers.erase(name);iop.gpr[2]=r;});return true;}
        const auto name=iop_string(iop_read(a,4),256);if(iop_drivers.contains(name))return result(-1);
        const auto function=iop_read(iop_read(a+16,4),4);
        iop_native_call(function,a,[this,name,a](std::uint32_t r){if(static_cast<std::int32_t>(r)>=0)iop_drivers.emplace(name,a);iop.gpr[2]=r;});return true;
    }
    if(lib=="sifman"){
        if(ordinal==5){if(!sif.command_buffer)throw std::runtime_error("SIF endpoint not initialized");return result(0);}
        if(ordinal==7){
            if(!b || b>32)throw std::runtime_error("Invalid IOP SIF DMA descriptor count");
            struct Transfer{std::uint32_t src,dst,size,attr;};std::vector<Transfer> transfers;
            for(std::uint32_t k=0;k<b;++k){const auto at=a+16*k;Transfer t{iop_read(at,4)&0x1fffffff,translate(iop_read(at+4,4),iop_read(at+8,4),true),iop_read(at+8,4),iop_read(at+12,4)};
                if(t.src>iop_ram.size() || t.size>iop_ram.size()-t.src || t.dst>ram.size() || t.size>ram.size()-t.dst || (t.attr&~4u))throw std::runtime_error("Invalid IOP SIF DMA span/attributes");transfers.push_back(t);}
            const auto interrupts=std::count_if(transfers.begin(),transfers.end(),[](const auto& t){return (t.attr&4)!=0;});
            if(sif.replies.size()+interrupts>32)throw std::runtime_error("SIF0 IRQ queue full");
            for(const auto& t:transfers){std::copy_n(iop_ram.data()+t.src,t.size,ram.data()+t.dst);if(t.attr&4)sif.replies.emplace_back();}
            const auto id=++sif.transfer_id;sif.completed_id=id;sif_pump();return result(id);
        }
        if(ordinal==8)return result(sif.dma_status(a));
        if(ordinal==29)return result(sif.command_buffer!=0);
    }
    if(lib=="sysclib"){
        if(ordinal==20 || ordinal==23 || ordinal==30){
            const auto offset=ordinal==20 ? static_cast<std::uint32_t>(iop_string(a).size()):0;
            if(ordinal==30){if(c>65536)throw std::runtime_error("IOP strncpy limit");bool ended=false;for(std::uint32_t k=0;k<c;++k){const auto ch=ended?0:iop_read(b+k,1);ended=ch==0;iop_write(a+k,ch,1);}}
            else {const auto text=iop_string(b,65536);for(std::size_t k=0;k<=text.size();++k)iop_write(a+offset+k,k==text.size()?0:static_cast<std::uint8_t>(text[k]),1);}
            return result(a);
        }
        if(ordinal==22 || ordinal==29){const auto limit=ordinal==29?c:65536u;if(limit>65536)throw std::runtime_error("IOP string compare limit");for(std::uint32_t k=0;k<limit;++k){const auto x=iop_read(a+k,1),y=iop_read(b+k,1);if(x!=y)return result(static_cast<int>(x)-static_cast<int>(y));if(!x)return result(0);}if(ordinal==29)return result(0);throw std::runtime_error("IOP unterminated strcmp");}
    }
    return false;
}
}
