// IRX relocation adapted from exoteric33 a50471a runtime/src/iop/kernel.cpp (MIT).
#include "ps2native/engine/console.hpp"
#include "ps2native/io.hpp"
#include <algorithm>
#include <map>
#include <format>

namespace ps2native::engine {
void Console::load_iop(const std::filesystem::path& path) {
    if (iop_active || (iop_work_pending() && !iop_module_returned)) throw std::runtime_error("Cannot replace a running IOP module");
    if (ps2native::regular_file_size(path)>16u<<20) throw std::runtime_error("IRX input exceeds 16 MiB bound");
    const auto file=ps2native::read_file(path);
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
    const auto base=(iop_heap_top+15)&~15u;
    const auto whole=std::max<std::uint64_t>(size,std::uint64_t{word(info+12)}+word(info+16)+word(info+20));
    if (whole>0x180000) throw std::runtime_error("IRX memory size overflows module arena");
    size=static_cast<std::uint32_t>(whole);
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
    for (std::uint32_t at=0;at+20<=initialized;at+=4) {
        if (get(at)!=0x41e00000) continue;
        const auto name=std::string(reinterpret_cast<const char*>(image.data()+at+12),8);
        const auto library=name.substr(0,name.find('\0'));
        for (auto stub=at+20;stub+8<=initialized;stub+=8) {
            const auto first=get(stub), second=get(stub+4);
            if (first!=0x03e00008 || (second&0xffff0000)!=0x24000000) break;
            imports.emplace(base+stub,std::pair(library,second&0xffff));
        }
    }
    std::copy(image.begin(),image.end(),iop_ram.begin()+base);
    iop_imports.insert(imports.begin(),imports.end()); iop_heap_top=base+size;
    iop={}; iop_import_slot=0; iop.pc=base+word(info+4); iop.gpr[28]=base+word(info+8);
    iop.gpr[29]=0x1ef000; iop.gpr[31]=0xfffffff0;
    iop_next_tick=now()+8; iop_active=true; iop_result=0;iop_module_returned=false;
    iop_current=iop_next_thread++;
    auto& thread=iop_threads[iop_current];thread.context=iop;thread.status=1;
    thread.entry=iop.pc;thread.stack=0x1df000;thread.size=0x10000;thread.gp=iop.gpr[28];
    thread.initial_priority=thread.priority=8;thread.attr=0x02000000;
}
}
