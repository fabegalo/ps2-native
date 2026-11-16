// RPC queue/ABI adaptation from exoteric33 a50471a (MIT), with the native-thread
// correction preserved from ps2-native 76295e2 / 620697b. No inline server fallback.
#include "ps2native/engine/console.hpp"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
namespace ps2native::engine {
bool Console::iop_rpc_import(const std::string& library,std::uint32_t ordinal) {
    if(library!="sifcmd")return false;
    const auto a=iop_arg(0),b=iop_arg(1);
    if(ordinal==14){sif.iop_sregs[0]=1;iop.gpr[2]=0;return true;}
    if(ordinal==19){
        if(rpc_queues.size()>=128 && !rpc_queues.contains(a))throw std::runtime_error("RPC queue registration limit");
        if(rpc_queues.contains(a))throw std::runtime_error("RPC queue is already registered");
        if(!iop_threads.contains(b))throw std::runtime_error("RPC queue thread does not exist");
        for(unsigned n=0;n<24;n+=4)iop_write(a+n,0,4);
        iop_write(a,b,4);rpc_queues[a]={b,{}};iop.gpr[2]=0;return true;
    }
    if(ordinal==17){
        if(rpc_servers.size()>=256)throw std::runtime_error("RPC server registration limit");
        const auto queue=iop_arg(6);
        if(!rpc_queues.contains(queue))throw std::runtime_error("RPC registration has no queue");
        RpcServer server{a,b,iop_arg(2),iop_arg(3),iop_arg(4),iop_arg(5),queue,iop.gpr[28]};
        if((server.function&3) || server.function>=iop_ram.size() || server.buffer>=iop_ram.size())throw std::runtime_error("RPC registration outside IOP RAM");
        for(const auto& [address,old]:rpc_servers)if(old.sid==b)throw std::runtime_error("RPC server id already registered");
        for(unsigned n=0;n<68;n+=4)iop_write(a+n,0,4);
        iop_write(a,b,4);iop_write(a+4,server.function,4);iop_write(a+8,server.buffer,4);
        iop_write(a+16,server.callback,4);iop_write(a+20,server.callback_buffer,4);iop_write(a+64,queue,4);
        auto tail=iop_read(queue+8,4);
        if(!tail)iop_write(queue+8,a,4);
        else {unsigned guard=0;while(iop_read(tail+56,4)){tail=iop_read(tail+56,4);if(++guard>256)throw std::runtime_error("RPC server list cycle");}iop_write(tail+56,a,4);}
        rpc_servers.emplace(a,server);std::fprintf(stderr,"[engine-rpc] register sid=%08X handler=%08X queue=%08X thread=%u\n",b,server.function,queue,rpc_queues.at(queue).thread);
        iop.gpr[2]=0;return true;
    }
    if(ordinal==22){rpc_next(a);return true;}
    if(ordinal==24){
        const auto found=rpc_servers.find(a);if(found==rpc_servers.end()){iop.gpr[2]=0;return true;}
        for(const auto& [id,call]:rpc_calls)if(call.request.packet[13]==a)throw std::runtime_error("RPC removal during a live call");
        for(const auto& [address,q]:rpc_queues)for(const auto& r:q.requests)if(r.packet[13]==a)throw std::runtime_error("RPC removal with queued calls");
        auto link=found->second.queue+8;for(unsigned guard=0;iop_read(link,4) && guard<256;++guard){if(iop_read(link,4)==a){iop_write(link,iop_read(a+56,4),4);break;}link=iop_read(link,4)+56;}
        rpc_servers.erase(found);iop.gpr[2]=a;return true;
    }
    if(ordinal==25){
        if(!rpc_queues.at(a).requests.empty())throw std::runtime_error("Removing RPC queue with pending requests");
        for(const auto& [thread,call]:rpc_calls)if(call.queue==a)throw std::runtime_error("Removing an active RPC queue");
        std::erase_if(rpc_servers,[&](const auto& item){return item.second.queue==a;});
        rpc_queues.erase(a);iop.gpr[2]=a;return true;
    }
    return false;
}
bool Console::rpc_command(std::span<const std::uint8_t> bytes) {
    const auto word=[&](unsigned n){if(n*4+4>bytes.size())throw std::runtime_error("Truncated RPC packet");return ps2::load<std::uint32_t>(bytes.data()+n*4);};
    const auto command=word(2);
    if(command!=0x80000009 && command!=0x8000000a)return false;
    if((bytes.size()&3) || bytes.size()>128)throw std::runtime_error("Invalid RPC packet alignment/size");
    if(command==0x80000009){
        const auto sid=word(8);
        for(const auto& [address,server]:rpc_servers)if(server.sid==sid){
            if(sif.replies.size()>=32)throw std::runtime_error("RPC reply queue full");
            sif.replies.push_back({48,word(1),0x80000008,0,word(4),word(5),word(6),word(7),command,address,server.buffer,server.callback_buffer});
            return true;
        }
        if(sid!=0x80000001){if(rpc_missing_server!=sid)std::fprintf(stderr,"[engine-rpc] absent sid=%08X\n",sid);rpc_missing_server=sid;}
        return false; // The existing protocol returns server=0 for an absent service.
    }
    const auto it=rpc_servers.find(word(13));if(it==rpc_servers.end())return false;
    const auto& server=it->second;auto& queue=rpc_queues.at(server.queue);
    const auto size=word(9);if(size>1u<<20 || size>iop_ram.size()-server.buffer || queue.requests.size()>=32)throw std::runtime_error("RPC request buffer/queue limit");
    const auto receive=translate(word(10),word(11),true);
    if(receive>ram.size() || word(11)>ram.size()-receive)throw std::runtime_error("RPC receive outside EE RAM");
    RpcRequest request;request.packet.resize(bytes.size()/4);std::memcpy(request.packet.data(),bytes.data(),bytes.size());
    request.packet[10]=receive;
    if(word(12)>1)throw std::runtime_error("Unsupported RPC receive mode");
    request.payload.assign(iop_ram.begin()+server.buffer,iop_ram.begin()+server.buffer+size);
    std::size_t pending=size;for(const auto& [at,q]:rpc_queues)for(const auto& r:q.requests)pending+=r.payload.size();
    if(pending>8u<<20)throw std::runtime_error("RPC queued payload budget");
    if(!queue.requests.empty())iop_write(queue.requests.back().packet[13]+60,server.address,4);
    else iop_write(server.queue+12,server.address,4);
    iop_write(server.queue+16,server.address,4);iop_write(server.address+60,0,4);queue.requests.push_back(std::move(request));
    auto& thread=iop_threads.at(queue.thread);
    if((thread.status&4) && thread.wait==6){if(!iop_active)iop_next_tick=std::max(iop_next_tick,now()+8);iop_ready(queue.thread);}
    return true;
}
void Console::rpc_next(std::uint32_t address) {
    auto& queue=rpc_queues.at(address);
    if(queue.thread!=iop_current || rpc_calls.contains(iop_current))throw std::runtime_error("RPC loop entered on the wrong/busy thread");
    iop.gpr[4]=address;iop.pc=0xffffffb4;iop.branchPending=false;
    if(queue.requests.empty()){iop_write(address+4,0,4);iop_write(address+12,0,4);iop_write(address+16,0,4);iop_block(6,address);return;}
    auto request=std::move(queue.requests.front());queue.requests.pop_front();
    const auto& p=request.packet;const auto& server=rpc_servers.at(p[13]);
    iop_write(address+4,1,4);iop_write(address+12,queue.requests.empty()?0:queue.requests.front().packet[13],4);if(queue.requests.empty())iop_write(address+16,0,4);
    const auto saved=iop;const auto size=static_cast<std::uint32_t>(request.payload.size());
    std::copy(request.payload.begin(),request.payload.end(),iop_ram.begin()+server.buffer);
    iop_write(server.address+12,size,4);iop_write(server.address+28,p[7],4);iop_write(server.address+32,p[5],4);iop_write(server.address+36,p[8],4);iop_write(server.address+40,p[10],4);iop_write(server.address+44,p[11],4);iop_write(server.address+48,p[12],4);iop_write(server.address+52,p[4],4);
    const auto function=p[8];
    rpc_calls.emplace(iop_current,RpcCall{std::move(request),saved,address});
    iop.pc=server.function;iop.gpr[4]=function;iop.gpr[5]=server.buffer;iop.gpr[6]=size;iop.gpr[7]=0;
    iop.gpr[28]=server.gp;iop.gpr[29]=(saved.gpr[29]-64)&~15u;iop.gpr[31]=0xffffffb0;
    iop.pendingLoad=false;
}
void Console::rpc_finish() {
    const auto it=rpc_calls.find(iop_current);if(it==rpc_calls.end())throw std::runtime_error("RPC return without a live request");
    auto call=std::move(it->second);rpc_calls.erase(it);const auto& p=call.request.packet;
    const auto result=iop.gpr[2];const auto n=p[11];
    if(n && result){if(result>iop_ram.size() || n>iop_ram.size()-result)throw std::runtime_error("RPC handler result outside IOP RAM");std::copy_n(iop_ram.data()+result,n,ram.data()+p[10]);}
    if(p[12]){
        if(sif.replies.size()>=32)throw std::runtime_error("RPC completion queue full");
        sif.replies.push_back({48,p[1],0x80000008,0,p[4],p[5],p[6],p[7],0x8000000a,0,0,0});
    } else {
        const auto packet=translate(p[5],64,true);if(packet>ram.size() || 64>ram.size()-packet)throw std::runtime_error("RPC no-interrupt completion outside EE RAM");
        ps2::store(ram.data()+packet+16,0u);ps2::store(ram.data()+packet+24,0u);
    }
    auto& server=rpc_servers.at(p[13]);
    if(server.completions++==0 || std::getenv("PS2N_ENGINE_TRACE"))std::fprintf(stderr,"[engine-rpc] complete sid=%08X fn=%u result=%08X tick=%llu\n",server.sid,p[8],n>=4 && result?iop_read(result,4):0,static_cast<unsigned long long>(ticks));
    iop=call.saved;sif_pump();rpc_next(call.queue);
    if(iop_reschedule)iop_schedule();
}
}
