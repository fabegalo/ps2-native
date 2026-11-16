#include "ps2native/engine/console.hpp"
#include "iop_cdvd.hpp"
#include "iso_image.hpp"
#include "ps2native/io.hpp"
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
static int original_poff_probe(char** argv){
    Console c;c.initialize_sif();c.cdvd->hardware=ps2native::find_console_hardware(argv[2]);
    check(c.cdvd->hardware,"Probe requires a known console profile");c.cdvd->mount(ps2::os::path(argv[3]));c.boot_cdvd(ps2::os::path(argv[1]),true);c.run_iop();
    auto server=std::find_if(c.rpc_servers.begin(),c.rpc_servers.end(),[](const auto& item){return item.second.sid==0x80000596;});
    check(server!=c.rpc_servers.end(),"Original XCDVDFSV must register POFF itself");
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
    std::puts("engine-cdvd: original XCDVDFSV POFF wait/event/completion PASS; synthetic client/event injection, not a physical power-off");return 0;
}
int main(int argc,char** argv){try{
    if(argc==4)return original_poff_probe(argv);
    if(argc!=1)throw std::runtime_error("usage: ps2n-engine-cdvd-tests [ROM0 PROFILE RAW_DISC]");
    Console c;c.initialize_sif();const auto p=c.load_iop(module(true),"original-provider-fixture"),q=c.load_iop(module(false),"original-consumer-fixture");
    check(p!=q && c.iop_modules.size()==2,"IRXs reserve separate RAM while entries coexist");
    const auto first=c.iop_modules.at(p).thread,second=c.iop_modules.at(q).thread;
    check(c.iop_threads.at(first).stack!=c.iop_threads.at(second).stack,"Module entry contexts cannot share a stack");
    c.run_iop();check(c.iop_read(0x800,4)==7,"Actual IOP import calls the original resident export");
    check(c.iop_modules.at(p).resident && c.iop_modules.at(q).resident,"Both original module entries remain resident");
    check(c.iop_libraries.at("native").owner==p,"Export ownership follows its original module");
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
    const auto observed=c.sif.replies.size();while(c.sif.replies.size()<31)c.sif.replies.emplace_back();
    for(unsigned k=0;k<2;++k){c.iop_write(0x8000+k*16,0x9000,4);c.iop_write(0x8004+k*16,0xd000,4);c.iop_write(0x8008+k*16,16,4);c.iop_write(0x800c+k*16,4,4);}
    c.iop.gpr[4]=0x8000;c.iop.gpr[5]=2;rejected=false;try{c.iop_import("sifman",7);}catch(const std::runtime_error&){rejected=true;}
    check(rejected && ps2::ld32(0xd000)==0 && c.sif.replies.size()==31,"IOP SIF DMA validates completion capacity before copying data");
    while(c.sif.replies.size()>observed)c.sif.replies.pop_back();
    const auto scratch=std::filesystem::temp_directory_path()/("ps2n-cdvd-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(scratch);
    struct Cleanup{Console& c;std::filesystem::path path;~Cleanup(){c.cdvd->media.reset();std::error_code ec;for(const auto name:{"disc.bin","reset.elf"})std::filesystem::remove(path/name,ec);std::filesystem::remove(path,ec);}} cleanup{c,scratch};
    iso_test::IsoOptions options;options.raw_mode=2;const auto iso=iso_test::build_iso(options);ps2native::write_file_atomically(scratch/"disc.bin",iso);c.cdvd->mount(scratch/"disc.bin");
    check(ps2::slow_ld8(0xbf40200f)==0x12,"EE and IOP see the same mounted CDVD media");
    c.cdvd->write(0x1f8010b0,0x1000,4);c.cdvd->write(0x1f8010b4,0x40080,4);c.cdvd->write(0x1f8010b8,0x1000200,4);c.cdvd->write(0x1f8010f0,0x8000,4);
    const std::uint8_t params[]={16,0,0,0,1,0,0,0,0,0,0};for(const auto byte:params)c.cdvd->write(0x1f402005,byte,1);
    c.cdvd->write(0x1f402004,6,1);const auto due=c.cdvd->due;
    check(c.iop_read(0x1000,4)==0 && due>c.ticks+8,"CDVD read waits for its scheduled seek/sector transfer");
    c.advance_to(due);check(c.iop_read(0x1000,4)==0x30444301 && c.iop_read(0x1004,2)==0x3130,"Mounted media sector bytes reach actual IOP RAM");
    check(c.cdvd->dma.at(0x1f8010b0)==0x1800 && !(c.cdvd->dma.at(0x1f8010b8)&0x1000000) && c.cdvd->irq==2,"CDVD DMA advances and completes before its IRQ");
    check(c.iop_read(0x1f8010f4,4)&(1u<<27),"DMA3 completion sets its shared controller flag");
    const std::uint32_t irq_code[]={0x3c081f40,0x35082008,0x240900ff,0xa1090000,0x34089600,0x8d090000,0,0x25290001,0xad090000,0x03e00008,0x24020001};
    for(unsigned n=0;n<std::size(irq_code);++n)c.iop_write(0x7000+n*4,irq_code[n],4);
    c.iop_irqs[2]={0x7000,0,0,true};c.run_iop();
    check(c.iop_read(0x9600,4)==1 && !c.cdvd->irq,"CDVD IRQ executes the registered original R3000A handler on the shared clock");
    c.cdvd->write(0x1f402004,0,1);c.advance_to(c.cdvd->due);
    ps2::slow_st8(0xbf402008,0xff);check(!c.cdvd->irq,"EE acknowledges the same device IRQ");
    // Reset a console with a queued RPC and unfinished physical operation.
    const std::uint32_t pending[]={56,0,0x8000000a,0,9,0xb000,9,0xb200,0,4,0xc000,4,1,0x9200};
    c.sif_command({reinterpret_cast<const std::uint8_t*>(pending),sizeof(pending)});c.cdvd->write(0x1f402004,2,1);
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
