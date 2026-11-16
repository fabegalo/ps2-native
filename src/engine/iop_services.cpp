// Kernel/device registration adapts exoteric33 a50471a libs.cpp (MIT).
#include "ps2native/engine/console.hpp"
#include <algorithm>
namespace ps2native::engine {
void Console::iop_native_call(std::uint32_t function,std::uint32_t argument,std::function<void(std::uint32_t)> finish){
    if((function&3) || function>=iop_ram.size())throw std::runtime_error("Native IOP callback outside RAM");
    auto& stack=iop_native_calls[iop_current];if(stack.size()>=16)throw std::runtime_error("Native IOP callback depth limit");
    stack.push_back({iop,std::move(finish)});iop.pc=function;iop.gpr[4]=argument;iop.gpr[31]=0xffffffa0;iop.branchPending=iop.pendingLoad=false;
}
bool Console::iop_services_import(const std::string& lib,std::uint32_t ordinal){
    const auto a=iop_arg(0),b=iop_arg(1),c=iop_arg(2);const auto result=[&](std::int32_t v){iop.gpr[2]=static_cast<std::uint32_t>(v);return true;};
    if(lib=="intrman"){
        if(ordinal==4){if(a>=64 || !c || (c&3) || c>=iop_ram.size())return result(-1);if(iop_irqs.contains(a))return result(-1);iop_irqs.emplace(a,IopIrq{c,iop_arg(3),iop.gpr[28],false});return result(0);}
        if(ordinal==5)return result(iop_irqs.erase(a)?0:-1);
        if(ordinal==6 || ordinal==7){const auto it=iop_irqs.find(a);if(it==iop_irqs.end())return result(-1);if(ordinal==7 && b)iop_write(b,it->second.enabled,4);it->second.enabled=ordinal==6;return result(0);}
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
        if(ordinal==8)return result(a && a<=sif.completed_id ? -1:0);
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
