// IRX relocation adapted from exoteric33 a50471a runtime/src/iop/kernel.cpp (MIT).
#include "ps2native/engine/console.hpp"
#include "ps2native/io.hpp"
#include "ps2native/bios.hpp"
#include "iop_cdvd.hpp"
#include "ps2native/sha256.hpp"
#include "ps2/os.hpp"
#include <cstdio>
#include <algorithm>
#include <map>
#include <format>

namespace ps2native::engine {
void Console::load_iop(const std::filesystem::path& path) {
    if (ps2native::regular_file_size(path)>16u<<20) throw std::runtime_error("IRX input exceeds 16 MiB bound");
    const auto file=ps2native::read_file(path);
    (void)load_iop(file,ps2::os::utf8(path));
}
std::uint32_t Console::load_iop(std::span<const std::uint8_t> file,std::string origin) {
    const auto word=[&](std::size_t at) {
        if (at>file.size() || 4>file.size()-at) throw std::runtime_error("Truncated IRX");
        return ps2::load<std::uint32_t>(file.data()+at);
    };
    const auto half=[&](std::size_t at) { return static_cast<std::uint16_t>(word(at)&0xffff); };
    if (file.size()<52 || word(0)!=0x464c457f || file[4]!=1 || file[5]!=1 || half(18)!=8 || (half(16)!=0xff80 && half(16)!=0xff81))
        throw std::runtime_error("Expected little-endian MIPS IRX");
    const auto phoff=word(28), shoff=word(32); const auto phsize=half(42), phnum=half(44), shsize=half(46), shnum=half(48);
    if (phsize!=32 || shsize!=40 || phnum>128 || shnum>4096 || phoff>file.size() || std::uint64_t{phnum}*32>file.size()-phoff || shoff>file.size() || std::uint64_t{shnum}*40>file.size()-shoff)
        throw std::runtime_error("Invalid IRX header tables");
    std::uint32_t info=0,offset=0,initialized=0,size=0;
    for (unsigned i=0;i<phnum;++i) {
        const auto ph=phoff+i*32;
        if (word(ph)==0x70000080) info=word(ph+4);
        if (word(ph)==1) {
            if (size || word(ph+8)!=0) throw std::runtime_error("Multiple/nonzero-based IRX segments unsupported");
            offset=word(ph+4); initialized=word(ph+16); size=word(ph+20);
        }
    }
    if (!info || info>file.size() || 24>file.size()-info || !size || initialized>size || offset>file.size() || initialized>file.size()-offset)
        throw std::runtime_error("Invalid IRX load segment/metadata");
    std::uint32_t base=0x10000;
    const auto whole=std::max<std::uint64_t>(size,std::uint64_t{word(info+12)}+word(info+16)+word(info+20));
    if (whole>0x180000) throw std::runtime_error("IRX memory size overflows module arena");
    size=static_cast<std::uint32_t>(whole);
    const auto reserved=(size+15)&~15u;
    for(const auto& [at,bytes]:iop_allocations){if(reserved<=at-base)break;base=std::max(base,at+bytes);}
    base=(base+15)&~15u;
    if (base>=0x180000 || size>0x180000-base || word(info+4)>=size || word(info+8)>0x1fffff-base)
        throw std::runtime_error("IRX outside IOP module/stack/service boundaries");
    std::vector<std::uint8_t> image(size);
    std::copy_n(file.data()+offset,initialized,image.data());
    const auto get=[&](std::uint32_t at) {
        if ((at&3) || at>image.size() || 4>image.size()-at) throw std::runtime_error("IRX relocation outside image");
        return ps2::load<std::uint32_t>(image.data()+at);
    };
    const auto put=[&](std::uint32_t at,std::uint32_t v) { (void)get(at); ps2::store(image.data()+at,v); };
    for (unsigned i=0;i<shnum;++i) {
        const auto sh=shoff+i*40;
        if (word(sh+4)!=9) continue;
        const auto off=word(sh+16), bytes=word(sh+20);
        if ((bytes&7) || off>file.size() || bytes>file.size()-off) throw std::runtime_error("Invalid IRX relocation table");
        std::vector<std::uint32_t> high;
        for (std::uint32_t k=0;k<bytes;k+=8) {
            const auto at=word(off+k), type=word(off+k+4)&255, instruction=get(at);
            if (type==2) put(at,instruction+base);
            else if (type==4) put(at,(instruction&0xfc000000)|(((instruction&0x3ffffff)+(base>>2))&0x3ffffff));
            else if (type==5) {
                if (half(16)==0xff81) {
                    if (k+8>=bytes) throw std::runtime_error("Unpaired IRX v2 HI16");
                    const auto low=get(word(off+k+8));
                    const auto value=(instruction<<16)+static_cast<std::int16_t>(low)+base;
                    put(at,(instruction&0xffff0000)|((value+0x8000)>>16));
                } else high.push_back(at);
            } else if (type==6) {
                if (high.empty()) put(at,(instruction&0xffff0000)|((instruction+base)&0xffff));
                else {
                    for (auto h : high) {
                        const auto hi=get(h),value=(hi<<16)+static_cast<std::int16_t>(instruction)+base;
                        put(h,(hi&0xffff0000)|((value+0x8000)>>16));
                    }
                    high.clear(); put(at,(instruction&0xffff0000)|((instruction+base)&0xffff));
                }
            } else if (type!=0) throw std::runtime_error(std::format("Unsupported IRX relocation {}",type));
        }
        if (!high.empty()) throw std::runtime_error("Unpaired IRX HI16");
    }
    std::map<std::uint32_t,std::pair<std::string,std::uint32_t>> imports;
    std::map<std::uint32_t,std::uint16_t> versions;
    for (std::uint32_t at=0;at+20<=initialized;at+=4) {
        if (get(at)!=0x41e00000) continue;
        const auto name=std::string(reinterpret_cast<const char*>(image.data()+at+12),8);
        const auto library=name.substr(0,name.find('\0'));
        for (auto stub=at+20;stub+8<=initialized;stub+=8) {
            const auto first=get(stub), second=get(stub+4);
            if (first!=0x03e00008 || (second&0xffff0000)!=0x24000000) break;
            imports.emplace(base+stub,std::pair(library,second&0xffff));versions.emplace(base+stub,get(at+8)&0xffff);
        }
    }
    // Reserve before publishing either image or imports; every entry thread has a distinct stack.
    const auto stack_size=0x4000u;auto stack=0x180000u;
    std::vector<std::pair<std::uint32_t,std::uint32_t>> occupied;
    for(const auto& [id,t]:iop_threads)if(t.stack>=0x180000 && t.stack<0x1d0000)occupied.emplace_back(t.stack,t.size);
    std::sort(occupied.begin(),occupied.end());
    for(const auto& [at,bytes]:occupied){if(stack+stack_size<=at)break;stack=std::max(stack,at+bytes);}
    if(stack+stack_size>0x1d0000)throw std::runtime_error("No IOP module entry stack available");
    const auto id=iop_next_thread++;
    IopThread thread;thread.status=2;thread.entry=base+word(info+4);thread.gp=base+word(info+8);
    thread.stack=stack;thread.size=stack_size;thread.initial_priority=thread.priority=8;thread.attr=0x02000000;thread.module=id;thread.order=++iop_order;
    thread.context.pc=thread.entry;thread.context.gpr[28]=thread.gp;thread.context.gpr[29]=stack+stack_size-16;thread.context.gpr[31]=0xfffffff0;
    std::copy(image.begin(),image.end(),iop_ram.begin()+base);
    iop_import_versions.insert(versions.begin(),versions.end());
    iop_imports.insert(imports.begin(),imports.end());iop_allocations[base]=(size+15)&~15u;iop_heap_top=std::max(iop_heap_top,base+size);
    iop_modules.emplace(id,IopModule{base,size,thread.entry,thread.gp,id,std::move(origin)});
    iop_threads.emplace(id,thread);iop_module_returned=false;iop_reschedule=true;
    if(!iop_active){iop_schedule();iop_next_tick=now()+8;}
    return id;
}
void Console::iop_boot_step() {
    if(iop_boot_waiting) {
        const auto& m=iop_modules.at(iop_boot_waiting);
        if(!m.returned)return;
        if(!m.resident)throw std::runtime_error("IOPRP module did not remain resident: "+m.origin);
        std::fprintf(stderr,"[engine-boot] resident %s base=%08X tick=%llu\n",m.origin.c_str(),m.base,static_cast<unsigned long long>(ticks));
        iop_boot_waiting=0;
    }
    if(!iop_boot_modules.empty()) {
        auto module=std::move(iop_boot_modules.front());iop_boot_modules.pop_front();
        iop_boot_waiting=load_iop(module.second,"ioprp:"+module.first);return;
    }
    const auto registered=[&](std::uint32_t sid){return std::any_of(rpc_servers.begin(),rpc_servers.end(),[&](const auto& p){return p.second.sid==sid;});};
    if(!registered(0x80000001) || !registered(0x80000006) || !registered(0x80000592)) {
        if(!iop_work_pending())throw std::runtime_error("IOPRP initialization ended without its original RPC registrations");
        return;
    }
    // The HLE boot manager completes only after the original entries and service registrations complete.
    sif.smflag |= 0x70000;iop_boot_active=false;
    std::fprintf(stderr,"[engine-boot] original IOPRP services ready tick=%llu\n",static_cast<unsigned long long>(ticks));
}
void Console::boot_cdvd(const std::filesystem::path& path,bool extended) {
    const auto bytes=ps2native::read_file(path);const auto bios=ps2native::load_bios_rom0(bytes);
    iop_rom0=path;
    if(cdvd->hardware && cdvd->hardware->romver!=bios.version.romver)throw std::runtime_error("CDVD console profile does not match ROM0 revision");
    std::fprintf(stderr,"[engine-firmware] ROM0 %s ROMVER=%s\n",bios.sha256.c_str(),bios.version.romver.c_str());
    const char* names[]={extended?"XCDVDMAN":"CDVDMAN",extended?"XCDVDFSV":"CDVDFSV"};
    for(const auto name:names){
        const auto* module=bios.find(name);if(!module)throw std::runtime_error(std::string("Original ROM0 module missing: ")+name);
        const auto image=std::span(bios.rom0).subspan(module->offset,module->size);
        const auto id=load_iop(image,std::string("rom0:")+name);
        const auto deadline=ticks+5000000;
        while(!iop_modules.at(id).returned){if(!iop_work_pending() || ticks>=deadline)throw std::runtime_error("Original CDVD module entry failed to return");advance_to(iop_next_tick);}
        const auto& loaded=iop_modules.at(id);
        std::fprintf(stderr,"[engine-module] %s base=%08X bytes=%u result=%d sha256=%s\n",name,loaded.base,loaded.size,loaded.result,ps2native::sha256_hex(image).c_str());
        if(!loaded.resident)throw std::runtime_error("Original CDVD module did not stay resident");
    }
    const auto deadline=ticks+5000000;
    while(std::none_of(rpc_servers.begin(),rpc_servers.end(),[](const auto& item){return item.second.sid==0x80000592;})){
        if(!iop_work_pending() || ticks>=deadline)throw std::runtime_error("Original CDVDFSV did not register CDVD Init RPC");advance_to(iop_next_tick);
    }
}
std::uint32_t Console::iop_allocate(std::uint32_t bytes) {
    if(!bytes || bytes>0x170000)return 0;bytes=(bytes+15)&~15u;auto base=0x10000u;
    for(const auto& [at,size]:iop_allocations){if(bytes<=at-base)break;base=at+size;}
    if(base>=0x180000 || bytes>0x180000-base)return 0;
    iop_allocations.emplace(base,bytes);std::fill_n(iop_ram.begin()+base,bytes,0);iop_heap_top=std::max(iop_heap_top,base+bytes);return base;
}
void Console::iop_release(std::uint32_t address) {
    const auto it=iop_allocations.find(address);if(it==iop_allocations.end())throw std::runtime_error("IOP free of an unallocated block");
    if(std::any_of(iop_modules.begin(),iop_modules.end(),[&](const auto& item){return item.second.base==address && (!item.second.returned || item.second.resident);}))throw std::runtime_error("Cannot free a live IOP module");
    std::fill_n(iop_ram.begin()+address,it->second,0);iop_allocations.erase(it);
}
void Console::iop_module_return() {
    if(!iop_current || !iop_threads.at(iop_current).module)return;
    const auto id=iop_threads.at(iop_current).module;auto& module=iop_modules.at(id);
    iop_threads.at(iop_current).delete_on_exit=true;module.result=static_cast<std::int32_t>(iop.gpr[2]);module.returned=true;
    module.resident=module.result==0 || module.result==2;
    if(const auto waiter=iop_module_waiters.find(id);waiter!=iop_module_waiters.end()) {
        for(const auto& w:waiter->second){
            if(w.result)iop_write(w.result,static_cast<std::uint32_t>(module.result),4);
            iop_ready(w.thread,id);
            if(!w.library.empty()){
                const auto lib=iop_libraries.find(w.library);
                if(!module.resident || lib==iop_libraries.end() || w.ordinal>=lib->second.exports.size() || (w.version && ((w.version&0xff00)!=(lib->second.version&0xff00) || (w.version&255)>(lib->second.version&255))))throw std::runtime_error("Original dependency did not register the required export ABI");
                iop_threads.at(w.thread).context.pc=lib->second.exports[w.ordinal];
            }
        }
        iop_module_waiters.erase(waiter);
    }
    if(!module.resident){
        for(const auto& [name,lib]:iop_libraries)if(lib.owner==id)throw std::runtime_error("Nonresident IOP module left registered exports");
        const auto base=module.base,end=base+module.size;
        for(const auto& [tid,t]:iop_threads)if(tid!=iop_current && t.status!=16 && t.entry>=base && t.entry<end)throw std::runtime_error("Nonresident IOP module left live threads");
        std::erase_if(iop_import_versions,[&](const auto& item){return item.first>=base && item.first<end;});
        std::erase_if(iop_imports,[&](const auto& item){return item.first>=base && item.first<end;});
        iop_allocations.erase(base);std::fill(iop_ram.begin()+base,iop_ram.begin()+end,0);
    }
}
bool Console::iop_loader_import(const std::string& library,std::uint32_t ordinal,std::uint32_t version) {
    const auto a=iop_arg(0),b=iop_arg(1);const auto result=[&](std::int32_t value){iop.gpr[2]=static_cast<std::uint32_t>(value);return true;};
    if(library=="secrman"){
        if(iop_rom0.empty() || !iop_current)throw std::runtime_error("Original SECRMAN requires the selected user ROM0 and an IOP thread");
        std::uint32_t id=0;for(const auto& [candidate,m]:iop_modules)if(m.origin=="rom0:SECRMAN" && !m.returned)id=candidate;
        if(!id){
            const auto bios=ps2native::load_bios_rom0(ps2native::read_file(iop_rom0));const auto* m=bios.find("SECRMAN");
            if(!m)throw std::runtime_error("Original SECRMAN missing from selected ROM0");
            id=load_iop(std::span(bios.rom0).subspan(m->offset,m->size),"rom0:SECRMAN");
            std::fprintf(stderr,"[engine-module] original SECRMAN dependency id=%u\n",id);
        }
        iop_module_waiters[id].push_back({iop_current,0,library,ordinal,version});iop_block(7,id);return true;
    }
    if(library=="modload" && ordinal>=12 && ordinal<=14){
        if(ordinal==12 || ordinal==13){for(const auto p:ordinal==12?std::vector<std::uint32_t>{a,b,iop_arg(2)}:std::vector<std::uint32_t>{a})if(p && ((p&3) || p>=iop_ram.size()))throw std::runtime_error("MODLOAD callback outside IOP RAM");}
        if(ordinal==12){iop_modload_callbacks[0]=a;iop_modload_callbacks[1]=b;iop_modload_callbacks[2]=iop_arg(2);}
        if(ordinal==13)iop_modload_callbacks[3]=a;
        if(ordinal==14){if(a)iop_write(a,iop_modload_callbacks[3],4);if(b)iop_write(b,iop_modload_callbacks[2],4);}
        return true;
    }
    if(library=="modload" && ordinal==15){
        auto path=iop_string(a,1024);const auto start=path.find_first_not_of(' ');path=start==path.npos ? "" : path.substr(start);
        std::transform(path.begin(),path.end(),path.begin(),[](unsigned char ch){return static_cast<char>(ch|32);});
        for(const auto prefix:{"mc","hd","net","dev"})if(path.starts_with(prefix)) {
            const auto at=std::char_traits<char>::length(prefix);
            return result(path.size()>at && path[at]>='0' && path[at]<=':');
        }
        return result(0); // Matched ROM0 MODLOAD ordinal 15, not PS2SDK's unconditional success placeholder.
    }
    if(library=="modload" && ordinal==7){
        const auto path=iop_string(a,1024);const auto arguments=iop_arg(2),out=iop_arg(3),caller=iop_current;
        if(!caller || b>4096)throw std::runtime_error("Invalid LoadStartModule caller/arguments");
        if(out)(void)iop_read(out,4);
        std::vector<std::string> words{path};
        for(std::uint32_t k=0;k<b;){const auto text=iop_string(arguments+k,b-k);words.push_back(text);k+=static_cast<std::uint32_t>(text.size())+1;if(words.size()>64)throw std::runtime_error("Too many module arguments");}
        std::vector<std::uint8_t> image;
        if(path.starts_with("cdrom0:") || path.starts_with("cdrom:")) {
            if(!cdvd->media)return result(-203);
            image=cdvd->media->read(path.substr(path.find(':')+1));
        } else if(path.starts_with("rom0:")) {
            if(iop_rom0.empty())return result(-203);
            const auto bios=ps2native::load_bios_rom0(ps2native::read_file(iop_rom0));const auto* m=bios.find(path.substr(5));
            if(!m)return result(-203);image.assign(bios.rom0.begin()+m->offset,bios.rom0.begin()+m->offset+m->size);
        } else throw std::runtime_error("Unintegrated module device: "+path);
        const auto id=load_iop(image,path);auto& t=iop_threads.at(iop_modules.at(id).thread);
        const auto argv=t.stack+t.size-0x2100;auto data=argv+0x110;
        for(unsigned k=0;k<words.size();++k){iop_write(argv+k*4,data,4);for(const auto ch:words[k])iop_write(data++,static_cast<std::uint8_t>(ch),1);iop_write(data++,0,1);}
        iop_write(argv+static_cast<std::uint32_t>(words.size())*4,0,4);
        t.context.gpr[4]=static_cast<std::uint32_t>(words.size());t.context.gpr[5]=argv;t.context.gpr[29]=argv-16;
        iop_module_waiters[id].push_back({caller,out});iop_block(7,id);
        std::fprintf(stderr,"[engine-module] starting %s id=%u argc=%zu\n",path.c_str(),id,words.size());return true;
    }
    if(library=="sysmem" && ordinal==4){if(a!=0 || iop_arg(2))throw std::runtime_error("IOP fixed/top allocation mode is not integrated");return result(iop_allocate(b));}
    if(library=="sysmem" && ordinal==5){iop_release(a);return result(0);}
    if(library!="loadcore")return false;
    if(ordinal==4 || ordinal==5){return result(0);} // Interpreter reads the coherent Console RAM directly.
    if(ordinal==6){
        if(iop_read(a,4)!=0x41c00000)throw std::runtime_error("Invalid IOP export table");
        std::string name;for(unsigned k=0;k<8;++k){const auto ch=iop_read(a+12+k,1);if(!ch)break;name+=static_cast<char>(ch);}const auto version=iop_read(a+8,4)&0xffff;
        if(iop_libraries.contains(name))return result(-1);
        IopLibrary lib;lib.address=a;lib.version=version;
        for(const auto& [id,m]:iop_modules)if((!m.returned || m.resident) && a>=m.base && a-m.base<m.size){lib.owner=id;break;}
        for(unsigned k=0;k<256;++k){const auto function=iop_read(a+20+k*4,4);if(!function)break;if((function&3) || (function&0x1fffffff)>=iop_ram.size())throw std::runtime_error("IOP export outside RAM");lib.exports.push_back(function);}
        if(lib.exports.empty())throw std::runtime_error("Empty IOP export table");
        iop_libraries.emplace(name,std::move(lib));return result(0);
    }
    if(ordinal==7){for(auto it=iop_libraries.begin();it!=iop_libraries.end();++it)if(it->second.address==a){iop_libraries.erase(it);return result(0);}return result(-1);}
    if(ordinal==12){std::string name;for(unsigned k=0;k<8;++k){const auto ch=iop_read(a+4+k,1);if(!ch)break;name+=static_cast<char>(ch);}const auto it=iop_libraries.find(name);return result(it==iop_libraries.end()?0:it->second.address);}
    return false;
}
}
