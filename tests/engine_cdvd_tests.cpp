#include "ps2native/engine/console.hpp"
#include "iop_cdvd.hpp"
#include "iso_image.hpp"
#include "ps2native/io.hpp"
#include "ps2native/bios.hpp"
#include "ps2/os.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <tuple>
#include <algorithm>
#include <stdexcept>
using namespace ps2native::engine;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
// Original, tiny IRXs: provider registers an export; consumer calls it through its actual import stub.
static std::vector<std::uint8_t> module(bool provider){
    std::vector<std::uint8_t> file(0x580);
    const auto w=[&](unsigned at,std::uint32_t v){ps2::store(file.data()+at,v);};
    const auto h=[&](unsigned at,std::uint16_t v){ps2::store(file.data()+at,v);};
    w(0,0x464c457f);file[4]=file[5]=file[6]=1;h(16,0xff80);h(18,8);w(20,1);w(28,52);w(32,0x500);h(40,52);h(42,32);h(44,2);h(46,40);h(48,2);
    w(52,0x70000080);w(56,0x80);w(84,1);w(88,0x100);w(100,0x400);w(104,0x500);
    w(0x80,0x300);w(0x84,0);w(0x88,0x300);w(0x8c,0x300);w(0x90,0x100);w(0x94,0x100);
    const std::uint32_t code[]={0x27bdfff0,0xafbf000c,provider?0x3c040000u:0u,provider?0x24840200u:0u,0x0c000040,0,provider?0u:0xac020800u,0x8fbf000c,0,0x27bd0010,0x03e00008,0x00001021};
    std::memcpy(file.data()+0x100,code,sizeof(code));w(0x180,0x03e00008);w(0x184,0x24020007);
    w(0x1ec,0x41e00000);w(0x1f4,0x100);std::memcpy(file.data()+0x1f8,provider?"loadcore":"native",provider?8:6);w(0x200,0x03e00008);w(0x204,0x24000000|(provider?6:0));
    if(provider){w(0x300,0x41c00000);w(0x308,0x100);std::memcpy(file.data()+0x30c,"native",6);w(0x314,0x80);}
    w(0x52c,9);w(0x538,0x550);w(0x53c,provider?32:8);
    if(provider){const std::uint32_t rel[]={8,5,12,6,16,4,0x214,2};std::memcpy(file.data()+0x550,rel,sizeof(rel));}
    else {w(0x550,16);w(0x554,4);}
    return file;
}
static int original_poff_probe(int argc,char** argv){
    Console c;c.initialize_sif();c.cdvd->hardware=ps2native::find_console_hardware(argv[2]);
    check(c.cdvd->hardware,"Probe requires a known console profile");c.cdvd->mount(ps2::os::path(argv[3]));c.boot_cdvd(ps2::os::path(argv[1]),true);c.run_iop();
    for(const auto sid:{0x80000001u,0x80000003u,0x80000006u,0x80000592u})check(std::any_of(c.rpc_servers.begin(),c.rpc_servers.end(),[&](const auto& item){return item.second.sid==sid && item.second.function!=0 && c.rpc_queues.contains(item.second.queue);}),"ROM service requires its original handler and live IOP queue");
    if(argc==5){
        const auto image=ps2native::load_iop_reboot_image(c.cdvd->media->read(argv[4]));
        const std::string args=std::string("rom0:UDNL cdrom0:")+argv[4];check(args.size()<=80,"Probe reboot argument bound");
        std::vector<std::uint8_t> packet(104);ps2::store(packet.data(),104u);ps2::store(packet.data()+8,0x80000003u);ps2::store(packet.data()+16,static_cast<std::uint32_t>(args.size()));std::copy(args.begin(),args.end(),packet.begin()+24);
        ps2::st32(0xe000,0x12345678);const auto before=c.ticks;c.sif_command(packet);
        check(c.iop_boot_active && !(c.sif.smflag&0x40000),"IOPRP cannot signal completion before original entries run");
        c.run_iop();
        check(!c.iop_boot_active && (c.sif.smflag&0x40000) && c.ticks>before && ps2::ld32(0xe000)==0x12345678,"Original IOPRP boot completion preserves EE RAM and logical time");
        for(const auto sid:{0x80000001u,0x80000006u,0x80000592u})check(std::any_of(c.rpc_servers.begin(),c.rpc_servers.end(),[&](const auto& item){return item.second.sid==sid;}),"IOPRP services must come from original RPC registrations");
        check(c.iop_modules.size()==4 && std::all_of(c.iop_modules.begin(),c.iop_modules.end(),[](const auto& item){return item.second.returned && item.second.resident;}),"All four original IOPRP service entries finish resident");
        c.iop_current=200;c.iop_threads[200].status=1;c.iop_active=true;c.iop.pc=0x7500;c.iop.gpr[31]=0xffffffd0;c.iop.gpr[4]=0x7500;
        c.iop_import("secrman",4,0x101);check(c.iop_threads.at(200).status==4,"Missing original dependency blocks its importing thread");c.run_iop();
        check(c.iop_module_waiters.empty() && c.iop_libraries.contains("secrman") && c.iop_modules.at(c.iop_libraries.at("secrman").owner).resident && c.iop_threads.at(200).status==16,"SECRMAN original entry registers exports before the pending native call resumes");
        std::printf("engine-cdvd: IOPRP %s original FILEIO/CDVDMAN/CDVDFSV/LOADFILE boot and SECRMAN dependency PASS\n",image.sha256.c_str());
    }
    // Synthetic native caller, real supplied CDVDMAN instructions/IRQ/DMA and real read-only disc bytes.
    const auto native_cdvd=[&](unsigned ordinal,std::array<std::uint32_t,4> args){
        const auto& library=c.iop_libraries.at("cdvdman");const auto& owner=c.iop_modules.at(library.owner);
        c.iop={};c.iop_current=200;c.iop_active=true;c.iop_threads[200]={};auto& t=c.iop_threads.at(200);t.status=1;t.priority=8;
        c.iop.pc=library.exports.at(ordinal);c.iop.gpr[28]=owner.gp;c.iop.gpr[29]=0x1edff0;c.iop.gpr[31]=0xffffffd0;
        for(unsigned n=0;n<4;++n)c.iop.gpr[4+n]=args[n];
        c.iop_next_tick=c.ticks+8;c.execution_limit=c.ticks+20000000;c.run_iop();
        return c.iop_threads.at(200).context.gpr[2];
    };
    check(native_cdvd(4,{0,0,0,0})==1,"Original sceCdInit must return its actual success");
    check(native_cdvd(75,{1,0,0,0})==1,"Original sceCdMmode selects the actual CD medium before reading");
    check(native_cdvd(13,{0,0,0,0})==2,"Original sceCdDiskReady observes the supplied disc");
    c.iop_write(0x7d00,0x8500,4);
    const auto read_result=native_cdvd(6,{16,1,0x100000,0x7d00});
    if(read_result!=1)std::fprintf(stderr,"[native-read] result=%08X ncmd=%02X irq=%02X error=%02X howto=%02X ticks=%llu\n",read_result,c.cdvd->ncommand,c.cdvd->irq,c.cdvd->error,c.cdvd->howto,static_cast<unsigned long long>(c.ticks));
    check(read_result==1,"Original sceCdRead must accept the real media request");
    std::array<std::uint8_t,2048> expected{};c.cdvd->media->read_sector(16,expected);
    check(c.cdvd->error==0 && c.cdvd->remaining==0 && std::equal(expected.begin(),expected.end(),c.iop_ram.begin()+0x100000),"Original CDVDMAN reads exactly the supplied media through the shared DMA");
    auto server=std::find_if(c.rpc_servers.begin(),c.rpc_servers.end(),[](const auto& item){return item.second.sid==0x80000596;});
    check(server!=c.rpc_servers.end(),"Original CDVDFSV must register POFF itself");
    const auto endpoint=server->first,thread=c.rpc_queues.at(server->second.queue).thread;
    for(unsigned iteration=0;iteration<2;++iteration){
        const auto before=server->second.completions;const auto replies=c.sif.replies.size();
        const std::uint32_t request[]={56,0,0x8000000a,0,6+iteration,0xb000,1+iteration,0xb200,1,0,0xc000,4,1,endpoint};
        c.sif_command({reinterpret_cast<const std::uint8_t*>(request),sizeof(request)});c.run_iop();
        const auto& waiting=c.iop_threads.at(thread);
        check((waiting.status&4) && waiting.wait==4 && waiting.wait_bits==4 && c.iop_events.contains(waiting.wait_id),"Original POFF handler waits on CDVDMAN's event bit 4");
        check(c.rpc_calls.contains(thread) && server->second.completions==before && c.sif.replies.size()==replies,"POFF has no reply before the CDVDMAN event");
        // Controlled test input at the kernel event boundary, not a normal-path power-off or a fabricated service reply.
        c.iop.gpr[4]=waiting.wait_id;c.iop.gpr[5]=4;c.iop_import("thevent",7);c.run_iop();
        check(!c.rpc_calls.contains(thread) && server->second.completions==before+1 && c.sif.replies.size()==replies+1 && ps2::ld32(0xc000)==1,"Original POFF sends exactly one completion after its event");
        c.run_iop();check(c.sif.replies.size()==replies+1,"Idle POFF cannot emit repeated replies");
    }
    std::puts("engine-cdvd: original CDVDMAN CAV read and CDVDFSV POFF wait/event/completion PASS; synthetic client/event injection, not a physical power-off");return 0;
}
int main(int argc,char** argv){try{
    if(argc==4 || argc==5)return original_poff_probe(argc,argv);
    if(argc!=1)throw std::runtime_error("usage: ps2n-engine-cdvd-tests [ROM0 PROFILE RAW_DISC [IOPRP_PATH]]");
    Console c;
    check(c.cdvd->read(0x1f402018,1)==0 && c.cdvd->read(0x1f402017,1)==0x40,"Empty SDATAOUT reads do not fabricate a pending result");
    c.cdvd->scmd(0x1b);check(c.cdvd->read(0x1f402018,1)==0 && c.cdvd->sresults.empty(),"Idle power-off cancellation produces exactly its device result");
    c.cdvd->sresults={0x34,0x56};check(c.cdvd->read(0x1f402018,1)==0x34 && c.cdvd->read(0x1f402017,1)==0 && c.cdvd->read(0x1f402018,1)==0x56 && c.cdvd->read(0x1f402017,1)==0x40 && c.cdvd->read(0x1f402018,1)==0,"FIFO drains exact bytes and subsequent reads remain empty");
    c.initialize_sif();const auto p=c.load_iop(module(true),"original-provider-fixture"),q=c.load_iop(module(false),"original-consumer-fixture");
    check(p!=q && c.iop_modules.size()==2,"IRXs reserve separate RAM while entries coexist");
    const auto first=c.iop_modules.at(p).thread,second=c.iop_modules.at(q).thread;
    check(c.iop_threads.at(first).stack!=c.iop_threads.at(second).stack,"Module entry contexts cannot share a stack");
    c.run_iop();check(c.iop_read(0x800,4)==7,"Actual IOP import calls the original resident export");
    check(c.iop_modules.at(p).resident && c.iop_modules.at(q).resident,"Both original module entries remain resident");
    check(c.iop_libraries.at("native").owner==p,"Export ownership follows its original module");
    const auto exports=c.iop_libraries.at("native").address;
    c.iop.gpr[4]=exports;c.iop_import("loadcore",11);check(c.iop.gpr[2]==exports+20,"QueryLibraryEntryTable returns the actual export pointer array");
    c.iop.gpr[4]=exports;c.iop.gpr[5]=2;c.iop_import("loadcore",27);check(c.iop.gpr[2]==0 && c.iop_read(exports+10,2)==2,"Reboot library mode updates the actual shared export header");
    c.iop.gpr[5]=4;c.iop_import("loadcore",27);check(c.iop_read(exports+10,2)==4,"Reboot library mode replaces only its own bits");
    c.iop.gpr[4]=0;c.iop_import("loadcore",27);check(static_cast<std::int32_t>(c.iop.gpr[2])==-214,"Null library receives the original illegal-library error");
    c.iop_write(0x6004,0x10000000,4);c.iop_write(0x6010,0x6100,4);c.iop_write(0x6100+23*4,0x7000,4);c.iop_drivers["probe"]=0x6000;
    const std::string control="probe12:control";for(unsigned n=0;n<=control.size();++n)c.iop_write(0x6a00+n,n==control.size()?0:control[n],1);
    const std::uint32_t devctl_code[]={0x8fa80014,0,0xad060000,0x8fa90010,0,0xad090004,0x8c890004,0,0xad090008,0xad1c000c,0x03e00008,0x2402fff9};
    for(unsigned n=0;n<std::size(devctl_code);++n)c.iop_write(0x7000+n*4,devctl_code[n],4);
    c.iop_write(0x7500,0xac026c10,4);c.iop_write(0x7504,0x03e00008,4);c.iop_write(0x7508,0,4);
    c.iop_current=200;c.iop_threads[200].status=1;c.iop_active=true;c.iop={};c.iop.pc=0x7500;c.iop.gpr[31]=0xffffffd0;c.iop.gpr[28]=0xabcdef;c.iop.gpr[29]=0x1cf000;
    c.iop.gpr[4]=0x6a00;c.iop.gpr[5]=0x12345678;c.iop.gpr[6]=0x6900;c.iop.gpr[7]=8;c.iop_write(0x1cf010,0x6c00,4);c.iop_write(0x1cf014,16,4);
    const auto reservations=c.iop_allocations.size();c.iop_import("ioman",31);c.iop_next_tick=c.ticks+8;c.run_iop();
    check(c.iop_read(0x6c00,4)==0x12345678 && c.iop_read(0x6c04,4)==8 && c.iop_read(0x6c08,4)==12 && c.iop_read(0x6c0c,4)==0xabcdef && c.iop_read(0x6c10,4)==0xfffffff9,"Native devctl receives the command, stack arguments, unit and GP, and returns its actual error");
    check(c.iop_allocations.size()==reservations && c.iop_threads.at(200).context.gpr[29]==0x1cf000,"Native devctl restores its caller stack and releases its temporary file context");
    c.iop_drivers.erase("probe");c.iop_threads.erase(200);c.iop_current=0;c.iop_active=false;
    auto transient=module(false);ps2::store(transient.data()+0x12c,0x24020001u);
    const auto retired=c.load_iop(transient,"original-nonresident-fixture");c.run_iop();const auto released=c.iop_modules.at(retired).base;
    check(!c.iop_modules.at(retired).resident && !c.iop_allocations.contains(released),"Nonresident IRX releases its RAM and entry context");
    const auto reused=c.load_iop(transient,"original-nonresident-fixture-reloaded");c.run_iop();
    check(reused!=retired && c.iop_modules.at(reused).base==released,"Reused IRX RAM has a fresh lifecycle identity");
    auto replacement=module(true);std::memset(replacement.data()+0x30c,0,8);std::memcpy(replacement.data()+0x30c,"other",5);
    const auto fresh=c.load_iop(replacement,"resident-in-reused-arena");c.run_iop();
    check(c.iop_modules.at(fresh).base==released && c.iop_libraries.at("other").owner==fresh,"Exports in reused RAM belong to the live module generation");
    // Two original R3000A handlers share a semaphore: one blocks, the other wakes it.
    c.iop_write(0x8800,0,4);c.iop_write(0x8804,0,4);c.iop_write(0x8808,0,4);c.iop_write(0x880c,1,4);
    c.iop.gpr[4]=0x8800;c.iop_import("thsemap",4);const auto sema=c.iop.gpr[2];
    for(unsigned k=0;k<2;++k){
        const auto thread=c.iop_next_thread++,queue=0x9000+k*0x100,server=0x9200+k*0x100,buffer=0x9400+k*0x100,function=0x5000+k*0x100;
        Console::IopThread t;t.status=2;t.priority=10+k*10;t.context.pc=0x4000+k*0x20;t.context.gpr[29]=0x1c0000+k*0x1000;t.order=++c.iop_order;
        c.iop_threads.emplace(thread,t);
        const std::uint32_t loop[]={0x34040000|queue,0x0c001080,0};for(unsigned n=0;n<3;++n)c.iop_write(t.context.pc+n*4,loop[n],4);
        const std::uint32_t body[]={0x27bdfff0,0xafbf000c,0xafa50008,0x24040000|sema,k?0x0c001090u:0x0c001088u,0,0x8fa20008,0x8fbf000c,0x8c430000,0,0x24630001,0xac430000,0x27bd0010,0x03e00008,0};
        for(unsigned n=0;n<std::size(body);++n)c.iop_write(function+n*4,body[n],4);
        c.iop.gpr[4]=queue;c.iop.gpr[5]=thread;c.iop_import("sifcmd",19);
        c.iop.gpr[4]=server;c.iop.gpr[5]=0xabc00000+k;c.iop.gpr[6]=function;c.iop.gpr[7]=buffer;c.iop.gpr[29]=0x170000;
        c.iop_write(0x170010,0,4);c.iop_write(0x170014,0,4);c.iop_write(0x170018,queue,4);c.iop_import("sifcmd",17);
    }
    for(const auto [at,lib,ordinal]:{std::tuple{0x4200u,"sifcmd",22u},std::tuple{0x4220u,"thsemap",8u},std::tuple{0x4240u,"thsemap",6u}}){c.iop_write(at,0x03e00008,4);c.iop_write(at+4,0x24000000|ordinal,4);c.iop_imports[at]={lib,ordinal};}
    c.iop_current=0;c.iop_active=false;c.iop_reschedule=true;c.iop_next_tick=c.ticks+8;try{c.run_iop();}catch(...){std::fprintf(stderr,"RPC setup pc=%08X thread=%u tick=%llu\n",c.iop.pc,c.iop_current,static_cast<unsigned long long>(c.ticks));throw;}
    check(c.rpc_queues.size()==2 && !c.iop_work_pending(),"Registered server threads sleep on their real queues");
    for(unsigned k=0;k<2;++k){
        c.iop_write(0x9400+k*0x100,10+10*k,4);
        const std::uint32_t request[]={56,0,0x8000000a,0,6+k,0xb000+k*0x100,1+k,0xb200+k*0x100,0,4,0xc000+k*0x100,4,1,0x9200+k*0x100};
        c.sif_command({reinterpret_cast<const std::uint8_t*>(request),sizeof(request)});
    }
    std::size_t peak=0;bool waited=false;
    while(c.iop_work_pending()){
        try{c.advance_to(c.iop_next_tick);}catch(...){std::fprintf(stderr,"RPC execution pc=%08X thread=%u queue=%08X tick=%llu\n",c.iop.pc,c.iop_current,c.iop.gpr[4],static_cast<unsigned long long>(c.ticks));throw;}peak=std::max(peak,c.rpc_calls.size());
        for(const auto& [id,t]:c.iop_threads)waited|=(t.status&4) && t.wait==3;
    }
    check(waited && peak==2,"RPC callbacks run concurrently on the original waiting/scheduled threads");
    check(ps2::ld32(0xc000)==11 && ps2::ld32(0xc100)==21 && c.sif.replies.size()==2,"Each RPC completes from actual handler results through SIF0");
    check(c.rpc_calls.empty(),"Completed RPC contexts are retired");
    check(c.cdvd->read(0x1f40200f,1)==0,"No media means no disc type");
    c.cdvd->write(0x1f402004,2,1);check(c.cdvd->irq==0,"CDVD completion cannot occur inline");
    c.advance_to(c.cdvd->due);check(c.cdvd->error==0x12 && c.cdvd->irq,"No-media command completes with a real error/IRQ");
    c.cdvd->write(0x1f402008,0xff,1);check(!c.cdvd->irq,"CDVD IRQ acknowledgement clears its source");
    c.iop_active=true;c.iop.pc=0x7400;c.iop_next_tick=c.ticks+16;const auto instructions=c.iop_instructions;
    c.cdvd->write(0x1f402004,0,1);c.advance_to(c.ticks+8);
    check(c.cdvd->irq==2 && c.iop_instructions==instructions,"Earlier device deadlines are not overwritten by a later IOP instruction");
    c.iop_active=false;c.cdvd->write(0x1f402008,0xff,1);
    std::vector<std::uint8_t> malformed(57);ps2::store(malformed.data(),57u);ps2::store(malformed.data()+8,0x8000000au);
    bool rejected=false;try{c.sif_command(malformed);}catch(const std::runtime_error&){rejected=true;}
    check(rejected && c.rpc_calls.empty(),"Malformed RPC alignment cannot publish a request");
    std::uint32_t bad_reset[]={24,0,0x80000003,0,81,0};
    const auto old_modules=c.iop_modules.size(),old_servers=c.rpc_servers.size();
    rejected=false;try{c.sif_command({reinterpret_cast<const std::uint8_t*>(bad_reset),sizeof(bad_reset)});}catch(const std::runtime_error&){rejected=true;}
    check(rejected && c.iop_modules.size()==old_modules && c.rpc_servers.size()==old_servers,"Malformed reset cannot retire the live IOP session");
    c.iop.gpr[4]=1;c.iop.gpr[5]=32;c.iop.gpr[6]=0;c.iop.gpr[7]=0x6309;c.iop_import("timrman",5);check(static_cast<std::int32_t>(c.iop.gpr[2])==-150,"ReferHardTimer cannot invent an allocated clock");
    c.iop.gpr[6]=1;c.iop_import("timrman",4);const auto timer=c.iop.gpr[2];check(timer==0x6bf8014a,"Hard timer allocation uses actual capabilities and encoded hardware address");
    c.iop.gpr[4]=timer;c.iop.gpr[5]=0x80;c.iop_import("timrman",7);const auto timer_start=c.ticks;c.advance_to(timer_start+80);c.iop_import("timrman",10);check(c.iop.gpr[2]==10 && c.iop_read(0xbf8014a0,4)==10,"HLE and native timer reads use the shared EE/IOP clock");
    c.iop.gpr[4]=1;c.iop.gpr[5]=32;c.iop.gpr[6]=0;c.iop.gpr[7]=0x6309;c.iop_import("timrman",5);check(c.iop.gpr[2]==timer,"ReferHardTimer matches mode, source and width");
    c.iop.gpr[4]=timer;c.iop_import("timrman",6);c.iop_import("timrman",6);c.iop_import("timrman",10);check(static_cast<std::int32_t>(c.iop.gpr[2])==-151,"Timer references must be released before its id becomes invalid");
    c.iop.gpr[4]=11;c.iop.gpr[5]=3;c.iop_import("dmacman",33);c.iop_import("dmacman",34);check(c.iop_read(0xbf801570,4)==0xb0000,"DMA priority and channel enable share real controller bits");c.iop_import("dmacman",35);check(c.iop_read(0xbf801570,4)==0x30000,"Disabling DMA preserves its priority");
    c.iop.gpr[4]=0x1000;c.iop.gpr[5]=0x2000;c.iop.gpr[6]=0x3000;c.iop_import("modload",12);c.iop.gpr[4]=0x4000;c.iop_import("modload",13);c.iop.gpr[4]=0x8600;c.iop.gpr[5]=0x8604;c.iop_import("modload",14);check(c.iop_read(0x8600,4)==0x4000 && c.iop_read(0x8604,4)==0x3000,"Original security callback registration remains queryable in the Console");
    const std::uint32_t vblank_code[]={0x8c086700,0,0x00084880,0x25296710,0xad240000,0x25080001,0xac086700,0x03e00008,0x24020001};
    for(unsigned k=0;k<std::size(vblank_code);++k){c.iop_write(0x7800+k*4,vblank_code[k],4);c.iop_write(0x7840+k*4,vblank_code[k],4);}c.iop_write(0x7860,0x24020000,4);
    for(const auto [pc,prio,arg]:{std::tuple{0x7800u,20u,2u},std::tuple{0x7840u,10u,1u}}){c.iop.gpr[4]=0;c.iop.gpr[5]=prio;c.iop.gpr[6]=pc;c.iop.gpr[7]=arg;c.iop_import("vblank",8);check(c.iop.gpr[2]==0,"VBlank registers real IOP functions");}
    c.iop_intr_enabled=false;c.iop_vblank_edge(0);c.run_iop();check(c.iop_read(0x6700,4)==0 && c.iop_vblank_pending,"Masked VBlank remains pending without invoking callbacks");
    c.iop_intr_enabled=true;c.run_iop();check(c.iop_read(0x6700,4)==2 && c.iop_read(0x6710,4)==1 && c.iop_read(0x6714,4)==2,"VBlank callbacks execute native R3000A in priority order with original arguments");
    check(c.iop_vblank_handlers.size()==1 && c.iop_vblank_handlers.front().function==0x7800,"A zero callback result unregisters only that VBlank handler");
    c.iop.gpr[4]=0;c.iop.gpr[5]=0x7800;c.iop_import("vblank",9);check(c.iop_vblank_handlers.empty(),"Explicit VBlank release removes the real registration");
    const auto observed=c.sif.replies.size();while(c.sif.replies.size()<31)c.sif.replies.emplace_back();
    for(unsigned k=0;k<2;++k){c.iop_write(0x8000+k*16,0x9000,4);c.iop_write(0x8004+k*16,0xd000,4);c.iop_write(0x8008+k*16,16,4);c.iop_write(0x800c+k*16,4,4);}
    c.iop.gpr[4]=0x8000;c.iop.gpr[5]=2;rejected=false;try{c.iop_import("sifman",7);}catch(const std::runtime_error&){rejected=true;}
    check(rejected && ps2::ld32(0xd000)==0 && c.sif.replies.size()==31,"IOP SIF DMA validates completion capacity before copying data");
    while(c.sif.replies.size()>observed)c.sif.replies.pop_back();
    const auto scratch=std::filesystem::temp_directory_path()/("ps2n-cdvd-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(scratch);
    struct Cleanup{Console& c;std::filesystem::path path;~Cleanup(){c.cdvd->media.reset();std::error_code ec;for(const auto name:{"disc.bin","reset.elf","dvd.iso"})std::filesystem::remove(path/name,ec);std::filesystem::remove(path,ec);}} cleanup{c,scratch};
    auto dvd=iso_test::build_iso({});ps2native::write_file_atomically(scratch/"dvd.iso",dvd);c.cdvd->mount(scratch/"dvd.iso",IopCdvd::Medium::Dvd);
    auto toc=c.cdvd->dvd_toc();check(toc[0]==4 && toc[14]==1 && !c.cdvd->media->dvd_layer1_start(),"Single-volume DVD TOC derives its end from the mounted image");
    c.cdvd->media.reset();dvd.resize(48*2048);std::copy_n(dvd.begin()+16*2048,2048,dvd.begin()+24*2048);
    ps2native::write_file_atomically(scratch/"dvd.iso",dvd);c.cdvd->mount(scratch/"dvd.iso",IopCdvd::Medium::Dvd);toc=c.cdvd->dvd_toc();
    check(c.cdvd->media->user_sector_count()==48 && c.cdvd->media->dvd_layer1_start()==24 && toc[0]==0x24 && toc[14]==0x21 && toc[21]==3 && toc[23]==23,"Second PVD establishes logical DVD9 geometry without truncating physical sectors");
    std::array<std::uint8_t,2048> layer_sector{};c.cdvd->media->read_sector(24,layer_sector);check(layer_sector[0]==1 && layer_sector[1]=='C',"Second-volume sectors come from the original image");
    c.cdvd->dma.at(0x1f8010b0)=0x100000;c.cdvd->dma.at(0x1f8010b4)=0x810004;c.cdvd->dma.at(0x1f8010b8)=0x1000000;c.cdvd->dma.at(0x1f8010f0)=0x8000;
    c.cdvd->write(0x1f402005,0,1);const auto toc_start=c.ticks;c.cdvd->write(0x1f402004,9,1);
    check(c.cdvd->due==toc_start+8 && !c.cdvd->spinning && !c.cdvd->irq,"TOC schedules real DMA instead of completing in the issuing call");
    c.advance_to(c.cdvd->due);check(std::equal(toc.begin(),toc.end(),c.iop_ram.begin()+0x100000) && !(c.cdvd->dma.at(0x1f8010b8)&0x1000000) && c.cdvd->irq==2 && c.cdvd->read(0x1f402013,1)==0x12,"TOC transfers exact DVD structure bytes and updates DMA, IRQ and spindle state");
    c.cdvd->irq=0;c.cdvd->dma.at(0x1f8010b0)=0x110000;c.cdvd->dma.at(0x1f8010b4)=0x810004;c.cdvd->dma.at(0x1f8010b8)=0x1000200;
    c.cdvd->write(0x1f402006,0x8c,1);for(const auto byte:std::array<std::uint8_t,11>{24,0,0,0,1,0,0,0,20,0x83,0})c.cdvd->write(0x1f402005,byte,1);
    c.cdvd->write(0x1f402004,8,1);const auto dvd_due=c.cdvd->due;c.advance_to(dvd_due-1);
    check(c.iop_read(0x110000,4)==0 && !c.cdvd->irq,"DVD payload and framing cannot arrive before the seek/sector deadline");
    c.advance_to(dvd_due);check(c.iop_read(0x110000,4)==0x00000321 && std::equal(layer_sector.begin(),layer_sector.end(),c.iop_ram.begin()+0x11000c) && c.cdvd->dma.at(0x1f8010b0)==0x110810 && c.cdvd->irq==2,"DVD9 second-volume DMA uses the computed layer address and exact mounted payload");
    c.cdvd->write(0x1f402006,0x80,1);
    c.cdvd->irq=0;c.cdvd->dma.at(0x1f8010f0)=0;c.cdvd->dma.at(0x1f8010f4)=0;
    c.cdvd->media.reset();dvd[24*2048]=0;ps2native::write_file_atomically(scratch/"dvd.iso",dvd);c.cdvd->mount(scratch/"dvd.iso",IopCdvd::Medium::Dvd);
    bool bad_layout=false;try{(void)c.cdvd->dvd_toc();}catch(const std::runtime_error&){bad_layout=true;}check(bad_layout,"Unestablished second-volume geometry cannot receive a made-up TOC");
    auto dynamic=module(false);ps2::store(dynamic.data()+0x100,0xac040830u);ps2::store(dynamic.data()+0x104,0xac050834u);ps2::store(dynamic.data()+0x108,0x03e00008u);ps2::store(dynamic.data()+0x10c,0x00001021u);
    iso_test::IsoOptions options;options.system_cnf.assign(reinterpret_cast<const char*>(dynamic.data()),dynamic.size());options.raw_mode=2;const auto iso=iso_test::build_iso(options);ps2native::write_file_atomically(scratch/"disc.bin",iso);c.cdvd->mount(scratch/"disc.bin");
    c.iop_current=200;c.iop_threads[200].status=1;c.iop_active=true;c.iop.pc=0x7500;c.iop.gpr[31]=0xffffffd0;c.iop_write(0x7500,0x03e00008,4);c.iop_write(0x7504,0,4);
    const std::string module_path="cdrom0:/SYSTEM.CNF;1";
    for(unsigned k=0;k<=module_path.size();++k)c.iop_write(0x8400+k,k==module_path.size()?0:module_path[k],1);
    c.iop_write(0x8480,0x00620061,4);c.iop_write(0x8500,0x12345678,4);
    c.iop.gpr[4]=0x8400;c.iop.gpr[5]=4;c.iop.gpr[6]=0x8480;c.iop.gpr[7]=0x8500;c.iop_import("modload",7);
    check(c.iop_threads.at(200).status==4 && c.iop_read(0x8500,4)==0x12345678,"LoadStartModule blocks until its original entry returns");
    const auto& entry=c.iop_threads.at(c.iop_modules.rbegin()->second.thread);const auto module_argv=entry.context.gpr[5],entry_stack=entry.stack;
    check(c.iop_string(c.iop_read(module_argv,4))==module_path && c.iop_string(c.iop_read(module_argv+4,4))=="a" && c.iop_string(c.iop_read(module_argv+8,4))=="b","Module argv uses live IOP RAM and preserves argument boundaries before entry returns");
    c.run_iop();check(c.iop_module_waiters.empty() && c.iop_read(0x8500,4)==0 && c.iop_read(0x830,4)==3,"LoadStartModule executes the original entry with pathname and NUL-separated arguments");
    check(c.iop_read(0x834,4)==module_argv && !c.iop_allocations.contains(entry_stack),"Original entry consumes its actual argv and its retired stack reservation is released");
    const auto staged=module(false);const auto staged_at=c.iop_allocate(static_cast<std::uint32_t>(staged.size()));std::copy(staged.begin(),staged.end(),c.iop_ram.begin()+staged_at);
    c.iop_write(0x800,0,4);c.iop_current=200;c.iop_threads[200].status=1;c.iop_active=true;c.iop.pc=0x7500;c.iop.gpr[31]=0xffffffd0;c.iop.gpr[29]=0x1cf000;c.iop.gpr[4]=staged_at;
    c.iop_import("modload",10);const auto held=c.iop.gpr[2];
    check(c.iop_modules.contains(held) && !c.iop_modules.at(held).returned && c.iop_threads.at(c.iop_modules.at(held).thread).status==16 && c.iop_read(0x800,4)==0,"LoadModuleBuffer relocates and reserves an IRX without starting its entry");
    c.iop.gpr[4]=held;c.iop.gpr[5]=0x8400;c.iop.gpr[6]=0;c.iop.gpr[7]=0;c.iop_write(0x1cf010,0x8500,4);c.iop_import("modload",8);c.run_iop();
    check(c.iop_modules.at(held).returned && c.iop_modules.at(held).resident && c.iop_read(0x800,4)==7 && c.iop_read(0x8500,4)==0,"StartModule executes the reserved original entry and completes its waiting caller");
    check(std::equal(staged.begin(),staged.end(),c.iop_ram.begin()+staged_at),"Buffer loading preserves its original source bytes");c.iop_release(staged_at);
    for(const auto text:{"cdrom0:/module.irx","rom0:CDVDMAN","mc0:/module.irx"," HD0:/x","net:/x","dev0:/x"}){
        const std::string path=text;for(unsigned k=0;k<=path.size();++k)c.iop_write(0x8400+k,k==path.size()?0:path[k],1);
        c.iop.gpr[4]=0x8400;c.iop_import("modload",15);check(c.iop.gpr[2]==(path.starts_with("cdrom") || path.starts_with("rom0") ? 0u:1u),"Boot-device checks match the supplied ROM0 MODLOAD predicate");
    }
    c.iop_write(0xbf808200,0x1234,4);c.iop_write(0xbf808268,12,4);check(c.iop_read(0xbf808200,4)==0x1234 && c.iop_read(0xbf808268,4)==12 && !c.iop_read(0xbf808280,4),"SIO2 initialization retains real parameter registers without claiming a transfer");
    check(ps2::slow_ld8(0xbf40200f)==0x12,"EE and IOP see the same mounted CDVD media");
    c.cdvd->write(0x1f8010b0,0x1000,4);c.cdvd->write(0x1f8010b4,0x40080,4);c.cdvd->write(0x1f8010b8,0x1000200,4);c.cdvd->write(0x1f8010f0,0x8000,4);
    const std::uint8_t params[]={16,0,0,0,1,0,0,0,0,0,0};for(const auto byte:params)c.cdvd->write(0x1f402005,byte,1);
    c.cdvd->write(0x1f402004,6,1);const auto due=c.cdvd->due;
    check(c.iop_read(0x1000,4)==0 && due>c.ticks+8,"CDVD read waits for its scheduled seek/sector transfer");
    c.advance_to(due);check(c.iop_read(0x1000,4)==0x30444301 && c.iop_read(0x1004,2)==0x3130,"Mounted media sector bytes reach actual IOP RAM");
    check(c.cdvd->dma.at(0x1f8010b0)==0x1800 && !(c.cdvd->dma.at(0x1f8010b8)&0x1000000) && c.cdvd->irq==2,"CDVD DMA advances and completes before its IRQ");
    check(c.iop_read(0x1f8010f4,4)&(1u<<27),"DMA3 completion sets its shared controller flag");
    c.cdvd->write(0x1f402008,0xff,1);
    c.cdvd->write(0x1f8010b0,0x3000,4);c.cdvd->write(0x1f8010b4,0x80080,4);c.cdvd->write(0x1f8010b8,0x1000200,4);
    const std::uint8_t cav_params[]={16,0,0,0,2,0,0,0,5,0x85,0};for(const auto byte:cav_params)c.cdvd->write(0x1f402005,byte,1);
    c.cdvd->write(0x1f402004,6,1);const auto cav_due=c.cdvd->due,cav_duration=c.cdvd->sector_ticks;
    check(cav_duration>(491520ull/24)*8,"CAV inner-track rate cannot silently use nominal CLV timing");
    c.advance_to(cav_due-1);check(c.iop_read(0x3000,4)==0 && !c.cdvd->irq,"CAV sector bytes cannot appear before the shared device deadline");
    c.advance_to(cav_due);check(c.iop_read(0x3000,4)==0x30444301 && c.cdvd->remaining==1 && !c.cdvd->irq && c.cdvd->sector_ticks<=cav_duration,"CAV reads real bytes and recomputes radial timing for the next sector");
    c.advance_to(c.cdvd->due);std::array<std::uint8_t,2048> next_sector{};c.cdvd->media->read_sector(17,next_sector);
    check(c.cdvd->remaining==0 && c.cdvd->irq==2 && std::equal(next_sector.begin(),next_sector.end(),c.iop_ram.begin()+0x3800),"Multi-sector CAV transfer completes with exact original media data");
    const std::uint32_t irq_code[]={0x3c081f40,0x35082008,0x240900ff,0xa1090000,0x34089600,0x8d090000,0,0x25290001,0xad090000,0x03e00008,0x24020001};
    for(unsigned n=0;n<std::size(irq_code);++n)c.iop_write(0x7000+n*4,irq_code[n],4);
    c.iop_irqs[2]={0x7000,0,0,true};c.run_iop();
    check(c.iop_read(0x9600,4)==1 && !c.cdvd->irq,"CDVD IRQ executes the registered original R3000A handler on the shared clock");
    c.iop_write(0x9800,1000000,4);c.iop_write(0x9804,0,4);c.iop.gpr[4]=0x9800;c.iop.gpr[5]=0x7000;c.iop.gpr[6]=0;c.iop_import("thbase",35);
    check(c.iop.gpr[2]==0 && !c.iop_alarms.empty(),"Reset regression starts with an actual pending kernel alarm");
    c.cdvd->write(0x1f402004,0,1);c.advance_to(c.cdvd->due);
    ps2::slow_st8(0xbf402008,0xff);check(!c.cdvd->irq,"EE acknowledges the same device IRQ");
    // Reset a console with a queued RPC and unfinished physical operation.
    const std::uint32_t pending[]={56,0,0x8000000a,0,9,0xb000,9,0xb200,0,4,0xc000,4,1,0x9200};
    c.sif_command({reinterpret_cast<const std::uint8_t*>(pending),sizeof(pending)});c.cdvd->write(0x1f402004,2,1);
    const auto before_reset=c.ticks;ps2::st32(0xe000,0x12345678);c.ee.pc=0x1234;
    c.sif.main_address=0x5000;c.sif.ee_receive=0x6000;c.sif.msflag=0x40;
    c.sif.iop_handlers[0x123]={0x7000,0,0};c.sif.iop_commands.emplace_back(c.sif.iop_handlers.at(0x123),std::vector<std::uint8_t>(16));
    c.reset_iop();
    check(c.ticks==before_reset && c.ee.pc==0x1234 && ps2::ld32(0xe000)==0x12345678,"IOP reset preserves EE execution, memory and the single clock");
    check(c.iop_alarms.empty() && c.iop_modules.empty() && c.iop_threads.empty() && c.rpc_servers.empty() && c.rpc_calls.empty() && c.sif.replies.empty() && c.sif.iop_handlers.empty() && c.sif.iop_commands.empty(),"IOP reset cannot retain stale modules, threads, RPC or callbacks");
    check(c.sif.main_address==0x5000 && c.sif.ee_receive==0x6000 && c.sif.msflag==0x40 && !c.sif.smflag && !c.sif.command_buffer,"IOP reset preserves EE SIF registers without inventing boot readiness");
    std::vector<std::uint8_t> elf(88);const auto w=[&](unsigned at,std::uint32_t value){ps2::store(elf.data()+at,value);};const auto h=[&](unsigned at,std::uint16_t value){ps2::store(elf.data()+at,value);};
    w(0,0x464c457f);elf[4]=elf[5]=elf[6]=1;h(16,2);h(18,8);w(20,1);w(24,0x100000);w(28,52);h(40,52);h(42,32);h(44,1);w(52,1);w(56,84);w(60,0x100000);w(68,4);w(72,4);w(76,5);w(80,4);
    ps2native::write_file_atomically(scratch/"reset.elf",elf);c.load(scratch/"reset.elf");
    check(c.rpc_servers.empty() && c.rpc_calls.empty() && c.rpc_queues.empty() && c.sif.replies.empty() && c.iop_modules.empty() && c.iop_allocations.empty(),"Reset retires modules, allocations, RPC requests and responses together");
    check(c.cdvd->due==~0ull && !c.cdvd->irq && c.cdvd->read(0x1f40200f,1)==0x12,"Reset cancels CDVD work while retaining the original mounted disc");
    check(ps2native::read_file(scratch/"disc.bin")==iso,"Reads and reset preserve the original media bytes");
    auto native_stdio=module(true),caller=module(false);std::memset(native_stdio.data()+0x30c,0,8);std::memcpy(native_stdio.data()+0x30c,"stdio",5);std::memset(caller.data()+0x1f8,0,8);std::memcpy(caller.data()+0x1f8,"stdio",5);ps2::store(caller.data()+0x204,0x24000004u);
    (void)c.load_iop(native_stdio,"native-stdio-without-printf");(void)c.load_iop(caller,"missing-native-ordinal");rejected=false;
    try{c.run_iop();}catch(const std::runtime_error& e){rejected=std::string(e.what()).find("Missing native IOP export ordinal")!=std::string::npos;}
    check(rejected,"HLE cannot fabricate a missing export of an already registered native library");
    std::puts("engine-cdvd: resident IRX, relocations, native exports, concurrent RPC, sectors, no-media, IRQ and reset PASS");
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
