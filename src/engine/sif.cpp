// SIF protocol adaptation from exoteric33/ps2-native-recomp a50471a (MIT).
// Reference contracts: ps2dev/ps2sdk ee/kernel/src/{sifcmd,sifrpc}.c (AFL-2.0).
#include "ps2native/engine/console.hpp"
#include <algorithm>
#include <format>
#include <cstdio>
#include <stdexcept>
#include "iop_cdvd.hpp"
#include "ps2native/bios.hpp"

namespace ps2native::engine {
namespace {
std::uint32_t physical(std::uint32_t address) {
    if (address >= 0x80000000 && address < 0xc0000000) return address & 0x1fffffff;
    if ((address >> 28) == 2 || (address >> 28) == 3) return address & 0xfffffff;
    return address;
}
std::span<std::uint8_t> memory(std::vector<std::uint8_t>& bytes, std::uint32_t address, std::size_t length) {
    address = physical(address);
    if (address > bytes.size() || length > bytes.size() - address) throw std::runtime_error("SIF memory span outside RAM");
    return {bytes.data() + address, length};
}
}
void Console::initialize_sif() {
    sif = {};
    dmac_status &= ~(1u<<5);
    sif.command_buffer = static_cast<std::uint32_t>(iop_ram.size()) - 0x10000;
    std::fill(iop_ram.begin() + sif.command_buffer, iop_ram.end(), 0);
    // The HLE SIF manager and CMD receiver are now registered; no original IOP BOOTEND is claimed.
    sif.smflag = 0x30000;
    reschedule_requested = false;
}
std::uint32_t Console::sif_getreg(std::uint32_t reg) {
    account();
    switch(reg) {
    case 1: return sif.main_address;
    case 2: return sif.command_buffer;
    case 3: return sif.msflag;
    case 4: return sif.smflag;
    default:
        if (reg >= 0x80000000 && reg <= 0x80000004) return sif_registers[reg];
        throw std::runtime_error(std::format("Unsupported SIF register 0x{:08X}",reg));
    }
}
void Console::sif_setreg(std::uint32_t reg, std::uint32_t value) {
    account();
    if (reg == 1) sif.main_address = value;
    else if (reg == 3) sif.msflag |= value;
    else if (reg == 4) sif.smflag &= ~value;
    else if (reg >= 0x80000000 && reg <= 0x80000004) sif_registers[reg] = value;
    else throw std::runtime_error(std::format("Unsupported SIF register write 0x{:08X}",reg));
}
void Console::sif_command(std::span<const std::uint8_t> packet) {
    if (packet.size() < 16 || (ps2::load<std::uint32_t>(packet.data()) & 0xff) != packet.size())
        throw std::runtime_error("Invalid SIF command packet length");
    const auto cid = ps2::load<std::uint32_t>(packet.data() + 8);
    const auto opt = ps2::load<std::uint32_t>(packet.data() + 12);
    if (const auto native=sif.iop_handlers.find(cid); native!=sif.iop_handlers.end()) {
        if (sif.iop_commands.size()>=32) throw std::runtime_error("IOP SIF command queue full");
        sif.iop_commands.emplace_back(native->second,std::vector<std::uint8_t>(packet.begin(),packet.end()));
        if (!iop_active && !iop_callback_active) iop_next_tick=std::max(iop_next_tick,now()+8);
        return;
    }
    if(cid==0x80000003) {
        if(packet.size()<24)throw std::runtime_error("Truncated SIF reset packet");
        const auto length=ps2::load<std::uint32_t>(packet.data()+16),mode=ps2::load<std::uint32_t>(packet.data()+20);
        if(length>80 || length>packet.size()-24)throw std::runtime_error("Invalid SIF reset arguments");
        const std::string args(reinterpret_cast<const char*>(packet.data()+24),length);
        const auto prefix=std::string("rom0:UDNL cdrom0:");
        if(mode || !args.starts_with(prefix) || !cdvd->media)throw std::runtime_error("Unsupported IOP reboot arguments: "+args);
        const auto image=ps2native::load_iop_reboot_image(cdvd->media->read(args.substr(prefix.size())));
        std::deque<std::pair<std::string,std::vector<std::uint8_t>>> modules;
        for(const auto name:{"FILEIO","CDVDMAN","CDVDFSV","LOADFILE"}) {
            const auto* m=image.find(name);if(!m)throw std::runtime_error(std::string("Missing original IOPRP module: ")+name);
            modules.emplace_back(std::string(name),std::vector<std::uint8_t>(image.bytes.begin()+m->offset,image.bytes.begin()+m->offset+m->size));
        }
        account();reset_iop();sif.command_buffer=static_cast<std::uint32_t>(iop_ram.size())-0x10000;
        iop_boot_modules=std::move(modules);iop_boot_active=true;iop_next_tick=now()+8;
        std::fprintf(stderr,"[engine-boot] IOPRP %s; original FILEIO/CDVDMAN/CDVDFSV/LOADFILE, kernel/SIF HLE\n",image.sha256.c_str());
        return;
    }
    if(rpc_command(packet))return;
    if (cid == 0x80000000 || (cid == 0x80000002 && opt == 0)) {
        if (packet.size() < 20) throw std::runtime_error("Truncated SIF receive-address command");
        const auto address = ps2::load<std::uint32_t>(packet.data()+16);
        (void)memory(ram,address,128);
        sif.ee_receive = physical(address);
    } else if (cid == 0x80000002 && opt == 1) {
        // RPC init completes through the EE's original SetSreg command handler.
        if (sif.replies.size() >= 32) throw std::runtime_error("SIF reply queue full");
        sif.rpc_ready = true;
        sif.replies.push_back({24,0,0x80000001,0,0,1});
    } else if (cid == 0x80000009 || cid == 0x8000000a) {
        const auto required = cid == 0x80000009 ? 36u : 56u;
        if (packet.size() < required || !sif.rpc_ready) throw std::runtime_error("Invalid/uninitialized SIF RPC packet");
        if (sif.replies.size() >= 32) throw std::runtime_error("SIF reply queue full");
        const auto word = [&](std::size_t i) { return ps2::load<std::uint32_t>(packet.data()+i*4); };
        std::vector<std::uint32_t> reply{48,word(1),0x80000008,0,word(4),word(5),word(6),word(7),cid,0,0,0};
        const auto buffer = sif.command_buffer + 0x1000;
        const auto endpoint = sif.command_buffer + 0x2000;
        if (cid == 0x80000009) {
            if (word(8) == 0x80000001) { // The only registered RPC service is FILEIO/TTY.
                reply[9]=endpoint; reply[10]=buffer; reply[11]=buffer+0x800;
            }
        } else {
            if (word(13) != endpoint) throw std::runtime_error("Unregistered SIF RPC endpoint");
            const auto method=word(8), send_size=word(9), receive=word(10), receive_size=word(11);
            if (send_size > 0x800 || receive_size < 4) throw std::runtime_error("Invalid TTY RPC buffer size");
            const auto args=memory(iop_ram,buffer,send_size);
            const auto result_buffer=memory(ram,receive,receive_size);
            const auto arg=[&](std::size_t i) {
                if (i*4+4 > args.size()) throw std::runtime_error("Truncated TTY RPC arguments");
                return ps2::load<std::uint32_t>(args.data()+i*4);
            };
            std::int32_t result=-9;
            if (method == 0) {
                const auto flags=arg(0);
                const auto end=std::find(args.begin()+4,args.end(),0);
                if (end == args.end()) throw std::runtime_error("Unterminated TTY open path");
                const std::string name(reinterpret_cast<const char*>(args.data()+4),static_cast<std::size_t>(end-(args.begin()+4)));
                if (name != "tty:" && name != "tty0:") throw std::runtime_error("Unsupported FILEIO path: "+name);
                if (!(flags&3) || (flags & ~3u)) throw std::runtime_error("Unsupported TTY open flags");
                std::uint32_t fd=0; while(sif.tty_descriptors.contains(fd)) ++fd;
                sif.tty_descriptors.emplace(fd,flags); result=static_cast<std::int32_t>(fd);
            } else if (method == 1) {
                result=sif.tty_descriptors.erase(arg(0)) ? 0 : -9;
            } else if (method == 3) {
                const auto fd=arg(0), pointer=arg(1), size=arg(2);
                const auto descriptor=sif.tty_descriptors.find(fd);
                if (descriptor != sif.tty_descriptors.end() && (descriptor->second&2)) {
                    if (size > (1u<<20) || output.size() > (1u<<20)-size) throw std::runtime_error("TTY output limit exceeded");
                    const auto bytes=memory(ram,pointer,size);
                    tty_write(false,{reinterpret_cast<const char*>(bytes.data()),bytes.size()});
                    result=static_cast<std::int32_t>(size);
                }
            } else throw std::runtime_error(std::format("Unsupported FILEIO/TTY method {}",method));
            ps2::store(result_buffer.data(),result);
        }
        sif.replies.push_back(std::move(reply));
    } else throw std::runtime_error(std::format("Unsupported SIF command 0x{:08X}",cid));
}
std::uint32_t Console::sif_dma(std::uint32_t descriptors, std::uint32_t count) {
    account();
    if (!count || count > 32) throw std::runtime_error("Invalid SIF DMA descriptor count");
    const auto list = memory(ram,descriptors,static_cast<std::size_t>(count)*16);
    struct Transfer { std::uint32_t destination, size, attr; std::span<std::uint8_t> source; };
    std::vector<Transfer> transfers;
    // Validate the complete list before writing either memory.
    for (std::uint32_t i=0; i<count; ++i) {
        const auto* d=list.data()+i*16;
        const auto source=ps2::load<std::uint32_t>(d), dest=ps2::load<std::uint32_t>(d+4);
        const auto size=ps2::load<std::uint32_t>(d+8), attr=ps2::load<std::uint32_t>(d+12);
        if (size > 1u<<20 || attr & ~0x44u) throw std::runtime_error("Unsupported SIF DMA size/attributes");
        const auto rounded=(size+15u)&~15u;
        (void)memory(iop_ram,dest,rounded);
        transfers.push_back({dest,size,attr,memory(ram,source,rounded)});
    }
    const auto id=++sif.transfer_id;
    for (const auto& t : transfers) {
        const auto dest=memory(iop_ram,t.destination,t.source.size());
        std::copy(t.source.begin(),t.source.end(),dest.begin());
        if (physical(t.destination) == sif.command_buffer) sif_command(dest.first(t.size));
    }
    sif.completed_id=id;
    sif_pump();
    return id;
}
void Console::iop_send_command(std::uint32_t cid,std::uint32_t packet,std::uint32_t size,
                               std::uint32_t source,std::uint32_t destination,std::uint32_t extra) {
    if (size<16 || size>128 || (size&3) || sif.replies.size()>=32) throw std::runtime_error("Invalid/full IOP SIF command");
    const auto bytes=memory(iop_ram,packet,size);
    if (extra) {
        if (extra>1u<<20) throw std::runtime_error("IOP SIF extra data exceeds bound");
        const auto from=memory(iop_ram,source,extra),to=memory(ram,destination,extra);
        std::copy(from.begin(),from.end(),to.begin());
    }
    ps2::store(bytes.data(),size|(extra<<8)); ps2::store(bytes.data()+4,destination); ps2::store(bytes.data()+8,cid);
    std::vector<std::uint32_t> words(size/4);
    std::memcpy(words.data(),bytes.data(),size); sif.replies.push_back(std::move(words));
    ++sif.transfer_id; sif.completed_id=sif.transfer_id;
    sif_pump();
}
void Console::sif_pump() {
    if (!sif.armed || sif.interrupt || in_interrupt || !sif.ee_receive || sif.replies.empty()) return;
    const auto& reply=sif.replies.front();
    const auto target=memory(ram,sif.ee_receive,reply.size()*4);
    std::memcpy(target.data(),reply.data(),target.size());
    dma_registers[0x1000c010] = sif.ee_receive + static_cast<std::uint32_t>(target.size());
    dma_registers[0x1000c020] = 0;
    dma_registers[0x1000c000] &= ~0x100u;
    sif.replies.pop_front(); sif.armed=false; sif.interrupt=true;
    dmac_status |= 1u<<5;
}
bool Console::interrupt_ready() const {
    return !in_interrupt && (ee.cop0[12] & 0x10000) &&
        ((intc_status & intc_mask) || (dmac_status & (dmac_status >> 16) & 0x3ff));
}
void Console::service_interrupts() {
    if (!interrupt_ready()) return;
    account();
    const auto saved = ee;
    in_interrupt=true;
    const auto restore=[&] {const auto live_vu0=ee.vu0;ee=saved;ee.vu0=live_vu0;ee.budget=100000;in_interrupt=false;};
    try {
        const auto intc=intc_status & intc_mask;
        const auto dma=dmac_status & (dmac_status >> 16) & 0x3ff;
        for(unsigned source=0;source<2;++source)for(unsigned cause=0;cause<(source ? 10u : 15u);++cause) {
            if(!((source ? dma : intc)&(1u<<cause)))continue;
            if(source){dmac_status &= ~(1u<<cause);if(cause==5)sif.interrupt=false;}
            else intc_status &= ~(1u<<cause);
            bool served=false;
            const auto registered=interrupt_order;
            for(const auto id:registered) {
                const auto it=handlers.find(id);
                if(it==handlers.end() || it->second.cause!=(cause | (source ? 0x100u : 0u)))continue;
                const auto handler=it->second;served=true;
                const auto live_vu0=ee.vu0;ee=saved;ee.vu0=live_vu0;
                ee.r[4].w[0]=cause;ee.r[5].w[0]=handler.argument;
                ee.pc=handler.function;ee.r[28].w[0]=handler.gp;ee.r[31].w[0]=0xffffffe0;
                do {ee.pc &= ~ps2::kUnwind;ps2::dispatch(ee,0,0);} while((ee.pc & ~ps2::kUnwind)!=0xffffffe0);
                account();
                if(ee.r[2].sw[0]<0)break;
            }
            if(!served)throw std::runtime_error("Enabled EE interrupt has no original handler");
        }
    } catch(...) {restore();throw;}
    restore();sif_pump();
}
}
