#include "hle_fixture.hpp"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "boot_checks.hpp"
#include "ps2native/boot.hpp"
#include "ps2native/checkpoint.hpp"
#include "ps2native/hle.hpp"
#include "ps2native/ee_system.hpp"
#include "ps2native/sha256.hpp"
#include "ps2native/iop_ops.hpp"
#include "ps2native/iop_aot.hpp"
#include "iop_memory_native.hpp"
#include "iop_services.hpp"
#include "emulator/services/iop_module_loader.h"
#include "emulator/imports/iop_ioman.h"
#include "emulator/imports/iop_imports.h"
#include "emulator/services/iop_rpc.h"
#include "emulator/core/iop_kernel.h"
#include "ps2x/iop/ps2_path.h"
#include "iso_image.hpp"
#include "../tools/dvd_geometry_cli.hpp"
#include <iostream>

using namespace ps2native;
using boot_checks::require;
template<class F> void rejects(F&& operation,const char* text) {
    try{operation();}catch(const std::exception& e){require(std::string(e.what()).find(text)!=std::string::npos,e.what());return;}
    throw std::runtime_error(std::string("Expected refusal: ")+text);
}
template<auto Words,std::uint32_t Base> auto fixed_iop_entries() {
    return []<std::size_t... I>(std::index_sequence<I...>) {
        return std::array{IopAotEntry{Base+static_cast<std::uint32_t>(I)*4u,Words[I],[](Iop& cpu){cpu.execute_native<Words[I]>();}}...};
    }(std::make_index_sequence<Words.size()>{});
}
std::vector<std::uint8_t> module_info_irx(std::vector<std::uint8_t> payload) {
    const auto size=payload.size();
    direct_test::Program program{{{0,5,std::move(payload)}},0,{},{}};
    auto bytes=direct_test::elf(program);
    std::copy_n(bytes.begin()+52u,32u,bytes.begin()+84u);
    std::fill(bytes.begin()+52u,bytes.begin()+84u,0u);
    direct_test::put(bytes,16u,0xFF80u,2u);direct_test::put(bytes,24u,0u);direct_test::put(bytes,44u,2u,2u);
    direct_test::put(bytes,52u,0x70000080u);direct_test::put(bytes,56u,116u);
    direct_test::put(bytes,68u,24u);direct_test::put(bytes,72u,24u);direct_test::put(bytes,80u,4u);
    direct_test::put(bytes,116u,UINT32_MAX);direct_test::put(bytes,128u,size);
    return bytes;
}
std::vector<std::uint8_t> service_provider_irx(const char* name,std::uint32_t /*base*/,std::uint16_t version) {
    std::vector<std::uint8_t> payload(256u);direct_test::put(payload,0u,0xFFFFFFFFu);
    direct_test::put(payload,32u,64u);direct_test::put(payload,36u,version,2u);
    std::copy_n(name,std::strlen(name)+1u,payload.begin()+64u);
    auto irx=module_info_irx(std::move(payload));direct_test::put(irx,116u,32u);
    const auto relocation=irx.size(),sections=relocation+8u;
    const auto fileOffset=std::uint32_t{irx[88]}|(std::uint32_t{irx[89]}<<8u)|(std::uint32_t{irx[90]}<<16u)|(std::uint32_t{irx[91]}<<24u);
    irx.resize(sections+120u);
    direct_test::put(irx,relocation,32u);direct_test::put(irx,relocation+4u,2u);
    direct_test::put(irx,32u,sections);direct_test::put(irx,46u,40u,2u);direct_test::put(irx,48u,3u,2u);
    direct_test::put(irx,sections+44u,1u);direct_test::put(irx,sections+48u,6u);
    direct_test::put(irx,sections+56u,fileOffset);direct_test::put(irx,sections+60u,256u);
    direct_test::put(irx,sections+84u,9u);direct_test::put(irx,sections+96u,relocation);
    direct_test::put(irx,sections+100u,8u);direct_test::put(irx,sections+108u,1u);direct_test::put(irx,sections+116u,8u);
    return irx;
}
std::vector<std::uint8_t> reboot_fixture(std::vector<std::uint8_t> server=std::vector<std::uint8_t>(17),std::vector<std::uint8_t> driver=std::vector<std::uint8_t>(16),std::vector<std::uint8_t> dma=std::vector<std::uint8_t>(16)) {
    std::array<m20::File,6> files{{{"RESET",{},{}},{"ROMDIR",{},{}},{"EXTINFO",{},{}},
        {"DMACMAN",std::move(dma),{}},{"CDVDMAN",std::move(driver),{}},{"CDVDFSV",std::move(server),{}}}};
    m20::field(files[4].extinfo,2,0x0301,{});m20::field(files[5].extinfo,2,0x0302,{});
    for(const auto& file:files)files[2].data.insert(files[2].data.end(),file.extinfo.begin(),file.extinfo.end());
    auto& directory=files[1].data;directory.resize((files.size()+1u)*16u);
    for(unsigned i=0;i<files.size();++i) {
        std::copy(files[i].name.begin(),files[i].name.end(),directory.begin()+i*16u);
        direct_test::put(directory,i*16u+10u,files[i].extinfo.size(),2);
        direct_test::put(directory,i*16u+12u,files[i].data.size());
    }
    std::vector<std::uint8_t> result;
    for(const auto& file:files) {result.insert(result.end(),file.data.begin(),file.data.end());while(result.size()%16u)result.push_back(0);}
    return result;
}
template<class C> int run(int argc,C** argv)try {
    std::cout.setf(std::ios::unitbuf);
    require(argc==3,"ELF and scratch directory required");
    {
        cli::DiagnosticDvdOptions options;
        require(!options.geometry(false),"diagnostic DVD geometry is absent by default");
        require(options.parse("--diagnostic-dvd-layer0=48"),"parse explicit layer size");
        rejects([&]{(void)options.geometry(true);},"track path");
        require(options.parse("--diagnostic-dvd-track=ptp") && !options.geometry(true)->opposite_track_path,
            "explicit PTP is retained, never treated as a missing bool");
        rejects([&]{(void)options.geometry(false);},"--disc");
        rejects([&]{(void)options.parse("--diagnostic-dvd-layer0=48");},"duplicate");
        for(const auto flag:{"--diagnostic-dvd-layer0=0","--diagnostic-dvd-layer0=4294967296","--diagnostic-dvd-track=guess"}) {
            cli::DiagnosticDvdOptions invalid;rejects([&]{(void)invalid.parse(flag);},"diagnostic DVD");
        }
    }
    const auto cdvdDisc=std::filesystem::path(argv[2])/"cdvd-hle.iso";
    std::filesystem::create_directories(cdvdDisc.parent_path());
    const auto rebootBytes=reboot_fixture();
    const auto reboot=load_iop_reboot_image(rebootBytes);
    require(reboot.find("RESET")->size==0 && reboot.find("CDVDMAN")->version==0x0301 &&
        reboot.sha256==sha256_hex(rebootBytes),"IOPRP reuses strict ROMDIR/EXTINFO parsing without claiming a reset-vector program");
    auto unpadded=rebootBytes;unpadded.resize(unpadded.size()-15u);
    require(load_iop_reboot_image(std::move(unpadded)).find("CDVDFSV")->size==17u,
        "IOPRP validates internal file alignment without requiring padding after the last file");
    rejects([&]{(void)load_bios_rom0(rebootBytes);},"exactly 4194304");
    rejects([&]{(void)load_iop_reboot_image(rebootBytes,BiosLoadOptions{std::string(64,'0')});},"does not match");
    const std::array residentEntries{
        IopAotEntry{0x10400u,0x24020000u,[](Iop& cpu){cpu.execute_native<0x24020000u>();}},
        IopAotEntry{0x10404u,0x03E00008u,[](Iop& cpu){cpu.execute_native<0x03E00008u>();}},
        IopAotEntry{0x10408u,0u,[](Iop& cpu){cpu.execute_native<0u>();}},
        IopAotEntry{0x10300u,0x24020000u,[](Iop& cpu){cpu.execute_native<0x24020000u>();}},
        IopAotEntry{0x10304u,0x03E00008u,[](Iop& cpu){cpu.execute_native<0x03E00008u>();}},
        IopAotEntry{0x10308u,0u,[](Iop& cpu){cpu.execute_native<0u>();}},
        IopAotEntry{0x10000u,0x24020000u,[](Iop& cpu){cpu.execute_native<0x24020000u>();}},
        IopAotEntry{0x10004u,0x03E00008u,[](Iop& cpu){cpu.execute_native<0x03E00008u>();}},
        IopAotEntry{0x10008u,0u,[](Iop& cpu){cpu.execute_native<0u>();}},
        IopAotEntry{0x10100u,0x24020000u,[](Iop& cpu){cpu.execute_native<0x24020000u>();}},
        IopAotEntry{0x10104u,0x03E00008u,[](Iop& cpu){cpu.execute_native<0x03E00008u>();}},
        IopAotEntry{0x10108u,0u,[](Iop& cpu){cpu.execute_native<0u>();}},
        IopAotEntry{0x10200u,0x24020000u,[](Iop& cpu){cpu.execute_native<0x24020000u>();}},
        IopAotEntry{0x10204u,0x03E00008u,[](Iop& cpu){cpu.execute_native<0x03E00008u>();}},
        IopAotEntry{0x10208u,0u,[](Iop& cpu){cpu.execute_native<0u>();}},
        IopAotEntry{0x10000u,0x40087800u,[](Iop& cpu){cpu.execute_native<0x40087800u>();}},
        IopAotEntry{0x10004u,0u,[](Iop& cpu){cpu.execute_native<0u>();}},
        IopAotEntry{0x10008u,0x3C090001u,[](Iop& cpu){cpu.execute_native<0x3C090001u>();}},
        IopAotEntry{0x1000Cu,0xAD281000u,[](Iop& cpu){cpu.execute_native<0xAD281000u>();}},
        IopAotEntry{0x10010u,0x03E00008u,[](Iop& cpu){cpu.execute_native<0x03E00008u>();}},
        IopAotEntry{0x10014u,0x24020000u,[](Iop& cpu){cpu.execute_native<0x24020000u>();}}};
    const auto residentSource=sha256_hex(m20::le_words({0x24020000u,0x03E00008u,0u}));
    const auto residentIdentity=iop_aot_identity(residentSource,residentEntries);
    const IopAotModule residentModule{residentSource,residentIdentity,residentEntries};
    const IopAotRegistration residentRegistration(residentModule);
    direct_test::Program residentProgram{{{0,5,m20::le_words({0x24020000u,0x03E00008u,0u})}},0,{},{}};
    auto residentIrX=direct_test::elf(residentProgram);direct_test::put(residentIrX,16,0x0008FF80u);direct_test::put(residentIrX,24,0);
    const auto executableReboot=reboot_fixture(residentIrX,residentIrX,residentIrX);
    iso_test::IsoOptions discOptions;
    discOptions.sectors=discOptions.written=std::max(24u,22u+static_cast<unsigned>((executableReboot.size()+2047u)/2048u));
    auto discBytes=iso_test::build_iso(discOptions);
    auto* directory=discBytes.data()+18u*2048u;std::size_t directoryEnd=0;
    while(directory[directoryEnd])directoryEnd+=directory[directoryEnd];
    iso_test::directory_record(directory+directoryEnd,22u,static_cast<std::uint32_t>(executableReboot.size()),false,"IOPRP.IMG;1");
    std::copy(executableReboot.begin(),executableReboot.end(),discBytes.begin()+22u*2048u);
    write_file_atomically(cdvdDisc,discBytes);
    const auto fixture=hle_test::program();const auto image=parse_ee_elf(read_file(argv[1]));
    PS2Runtime runtime;auto* memory=&runtime.memory();auto& state=runtime.cpu();
    const auto* module=&aot_module();
    runtime.prepare_direct_elf(image,module);

    {
        HleKernel kernel(runtime);
        configure_hle_spu2_quiescent_reset(*memory);
        auto& soundIop=const_cast<Iop&>(memory->iop());
        rejects([&]{soundIop.diagnostic_spu2_pio_word_cycles(0u);},"positive word cycles");
        soundIop.diagnostic_spu2_pio_word_cycles(1u);
        Ps2RecompIopServices::write_device(soundIop,0x1F90019Au,2u,0x8000u);
        rejects([&]{configure_hle_spu2_quiescent_reset(*memory);},"before audio configuration");
        Ps2RecompIopServices::write_device(soundIop,0x1F90019Au,2u,0u);
        require(memory->data_cache_enabled(),"generic HLE attaches the existing functional D-cache");
        rejects([&]{(void)memory->data_cache_operation(0x18u,0xA0004000u,{});},"requires a cached address");
        memory->diagnostic_data_cache(true);
        std::uint64_t executed=0;ExecutionContext ctx{state,*memory,executed,100,UINT32_MAX};
        execute_cache_instruction(ctx,0xBC100000u);
        require(!state.cp0(Cop0::TagLo) && state.taglo_unknown==0xF87u,
            "CACHE DXLTG reads tags initialized by HLE software, preserving unknown reserved bits");
        require(memory->data_cache_operation(0x11u,0u,{}).unknown==UINT32_MAX,
            "initializing HLE cache tags never fabricates cache-line data");
        require(memory->data_cache_operation(0x10u,63u*64u+1u,{}).unknown==0xF87u,
            "HLE software initializes both ways including the final cache set");
        state.set_cp0(Cop0::Config,0x10443u);memory->set_cache_config(state.cp0(Cop0::Config));
        memory->write32(0xA0004000u,0x11223344u);memory->write32(0x80004000u,0x55667788u);
        require(memory->read32(0xA0004000u)==0x11223344u && memory->read32(0x80004000u)==0x55667788u,
            "HLE uses the same dirty cache/backing RAM visibility as the physical DMA bus");
        state.set_reg32(3,0x64u);state.set_reg32(4,0u);execute_ee_system<MipsOpcode::Syscall>(ctx,12u);
        require(memory->read32(0xA0004000u)==0x55667788u,"SDK FlushCache publishes the real dirty line to shared backing RAM");
        memory->write32(0x80004000u,0xAABBCCDDu);
        (void)memory->data_cache_operation(0x18u,0xA0004000u,{});
        require(memory->read32(0xA0004000u)==0xAABBCCDDu,"explicit HLE diagnostic alias translation publishes the actual physical dirty line");
        rejects([&]{memory->diagnostic_data_cache(false);},"before CPU execution");
        rejects([&]{configure_hle_spu2_quiescent_reset(*memory);},"unused HLE session");
        const auto soundWrite=[&](unsigned address,unsigned value){Ps2RecompIopServices::write_device(soundIop,address,2u,value);};
        soundWrite(0x1F90059Au,0u);soundWrite(0x1F9001A8u,0u);soundWrite(0x1F9001AAu,0x2800u);
        soundWrite(0x1F9001ACu,0x1234u);soundWrite(0x1F90019Au,0x8010u);
        require(!soundIop.spu2_pio_words(0) && !soundIop.spu2_ram_halfword(0x2800u),
            "PIO FIFO does not publish its word before the selected transfer cycle");
        soundIop.run_until(soundIop.cycle()+1u,&memory->gs());
        require(!soundIop.stopped() && soundIop.spu2_pio_words(0)==1u && soundIop.spu2_ram_halfword(0x2800u)==0x1234u,
            "one IOP cycle transfers the actual 16-bit FIFO word to canonical SPU2 RAM");
        rejects([&]{soundIop.diagnostic_spu2_pio_word_cycles(1u);},"before guest execution");
    }
    runtime.prepare_direct_elf(image,module);

    {
        // Exercise the SDK imports against the physical transport, without a guest copy shortcut.
        Ps2RecompIopServices services(*memory,state);auto& iop=const_cast<Iop&>(memory->iop());iop.start_hle_services(&services);
        auto& bus=static_cast<ps2x::iop::IopHost&>(services).nativeMemory();
        ps2x::iop::detail::IopKernel kernel(bus);
        ps2x::iop::detail::IopRpcBridge rpc(services,bus,kernel);
        ps2x::iop::detail::IopCpuState cpu{};
        constexpr auto destination=hle_test::data+0x4000u;
        require(bus.allocate(4096u)==0x10000u,"reserve the synthetic DMA source in the shared allocator");
        for(unsigned offset=0;offset<4096u;offset+=4u) {
            bus.write32(0x10000u+offset,0xCAFE0000u+offset);memory->write32(destination+offset,0u);
        }
        bus.write32(0x8000u,0x10000u);bus.write32(0x8004u,destination);
        bus.write32(0x8008u,4096u);bus.write32(0x800Cu,0u);
        const auto submit=[&]{cpu.gpr[4]=0x8000u;cpu.gpr[5]=1u;require(rpc.dispatchSifManImport(7u,cpu),"SDK dispatches SifSetDma");return cpu.gpr[2];};
        const auto status=[&](unsigned id){cpu.gpr[4]=id;require(rpc.dispatchSifManImport(8u,cpu),"SDK dispatches SifDmaStat");return static_cast<int>(cpu.gpr[2]);};
        const auto first=submit();
        require(first && status(first)==0 && !memory->read32(destination) && rpc.nativeHasSifDma(),
            "SifSetDma arms the actual channel and cannot publish or finish a 4 KiB transfer immediately");
        bus.write32(0x8004u,destination+4096u);bus.write32(0x8008u,16u);
        const auto second=submit();require(second!=first && status(second)==1,"another DMA queues behind the active physical transfer");
        iop.ee_dma_write(0x1000E000u,1u);iop.ee_dma_write(0x1000E020u,1u<<21u);
        iop.ee_dma_write(0x1000C020u,0u);iop.ee_dma_write(0x1000C000u,0x184u);
        for(unsigned n=0;n<1024u && rpc.nativeHasSifDma();++n) {
            iop.run_until(iop.cycle()+1u,&memory->gs());rpc.nativePumpSifDma();
        }
        require(!iop.stopped() && status(first)==-1 && status(second)==-1 && !rpc.nativeHasSifDma(),
            "real DMA cycles drain both submissions before their IDs become complete");
        for(unsigned offset=0;offset<4096u;offset+=4u)require(memory->read32(destination+offset)==0xCAFE0000u+offset,"SIF0 preserves every source word");
        require(memory->read32(destination+4096u)==0xCAFE0000u && !iop.interpreted_instructions(),
            "the queued transfer uses the same receiver and no interpreted instructions");
        rpc.reset();
    }
    runtime.prepare_direct_elf(image,module);

    {
        using O=ps2x::iop::PadOperation;
        bool connected=false;unsigned polls=0;std::array<unsigned,3> motor{};
        Iop::PadInput input;input.buttons=static_cast<uint16_t>(0xFFFFu&~(1u<<14u));input.lx=17u;input.ry=230u;
        input.pressureValid=1u<<6u;input.pressure[6]=93u;
        Ps2RecompIopServices services(*memory,state);auto& iop=const_cast<Iop&>(memory->iop());iop.start_hle_services(&services);
        auto& host=static_cast<ps2x::iop::IopHost&>(services);auto& bus=static_cast<ps2x::iop::IopHost&>(services).nativeMemory();
        ps2x::iop::RpcRequest absent;absent.sid=0x80000100u;require(!services.rpc_server(absent),"PADMAN cannot bind before its SDK provider is loaded");
        require(services.load_irx("PADMAN",service_provider_irx("ds2u_d ",0x10000u,0x320u)).startResult==0,"PADMAN starts the SDK provider without executing its invalid fixture instruction");
        iop.set_pad_input_source([&](unsigned port,std::uint64_t)->std::optional<Iop::PadInput>{++polls;return !port && connected?std::optional{input}:std::nullopt;});
        iop.set_pad_output_sink([&](unsigned port,uint8_t small,uint8_t large){motor={port,small,large};return true;});
        constexpr auto area=hle_test::data+0x1000u,status=hle_test::data+0x1200u,oldArea=hle_test::data+0x1400u;
        const auto rpc=[&](uint32_t sid,std::vector<uint8_t> packet){
            ps2x::iop::RpcRequest request;request.sid=sid;require(services.rpc_server(request),"SDK PADMAN endpoint is registered");
            require(bus.writeRam(request.serverBuffer,packet.data(),packet.size()),"PADMAN request arrives through IOP RAM");
            request.function=1u;request.send.size=packet.size();request.receive={hle_test::data+0x1800u,128u};
            const auto result=services.rpc_call(request).value();require(result.second==128u,"PADMAN main reply is staged for real SIF0");return result.first;
        };
        const auto packet=[](uint32_t command,std::initializer_list<std::pair<unsigned,uint32_t>> args={}){
            std::vector<uint8_t> data(128u);direct_test::put(data,0u,command);for(const auto [word,value]:args)direct_test::put(data,word*4u,value);return data;
        };
        const auto latest=[&](uint32_t base,unsigned stride,unsigned frameOffset){
            return memory->read32(base+frameOffset)<memory->read32(base+stride+frameOffset)?base+stride:base;
        };
        auto result=rpc(0x80000100u,packet(18u));require(bus.read32(result+12u)==0x320u,"PADMAN reports the actual IRX version");
        result=rpc(0x80000100u,packet(16u,{{4u,status}}));require(bus.read32(result+12u)==1u,"PADMAN initializes connection buffers");
        result=rpc(0x80000100u,packet(1u,{{4u,area}}));require(bus.read32(result+12u)==1u,"PADMAN opens an EE pad area");
        services.on_vblank();auto current=latest(area,128u,88u);
        require(memory->read8(current+112u)==0u && memory->read32(current+96u)==0u,"No host input reports disconnected instead of a fake stable pad");
        connected=true;services.on_vblank();current=latest(area,128u,88u);
        require(memory->read8(current+112u)==6u && memory->read32(current+96u)==32u,"A real input source makes the SDK pad stable on a video event");
        require(memory->read8(current+2u)==static_cast<uint8_t>(input.buttons) && memory->read8(current+3u)==static_cast<uint8_t>(input.buttons>>8u),"PADMAN publishes actual active-low buttons");
        require(memory->read16(current+80u)==4u && memory->read16(current+82u)==7u && memory->read8(current+104u)==2u,"Libpad sees both digital and analog modes");
        const auto before=polls,frame=memory->read32(current+88u);
        result=rpc(0x80000100u,packet(6u,{{3u,1u},{4u,3u}}));require(bus.read32(result+20u)==1u,"PADMAN accepts a real mode change");
        ps2x::iop::PadSnapshot snap;ps2x::iop::PadRequest inspect{O::Snapshot,{0u,0u}};inspect.snapshot=&snap;
        require(host.pad(inspect)==1 && snap.reqState==2u && snap.frame==frame && polls==before,"Mode changes remain busy until the canonical video event");
        services.on_vblank();current=latest(area,128u,88u);
        require(memory->read8(current+1u)==0x73u && memory->read8(current+6u)==17u && memory->read8(current+5u)==230u && memory->read8(current+113u)==0u,"Analog data and command completion share one frame");
        result=rpc(0x80000100u,packet(10u,{{3u,0xFFFu}}));require(bus.read32(result+16u)==1u,"PADMAN accepts pressure mode");
        services.on_vblank();current=latest(area,128u,88u);
        require(memory->read8(current+1u)==0x79u && memory->read8(current+14u)==93u && memory->read8(current+31u)==0x5Au,"SDK libpad preserves pressure and ACK bytes");
        auto align=packet(8u);const std::array<uint8_t,6> map{0u,1u,0xFFu,0xFFu,0xFFu,0xFFu};std::copy(map.begin(),map.end(),align.begin()+12u);
        result=rpc(0x80000100u,align);require(bus.read32(result+20u)==1u,"Actuator alignment is stored by the SDK");services.on_vblank();
        auto direct=packet(7u);direct[12u]=1u;direct[13u]=128u;result=rpc(0x80000100u,direct);
        require(bus.read32(result+20u)==1u && motor==std::array<unsigned,3>{0u,1u,128u},"Actuation reaches the real host output adapter");
        result=rpc(0x80000100u,packet(14u,{{4u,1u}}));require(bus.read32(result+12u)==1u && motor[1]==0u && motor[2]==0u,"Closing a pad stops its motors");
        const auto closedPolls=polls;services.on_vblank();require(polls==closedPolls,"Closed ports do not sample input or write recycled buffers");
        result=rpc(0x8000010Fu,packet(0x80000100u,{{4u,oldArea}}));require(bus.read32(result+12u)==1u,"Old libpad RPC opens its own 64-byte ABI");
        services.on_vblank();current=latest(oldArea,64u,0u);
        require(memory->read8(current+4u)==6u && memory->read32(current+40u)==32u && memory->read8(current+8u+3u)==static_cast<uint8_t>(input.buttons>>8u),"Old libpad reads the same controller through its correct buffer layout");
        result=rpc(0x80000100u,packet(1u,{{1u,2u},{4u,area}}));require(bus.read32(result+12u)==0u,"PADMAN rejects invalid ports without opening a fake device");
        result=rpc(0x80000100u,packet(1u,{{4u,area+4u}}));require(bus.read32(result+12u)==0u,"PADMAN rejects unaligned EE areas");
        connected=false;services.on_vblank();current=latest(oldArea,64u,0u);require(memory->read8(current+4u)==0u,"Disconnecting the host source reaches the old client too");
        iop.stop_hle_services();
    }
    runtime.prepare_direct_elf(image,module);
    {
        using O=ps2x::iop::MemoryCardOperation;
        const auto root=std::filesystem::absolute(std::filesystem::path(argv[2])/std::filesystem::u8path(u8"mc-servi\u00E7o")/"mc0");
        constexpr auto data=hle_test::data+0x800u;
        const std::array<uint8_t,11> payload{1,3,5,7,9,11,13,15,17,19,21};
        for(const auto* manager:{"mc2_d ","mcman_tool"}) {
            runtime.prepare_direct_elf(image,module);
            const bool toolManager=std::string_view(manager)=="mcman_tool";
            Ps2RecompIopServices services(*memory,state);services.configure_memory_card_root(root);
            auto& host=static_cast<ps2x::iop::IopHost&>(services);
            const auto call=[&](O operation,std::array<uint32_t,6> args={},std::optional<std::string> name={}) {
                ps2x::iop::MemoryCardRequest request{operation,args};request.name=std::move(name);return host.memoryCard(request);
            };
            const auto pc=state.pc,reg2=state.reg32(2);
            require(call(O::Init)==0,"SDK MemoryCard initializes its console-owned backend");
            const auto fd=call(O::Open,{0,0,0,0x203u},"save-test");require(fd>0,"SDK MemoryCard creates a save");
            for(unsigned i=0;i<payload.size();++i)memory->write8(data+i,payload[i]);
            require(call(O::Write,{static_cast<uint32_t>(fd),data,static_cast<uint32_t>(payload.size())})==payload.size(),"MemoryCard writes the whole checked EE buffer");
            require(call(O::Seek,{static_cast<uint32_t>(fd),0,0})==0,"MemoryCard seeks through the SDK backend");
            require(call(O::Read,{static_cast<uint32_t>(fd),0x1FFFFFFFu,4u})==-5,"Invalid EE output cannot advance the save cursor");
            for(unsigned i=0;i<20u;++i)memory->store8_observed(data+0x40u+i,{0xA5u,0xFFu});
            require(call(O::Read,{static_cast<uint32_t>(fd),data+0x40u,20u})==payload.size(),"MemoryCard short read reports actual bytes");
            for(unsigned i=0;i<payload.size();++i)require(memory->read8(data+0x40u+i)==payload[i],"MemoryCard read preserves payload");
            require(memory->load8_observed(data+0x40u+payload.size()).unknown==0xFFu,"EOF does not publish unwritten bytes");
            require(call(O::Read,{999u,data+0x40u,20u})==-4 && memory->read8(data+0x40u)==payload[0],"Invalid descriptor does not overwrite EE output");
            require(call(O::GetDir,{0,0,0,0,4u,data+0x100u},"save-*")==1,"SDK directory lists real saves instead of returning empty success");
            require(call(O::Close,{static_cast<uint32_t>(fd)})==0 && call(O::Close,{static_cast<uint32_t>(fd)})==-4,"MemoryCard close preserves invalid-handle errors");
            uint8_t unused{};rejects([&]{(void)host.readGuest(data,&unused,1u);},"outside MemoryCard HLE");
            rejects([&]{(void)call(O::Unformat,{0,0});},"persistent format state");
            require(std::filesystem::exists(root/"save-test"),"Unsupported format operations cannot erase a save");
            auto& iop=const_cast<Iop&>(memory->iop());iop.start_hle_services(&services);
            const auto provider=[&](const char* name,uint32_t base,uint16_t version) {
                return services.load_irx(name,service_provider_irx(name,base,version));
            };
            ps2x::iop::RpcRequest rpc;rpc.sid=0x80000400u;
            require(!services.rpc_server(rpc),"MCSERV is absent before a real provider is loaded");
            require(provider(manager,0x10000u,toolManager?0x20Fu:0x206u).startResult==0 && !services.rpc_server(rpc),"MCMAN starts its real backend without exposing an uninitialized server");
            require(provider(toolManager?"mcserv":"mc2_s ",0x10100u,toolManager?0x20Au:0x205u).startResult==0 && services.rpc_server(rpc),"MCSERV binds only after the SDK provider is active");
            auto& bus=static_cast<ps2x::iop::IopHost&>(services).nativeMemory();
            rpc.function=0xFEu;rpc.send.size=0u;rpc.receive={data+0x300u,12u};
            const auto initReply=services.rpc_call(rpc).value();
            require(bus.read32(initReply.first)==0u &&
                    bus.read32(initReply.first+4u)==(toolManager?0x20Au:0x205u) &&
                    bus.read32(initReply.first+8u)==(toolManager?0x20Fu:0x206u),
                    "MCSERV initialization reports the loaded IRX versions, not fixed SDK defaults");
            std::vector<uint8_t> packet(1044u);direct_test::put(packet,12u,4u);direct_test::put(packet,16u,data+0x200u);
            std::copy_n("save-*",7u,packet.begin()+20u);
            require(bus.writeRam(rpc.serverBuffer,packet.data(),packet.size()),"Receive MCSERV input in canonical IOP RAM");
            rpc.function=0x0Du;rpc.send.size=packet.size();rpc.receive={data+0x300u,4u};
            const auto reply=services.rpc_call(rpc).value();
            require(reply.second==16u,"MCSERV response is padded for SIF0");
            require(bus.read32(reply.first)==1u,"MCSERV routes the packet name from IOP to the actual directory backend");
            require(memory->read8(data+0x200u+32u)=='s',"MCSERV writes the real directory entry to EE");
            require(bus.writeRam(rpc.serverBuffer,packet.data(),packet.size()),"Repeat MCSERV input");
            const auto nextReply=services.rpc_call(rpc).value();
            require(bus.read32(nextReply.first)==1u,"MCSERV repeated responses retain their allocation bounds");
            direct_test::put(packet,8u,1u);require(bus.writeRam(rpc.serverBuffer,packet.data(),packet.size()),"Receive directory continuation");
            const auto finalReply=services.rpc_call(rpc).value();require(bus.read32(finalReply.first)==0u,"MCSERV directory continuation reaches the real end");
            iop.stop_hle_services();
            require(state.pc==pc && state.reg32(2)==reg2,"IOP MemoryCard HLE leaves EE registers unchanged");
            rejects([&]{services.configure_memory_card_root(root);},"unused service");
        }
        {
            Ps2RecompIopServices services(*memory,state);services.configure_memory_card_root(root);
            auto& host=static_cast<ps2x::iop::IopHost&>(services);
            require(host.memoryCard({O::Init})==0,"MemoryCard restarts without deleting saves");
            ps2x::iop::MemoryCardRequest open{O::Open,{0,0,0,1u}};open.name="save-test";
            const auto fd=host.memoryCard(open);require(fd>0,"Saved file survives session destruction");
            require(host.memoryCard({O::Read,{static_cast<uint32_t>(fd),data+0x40u,20u}})==payload.size(),"Persisted save remains readable");
            require(host.memoryCard({O::Close,{static_cast<uint32_t>(fd)}})==0,"Close persisted save");
        }
        std::filesystem::remove(root/"save-test");std::filesystem::remove(root);std::filesystem::remove(root.parent_path()/"mc1");std::filesystem::remove(root.parent_path());
    }
    runtime.prepare_direct_elf(image,module);

    {
        const auto transfer=[&] {
            constexpr auto tag=hle_test::data+0x600u;
            memory->write32(tag,0x70000001u);memory->write32(tag+4u,0);
            memory->write32(tag+8u,0x01000101u);memory->write32(tag+12u,0x6C010000u);
            for(unsigned i=0;i<4;++i)memory->write32(tag+16u+i*4u,0x12340000u+i);
            memory->write32(0xB000E020u,1u);memory->write32(0xB0008020u,0);
            memory->write32(0xB0008030u,tag);memory->write32(0xB0008000u,0x145u);
            state.stall(256);memory->sync_iop();
            return !(memory->iop().ee_dma_read(0x10008000u)&0x100u) && memory->dma_cop0_condition() &&
                memory->read32(0xB1004000u)==0x12340000u && memory->read32(0xB100400Cu)==0x12340003u;
        };
        require(!transfer(),"bare ELF reset leaves DMA stopped without kernel initialization");
        require(memory->gs().observe_csr().unknown&(1u<<13u),"bare ELF does not inherit an HLE FIELD phase");
        runtime.prepare_direct_elf(image,module);
        HleKernel kernel(runtime);
        require(transfer(),"SDK HLE initialization permits VIF UNPACK into canonical VU RAM and DMA completion");
        const auto video=memory->gs().vertical_clock();
        require(video.state==GsRegisters::VerticalClock::State::Running && video.field==525u,
            "SDK HLE default video service uses the canonical NTSC field clock");
        memory->write32(0xB000F000u,4u);
        const auto edge=video.next_edge(state.cycles()+1u,false);
        state.stall(edge-state.cycles()+8u);memory->sync_iop();
        require((memory->read32(0xB000F000u)&4u) && (memory->iop().diagnostic_istat()&1u),
            "one canonical VBlank edge reaches both EE and IOP interrupt controllers");
        const auto csr=memory->load64_observed(kGsCsr);
        require(!(csr.unknown&(1u<<13u)) && (csr.value&(1u<<13u)),
            "SDK FIELD phase changes on the shared GS edge, without defining unrelated CSR bits");
        rejects([&]{HleKernel duplicate(runtime);},"already owns");
        state.set_reg32(4,0x1234);const auto pc=state.pc;
        rejects([&]{runtime.prepare_direct_elf(image,module);},"Detach HLE");
        require(state.pc==pc && state.reg32(4)==0x1234,"refused reset preserves the live CPU");
    }
    runtime.prepare_direct_elf(image,module);
    {
        HleKernel kernel(runtime);std::uint64_t executed=0;
        ExecutionContext context{state,*memory,executed,100,UINT32_MAX};
        const auto call=[&](unsigned number,std::uint32_t a=0,std::uint32_t b=0,std::uint32_t c=0) {
            state.set_reg32(3,number);state.set_reg32(4,a);state.set_reg32(5,b);state.set_reg32(6,c);
            execute_ee_system<MipsOpcode::Syscall>(context,12);return state.reg32(2);
        };
        require(memory->gs().vram().read(0,64,0,0,0)==0u && !memory->gs().vram().pixel_writes(),
            "SDK cold VRAM initialization defines zero without counting guest pixel writes");
        for(unsigned phase=0;phase<2;++phase) {
            require(memory->gs().latched(kGsPmode)==0u && memory->gs().latched(kGsSmode2)==0u &&
                call(0x70)==0x7F00u,"SDK GS cold/soft reset exposes defined control fields");
            require(memory->gs().general(8)==0u && memory->gs().general(9)==0u &&
                memory->gs().general(0x1A)==1u && memory->gs().general(0x01)==0x3F80000080808080ull &&
                memory->gs().general(0x40)==((639ull<<16u)|(447ull<<48u)) &&
                memory->gs().general(0x4D)==(10ull<<16u),
                "SDK cold/soft graphics profile retains upstream context and latch defaults");
            require(call(0x02,1,2,1)==0 && memory->gs().latched(kGsPmode)==1u &&
                memory->gs().latched(kGsSmode2)==3u,"SDK CRT configuration uses the same GS after each reset");
            require(call(0x02,1,2,0)==0 && memory->gs().latched(kGsSmode2)==1u,
                "NTSC interlaced field/frame configuration reaches the canonical GS");
            const auto retired=state.retired_instructions;
            rejects([&]{(void)call(0x02,1,3,0);},"CRT configuration is unsupported");
            require(state.retired_instructions==retired && memory->gs().latched(kGsSmode2)==1u,
                "unsupported CRT modes refuse before changing video state or retiring a syscall");
            rejects([&]{(void)memory->read64(kGsPmode);},"no register model");
            memory->write64(kGsSmode2,0x396881u);
            require(memory->gs().latched(kGsSmode2)==0x396881u,
                "SDK profile retains SMODE2 padding without replacing the guest word");
            if(!phase) {
                memory->write128(kGifFifo,1u|(1ull<<15u)|(1ull<<60u),0xEu);
                memory->write128(kGifFifo,5u,9u);
                require(memory->gs().general(9)==5u,"guest GIF writes reach the HLE graphics context");
                const auto send=[&](unsigned index,std::uint64_t value) {
                    memory->write128(kGifFifo,1u|(1ull<<15u)|(1ull<<60u),0xEu);
                    memory->write128(kGifFifo,value,index);
                };
                send(0x50,1ull<<48u);send(0x51,0);send(0x52,2u|(2ull<<32u));send(0x53,0);
                memory->write128(kGifFifo,1u|(1ull<<15u)|(2ull<<58u),0u);
                memory->write128(kGifFifo,0x2222222211111111ull,0x4444444433333333ull);
                memory->write64(kGsCsr,kCsrReset);
                require(memory->gs().vram().read(0,64,0,0,0)==0x11111111u && memory->gs().vram().pixel_writes()==4u,
                    "soft GS reset preserves uploaded VRAM and actual pixel write accounting");
            }
        }
    }
    {
        const AotBlock stuckBlock{hle_test::text+0x800u,[](ExecutionContext&){}};
        auto stuck=*module;stuck.blocks=&stuckBlock;stuck.block_count=1;
        runtime.prepare_direct_elf(image,&stuck);HleKernel kernel(runtime);state.pc=stuckBlock.address;
        BootOptions options;options.max_instructions=1;options.max_trace_events=16;options.tail_instructions=4;
        const auto report=run_ee_boot(state,*memory,&stuck,options);
        require(report.outcome==BootOutcome::BlockPoint && !report.retired && report.reason.find("made no progress")!=std::string::npos,
            "a native block without instruction or kernel progress is still rejected with HLE attached");
        rejects([&]{(void)run_aot_module(stuck,state,*memory,0u,1u);},"did not make progress");
    }
    runtime.prepare_direct_elf(image,module);
    {
        HleKernel kernel(runtime);std::uint64_t executed=0;
        ExecutionContext context{state,*memory,executed,100,UINT32_MAX};
        constexpr auto flag=hle_test::data+0x200u,tick=flag+8u;
        memory->write32(flag,0xA5A5A5A5u);memory->write64(tick,~0ull);
        state.set_reg32(3,0x73);state.set_reg32(4,flag);state.set_reg32(5,tick);
        execute_ee_system<MipsOpcode::Syscall>(context,12);
        require(!memory->read32(flag) && !memory->read64(tick),
            "SDK direct output writes survive the syscall output transaction");
        const auto video=memory->gs().vertical_clock();
        state.pc=hle_test::text+0x800u;
        BootOptions options;options.max_instructions=video.half_line*video.field+4u;
        options.max_trace_events=16;options.tail_instructions=4;
        const auto report=run_ee_boot(state,*memory,module,options);
        require(report.outcome==BootOutcome::BudgetExhausted && !report.interpreted_instructions &&
            memory->read32(flag)==1u && memory->read64(tick)==1u,
            "canonical VBlank drives the SDK flag and tick while native code runs: "+report.reason+
            "; retired="+std::to_string(report.retired)+" cycles="+std::to_string(state.cycles())+
            " flag="+std::to_string(memory->read32(flag))+" tick="+std::to_string(memory->read64(tick)));
    }
    runtime.prepare_direct_elf(image,module);
    {
        HleKernel kernel(runtime);std::uint64_t executed=0;
        ExecutionContext context{state,*memory,executed,100,UINT32_MAX};
        constexpr auto flag=hle_test::data+0x200u,tick=flag+8u;
        state.set_reg32(3,0x73);state.set_reg32(4,flag);state.set_reg32(5,tick);
        execute_ee_system<MipsOpcode::Syscall>(context,12);
        const auto edge=memory->gs().vertical_clock().next_edge(state.cycles()+1u,false);
        state.stall(edge-state.cycles());state.pc=hle_test::text+0x800u;
        require(run_aot_module(*module,state,*memory,hle_test::text+0x804u,64u)==1u &&
            memory->read32(flag)==1u && memory->read64(tick)==1u,
            "direct AOT dispatch accepts a real SDK transition at block entry without fabricating retirement");
    }
    runtime.prepare_direct_elf(image,module);
    {
        HleKernel kernel(runtime);std::uint64_t executed=0;
        ExecutionContext context{state,*memory,executed,100,UINT32_MAX};
        state.set_reg32(3,0x40);state.set_reg32(4,hle_test::data);
        execute_ee_system<MipsOpcode::Syscall>(context,12);const auto semaphore=state.reg32(2);
        state.set_reg32(3,0x18);state.set_reg32(4,256);state.set_reg32(5,hle_test::text+0x900u);state.set_reg32(6,semaphore);
        execute_ee_system<MipsOpcode::Syscall>(context,12);const auto deadline=kernel.next_deadline();
        state.pc=hle_test::text+0x800u;state.set_reg32(3,0x44);state.set_reg32(4,semaphore);
        execute_ee_system<MipsOpcode::Syscall>(context,12);
        BootOptions options;options.max_instructions=1000;options.stop_at=hle_test::text+0x804u;
        options.max_trace_events=16;options.tail_instructions=4;
        const auto report=run_ee_boot(state,*memory,module,options);
        require(report.outcome==BootOutcome::StopAddress && kernel.current_thread_id()==1 &&
            state.reg32(2)==semaphore && state.cycles()>=deadline && !report.interpreted_instructions,
            "an SDK alarm wakes the sole waiting thread through canonical time and native callback execution");
    }
    runtime.prepare_direct_elf(image,module);
    {
        HleKernel kernel(runtime);std::uint64_t executed=0;
        ExecutionContext context{state,*memory,executed,100,UINT32_MAX};
        const auto call=[&](unsigned number,std::uint32_t address) {
            state.set_reg32(3,number);state.set_reg32(4,address);
            execute_ee_system<MipsOpcode::Syscall>(context,12);
            return static_cast<std::int32_t>(state.reg32(2));
        };
        constexpr auto output=hle_test::data+0x200u;
        require(call(0x4B,output)==0,"SDK GetOsdConfigParam writes canonical EE RAM");
        const auto original=memory->read32(output);
        memory->write32(output,original^1u);
        require(call(0x4A,output)==0 && call(0x4B,output+4u)==0 && memory->read32(output+4u)==(original^1u),
            "SDK OSD read/write share one session configuration");
        PS2Runtime otherRuntime;auto* other=&otherRuntime.memory();auto& otherState=otherRuntime.cpu();
        otherRuntime.prepare_direct_elf(image,module);HleKernel otherKernel(otherRuntime);
        std::uint64_t otherExecuted=0;ExecutionContext otherContext{otherState,*other,otherExecuted,100,UINT32_MAX};
        otherState.set_reg32(3,0x4B);otherState.set_reg32(4,output);
        execute_ee_system<MipsOpcode::Syscall>(otherContext,12);
        require(other->read32(output)==original,"SDK OSD globals are isolated between canonical console sessions");
        require(call(0x4B,0)==-1 && memory->read32(output+4u)==(original^1u),
            "SDK invalid OSD output preserves previously published RAM");
    }
    runtime.prepare_direct_elf(image,module);
    {
        state.pc=hle_test::text+0x800u;
        BootOptions budget;budget.max_instructions=25000;budget.stop_at=hle_test::text+0x600u;
        budget.max_trace_events=16;budget.tail_instructions=4;
        const auto control=run_ee_boot(state,*memory,module,budget);
        require(control.outcome==BootOutcome::BudgetExhausted && control.retired==25000 && !control.interpreted_instructions,
            "A bare native busy loop cannot enter the alarm handler without a kernel event");
        runtime.prepare_direct_elf(image,module);HleKernel kernel(runtime);
        std::uint64_t executed=0;ExecutionContext context{state,*memory,executed,100,UINT32_MAX};
        state.set_reg32(3,0x18);state.set_reg32(4,1);state.set_reg32(5,hle_test::text+0x600u);state.set_reg32(6,0x1234);
        execute_ee_system<MipsOpcode::Syscall>(context,12);
        require(state.reg32(2)>0,"The SDK scheduler registers a real guest-cycle alarm");
        const auto deadline=kernel.next_deadline();state.pc=hle_test::text+0x800u;
        const auto alarm=run_ee_boot(state,*memory,module,budget);
        require(alarm.outcome==BootOutcome::StopAddress && state.pc==hle_test::text+0x600u &&
            alarm.aot_instructions==alarm.retired && !alarm.interpreted_instructions &&
            state.cycles()>=deadline && state.cycles()<=deadline+1u && state.count()==state.cycles(),
            "An SDK alarm interrupts native code at its canonical cycle deadline, including accelerated busy loops and delay slots");
    }
    runtime.prepare_direct_elf(image,module);
    std::uint64_t total=0,aot=0,interpreted=0;
    {
        HleKernel kernel(runtime);
        BootOptions options;options.max_instructions=7;options.max_trace_events=16;options.tail_instructions=4;
        options.stop_at=fixture.stop;options.copy_device_histories=false;
        for(unsigned slice=0;slice<1000;++slice) {
            const auto report=run_ee_boot(state,*memory,module,options);
            total+=report.retired;aot+=report.aot_instructions;interpreted+=report.interpreted_instructions;
            require(report.outcome!=BootOutcome::BlockPoint,report.reason);
            if(report.outcome==BootOutcome::StopAddress)break;
            require(slice<999,"bounded HLE execution");
        }
        require(state.pc==fixture.stop && kernel.current_thread_id()==1,"main resumes after child exits");
        require(kernel.syscall_count()==15,"all kernel services executed, including blocking calls");
        require(memory->read32(hle_test::data+0x8C)==1,"semaphore records the waiting child");
        require(memory->read32(hle_test::data+0x100)==1,"blocked semaphore resumes with its ID");
        require(memory->read32(hle_test::data+0x104)==4 && memory->read32(hle_test::data+0x108)==0,"event wakeup publishes bits and success");
        require(memory->read32(hle_test::data+0x10C)==static_cast<std::uint32_t>(-419),"empty semaphore error preserved");
        require(memory->read32(hle_test::data+0x118)==static_cast<std::uint32_t>(-421) && memory->read32(hle_test::data+0x110)==0xDEADBEEF,"event clear and failed poll preserve output");
        require(memory->read32(hle_test::data+0x140)==16,"exited child is dormant");
        require(memory->read32(hle_test::data+0x114)==Memory::kMainRamBytes,"GetMemorySize returns actual memory capacity");
        require(state.cycles()==total && state.count()==total,"thread switches preserve the one console clock and Count");
        const std::filesystem::path path=std::filesystem::path(argv[2])/"unsupported.ps2ncp";
        BootReport boundary;boundary.pc=state.pc;boundary.outcome=BootOutcome::StopAddress;
        rejects([&]{save_checkpoint(path,state,*memory,boundary,{});},"HLE kernel objects");
        require(!file_exists(path),"incomplete kernel state is never published as a checkpoint");
        rejects([&]{(void)load_checkpoint(path,state,*memory,{});},"HLE kernel objects");
        rejects([&]{(void)checkpoint_state_sha256(state,*memory);},"HLE kernel objects");
    }
    require(!memory->hle_kernel(),"detach restores ordinary runtime ownership");
    rejects([&]{(void)checkpoint_state_sha256(state,*memory);},"HLE kernel objects");
    std::vector<std::uint8_t> data(0x3000);
    for(unsigned i=0;i<data.size();++i)data[i]=memory->read8(hle_test::data+i);
    std::cout<<"HLE_GUEST_RESULT "<<sha256_hex(data)<<" pc="<<state.pc<<" cycles="<<state.cycles()<<'\n';
    require(total==aot && interpreted==0,"all guest instructions used static AOT");
    runtime.prepare_direct_elf(image,module);
    {
        HleKernel kernel(runtime);
        ps2x::iop::detail::IopMemory upstreamMemory(const_cast<Iop&>(memory->iop()));
        const char boundedString[]="short";
        require(upstreamMemory.writeRam(0x121650u,boundedString,sizeof boundedString) &&
            upstreamMemory.readString(0x121650u,1024u*1024u)=="short",
            "SDK strlen's maximum bound does not require a short terminated string to occupy that whole range");
        rejects([&]{(void)upstreamMemory.readString(0x121650u,5u);},"exceeds its bound");
        const char edgeString[]{'x',0};
        require(upstreamMemory.writeRam(kIopRamBytes-2u,edgeString,2u) && upstreamMemory.readString(kIopRamBytes-2u,1024u)=="x",
            "a terminator at the last RAM byte is valid");
        require(upstreamMemory.writeRam(kIopRamBytes-1u,edgeString,1u),"unterminated boundary fixture");
        rejects([&]{(void)upstreamMemory.readString(kIopRamBytes-2u,1024u);},"without a terminator");
        direct_test::Program small{{{0,5,m20::le_words({0x2402002Au,0x03E00008u,0u}),0x100u}},0,{},{}};
        auto irx=direct_test::elf(small);direct_test::put(irx,16,0x0008FF80u);direct_test::put(irx,24,0);
        const auto loaded=ps2x::iop::detail::IopModuleLoader::load(irx,upstreamMemory,0x10000u);
        require(loaded && loaded.relocationsComplete && loaded.base==0x10000u && loaded.entry==0x10000u &&
            memory->iop().diagnostic_ram32(0x10000u)==0x2402002Au && memory->iop().diagnostic_ram32(0x10004u)==0x03E00008u,
            "The actual PS2Recomp IRX loader publishes its image into the console's canonical IOP RAM");
        require(loaded.executableRanges==std::vector<std::pair<std::uint32_t,std::uint32_t>>{{0x10000u,12u}},
            "SDK native preparation reports executable file bytes while excluding zero-filled BSS");
        const auto freeBefore=upstreamMemory.totalFreeMemory();
        const auto large=upstreamMemory.allocate(0x11ADF8u,256u);
        require(large>=loaded.base+loaded.size && upstreamMemory.allocationContaining(loaded.base)->size==loaded.size &&
            upstreamMemory.freeAllocation(large) && upstreamMemory.totalFreeMemory()==freeBefore,
            "module reservations share sysmem and leave room for large allocations without overlap");
        require(!upstreamMemory.allocate(256u,256u,0x1000u) && !upstreamMemory.allocate(256u,256u,loaded.base),
            "fixed allocations cannot overwrite kernel buffers or loaded images");
        const auto before=memory->iop().diagnostic_ram32(0x10000u);
        auto malformed=irx;direct_test::put(malformed,48u,1u,2u);
        require(!ps2x::iop::detail::IopModuleLoader::load(malformed,upstreamMemory,0x10000u) && memory->iop().diagnostic_ram32(0x10000u)==before,
            "a malformed section table is rejected before any valid native code is overwritten");
        malformed=irx;direct_test::put(malformed,56u,UINT32_MAX);
        require(!ps2x::iop::detail::IopModuleLoader::load(malformed,upstreamMemory,0x10000u) &&
            upstreamMemory.totalFreeMemory()==freeBefore && memory->iop().diagnostic_ram32(0x10000u)==before,
            "a failed segment load releases its reservation without overwriting the valid image");
        irx[0]=0;
        require(!ps2x::iop::detail::IopModuleLoader::load(irx,upstreamMemory,0x10000u) &&
            memory->iop().diagnostic_ram32(0x10000u)==before,"Upstream invalid-ELF refusal preserves the previous module");
        const auto path=ps2x::iop::parsePs2Path("cdrom0:\\MODULES\\FILE.IRX;1");
        require(path && path.device==ps2x::iop::Ps2PathDevice::Cdrom && path.path=="MODULES/FILE.IRX",
            "PS2Recomp path normalization is shared by native services");
    }
    {
        constexpr std::array child{0x2402002Au,0x03E00008u,0u};
        const auto childImage=module_info_irx(m20::le_words(std::vector<unsigned>(child.begin(),child.end())));
        std::array<unsigned,9> info{};
        require(ps2x::iop::detail::IopModuleLoader::probe(childImage,info) && info==std::array<unsigned,9>{4u,0u,0u,0u,12u,0u,0u,12u,UINT32_MAX},
            "SDK probe preserves the published IOPMOD FileInfo ABI");
        auto invalid=childImage;invalid[5]=2u;const auto expected=info;
        require(!ps2x::iop::detail::IopModuleLoader::probe(invalid,info) && info==expected,
            "invalid ELF leaves output metadata unchanged");
        invalid=childImage;direct_test::put(invalid,128u,13u);
        require(!ps2x::iop::detail::IopModuleLoader::probe(invalid,info),"IOPMOD sizes cannot exceed the load segment");
        constexpr std::array init{
            0x03E08021u,0x3C040012u,0x3C050001u,0x34A50300u,0x0C0040A5u,0u,0x3C080001u,0xAD020340u,
            0x00002021u,0x2405003Cu,0x00003021u,0x0C004095u,0u,0x24490030u,0x3C080001u,0xAD09030Cu,
            0x3C040012u,0x3C050001u,0x34A50300u,0x0C0040A7u,0u,0x3C080001u,0xAD020344u,
            0x8D04030Cu,0u,0x2484FFD0u,0x0C0040A9u,0u,
            0x3C080001u,0x8D190304u,0u,0x0320F809u,0u,0x3C080001u,0xAD020348u,
            0x8D040304u,0u,0x0C0040ADu,0u,0x3C080001u,0xAD02034Cu,
            0x0C0040ABu,0u,0x3C080001u,0xAD020350u,
            0x8D04034Cu,0u,0x0C0040AFu,0u,0x00042202u,0x00042200u,0x0C004097u,0u,0x3C080001u,0xAD020358u,
            0x8D040304u,0u,0x0C0040ADu,0u,0x3C080001u,0xAD020354u,
            0x00002021u,0x24050001u,0x00003021u,0x0C004095u,0u,0x3C080001u,0xAD02035Cu,
            0x00402021u,0x0C004097u,0u,
            0x24040001u,0x24050001u,0x00003021u,0x0C004095u,0u,0x3C080001u,0xAD020360u,
            0x24040002u,0x3C060012u,0x34C61101u,0x0C004095u,0u,0x3C080001u,0xAD020364u,
            0x00002021u,0x00002821u,0x0C004095u,0u,0x3C080001u,0xAD020368u,
            0x0C004099u,0u,0x3C080001u,0xAD02036Cu,0x0C00409Bu,0u,0x3C080001u,0xAD020370u,
            0x0200F821u,0x03E00008u,0x24020000u};
        std::vector<IopAotEntry> entries;
        const auto main=fixed_iop_entries<init,0x10000u>();
        const auto loaded=fixed_iop_entries<child,0x10430u>();
        entries.insert(entries.end(),main.begin(),main.end());entries.insert(entries.end(),loaded.begin(),loaded.end());
        std::vector<std::uint8_t> payload(0x400u);
        for(const auto& entry:main)direct_test::put(payload,entry.address-0x10000u,entry.word);
        const auto imports=[&](unsigned at,const std::string& name,std::initializer_list<unsigned> ordinals){
            direct_test::put(payload,at,0x41E00000u);direct_test::put(payload,at+8u,0x103u);
            std::copy(name.begin(),name.end(),payload.begin()+at+12u);at+=20u;
            for(const auto ordinal:ordinals){direct_test::put(payload,at,0x03E00008u);direct_test::put(payload,at+4u,0x24000000u|ordinal);at+=8u;}
        };
        imports(0x240u,"sysmem",{4u,5u,7u,8u});imports(0x280u,"loadcore",{22u,23u,16u,3u,24u,17u});
        const auto source=sha256_hex(payload),identity=iop_aot_identity(source,entries);
        const IopAotModule native{source,identity,entries};const IopAotRegistration registration(native);
        runtime.prepare_direct_elf(image,module);
        Ps2RecompIopServices services(*memory,state);auto& iop=const_cast<Iop&>(memory->iop());iop.start_hle_services(&services);
        const auto padding=services.allocate_heap(0x110000u);
        const auto input=services.allocate_heap(static_cast<unsigned>(childImage.size()));
        require(services.free_heap(padding)==0,"release the temporary placement reservation before loading code");
        auto& bus=static_cast<ps2x::iop::IopHost&>(services).nativeMemory();
        require(input==0x120000u && bus.writeRam(input,childImage.data(),childImage.size()),"the SDK owns the input ELF allocation");
        require(services.load_irx("loadcore-client.irx",module_info_irx(std::move(payload))).startResult==0,
            "original loadcore APIs analyze, relocate, register, execute, find and release a real synthetic IRX in AOT");
        const auto catalog=bus.read32(0x10350u);
        require(!services.module_range(0x10430u) && services.module_range(0x10000u).has_value(),
            "releasing a guest IRX invalidates only its executable capture ranges");
        require(bus.read32(0x10340u)==4u && bus.read32(0x10344u)==0u && bus.read32(0x10348u)==42u &&
            bus.read32(0x1034Cu)==0x10400u && !bus.read32(0x10354u) && bus.read32(catalog+20u)==1u &&
            bus.read32(0x10308u)==0x10430u && !iop.interpreted_instructions(),
            "FileInfo rebases GP, catalog release removes the child and native execution returns its real result");
        require(!bus.read32(0x10358u) && bus.read32(0x1035Cu)==0x10400u && bus.read32(0x10360u)==0x1EFF00u &&
            !bus.read32(0x10364u) && !bus.read32(0x10368u) && bus.read32(0x1036Cu)<=bus.read32(0x10370u) &&
            !(bus.read32(0x1036Cu)&255u) && !(bus.read32(0x10370u)&255u),
            "sysmem uses real 256-byte first/last/fixed allocation semantics, reuses freed holes and preserves free-space accounting");
        ps2x::iop::detail::IopImportRegistry bindings(bus);
        require(bindings.decode(0x10294u)->linked,"loaded import begins linked");
        bindings.setLinkedRange(0x10280u,0x80u,false);
        require(!bindings.decode(0x10294u)->linked,"UnLink invalidates the import binding");
        bindings.setLinkedRange(0x10294u,4u,true);
        require(!bindings.decode(0x10294u)->linked,"a half-linked import stub is still invalid");
        bindings.setLinkedRange(0x10298u,4u,true);
        require(bindings.decode(0x10294u)->linked && !bindings.decode(0x1029Cu)->linked,"partial Link restores only its own interval");
        bindings.reset();require(bindings.decode(0x1029Cu)->linked,"reset clears stale import binding intervals");
        iop.stop_hle_services();
    }
    // A resident IRX provider takes precedence over the SDK's HLE for the same library.
    {
        constexpr std::array init{0x03E08021u,0x3C040001u,0x34840100u,0x0C004015u,0u,
            0x0C004021u,0u,0x3C080001u,0xAD0201F0u,0x0200F821u,0x03E00008u,0x00001021u};
        const auto entries=fixed_iop_entries<init,0x10000u>();
        std::vector<std::uint8_t> payload(0x200u);
        for(const auto& entry:entries)direct_test::put(payload,entry.address-0x10000u,entry.word);
        const auto imports=[&](unsigned at,const char* name,unsigned ordinal) {
            direct_test::put(payload,at,0x41E00000u);direct_test::put(payload,at+8u,0x101u);
            std::copy_n(name,std::strlen(name),payload.begin()+at+12u);
            direct_test::put(payload,at+20u,0x03E00008u);direct_test::put(payload,at+24u,0x24000000u|ordinal);
        };
        imports(0x40u,"loadcore",6u);imports(0x70u,"cdvdman",28u);
        direct_test::put(payload,0xB0u,0xFFFFFFFFu);
        direct_test::put(payload,0x100u,0x41C00000u);direct_test::put(payload,0x108u,0x101u);
        std::copy_n("cdvdman",7u,payload.begin()+0x10Cu);
        for(unsigned ordinal=0;ordinal<=28u;++ordinal)direct_test::put(payload,0x114u+ordinal*4u,0x100B0u);
        const auto source=sha256_hex(payload),identity=iop_aot_identity(source,entries);
        const IopAotModule native{source,identity,entries};const IopAotRegistration registration(native);
        runtime.prepare_direct_elf(image,module);
        Ps2RecompIopServices services(*memory,state);auto& iop=const_cast<Iop&>(memory->iop());iop.start_hle_services(&services);
        auto& host=static_cast<ps2x::iop::IopHost&>(services);auto& bus=host.nativeMemory();
        ps2x::iop::detail::IopKernel kernel(bus);ps2x::iop::detail::IopCdvd cdvd(host,bus,kernel);
        ps2x::iop::detail::IopCpuState cpu{};
        rejects([&]{(void)cdvd.dispatchImport(28u,cpu);},"requires mounted canonical media");
        iop.mount_disc(std::make_shared<DiscImage>(cdvdDisc),Iop::DiscMedium::Ps2Cd);
        require(services.load_irx("status-client.irx",module_info_irx(std::move(payload))).startResult==0 &&
            bus.read32(0x101F0u)==0xAu && !iop.interpreted_instructions(),
            "sceCdStatus HLE overrides a loaded original export while the client remains native AOT");
        for(unsigned byte=0;byte<4u;++byte)iop.ee_cdvd_write(0x1F402005u,1u,byte?0u:1u);
        iop.ee_cdvd_write(0x1F402004u,1u,5u);
        require(cdvd.dispatchImport(28u,cpu) && cpu.gpr[2].value==0x12u && !cpu.gpr[2].unknown,
            "SDK status reports the canonical in-flight seek instead of constant pause");
        rejects([&]{(void)iop.ee_cdvd_read(0x1F40200Au,1u);},"bits 6-7");
        services.run_until(iop.cycle()+100000u);
        require(!iop.stopped() && cdvd.dispatchImport(28u,cpu) && cpu.gpr[2].value==0xAu &&
            iop.ee_cdvd_read(0x1F402008u,1u)==1u,
            "the same drive completion changes HLE status back to pause and preserves its real IRQ");
        iop.stop_hle_services();
    }
    {
#define IOP_PROVIDER_ENTRY(pc,word) IopAotEntry{pc,word,[](Iop& cpu){cpu.execute_native<word>();}}
        const std::array entries{
            IOP_PROVIDER_ENTRY(0x10000u,0x03E08021u),IOP_PROVIDER_ENTRY(0x10004u,0x3C040001u),
            IOP_PROVIDER_ENTRY(0x10008u,0x34840080u),IOP_PROVIDER_ENTRY(0x1000Cu,0x0C004035u),
            IOP_PROVIDER_ENTRY(0x10010u,0u),IOP_PROVIDER_ENTRY(0x10014u,0x0200F821u),
            IOP_PROVIDER_ENTRY(0x10018u,0x03E00008u),IOP_PROVIDER_ENTRY(0x1001Cu,0u),
            IOP_PROVIDER_ENTRY(0x10040u,0x03E00008u),IOP_PROVIDER_ENTRY(0x10044u,0x2402004Du),
            IOP_PROVIDER_ENTRY(0x10100u,0x03E08021u),IOP_PROVIDER_ENTRY(0x10104u,0x0C004065u),
            IOP_PROVIDER_ENTRY(0x10108u,0u),IOP_PROVIDER_ENTRY(0x1010Cu,0x0200F821u),
            IOP_PROVIDER_ENTRY(0x10110u,0x03E00008u),IOP_PROVIDER_ENTRY(0x10114u,0u)};
#undef IOP_PROVIDER_ENTRY
        std::vector<std::uint8_t> provider(0x100u),client(0x100u);
        for(const auto& entry:entries)direct_test::put(entry.address<0x10100u?provider:client,entry.address&0xFFu,entry.word);
        direct_test::put(provider,0x80u,0x41C00000u);direct_test::put(provider,0x88u,0x101u);
        const std::string library="cdvdman",loader="loadcore";
        std::copy(library.begin(),library.end(),provider.begin()+0x8Cu);
        for(unsigned ordinal=0;ordinal<=4;++ordinal)direct_test::put(provider,0x94u+ordinal*4u,0x10040u);
        direct_test::put(provider,0xC0u,0x41E00000u);direct_test::put(provider,0xC8u,0x101u);
        std::copy(loader.begin(),loader.end(),provider.begin()+0xCCu);
        direct_test::put(provider,0xD4u,0x03E00008u);direct_test::put(provider,0xD8u,0x24000006u);
        direct_test::put(client,0x80u,0x41E00000u);direct_test::put(client,0x88u,0x101u);
        std::copy(library.begin(),library.end(),client.begin()+0x8Cu);
        direct_test::put(client,0x94u,0x03E00008u);direct_test::put(client,0x98u,0x24000004u);
        const auto source=sha256_hex(provider),identity=iop_aot_identity(source,entries);
        const IopAotModule native{source,identity,entries};const IopAotRegistration registration(native);
        const auto irx=[](std::vector<std::uint8_t> payload){
            direct_test::Program program{{{0,5,std::move(payload)}},0,{},{}};
            auto result=direct_test::elf(program);direct_test::put(result,16,0x0008FF80u);direct_test::put(result,24,0);return result;
        };
        runtime.prepare_direct_elf(image,module);
        Ps2RecompIopServices services(*memory,state);auto& iop=const_cast<Iop&>(memory->iop());iop.start_hle_services(&services);
        require(services.load_irx("provider.irx",irx(std::move(provider))).startResult==0,"SDK registers the original export table");
        require(services.load_irx("client.irx",irx(std::move(client))).startResult==77 && !iop.interpreted_instructions(),
            "IRX imports execute the loaded provider in AOT instead of mixing its state with SDK HLE");
        iop.stop_hle_services();
    }
    // The SDK's registered RPC handler consumes the delivered IOP buffer and returns IOP bytes for SIF0.
    {
#define IOP_RPC_ENTRY(pc,word) IopAotEntry{pc,word,[](Iop& cpu){cpu.execute_native<word>();}}
        const std::array entries{
            IOP_RPC_ENTRY(0x10000u,0x03E08021u),IOP_RPC_ENTRY(0x10004u,0x3C040001u),
            IOP_RPC_ENTRY(0x10008u,0x34840140u),IOP_RPC_ENTRY(0x1000Cu,0x3C058765u),
            IOP_RPC_ENTRY(0x10010u,0x34A54321u),IOP_RPC_ENTRY(0x10014u,0x3C060001u),
            IOP_RPC_ENTRY(0x10018u,0x34C60080u),IOP_RPC_ENTRY(0x1001Cu,0x3C070001u),
            IOP_RPC_ENTRY(0x10020u,0x34E70180u),IOP_RPC_ENTRY(0x10024u,0x0C004035u),
            IOP_RPC_ENTRY(0x10028u,0u),IOP_RPC_ENTRY(0x1002Cu,0x0200F821u),
            IOP_RPC_ENTRY(0x10030u,0x03E00008u),IOP_RPC_ENTRY(0x10034u,0x24020000u),
            IOP_RPC_ENTRY(0x10080u,0x8CA20000u),IOP_RPC_ENTRY(0x10084u,0u),
            IOP_RPC_ENTRY(0x10088u,0x24420001u),IOP_RPC_ENTRY(0x1008Cu,0xACA20000u),
            IOP_RPC_ENTRY(0x10090u,0x03E00008u),IOP_RPC_ENTRY(0x10094u,0x00A01021u)};
#undef IOP_RPC_ENTRY
        std::vector<std::uint8_t> payload(0x200u);
        for(const auto& entry:entries)direct_test::put(payload,entry.address-0x10000u,entry.word);
        direct_test::put(payload,0xC0u,0x41E00000u);direct_test::put(payload,0xC8u,0x101u);
        const std::string library="sifcmd";std::copy(library.begin(),library.end(),payload.begin()+0xCCu);
        direct_test::put(payload,0xD4u,0x03E00008u);direct_test::put(payload,0xD8u,0x24000011u);
        const auto source=sha256_hex(payload),identity=iop_aot_identity(source,entries);
        const IopAotModule native{source,identity,entries};const IopAotRegistration registration(native);
        direct_test::Program program{{{0,5,std::move(payload)}},0,{},{}};
        auto irx=direct_test::elf(program);direct_test::put(irx,16,0x0008FF80u);direct_test::put(irx,24,0);
        runtime.prepare_direct_elf(image,module);
        Ps2RecompIopServices services(*memory,state);auto& iop=const_cast<Iop&>(memory->iop());iop.start_hle_services(&services);
        require(services.load_irx("rpc.irx",irx).startResult==0,"original IRX registers its RPC server through the SDK");
        ps2x::iop::RpcRequest request;request.sid=0x87654321u;
        require(services.rpc_server(request) && request.serverAddress==0x10140u && request.serverBuffer==0x10180u,
            "native SIF binding queries the actual SDK registry and its original IOP descriptor/buffer");
        auto& bus=static_cast<ps2x::iop::IopHost&>(services).nativeMemory();bus.write32(request.serverBuffer,41u);
        const auto eeBefore=memory->read32(hle_test::data);request.receive={hle_test::data,4u};request.send.size=4u;
        const auto [response,bytes]=services.rpc_call(request).value();
        require(bytes==16u && bus.read32(response)==42u && bus.read32(response+4u)==0u &&
            bus.read32(request.serverBuffer)==42u && memory->read32(hle_test::data)==eeBefore && !iop.interpreted_instructions(),
            "original RPC runs in AOT over canonical IOP RAM; EE output remains pending physical SIF0 DMA");
        request.serverAddress+=4u;rejects([&]{(void)services.rpc_call(request);},"registered server");
        iop.stop_hle_services();
    }
    {
        runtime.prepare_direct_elf(image,module);HleKernel kernel(runtime);
        ps2x::iop::detail::IopMemory bus(const_cast<Iop&>(memory->iop()));
        ps2x::iop::detail::IopIoman files(bus);
        struct Driver final : ps2x::iop::detail::IopGuestExecutor {
            ps2x::iop::detail::IopMemory& bus;unsigned calls=0;bool fail=false;
            explicit Driver(ps2x::iop::detail::IopMemory& memory):bus(memory){}
            unsigned executeNativeRpcFunction(unsigned,unsigned,unsigned,unsigned,unsigned,int) override {throw std::logic_error("unit driver cannot run RPC");}
            uint64_t nativeExecutionCycles() const override {throw std::logic_error("unit driver has no scheduler");}
            bool nativeInterruptsEnabled() const override {throw std::logic_error("unit driver has no interrupt controller");}
            unsigned executeGuestFunction(unsigned target,unsigned file,unsigned path,unsigned mode,unsigned,unsigned gp) override {
                ++calls;
                require(gp==0x12345678u,"IOMAN uses the registered driver's GP");
                if(target==0x10000u) {
                    require(bus.read32(file+4u)==12u && bus.readString(path,16u)=="file" && mode==1u,
                        "IOMAN forwards the parsed unit and original path suffix");
                    bus.write32(file+12u,0xAABBCCDDu);return fail?static_cast<unsigned>(-2):0u;
                }
                require(target==0x10004u && bus.read32(file+12u)==0xAABBCCDDu,"close retains the driver's private state");
                return static_cast<unsigned>(-7);
            }
        } driver(bus);
        const char name[]="disc",path[]="  disc12:file",missing[]="absent0:file";
        require(bus.writeRam(0x8000u,name,sizeof name) && bus.writeRam(0x8010u,path,sizeof path) &&
            bus.writeRam(0x8030u,missing,sizeof missing),"IOMAN error fixtures fit canonical RAM");
        bus.write32(0x8100u,0x8000u);bus.write32(0x8110u,0x8200u);
        bus.write32(0x820Cu,0x10000u);bus.write32(0x8210u,0x10004u);
        ps2x::iop::detail::IopCpuState cpu;cpu.gpr[28]=0x12345678u;
        const auto call=[&](unsigned ordinal,unsigned a=0,unsigned b=0,unsigned c=0) {
            cpu.gpr[4]=a;cpu.gpr[5]=b;cpu.gpr[6]=c;
            require(files.dispatchImport(static_cast<uint16_t>(ordinal),cpu,driver),"IOMAN operation is implemented in the SDK");
            return static_cast<int32_t>(cpu.gpr[2]);
        };
        require(call(20,0x8100u)==0 && call(4,0x8030u,1)==-19 && !driver.calls,"unknown devices retain ENODEV without invoking a driver");
        driver.fail=true;require(call(4,0x8010u,1)==-2,"failed Open preserves the driver's error");driver.fail=false;
        for(unsigned fd=0;fd<16u;++fd)require(call(4,0x8010u,1)==static_cast<int>(fd),"failed Open releases its reserved descriptor");
        const auto before=driver.calls;
        require(call(4,0x8010u,1)==-24 && call(5,UINT32_MAX)==-9 && call(6,16,0x9000u,4)==-9 &&
            call(6,0,0x1FFFFFu,4)==-14 && call(8,0,0,3)==-22 && driver.calls==before,
            "exhaustion, invalid descriptors, out-of-RAM reads and invalid seek modes do not reach driver code");
        require(call(5,0)==-7 && call(5,0)==-9 && call(4,0x8010u,1)==0,
            "Close releases the descriptor even when the driver returns an error");
        files.reset();require(call(5,1)==-9 && call(4,0x8010u,1)==-19,"reset invalidates descriptors and device registrations together");
    }
    // File-driver Read blocks in the RPC's original thread until another SDK thread signals it.
    {
        constexpr std::array init{
            0x03E08021u,0x3C040001u,0x348402A0u,0x0C004079u,0u,0x3C080001u,0xAD0202C0u,
            0x3C040001u,0x34840200u,0x0C004065u,0u,0x00402021u,0x0C004067u,0u,
            0x3C040001u,0x34840220u,0x0C004065u,0u,0x00402021u,0x0C004067u,0u,
            0x3C040001u,0x34840700u,0x0C00419Du,0u,
            0x0200F821u,0x03E00008u,0x24020000u};
        constexpr std::array worker{
            0x03E08021u,0x0C004069u,0u,0x00402821u,0x3C040001u,0x348402D0u,0x0C0040C1u,0u,
            0x3C040001u,0x34840240u,0x3C058765u,0x34A54321u,0x3C060001u,0x34C60400u,
            0x3C070001u,0x34E70280u,0x3C080001u,0x350802D0u,0xAFA80018u,0x0C0040C3u,0u,
            0x3C040001u,0x348402D0u,0x0C0040C5u,0u,0x0200F821u,0x03E00008u,0u};
        constexpr std::array handler{
            0x24080003u,0x14880003u,0u,0x08004230u,0u,
            0x24080002u,0x1088000Cu,0u,0x1080000Cu,0u,
            0x3C09000Bu,0x352971B0u,0x2529FFFFu,0x1520FFFEu,0u,
            0x3C020001u,0x34420280u,0x03E00008u,0u,0x1000FFFFu,0u,
            0x27BDFFE0u,0xAFBF001Cu,0x3C040001u,0x34840760u,0x24050001u,0x0C004195u,0u,0xAFA20018u,
            0x00402021u,0x3C050001u,0x34A50280u,0x24060004u,0x0C004199u,0u,0x3C080001u,0xAD0207E0u,
            0x8FA40018u,0x2405FFFBu,0x24060002u,0x0C00419Bu,0u,0x3C080001u,0xAD0207E4u,
            0x8FA40018u,0u,0x0C004197u,0u,0x3C080001u,0xAD0207E8u,
            0x3C020001u,0x34420280u,0x8FBF001Cu,0u,0x03E00008u,0x27BD0020u};
        constexpr std::array driverOpen{
            0x3C080001u,0xAD0407C0u,0xAD0507C4u,0xAD0607C8u,0xAD1C07CCu,0x03E00008u,0x24020000u};
        constexpr std::array driverRead{
            0x27BDFFF0u,0xAFBF000Cu,0xAFA50008u,0x3C080001u,0x8D0402C0u,0u,
            0x24050001u,0x24060010u,0x0C00407Du,0x00003821u,
            0x0C004069u,0u,0x3C080001u,0xAD0207D0u,
            0x8FA80008u,0x2409002Au,0xAD090000u,0x24020004u,0x8FBF000Cu,0u,0x03E00008u,0x27BD0010u};
        constexpr std::array driverSeek{0x03E00008u,0x00A61021u};
        constexpr std::array driverClose{0x03E00008u,0x2402FFF9u};
        constexpr std::array signaler{
            0x03E08021u,0x240403E8u,0x0C00406Bu,0u,0x3C080001u,0x8D0402C0u,0u,0x0C00407Bu,
            0x24050001u,0x08004200u,0u,0u};
        constexpr std::array secondQueue{
            0x0C004069u,0u,0x00402821u,0x3C040001u,0x34840A00u,0x0C0040C1u,0u,
            0x3C040001u,0x34840A20u,0x3C058765u,0x34A54322u,0x3C060001u,0x34C60900u,
            0x3C070001u,0x34E70A80u,0x3C080001u,0x35080A00u,0xAFA80018u,0x0C0040C3u,0u,
            0x3C040001u,0x34840A00u,0x0C0040C5u,0u,0x0200F821u,0x03E00008u,0u};
        constexpr std::array persistent{
            0x27BDFFF0u,0xAFBF000Cu,0x3C080001u,0x8D0402C0u,0u,0x24050002u,0x00003021u,0x00003821u,
            0x0C00407Du,0u,0x8FBF000Cu,0u,0x03E00008u,0x27BD0010u};
        constexpr std::array fastReply{
            0x27BDFFF0u,0xAFBF000Cu,0x10800006u,0u,0x3C080001u,0x8D0402C0u,0u,0x0C00407Bu,0x24050002u,
            0x3C020001u,0x34420A80u,0x2408002Au,0xAC480000u,0x8FBF000Cu,0u,0x03E00008u,0x27BD0010u};
        std::vector<IopAotEntry> entries;
        const auto append=[&](const auto& part){entries.insert(entries.end(),part.begin(),part.end());};
        append(fixed_iop_entries<init,0x10000u>());append(fixed_iop_entries<worker,0x10080u>());
        append(fixed_iop_entries<handler,0x10400u>());append(fixed_iop_entries<signaler,0x10140u>());
        append(fixed_iop_entries<driverOpen,0x10500u>());append(fixed_iop_entries<driverRead,0x10540u>());
        append(fixed_iop_entries<driverSeek,0x105B0u>());append(fixed_iop_entries<driverClose,0x105C0u>());
        append(fixed_iop_entries<secondQueue,0x10800u>());append(fixed_iop_entries<persistent,0x108C0u>());
        append(fixed_iop_entries<fastReply,0x10900u>());
        std::vector<std::uint8_t> payload(0xB00u);
        for(const auto& entry:entries)direct_test::put(payload,entry.address-0x10000u,entry.word);
        const auto imports=[&](unsigned at,const std::string& name,std::initializer_list<unsigned> ordinals){
            direct_test::put(payload,at,0x41E00000u);direct_test::put(payload,at+8u,0x101u);
            std::copy(name.begin(),name.end(),payload.begin()+at+12u);at+=20u;
            for(const auto ordinal:ordinals){direct_test::put(payload,at,0x03E00008u);direct_test::put(payload,at+4u,0x24000000u|ordinal);at+=8u;}
        };
        imports(0x180u,"thbase",{4,6,20,33});imports(0x1D0u,"thevent",{4,6,10});imports(0x2F0u,"sifcmd",{19,17,22});
        imports(0x640u,"ioman",{4,5,6,8,20});
        direct_test::put(payload,0x700u,0x10740u);direct_test::put(payload,0x710u,0x10720u);
        direct_test::put(payload,0x72Cu,0x10500u);direct_test::put(payload,0x730u,0x105C0u);
        direct_test::put(payload,0x734u,0x10540u);direct_test::put(payload,0x73Cu,0x105B0u);
        const std::string driverName="disc",fileName="  disc12:file.irx";
        std::copy(driverName.begin(),driverName.end(),payload.begin()+0x740u);
        std::copy(fileName.begin(),fileName.end(),payload.begin()+0x760u);
        direct_test::put(payload,0x208u,0x10080u);direct_test::put(payload,0x20Cu,0x1000u);direct_test::put(payload,0x210u,0x20u);
        direct_test::put(payload,0x228u,0x10140u);direct_test::put(payload,0x22Cu,0x1000u);direct_test::put(payload,0x230u,0x30u);
        const auto source=sha256_hex(payload),identity=iop_aot_identity(source,entries);
        const IopAotModule native{source,identity,entries};const IopAotRegistration registration(native);
        direct_test::Program program{{{0,5,std::move(payload)}},0,{},{}};
        auto irx=direct_test::elf(program);direct_test::put(irx,16,0x0008FF80u);direct_test::put(irx,24,0);
        runtime.prepare_direct_elf(image,module);
        Ps2RecompIopServices services(*memory,state);auto& iop=const_cast<Iop&>(memory->iop());iop.start_hle_services(&services);
        require(services.load_irx("queued-rpc.irx",irx).startResult==0,"IRX creates its RPC and event-signaling threads in the original kernel");
        services.run_until(iop.cycle()+1024u);
        ps2x::iop::RpcRequest request;request.sid=0x87654321u;
        require(services.rpc_server(request),"original RPC worker registers its queue before sleeping in RpcLoop");
        request.receive={hle_test::data,4u};const auto before=iop.cycle();
        const auto eeBefore=state.cycles();auto pending=services.rpc_call(request);
        require(!pending && !services.rpc_poll() && state.cycles()==eeBefore,
            "queued RPC start returns at its scheduler boundary without executing or completing the callback");
        for(unsigned n=0;!pending && n<1024u;++n) {services.run_until(iop.cycle()+4096u);pending=services.rpc_poll();}
        require(pending.has_value(),"the original blocked file RPC eventually returns through the SDK scheduler");
        const auto [response,bytes]=*pending;
        auto& bus=static_cast<ps2x::iop::IopHost&>(services).nativeMemory();
        require(bus.read32(0x107C4u)==0x10769u && bus.read32(0x107C8u)==1u && bus.read32(0x107D0u)==1u &&
            bus.read32(bus.read32(0x107C0u)+4u)==12u && bus.read32(bus.read32(0x107C0u)+8u)==0u &&
            bus.read32(0x107E0u)==4u && bus.read32(0x107E4u)==static_cast<unsigned>(-3) && bus.read32(0x107E8u)==static_cast<unsigned>(-7),
            "IOMAN forwards path suffix, mode, signed seek and close error, preserving the original worker identity across blocked Read");
        require(bytes==16u && bus.read32(response)==42u && iop.cycle()>before+30000u && !iop.interpreted_instructions() &&
            state.cycles()==iop.cycle()*kEeCyclesPerIopCycle,
            "RPC WaitEventFlag suspends the original worker; SDK schedules the delayed signaler, resumes AOT and keeps the single clock");
        request.function=1u;const auto nativeBefore=iop.native_instructions();
        pending=services.rpc_call(request);require(!pending,"long RPC begins asynchronously");
        for(unsigned n=0;!pending && n<1024u;++n) {services.run_until(iop.cycle()+4096u);pending=services.rpc_poll();}
        require(pending && pending->second==16u && bus.read32(pending->first)==42u &&
                iop.native_instructions()-nativeBefore>2'000'000u && !iop.interpreted_instructions(),
            "an original RPC can finish more than two million AOT instructions across bounded scheduler slices");
        request.function=3u;request.callToken=11u;
        require(!services.rpc_call(request),"an event subscription starts without completing");
        services.run_until(iop.cycle()+4096u);
        require(!services.rpc_poll(),"the subscription remains blocked on its real event");
        ps2x::iop::RpcRequest other;other.sid=0x87654322u;other.callToken=22u;
        require(services.rpc_server(other),"another original AOT thread has its own RPC queue");
        other.receive={hle_test::data+16u,4u};
        require(!services.rpc_call(other),"a second queue starts while the subscription is pending");
        require(iop.hle_events_pending(),"a ready SDK handler is canonical work even before its SIF completion exists");
        ps2x::iop::RpcRequest completed;
        pending.reset();
        for(unsigned n=0;!pending && n<64u;++n){services.run_until(iop.cycle()+1024u);pending=services.rpc_poll(&completed);}
        require(pending && completed.callToken==22u && completed.sid==other.sid && bus.read32(pending->first)==42u && !services.rpc_poll(),
            "the ready queue completes out of order with its own request identity, leaving the subscription pending");
        other.function=1u;other.callToken=33u;
        require(!services.rpc_call(other),"the second queue can signal the original subscription's event");
        unsigned returned=0;
        for(unsigned n=0;returned!=3u && n<64u;++n) {
            services.run_until(iop.cycle()+1024u);
            while(auto done=services.rpc_poll(&completed)) {
                require(bus.read32(done->first)==42u,"concurrent replies preserve the real return buffer");
                if(completed.callToken==11u)returned|=1u;
                else {require(completed.callToken==33u,"reply token belongs to its original queue");returned|=2u;}
            }
        }
        require(returned==3u && !iop.interpreted_instructions(),"both original RPC workers resume and complete on the shared AOT scheduler");
        request.callToken=0u;
        request.function=2u;pending=services.rpc_call(request);require(!pending,"non-returning RPC remains pending");
        const auto bounded=iop.cycle()+1024u;services.run_until(bounded);
        require(iop.cycle()<=bounded+1u && !services.rpc_poll(),
            "a non-returning callback cannot monopolize the host beyond the requested scheduler slice");
        rejects([&]{(void)services.rpc_call(request);},"unfinished call");
        iop.stop_hle_services();
    }
    // SDK alarms execute fixed-word callbacks on the shared clock, with normal and interrupt-safe cancellation.
    {
        constexpr std::array init{
            0x03E08021u,0x3C040001u,0x34840120u,0x3C050001u,0x34A50080u,0x3C060001u,0x34C601C0u,
            0x0C004055u,0u,0x3C080001u,0x8D0801D0u,0u,0x11000007u,0u,
            0x3C040001u,0x34840080u,0x3C050001u,0x34A501C0u,0x0C004057u,0u,0x0200F821u,0x03E00008u,0u};
        constexpr std::array callback{
            0x03E08021u,0x00808821u,0x8E280000u,0u,0x25080001u,0xAE280000u,0x0C004065u,0u,0xAE220004u,
            0x8E280008u,0u,0x11000006u,0u,0x3C040001u,0x34840080u,0x02202821u,0x0C004057u,0u,
            0x8E280000u,0u,0x29020002u,0x00021880u,0x00431021u,0x0200F821u,0x03E00008u,0u};
        std::vector<IopAotEntry> entries;
        const auto append=[&](const auto& part){entries.insert(entries.end(),part.begin(),part.end());};
        append(fixed_iop_entries<init,0x10000u>());append(fixed_iop_entries<callback,0x10080u>());
        const auto source=sha256_hex(m20::le_words(std::vector<std::uint32_t>(callback.begin(),callback.end()))),identity=iop_aot_identity(source,entries);
        const IopAotModule native{source,identity,entries};const IopAotRegistration registration(native);
        for(unsigned mode=0;mode<4u;++mode) {
            std::vector<std::uint8_t> payload(0x200u);
            for(const auto& entry:entries)direct_test::put(payload,entry.address-0x10000u,entry.word);
            direct_test::put(payload,0x120u,80u);direct_test::put(payload,0x140u,0x41E00000u);direct_test::put(payload,0x148u,0x101u);
            const std::string thbase="thbase",intrman="intrman";
            std::copy(thbase.begin(),thbase.end(),payload.begin()+0x14Cu);
            direct_test::put(payload,0x154u,0x03E00008u);direct_test::put(payload,0x158u,0x24000000u|(mode==1u?36u:35u));
            direct_test::put(payload,0x15Cu,0x03E00008u);direct_test::put(payload,0x160u,0x24000000u|((mode==1u || mode==3u)?38u:37u));
            direct_test::put(payload,0x180u,0x41E00000u);direct_test::put(payload,0x188u,0x102u);
            std::copy(intrman.begin(),intrman.end(),payload.begin()+0x18Cu);
            direct_test::put(payload,0x194u,0x03E00008u);direct_test::put(payload,0x198u,0x24000003u);
            direct_test::put(payload,0x1D0u,mode==1u || mode==2u?1u:0u);direct_test::put(payload,0x1C8u,mode==3u?1u:0u);
            direct_test::Program program{{{0,5,std::move(payload)}},0,{},{}};
            auto irx=direct_test::elf(program);direct_test::put(irx,16,0x0008FF80u);direct_test::put(irx,24,0);
            runtime.prepare_direct_elf(image,module);
            Ps2RecompIopServices services(*memory,state);auto& iop=const_cast<Iop&>(memory->iop());iop.start_hle_services(&services);
            require(services.load_irx("alarm.irx",irx).startResult==0,"IRX schedules or cancels its alarm through the original SDK kernel");
            auto& bus=static_cast<ps2x::iop::IopHost&>(services).nativeMemory();
            if(mode==0u) {
                require(services.has_native_work(),"a pending alarm prevents native idle skipping even without a thread");
                bus.setInterruptControl(0u);services.run_until(200u);
                require(!bus.read32(0x101C0u),"I_CTRL masks alarm delivery without dropping its deadline");
                bus.setInterruptControl(1u);services.run_until(iop.cycle()+1u);
                require(bus.read32(0x101C0u)==1u && bus.read32(0x101C4u)==1u,"alarm callback runs in interrupt context when unmasked");
                services.run_until(iop.cycle()+4u);require(bus.read32(0x101C0u)==1u,"nonzero callback result delays the repeat in IOP ticks");
            }
            services.run_until(1000u);
            require(bus.read32(0x101C0u)==(mode==0u?2u:mode==3u?1u:0u) && !services.has_native_work() && !iop.interpreted_instructions() &&
                state.cycles()==iop.cycle()*kEeCyclesPerIopCycle,
                "SetAlarm/iSetAlarm, CancelAlarm/iCancelAlarm, repeat, self-cancel and single-clock AOT behavior");
            ps2x::iop::detail::IopKernel kernel(bus);ps2x::iop::detail::IopCpuState arguments;
            arguments.gpr[5]=0u;require(kernel.dispatchThreadImport(35u,arguments,iop.cycle()) && static_cast<std::int32_t>(arguments.gpr[2])==-402,
                "SetAlarm rejects an invalid callback with the kernel illegal-entry code");
            arguments.gpr[4]=0x10080u;arguments.gpr[5]=0x101C0u;
            require(kernel.dispatchThreadImport(37u,arguments,iop.cycle()) && static_cast<std::int32_t>(arguments.gpr[2])==-105,
                "CancelAlarm preserves the handler-not-found result");
            iop.stop_hle_services();
        }
    }
    // CDVD and DMA3 completions enter the SDK's intrman even when every IOP thread is asleep.
    for(const unsigned irq:{2u,35u,17u}) {
        const bool dma=irq==35u;
#define IOP_IRQ_ENTRY(pc,word) IopAotEntry{pc,word,[](Iop& cpu){cpu.execute_native<word>();}}
        const std::array entries{
            IOP_IRQ_ENTRY(0x10004u,0x24040023u),IOP_IRQ_ENTRY(0x10020u,0x24040023u),
            IOP_IRQ_ENTRY(0x10004u,0x24040011u),IOP_IRQ_ENTRY(0x10020u,0x24040011u),
            IOP_IRQ_ENTRY(0x10000u,0x03E08021u),IOP_IRQ_ENTRY(0x10004u,0x24040002u),
            IOP_IRQ_ENTRY(0x10008u,0x00002821u),IOP_IRQ_ENTRY(0x1000Cu,0x3C060001u),
            IOP_IRQ_ENTRY(0x10010u,0x34C60080u),IOP_IRQ_ENTRY(0x10014u,0x24070055u),
            IOP_IRQ_ENTRY(0x10018u,0x0C004035u),IOP_IRQ_ENTRY(0x1001Cu,0u),
            IOP_IRQ_ENTRY(0x10020u,0x24040002u),IOP_IRQ_ENTRY(0x10024u,0x0C004037u),
            IOP_IRQ_ENTRY(0x10028u,0u),IOP_IRQ_ENTRY(0x1002Cu,0x0200F821u),
            IOP_IRQ_ENTRY(0x10030u,0x03E00008u),IOP_IRQ_ENTRY(0x10034u,0x24020000u),
            IOP_IRQ_ENTRY(0x10080u,0x3C080001u),IOP_IRQ_ENTRY(0x10084u,0xAD040180u),
            IOP_IRQ_ENTRY(0x10088u,0x3C08BF40u),IOP_IRQ_ENTRY(0x1008Cu,0x240900FFu),
            IOP_IRQ_ENTRY(0x10090u,0xA1092008u),IOP_IRQ_ENTRY(0x10094u,0x03E00008u),
            IOP_IRQ_ENTRY(0x10098u,0u)};
#undef IOP_IRQ_ENTRY
        std::vector<std::uint8_t> payload(0x200u);
        for(const auto& entry:entries)direct_test::put(payload,entry.address-0x10000u,entry.word);
        direct_test::put(payload,4u,0x24040000u|irq);
        direct_test::put(payload,0x20u,0x24040000u|irq);
        direct_test::put(payload,0xC0u,0x41E00000u);direct_test::put(payload,0xC8u,0x102u);
        const std::string library="intrman";std::copy(library.begin(),library.end(),payload.begin()+0xCCu);
        direct_test::put(payload,0xD4u,0x03E00008u);direct_test::put(payload,0xD8u,0x24000004u);
        direct_test::put(payload,0xDCu,0x03E00008u);direct_test::put(payload,0xE0u,0x24000006u);
        const auto source=sha256_hex(payload),identity=iop_aot_identity(source,entries);
        const IopAotModule native{source,identity,entries};const IopAotRegistration registration(native);
        direct_test::Program program{{{0,5,std::move(payload)}},0,{},{}};
        auto irx=direct_test::elf(program);direct_test::put(irx,16,0x0008FF80u);direct_test::put(irx,24,0);
        runtime.prepare_direct_elf(image,module);
        Ps2RecompIopServices services(*memory,state);auto& iop=const_cast<Iop&>(memory->iop());iop.start_hle_services(&services);
        iop.mount_disc(std::make_shared<DiscImage>(cdvdDisc),Iop::DiscMedium::Ps2Cd);
        require(services.load_irx("irq.irx",irx).startResult==0,"IRX registers and enables its IRQ through original intrman");
        auto& bus=static_cast<ps2x::iop::IopHost&>(services).nativeMemory();
        if(irq==17u) {
            services.run_until(iop.cycle()+100u);
            require(!iop.stopped() && !(iop.diagnostic_istat()&(1u<<17u)) && iop.native_instructions()==14u,
                "the SDK reset establishes idle SIO2 interrupt status before a real IRX enables IRQ17");
            iop.stop_hle_services();continue;
        }
        bus.setInterruptControl(0u);
        const auto submit=[&] {
            if(!dma){iop.ee_cdvd_write(0x1F402004u,1u,0u);return;}
            iop.ee_cdvd_write(0x1F402006u,1u,0x80u);
            bus.write32(0x1F8010F0u,0x8000u);bus.write32(0x1F801578u,1u);bus.write32(0x1F80157Cu,1u);
            bus.write32(0x1F8010B0u,0x9000u);bus.write32(0x1F8010B4u,0x10200u);bus.write32(0x1F8010B8u,0x41000200u);
            for(unsigned parameter=0;parameter<11u;++parameter)iop.ee_cdvd_write(0x1F402005u,1u,parameter==4u?1u:0u);
            iop.ee_cdvd_write(0x1F402004u,1u,6u);
        };
        const auto mask=dma?8u:4u;submit();services.run_until(iop.cycle()+(dma?100000u:100u));
        require(bus.read32(0x10180u)==0u && (iop.diagnostic_istat()&mask) && iop.ee_cdvd_read(0x1F402008u,1u)==1u && iop.native_instructions()==14u &&
            (!dma || ((bus.read32(0x1F8010F4u)&0x08080000u)==0x08080000u && !(bus.read32(0x1F8010B8u)&0x01000000u))),
            "the physical completion stays pending while I_CTRL masks delivery, without an invented callback");
        bus.setInterruptControl(1u);services.run_until(iop.cycle()+1u);
        require(bus.read32(0x10180u)==0x55u && !(iop.diagnostic_istat()&mask) && iop.native_instructions()==21u &&
            (!dma || !(bus.read32(0x1F8010F4u)&0x08000000u)) &&
            !iop.interpreted_instructions() && bus.interruptControl()==1u,
            "SDK dispatches the real CDVD IRQ in AOT while idle, preserves I_CTRL and acknowledges the physical controller");
        if(dma) {
            bus.write32(0x10180u,0u);bus.write32(0x1F8010F4u,(bus.read32(0x1F8010F4u)&0x00FFFFFFu)&~(1u<<19u));
            submit();services.run_until(iop.cycle()+100000u);
            require(!bus.read32(0x10180u) && iop.native_instructions()==21u,
                "a raw DICR channel mask suppresses DMA callbacks even when the SDK registry remains enabled");
        }
        iop.stop_hle_services();
    }
    // Real IRX imports enter the original SDK kernel; its child runs through fixed-word AOT.
    {
#define IOP_SDK_ENTRY(pc,word) IopAotEntry{pc,word,[](Iop& cpu){cpu.execute_native<word>();}}
        const std::array entries{
            IOP_SDK_ENTRY(0x10000u,0x03E08021u),IOP_SDK_ENTRY(0x10004u,0x3C040001u),
            IOP_SDK_ENTRY(0x10008u,0x34840100u),IOP_SDK_ENTRY(0x1000Cu,0x0C004035u),
            IOP_SDK_ENTRY(0x10010u,0u),IOP_SDK_ENTRY(0x10014u,0x00402021u),
            IOP_SDK_ENTRY(0x10018u,0x3C080001u),IOP_SDK_ENTRY(0x1001Cu,0x0C004037u),
            IOP_SDK_ENTRY(0x10020u,0x8D050184u),IOP_SDK_ENTRY(0x10024u,0x0200F821u),
            IOP_SDK_ENTRY(0x10028u,0x03E00008u),IOP_SDK_ENTRY(0x1002Cu,0x24020000u),
            IOP_SDK_ENTRY(0x10080u,0x3C080001u),IOP_SDK_ENTRY(0x10084u,0xAD040180u),
            IOP_SDK_ENTRY(0x10088u,0x03E00008u),IOP_SDK_ENTRY(0x1008Cu,0u)};
#undef IOP_SDK_ENTRY
        std::vector<std::uint8_t> payload(0x200u);
        for(const auto& entry:entries)direct_test::put(payload,entry.address-0x10000u,entry.word);
        direct_test::put(payload,0xC0u,0x41E00000u);direct_test::put(payload,0xC8u,0x100u);
        const std::string library="thbase";std::copy(library.begin(),library.end(),payload.begin()+0xCCu);
        direct_test::put(payload,0xD4u,0x03E00008u);direct_test::put(payload,0xD8u,0x24000004u);
        direct_test::put(payload,0xDCu,0x03E00008u);direct_test::put(payload,0xE0u,0x24000006u);
        direct_test::put(payload,0x108u,0x10080u);direct_test::put(payload,0x10Cu,0x1000u);
        direct_test::put(payload,0x110u,0x20u);direct_test::put(payload,0x184u,42u);
        const auto source=sha256_hex(payload),identity=iop_aot_identity(source,entries);
        const IopAotModule native{source,identity,entries};
        const IopAotRegistration registration(native);
        runtime.prepare_direct_elf(image,module);
        {
            ps2x::iop::detail::IopMemory bus(const_cast<Iop&>(memory->iop()));
            ps2x::iop::detail::IopKernel sourceKernel(bus);
            ps2x::iop::detail::IopCpuState cpu;cpu.gpr[4].value=0x10100u;cpu.gpr[4].unknown=1u;
            rejects([&]{(void)sourceKernel.dispatchThreadImport(4,cpu,0);},"unknown register bits");
            require(!sourceKernel.threadCount(),"SDK imports cannot consume unknown native arguments or create a phantom thread");
        }
        for(const unsigned mode:{0u,1u,2u}) {
            const bool prepared=mode!=0;
            runtime.prepare_direct_elf(image,module);
            Ps2RecompIopServices services(*memory,state);auto& iop=const_cast<Iop&>(memory->iop());
            iop.start_hle_services(&services);
            auto& bus=static_cast<ps2x::iop::IopHost&>(services).nativeMemory();
            require(bus.interruptMask()==0u,"SDK zero-valued device reads use the actual IOP controller");
            rejects([&]{(void)bus.read16(0x1F801078u);},"only 32-bit");
            require(bus.interruptControl()==1u && bus.interruptControl()==0u,"SDK I_CTRL reads retain the canonical read-clear side effect");
            bus.setInterruptControl(1u);
            rejects([&]{(void)bus.read32(0x1F8010F0u);},"undefined device bits");
            rejects([&]{bus.write8(0xC0000000u,0);},"KSEG2");
            auto bytes=payload;if(!prepared)direct_test::put(bytes,0,0x24020001u);
            if(mode==2u)direct_test::put(bytes,0xD8u,0x240000FFu);
            direct_test::Program program{{{0,5,std::move(bytes)}},0,{},{}};
            auto irx=direct_test::elf(program);direct_test::put(irx,16,0x0008FF80u);direct_test::put(irx,24,0);
            if(mode==2u) {
                rejects([&]{(void)services.load_irx("unsupported-import.irx",irx);},"unhandled import thbase:255");
                require(iop.native_instructions()==5u && !iop.interpreted_instructions() && !services.executing_cpu(),
                    "unsupported SDK imports stop at the import and restore the active CPU pointer instead of reporting success");
            } else if(!prepared) {
                rejects([&]{(void)services.load_irx("synthetic.irx",irx);},"missing IOP AOT");
                require(!iop.native_instructions() && !iop.interpreted_instructions() &&
                    iop.native_module_range(0x10000u)==std::pair{0x10000u,0x200u},
                    "unprepared IRX stops with its actual loaded region and no interpreter fallback");
            } else {
                const auto result=services.load_irx("synthetic.irx",irx);
                require(result.moduleId==1 && result.startResult==0 && iop.native_instructions()==12u,
                    "original SDK loader starts IRX and original kernel creates/starts the child through real imports");
                services.run_until(iop.cycle()+32u);
                require(iop.diagnostic_ram32(0x10180u)==42u && iop.native_instructions()==16u &&
                    !iop.interpreted_instructions() && !state.retired_instructions && state.cycles()==iop.cycle()*kEeCyclesPerIopCycle,
                    "SDK IOP scheduler preserves a load in the call delay slot and executes its AOT child on the single clock");
            }
            iop.stop_hle_services();
        }
    }
    runtime.prepare_direct_elf(image,module);
    {
        HleKernel kernel(runtime);std::uint64_t executed=0;
        ExecutionContext ctx{state,*memory,executed,100,UINT32_MAX};
        const auto call=[&](int number,unsigned a=0,unsigned b=0,unsigned c=0,unsigned d=0){
            state.set_reg32(3,number);state.set_reg32(4,a);state.set_reg32(5,b);
            state.set_reg32(6,c);state.set_reg32(7,d);execute_ee_system<MipsOpcode::Syscall>(ctx,12);
            return static_cast<std::int32_t>(state.reg32(2));
        };
        const auto semaphore=call(0x40,hle_test::data);
        require(semaphore==1 && call(0x12,5,hle_test::text+0x700,0,semaphore)>0 && call(0x16,5)==1,
            "SIF response callback uses a real kernel semaphore");
        call(0x78);
        const auto packet=hle_test::data+0x800u,transfer=hle_test::data+0x880u,reply=hle_test::data+0x900u;
        for(unsigned offset=0;offset<32u;offset+=4u)memory->write32(packet+offset,0);
        memory->write32(packet,20u);memory->write32(packet+8u,0x80000002u);memory->write32(packet+16u,reply);
        memory->write32(transfer,packet);memory->write32(transfer+4u,0x1000u);
        memory->write32(transfer+8u,20u);memory->write32(transfer+12u,0x44u);
        require(call(0x77,transfer,1)>0,"The asynchronous command enters the real SIF DMA");
        require(!memory->iop().idle_loop_horizon(),"pending SIF commands prevent idle time skipping");
        const auto resume=state.pc+4u;const auto retired=state.retired_instructions,cycles=state.cycles();
        call(0x44,semaphore);
        require(state.pc==hle_test::text+0x700 && state.retired_instructions==retired+1u && state.cycles()>cycles+1u,
            "A blocked thread advances only device time until the actual response IRQ, without retiring an idle instruction");
        BootOptions options;options.max_instructions=64;options.stop_at=resume;
        const auto report=run_ee_boot(state,*memory,module,options);
        require(report.outcome==BootOutcome::StopAddress && report.aot_instructions==5 && !report.interpreted_instructions &&
            state.reg32(2)==static_cast<std::uint32_t>(semaphore) && kernel.current_thread_id()==1 && memory->read32(reply+20u)==1,
            std::string("The native IRQ signals the semaphore and resumes the waiting thread after actual DMA completion: ")+
            boot_outcome_name(report.outcome)+" pc="+std::to_string(state.pc)+" aot="+std::to_string(report.aot_instructions)+
            " v0="+std::to_string(state.reg32(2))+" thread="+std::to_string(kernel.current_thread_id())+
            " reply="+std::to_string(memory->read32(reply+20u))+" reason="+report.reason);
    }
    for(const bool withBios:{false,true}) {
        runtime.prepare_direct_elf(image,module);
        HleKernel kernel(runtime);
        memory->mount_disc(std::make_shared<DiscImage>(cdvdDisc),Iop::DiscMedium::Ps2Cd);
        auto syntheticBoard=*find_console_hardware("SCPH-70004:GH-032-32");syntheticBoard.romver=m20::kRomver;
        if(withBios) {
            m20::File cdvd{"CDVDMAN",residentIrX,{}},fsv{"CDVDFSV",residentIrX,{}};
            m20::field(cdvd.extinfo,2,0x1234,{});m20::field(fsv.extinfo,2,0x2345,{});
            direct_test::Program probe{{{0,5,m20::le_words({0x40087800u,0u,0x3C090001u,0xAD281000u,0x03E00008u,0x24020000u})}},0,{},{}};
            auto dmaIrX=direct_test::elf(probe);direct_test::put(dmaIrX,16,0x0008FF80u);direct_test::put(dmaIrX,24,0);
            const m20::File dma{"DMACMAN",std::move(dmaIrX),{}};
            const m20::File modload{"MODLOAD",residentIrX,{}},loadfile{"LOADFILE",residentIrX,{}};
            const std::array modules{dma,cdvd,fsv,modload,loadfile};
            kernel.configure_bios(std::make_shared<const BiosImage>(load_bios_rom0(m20::build_firmware(m20::Stage::M20,modules).rom)));
            rejects([&]{configure_hle_console(*memory,find_console_hardware("SCPH-70004:GH-032-32"),nullptr);},"ROMVER differ");
            configure_hle_console(*memory,&syntheticBoard,nullptr);
            auto& iop=const_cast<Iop&>(memory->iop());
            iop.ee_cdvd_write(0x1F402017u,1u,0u);iop.ee_cdvd_write(0x1F402016u,1u,3u);
            for(const auto byte:syntheticBoard.mechacon->version)require(iop.ee_cdvd_read(0x1F402018u,1u)==byte,
                "HLE console binding supplies the selected MECHACON metadata to the single physical device");
        }
        std::uint64_t executed=0;ExecutionContext ctx{state,*memory,executed,100,UINT32_MAX};
        const auto call=[&](int number,unsigned a=0,unsigned b=0){
            state.set_reg32(3,number);state.set_reg32(4,a);state.set_reg32(5,b);
            execute_ee_system<MipsOpcode::Syscall>(ctx,12);return static_cast<std::int32_t>(state.reg32(2));
        };
        const auto packet=hle_test::data+0x800u,transfer=hle_test::data+0x880u;
        const auto reply=hle_test::data+0x900u,arguments=hle_test::data+0xA00u,result=hle_test::data+0xB00u;
        unsigned iopPayload=0x2100u;
        const auto send=[&](unsigned bytes,bool payload=false){
            call(0x78);
            const auto descriptor=[&](unsigned at,unsigned source,unsigned destination,unsigned size,unsigned flags){
                memory->write32(at,source);memory->write32(at+4u,destination);memory->write32(at+8u,size);memory->write32(at+12u,flags);
            };
            if(payload)descriptor(transfer,arguments,iopPayload,4u,0u);
            descriptor(transfer+(payload?16u:0u),packet,0x1000u,bytes,0x44u);
            require(call(0x77,transfer,payload?2u:1u)>0,"RPC packet uses real SIF submission");
            state.stall_cycles+=1000u*kEeCyclesPerIopCycle;(void)memory->read32(0xB000C400u);
        };
        for(unsigned offset=0;offset<64u;offset+=4u)memory->write32(packet+offset,0);
        memory->write32(packet,20u);memory->write32(packet+8u,0x80000002u);memory->write32(packet+16u,reply);send(20u);
        memory->write32(packet,64u);memory->write32(packet+8u,0x80000009u);memory->write32(packet+16u,5u);
        memory->write32(packet+20u,packet);memory->write32(packet+24u,7u);memory->write32(packet+28u,arguments);
        memory->write32(packet+32u,0x80000592u);send(64u);
        require(memory->read32(reply+8u)==0x80000008u && memory->read32(reply+32u)==0x80000009u &&
            memory->read32(reply+36u)==0x2000u && memory->read32(reply+40u)==0x2100u,
            "The actual mounted CDVD provider registers its descriptor before a real RPC bind response");
        memory->write32(arguments,0);memory->write32(result,0xDEADBEEFu);
        memory->write32(packet+8u,0x8000000Au);memory->write32(packet+32u,0);
        memory->write32(packet+36u,4u);memory->write32(packet+40u,result);memory->write32(packet+44u,4u);
        memory->write32(packet+48u,1u);memory->write32(packet+52u,0x2000u);send(64u,true);
        require(!memory->iop().stopped(),memory->iop().stop_reason().value_or("PS2Recomp CDVD RPC"));
        require(memory->read32(result)==1u && memory->read32(reply+32u)==0x8000000Au &&
            memory->iop().sif0_metrics().bytes==176u && memory->iop().native_instructions()==(withBios?18u:0u) && !memory->iop().interpreted_instructions(),
            "CDVD initialization uses actual SIF0 DMA and counts the selected BIOS IRX entry only when executed");
        if(withBios)require(memory->iop().diagnostic_ram32(0x11000u)==syntheticBoard.iop_prid,
            "SDK native contexts preserve the console's measured read-only IOP PRId instead of a zero-valued thread default");
        memory->write32(packet+8u,0x80000009u);memory->write32(packet+32u,0x80000003u);send(64u);
        require(memory->read32(reply+36u)==0x2200u && memory->read32(reply+40u)==0x2300u,
            "The upstream allocator owns a registered heap RPC endpoint in the same IOP");
        iopPayload=0x2300u;memory->write32(packet+8u,0x8000000Au);memory->write32(packet+52u,0x2200u);
        memory->write32(packet+32u,1u);memory->write32(arguments,128u);send(64u,true);
        const auto allocation=memory->read32(result);
        require(allocation>=(withBios?0x10500u:0x10000u) && memory->iop().diagnostic_ram32(allocation)==0u,
            "PS2Recomp allocates the same IOP RAM initialized by the SDK session");
        memory->write32(arguments,UINT32_MAX);send(64u,true);require(memory->read32(result)==0,
            "An oversized allocation cannot overflow or consume the valid heap block");
        memory->write32(packet+32u,2u);memory->write32(arguments,allocation);send(64u,true);
        require(memory->read32(result)==0,"Heap RPC releases the actual upstream allocation");
        send(64u,true);require(memory->read32(result)==UINT32_MAX,"Double free retains the upstream error");
        iopPayload=0x2100u;memory->write32(packet+52u,0x2000u);memory->write32(packet+32u,0);memory->write32(arguments,0);
        memory->write32(packet+44u,16u);
        if(withBios) {
            send(64u,true);
            require(memory->read32(result)==1u && memory->read32(result+4u)==0x2345u &&
                memory->read32(result+8u)==0x1234u && memory->read32(result+12u)==0,
                "CDVD versions come from the validated selected ROMDIR/EXTINFO, without a copied BIOS or invented module revision");
        } else rejects([&]{send(64u,true);},"CDVD version metadata is unavailable");
    }
    for(const bool validImage:{false,true}) {
        runtime.prepare_direct_elf(image,module);HleKernel kernel(runtime);
        const std::array modules{m20::File{"UDNL",std::vector<std::uint8_t>(16),{}}};
        kernel.configure_bios(std::make_shared<const BiosImage>(load_bios_rom0(m20::build_firmware(m20::Stage::M20,modules).rom)));
        memory->mount_disc(std::make_shared<DiscImage>(cdvdDisc),Iop::DiscMedium::Ps2Cd);
        auto& iop=const_cast<Iop&>(memory->iop());iop.ee_write(0x10000u,4,0x12345678u);
        memory->write32(hle_test::data+0x200u,0xCAFEBABEu);state.set_reg64(16,0xABCDEFu);
        const auto packet=hle_test::data+0x800u,transfer=hle_test::data+0x880u;
        const std::string arguments=validImage?"rom0:UDNL cdrom0:\\IOPRP.IMG;1":"rom0:UDNL cdrom0:\\IOPRP.IMG;2";
        for(unsigned offset=0;offset<112u;offset+=4u)memory->write32(packet+offset,0);
        memory->write32(packet,104u);memory->write32(packet+8u,0x80000003u);
        memory->write32(packet+16u,static_cast<std::uint32_t>(arguments.size()+1u));
        for(unsigned i=0;i<arguments.size();++i)memory->write8(packet+24u+i,arguments[i]);
        memory->write32(transfer,packet);memory->write32(transfer+4u,0x1000u);
        memory->write32(transfer+8u,104u);memory->write32(transfer+12u,0x44u);
        std::uint64_t executed=0;ExecutionContext ctx{state,*memory,executed,100,UINT32_MAX};
        state.set_reg32(3,0x77);state.set_reg32(4,transfer);state.set_reg32(5,1);
        execute_ee_system<MipsOpcode::Syscall>(ctx,12);
        const auto retired=state.retired_instructions,previousCycle=iop.cycle();state.stall_cycles+=1000u*kEeCyclesPerIopCycle;
        if(validImage) {
            require((memory->read32(0xB000F230u)&0x70000u)==0x70000u && iop.hle_services_active() &&
                iop.cycle()>previousCycle && iop.diagnostic_ram32(0x10000u)==0x24020000u && iop.diagnostic_ram32(0x1000u)==0u &&
                iop.diagnostic_ram32(0x10200u)==0x24020000u && iop.native_instructions()==9u && !iop.interpreted_instructions(),
                "Verified IOPRP starts its real IRX entry through the SDK loader and AOT before BOOTEND, without rewinding time");
            require(state.retired_instructions==retired && state.reg64(16)==0xABCDEFu &&
                memory->read32(hle_test::data+0x200u)==0xCAFEBABEu && memory->iop().ee_dma_read(0x1000E000u)==1u &&
                iop.native_instructions()==9u && !iop.interpreted_instructions(),
                "IOP service reset preserves EE CPU/RAM/DMAC and counts only actual IRX AOT execution");
        } else {
            rejects([&]{(void)memory->read32(0xB000F230u);},"IOP reboot image rejected");
            require(iop.diagnostic_ram32(0x10000u)==0x12345678u &&
                memory->read32(hle_test::data+0x200u)==0xCAFEBABEu,"A missing ISO revision cannot erase the previous IOP/EE state");
        }
    }
    // Unsupported services and symbolic descriptors must fail before publication.
    runtime.prepare_direct_elf(image,module);
    {
        HleKernel kernel(runtime);std::uint64_t executed=0;
        ExecutionContext ctx{state,*memory,executed,100,UINT32_MAX};
        const auto call=[&](int number,std::uint32_t a,std::uint32_t b) {
            state.set_reg32(3,number);state.set_reg32(4,a);state.set_reg32(5,b);
            execute_ee_system<MipsOpcode::Syscall>(ctx,12);
        };
        call(0x74,0x7F,hle_test::text+0x601);
        require(static_cast<std::int32_t>(state.reg32(2))==-1,"unaligned syscall handler is rejected");
        call(0x74,0x7F,hle_test::text+0x600);
        require(state.reg32(2)==0,"SetSyscall installs an executable guest handler");
        require(memory->read32((0x80011F80u+0x7Fu*4u))==hle_test::text+0x600,
            "SetSyscall publishes the actual handler in canonical kernel RAM");
        call(0x74,256,0);require(static_cast<std::int32_t>(state.reg32(2))==-1 &&
            memory->read32((0x80011F80u+0x7Fu*4u))==hle_test::text+0x600,"out-of-range syscall index preserves the table");
        state.set_reg64(16,0xCAFEBABE);state.set_reg64(31,0xDEAD);const auto cycles=state.cycles();
        const auto resume=state.pc+4;call(0x7F,0,0);
        require(state.pc==hle_test::text+0x600,"installed service enters original guest AOT code");
        BootOptions options;options.max_instructions=100;options.stop_at=resume;
        const auto report=run_ee_boot(state,*memory,module,options);
        require(report.outcome==BootOutcome::StopAddress && !report.interpreted_instructions && report.aot_instructions==6,
            "native handler recursively calls the builtin and returns without a guest trampoline instruction");
        require(state.reg32(2)==0x1234 && state.reg64(16)==0xCAFEBABE && state.reg64(31)==0xDEAD && state.cycles()==cycles+7,
            "handler return preserves caller registers and the one guest clock, publishing V0");
        memory->write32((0x80011F80u+0x7Fu*4u),0);call(0x7F,0,0);
        require(state.reg32(2)==Memory::kMainRamBytes,"removing an override restores the builtin service");
    }
    runtime.prepare_direct_elf(image,module);
    {
        HleKernel kernel(runtime);std::uint64_t executed=0;
        ExecutionContext ctx{state,*memory,executed,100,UINT32_MAX};
        const auto call=[&](int number,std::uint32_t a=0,std::uint32_t b=0) {
            state.set_reg32(3,number);state.set_reg32(4,a);state.set_reg32(5,b);
            execute_ee_system<MipsOpcode::Syscall>(ctx,12);return static_cast<std::int32_t>(state.reg32(2));
        };
        require(call(0x79,0x80000001u,hle_test::data)==0 &&
            memory->read32(0x80010404u)==hle_test::data && call(0x7A,0x80000001u)==static_cast<int>(hle_test::data),
            "SIF software registers use the kernel RAM, without host-global state");
        memory->write32(0x80010404u,0x1234);
        require(call(0x79,0x80000001u,0x5678)==0x1234,"SIF software SetReg returns the actual previous value");
        require(call(0x79,1,0xDEADBEEF)==0 && memory->iop().ee_sif_read(0)==0xDEADBEEF,
            "SIF hardware writes reach the canonical device");
        require(call(0x7A,4)==0x30000 && !memory->iop().stopped() && memory->iop().hle_services_active() &&
            call(0x7A,2)==0x1000 && memory->iop().diagnostic_ram32(0x1000)==0,
            "Explicit IOP HLE initializes canonical buffers and receiving DMA before publishing CMD readiness");
        const auto idle=memory->iop().idle_loop_horizon();
        require(idle && *idle>=memory->iop().cycle(),"quiescent HLE provides a due or future canonical device horizon");
        call(0x78);call(-0x78);
        require(memory->iop().ee_dma_read(0x1000C000u)==0x184u &&
            !(memory->iop().ee_dma_read(0x1000E010u)&0x20u) && !memory->iop().sif0_metrics().bytes,
            "SifSetDChain arms the existing destination channel without completing a transfer");
        require(call(0x6B)==0 && !memory->iop().ee_dma_read(0x1000C000u) &&
            !(memory->iop().ee_dma_read(0x1000E010u)&0x20u) && !memory->iop().sif0_metrics().bytes,
            "SifStopDma actually stops the idle SIF0 receiver without fabricating DMA completion");
        call(0x78);

        const auto packet=hle_test::data+0x800u,transfer=hle_test::data+0x880u,reply=hle_test::data+0x900u;
        for(unsigned offset=0;offset<32;offset+=4u)memory->write32(packet+offset,0);
        memory->write32(packet,20u);memory->write32(packet+8u,0x80000002u);memory->write32(packet+16u,reply);
        memory->write32(transfer,packet);memory->write32(transfer+4u,0x1000u);
        memory->write32(transfer+8u,20u);memory->write32(transfer+12u,0x44u);
        require(call(0x77,transfer,1)>0,"CMD initialization submits through the existing EE DMA");
        for(unsigned cycle=0;!memory->iop().ee_dma_read(0x1000C020u) && cycle<256u;++cycle) {
            state.stall_cycles+=kEeCyclesPerIopCycle;(void)memory->read32(0xB000C000u);
        }
        const auto beforeStop=executed;const auto pending=memory->iop().ee_dma_read(0x1000C020u);
        require(pending>0,"The actual SIF0 response has entered its payload phase");
        rejects([&]{(void)call(0x6B);},"SIF0 DMA register write while busy");
        require(executed==beforeStop && (memory->iop().ee_dma_read(0x1000C000u)&0x100u) &&
            memory->iop().ee_dma_read(0x1000C020u)==pending,"An unsupported active stop preserves the real transfer and syscall boundary");
        state.stall_cycles+=1000u*kEeCyclesPerIopCycle;
        (void)memory->read32(0xB000C400u);
        require(!memory->iop().stopped(),memory->iop().stop_reason().value_or("IOP HLE command"));
        require(memory->read32(reply)==24 && memory->read32(reply+8u)==0x80000001u &&
            memory->read32(reply+16u)==0 && memory->read32(reply+20u)==1 &&
            memory->iop().sif1_metrics().bytes==32 && memory->iop().sif0_metrics().bytes==32 &&
            !memory->iop().native_instructions() && !memory->iop().interpreted_instructions(),
            "RPC initialization traverses real SIF1 and SIF0 DMA without inventing IOP guest instructions");
        memory->write32(0xB000E010u,0x60u);
        memory->write32(packet,16u);memory->write32(packet+12u,1u);memory->write32(transfer+8u,16u);
        require(call(0x77,transfer,1)>0,"Header-only INIT_CMD submits the protocol's RPC synchronization");
        state.stall_cycles+=1000u*kEeCyclesPerIopCycle;
        (void)memory->read32(0xB000C400u);
        require(!memory->iop().stopped() && memory->iop().diagnostic_ram32(0x1108u)==1u,
            "RPC synchronization consumes no nonexistent address field");
        memory->write32(packet,64u);memory->write32(packet+32u,0xDEADBEEFu);memory->write32(transfer+8u,64u);
        memory->write32(packet+8u,0x80000009u);
        call(0x78);
        const auto previousSif0=memory->iop().sif0_metrics().bytes;
        require(call(0x77,transfer,1)>0,"An absent-server bind reaches the real command receiver");
        state.stall_cycles+=1000u*kEeCyclesPerIopCycle;
        (void)memory->read32(0xB000F230u);
        require(!memory->iop().stopped() && memory->read32(reply+8u)==0x80000008u &&
            memory->read32(reply+32u)==0x80000009u && !memory->read32(reply+36u) && !memory->read32(reply+40u) &&
            memory->iop().sif0_metrics().bytes==previousSif0+64u,
            "absent RPC servers complete binding through physical SIF0 with null server/buffer, not a fabricated service");
        memory->write32(0xB000E010u,0x60u);
        memory->write32(packet+8u,0x8000000Au);memory->write32(packet+52u,0xDEADBEEFu);
        require(call(0x77,transfer,1)>0,"An unregistered RPC call reaches the real command receiver");
        state.stall_cycles+=1000u*kEeCyclesPerIopCycle;
        rejects([&]{(void)memory->read32(0xB000F230u);},"call to an unregistered RPC server");
        require(memory->iop().stopped(),"An unregistered RPC service cannot execute or report successful completion");

        // Fixed-word native IOP receiver: initialize DMA, then wait at a native branch.
#define IOP_ENTRY(pc,word) IopAotEntry{pc,word,[](Iop& iop){iop.execute_native<word>();}}
        const std::array entries{
            IOP_ENTRY(0x1000u,0x3C08BF80u),IOP_ENTRY(0x1004u,0x34098000u),IOP_ENTRY(0x1008u,0xAD091570u),
            IOP_ENTRY(0x100Cu,0x34090001u),IOP_ENTRY(0x1010u,0xAD091578u),IOP_ENTRY(0x1014u,0xAD09157Cu),
            IOP_ENTRY(0x1018u,0x3C090080u),IOP_ENTRY(0x101Cu,0xAD0910F4u),IOP_ENTRY(0x1020u,0x3C090008u),
            IOP_ENTRY(0x1024u,0xAD091574u),IOP_ENTRY(0x1028u,0x3C090100u),IOP_ENTRY(0x102Cu,0x35290600u),
            IOP_ENTRY(0x1030u,0xAD091538u),IOP_ENTRY(0x1034u,0x1000FFFFu),IOP_ENTRY(0x1038u,0x00000000u)};
#undef IOP_ENTRY
        std::vector<std::uint32_t> words;for(const auto& entry:entries)words.push_back(entry.word);
        const auto payload=m20::le_words(words);const auto source=sha256_hex(payload);
        const auto identity=iop_aot_identity(source,entries);const IopAotModule receiver{source,identity,entries};
        const IopAotRegistration registered(receiver);
        memory->load_iop_program(payload,0x1000,0x1000);
        const auto descriptors=hle_test::data+0x400u,bytes=hle_test::data+0x1000u;
        for(unsigned i=0;i<8;++i)memory->write32(bytes+i*4u,0xABC00000u+i);
        const auto descriptor=[&](unsigned i,unsigned destination,unsigned attributes){
            memory->write32(descriptors+i*16u,bytes+i*16u);memory->write32(descriptors+i*16u+4,destination);
            memory->write32(descriptors+i*16u+8,16);memory->write32(descriptors+i*16u+12,attributes);
        };
        descriptor(0,0x4000,0);descriptor(1,0x5000,0x44);
        memory->write32(descriptors+28,0x100);
        require(call(0x77,descriptors,2)==0 && !memory->iop().ee_dma_read(0x1000C400u),
            "invalid second descriptor never starts a partial DMA");
        descriptor(1,0x5000,0x44);
        state.set_reg32(6,0);state.set_reg32(7,0x11223344);state.set_reg32(28,0x1234);
        const auto handler_id=call(0x12,6,hle_test::text+0x600);
        require(handler_id>0 && call(0x16,6)==1,"DMA callback registration enables the real channel mask");
        state.set_reg32(6,handler_id);state.set_reg32(7,0x55667788);state.set_reg32(28,0x9ABC);
        const auto first_handler=call(0x12,6,hle_test::text+0x600);
        require(first_handler>handler_id,"A callback can be inserted before an existing ID");
        require(call(0x5F,6,handler_id)==0 && call(0x5E,6,handler_id)==0 && call(0x5E,6,handler_id+99)==-1,
            "Individual handler enable/disable validates IDs separately from the hardware channel mask");
        memory->write32(0xB000E010u,0x60u);
        const auto transaction=call(0x77,descriptors,2);
        require(transaction>0 && call(0x76,transaction)==0 && call(0x77,descriptors,2)==0,
            "SIF DMA accepts one real submission, reports it active and rejects a busy resubmission");
        state.stall_cycles+=1000u*kEeCyclesPerIopCycle;
        (void)memory->read32(0xB000C400u); // Advance the same devices to the shared EE clock.
        require(!memory->iop().stopped(),memory->iop().stop_reason().value_or("IOP receiver"));
        for(unsigned i=0;i<4;++i)require(memory->iop().diagnostic_ram32(0x4000+i*4u)==0xABC00000u+i &&
            memory->iop().diagnostic_ram32(0x5000+i*4u)==0xABC00004u+i,"two referenced DMA payloads reach actual IOP RAM");
        state.set_reg64(16,0xCAFEBABE);state.set_reg32(28,0x5678);const auto resume=state.pc,previous_sp=state.reg32(29);
        const auto callbackStack=runtime.eeScheduler().invocationStackTop();
        require(poll_ee_interrupts(state,*memory) && state.pc==hle_test::text+0x600 &&
            state.reg32(4)==6 && state.reg32(5)==0x55667788 &&
            state.reg32(28)==0x9ABC && state.reg32(29)==callbackStack && (state.cp0(Cop0::Status)&kStatusEXL),
            "Actual DMA completion enters the registered native callback with its channel and GP");
        BootOptions callback;callback.max_instructions=64;callback.stop_at=resume;
        const auto irq=run_ee_boot(state,*memory,module,callback);
        require(irq.outcome==BootOutcome::StopAddress && irq.aot_instructions==12 && !irq.interpreted_instructions &&
            state.reg64(16)==0xCAFEBABE && state.reg32(28)==0x5678 && state.reg32(29)==previous_sp && !(state.cp0(Cop0::Status)&kStatusEXL) &&
            !(memory->iop().ee_dma_read(0x1000E010u)&0x40u),
            "Native DMA callback preserves caller registers, acknowledges only the real event and resumes the interrupted thread");
        require(memory->iop().sif1_metrics().bytes==32 && memory->iop().interpreted_instructions()==0 &&
            memory->iop().native_instructions()>0 && call(0x76,transaction)==-1,
            "DMA status changes only after the native IOP consumes the real source stream");
        require(call(0x17,6)==1 && call(0x13,6,handler_id)==1 && call(0x13,6,first_handler)==0 && call(0x13,6,handler_id)==-1,
            "DMA masks and callback removal retain their real state and error codes");
    }
    runtime.prepare_direct_elf(image,module);
    {
        HleKernel kernel(runtime);std::uint64_t executed=0;
        ExecutionContext ctx{state,*memory,executed,1,UINT32_MAX};
        state.set_reg32(3,0x777);const auto before=state;
        rejects([&]{execute_ee_system<MipsOpcode::Syscall>(ctx,12);},"unsupported PS2Recomp HLE syscall");
        require(state.pc==before.pc && state.retired_instructions==before.retired_instructions && kernel.syscall_count()==0,"unsupported syscall is transactional");
        state.set_reg32(3,0x40);state.set_reg32(4,hle_test::data);
        memory->store32_observed(hle_test::data+8,{0,1});
        rejects([&]{execute_ee_system<MipsOpcode::Syscall>(ctx,12);},"unknown bits");
        require(kernel.syscall_count()==0 && executed==0,"unknown descriptor is not consumed");
        memory->write32(hle_test::data+8,0);
        const auto call=[&](int number,std::uint32_t a=0,std::uint32_t b=0,std::uint32_t c=0,std::uint32_t d=0){
            state.set_reg32(3,static_cast<std::uint32_t>(number));state.set_reg32(4,a);state.set_reg32(5,b);
            state.set_reg32(6,c);state.set_reg32(7,d);execute_ee_system<MipsOpcode::Syscall>(ctx,12);
            return static_cast<std::int32_t>(state.reg32(2));
        };
        require(call(0x40,0)==-1,"bad semaphore descriptor returns the guest error");
        memory->store32_observed(hle_test::data,{0,UINT32_MAX});
        require(call(0x40,hle_test::data)==1,"failed descriptor did not allocate an object");
        require(call(0x45,1)==-419 && call(0x42,1)==1 && call(0x42,1)==-420 && call(-0x46,1)==1,
            "semaphore empty/overflow errors and interrupt-safe poll");
        require(call(0x50,hle_test::data+0x20)==1,"event creation");
        require(call(0x57,1,0,1)==-423 && call(0x57,1,4,0x80)==-405,"event pattern/mode validation");
        require(call(0x52,1,4)==0 && call(0x57,1,4,0x11,hle_test::data+0x104)==0 &&
                memory->read32(hle_test::data+0x104)==4 && call(0x57,1,4,1)==-421,"event consumption");
        require(call(0x3D,0x100000,0x10000)==0x100000 && call(0x3E)==0x110000,"bounded heap initialization");
        require(call(0x3D,0x10000,0x10000)==-1 && call(0x3D,UINT32_MAX,1)==-1 && call(0x3E)==0x110000,
            "unmapped or invalid heap preserves prior allocation range");
        const auto argumentBlock=hle_test::data+0x500u;
        require(call(0x3C,0x1234,hle_test::data+0x1000,0x1000,argumentBlock)>0 &&
            memory->read32(argumentBlock)==0,"direct ELF without a mounted disc has no invented arguments");
        memory->mount_disc(std::make_shared<DiscImage>(cdvdDisc),Iop::DiscMedium::Ps2Cd);
        require(call(0x70)==0x7F00 && call(0x71,0x7D00)==0x7F00 && call(0x70)==0x7D00 &&
            call(0x71,0x7F00)==0x7D00,"SDK GS IMR services share the real readable mask register");
        require(call(0x02,1,2,1)==0 && memory->gs().latched(kGsPmode)==1u &&
            memory->gs().latched(kGsSmode2)==3u,"SDK SetGsCrt reads internal GS state and publishes canonical writes");
        rejects([&]{(void)memory->read64(kGsPmode);},"no register model");
        memory->write32(argumentBlock-4u,0x12345678u);memory->write32(argumentBlock+324u,0x87654321u);
        require(call(0x3C,0x1234,hle_test::data+0x1000,0x1000,argumentBlock)>0 &&
            memory->read32(argumentBlock)==1 && memory->read32(argumentBlock+4u)==argumentBlock+68u &&
            memory->read32(argumentBlock+8u)==0,"SetupThread publishes mounted BOOT2 in the startup argument ABI");
        const std::string bootArgument="cdrom0:\\SLUS_000.01;1";
        for(unsigned i=0;i<=bootArgument.size();++i)
            require(memory->read8(argumentBlock+68u+i)==(i<bootArgument.size()?bootArgument[i]:0),"BOOT2 argument bytes");
        require(memory->read32(argumentBlock-4u)==0x12345678u && memory->read32(argumentBlock+324u)==0x87654321u,
            "startup arguments preserve neighboring memory");
        const auto priorGp=state.reg32(28);
        require(call(0x3C,0x9999,hle_test::data+0x1000,0x1000,4u)==-1 && state.reg32(28)==priorGp,
            "invalid startup argument buffer refuses before changing thread metadata");
        require(call(0x3C,0x1234,hle_test::data+0x1000,0x1000)==static_cast<int>(hle_test::data+0x2000) &&
                state.reg32(28)==0x1234,"SetupThread returns aligned stack and establishes GP");
        state.set_cp0(Cop0::Status,0);const auto retired=executed;
        rejects([&]{execute_ee_system<MipsOpcode::Mtc1>(ctx,0x44800000);},"Status.CU1");
        require(executed==retired,"guest disabling COP1 still receives the shared unusable fault");
        state.delay_pending=true;state.set_reg32(3,0x7F);
        rejects([&]{execute_ee_system<MipsOpcode::Syscall>(ctx,12);},"branch delay slot");state.delay_pending=false;
        rejects([&]{(void)call(0x44,1);},"no runnable thread");
        require(kernel.current_thread_id()==0,"a blocked thread is not silently resumed");
        BootOptions stopped;stopped.max_instructions=1;
        require(run_ee_boot(state,*memory,module,stopped).retired==0,"retry does not execute a blocked thread");
    }
    runtime.prepare_direct_elf(image,module);
    {
        memory->diagnostic_data_cache();memory->set_cache_config(0x10443u);
        state.set_cp0(Cop0::Config,0x10443u);
        memory->write32(0xA0004000u,0x11223344);
        require(memory->read32(0x80004000u)==0x11223344,"cache refill");
        memory->write32(0x80004000u,0xAABBCCDD);
        require(memory->read32(0xA0004000u)==0x11223344,"dirty CPU data is not backing RAM yet");
        HleKernel kernel(runtime);std::uint64_t executed=0;
        ExecutionContext ctx{state,*memory,executed,10,UINT32_MAX};
        state.set_reg32(3,0x64);state.set_reg32(4,0);execute_ee_system<MipsOpcode::Syscall>(ctx,12);
        require(memory->read32(0xA0004000u)==0xAABBCCDD,"FlushCache writes dirty data through the shared coherence path");
        memory->write32(0x80004000u,0xDEADBEEF);
        state.set_reg32(4,1);execute_ee_system<MipsOpcode::Syscall>(ctx,12);
        require(memory->read32(0x80004000u)==0xAABBCCDD,"invalidate discards dirty cache without inventing writeback");
        state.set_reg32(4,2);execute_ee_system<MipsOpcode::Syscall>(ctx,12);
        state.set_reg32(4,99);const auto before=executed;
        rejects([&]{execute_ee_system<MipsOpcode::Syscall>(ctx,12);},"unsupported HLE FlushCache");
        require(executed==before,"unsupported cache operation does not report success");
    }
    runtime.prepare_direct_elf(image,module);
    {
        HleKernel kernel(runtime);std::uint64_t executed=0;
        ExecutionContext ctx{state,*memory,executed,100,UINT32_MAX};
        const auto call=[&](int number,unsigned a=0,unsigned b=0,unsigned c=0,unsigned d=0){
            state.set_reg32(3,number);state.set_reg32(4,a);state.set_reg32(5,b);
            state.set_reg32(6,c);state.set_reg32(7,d);execute_ee_system<MipsOpcode::Syscall>(ctx,12);
            return static_cast<std::int32_t>(state.reg32(2));
        };
        const auto intc=call(0x10,11,hle_test::text+0x600,0,0x12345678u);
        const auto dma=call(0x12,6,hle_test::text+0x600,0,0x87654321u);
        require(intc==1 && dma==1 && call(0x5D,11,intc)==0 && call(0x5C,11,intc)==0 &&
            call(0x5C,6,intc)==-1,"INTC and DMA handler ID spaces stay independent");
        require(call(0x14,11)==1 && call(0x14,11)==0 && call(0x14,15)==-1,"INTC masks use real transition results");
        require(call(0x15,11)==1,"disable the source-kernel cause before direct hardware re-enable");
        memory->write32(0xB000F010u,1u<<11u);
        memory->write32(0xB0001020u,16);memory->write32(0xB0001000u,0);
        memory->write32(0xB0001010u,0x180u); // real T2 compare interrupt (INTC 11)
        state.stall_cycles+=32u;
        const auto resume=state.pc,stack=state.reg32(29);state.set_reg64(16,0xCAFEBABE);
        require(poll_ee_interrupts(state,*memory) && state.pc==hle_test::text+0x600 &&
            state.reg32(4)==11 && state.reg32(5)==0x12345678u,"Real timer IRQ observes the canonical mask after direct MMIO re-enable");
        BootOptions callback;callback.max_instructions=64;callback.stop_at=resume;
        const auto report=run_ee_boot(state,*memory,module,callback);
        require(report.outcome==BootOutcome::StopAddress && report.aot_instructions==6 &&
            !report.interpreted_instructions && state.reg64(16)==0xCAFEBABE && state.reg32(29)==stack &&
            !(state.cp0(Cop0::Status)&kStatusEXL),"INTC native return preserves the interrupted context and one clock");
        require(call(0x15,11)==1 && call(0x11,11,intc)==0 && call(0x13,6,dma)==0,
            "INTC callback removal cannot remove an equal-numbered DMA callback");
    }
    runtime.prepare_direct_elf(image,module);
    {
        HleKernel kernel(runtime);std::uint64_t executed=0;ExecutionContext ctx{state,*memory,executed,100,UINT32_MAX};
        const auto call=[&](std::uint32_t descriptor){state.set_reg32(3,0x40);state.set_reg32(4,descriptor);
            execute_ee_system<MipsOpcode::Syscall>(ctx,12);return static_cast<std::int32_t>(state.reg32(2));};
        const auto descriptor=hle_test::data+0xFF4u;
        memory->write32(descriptor+4u,1);memory->write32(descriptor+8u,0);
        memory->write32(descriptor+16u,0);memory->write32(descriptor+20u,0);
        memory->store32_observed(descriptor,{0,UINT32_MAX});memory->store32_observed(descriptor+12u,{0,UINT32_MAX});
        require(call(0x01FFFFF4u)==-1 && call(descriptor)==1,
            "A 24-byte ABI descriptor requires word alignment, spans valid pages, and rejects an unmapped tail before allocation");
    }
    runtime.prepare_direct_elf(image,module);
    {
        HleKernel kernel(runtime);std::uint64_t executed=0;
        ExecutionContext ctx{state,*memory,executed,100,UINT32_MAX};const auto args=hle_test::data+0x600u;
        const auto call=[&](int operation){state.set_reg32(3,0x7C);state.set_reg32(4,operation);state.set_reg32(5,args);
            execute_ee_system<MipsOpcode::Syscall>(ctx,12);return static_cast<std::int32_t>(state.reg32(2));};
        memory->write32(args,0x1234u);memory->write32(args+4u,0);memory->write32(args+8u,hle_test::text+0x600u);
        memory->write32(args+12u,hle_test::data+0x700u);
        require(call(1)==1 && call(1)==2,"PS2Recomp DECI2 endpoints allocate distinct console-owned IDs");
        memory->write32(args,1);memory->store32_observed(args+4u,{0,UINT32_MAX});
        require(call(-8)==0 && call(-8)==-8 && call(-9)==0 && call(-9)==-9 && call(4)==0 && call(-5)==0,
            "DECI2 locks and empty polls retain real state without consuming unused arguments");
        require(call(3)==-10 && call(-6)==-10 && call(2)==0 && call(2)==-2,
            "No attached debugger cannot fabricate sends or invalid-socket success");
        rejects([&]{(void)call(99);},"unsupported PS2Recomp DECI2 operation");
    }
    runtime.prepare_direct_elf(image,module);
    {
        HleKernel kernel(runtime);state.pc=0;
        BootOptions options;options.max_instructions=10;
        const auto report=run_ee_boot(state,*memory,module,options);
        require(report.outcome==BootOutcome::BlockPoint && report.retired==0 &&
            report.reason.find("no runnable thread")!=std::string::npos && kernel.current_thread_id()==0 &&
            runtime.eeScheduler().thread(1)->status==EeThreadStatus::Dormant,"SDK thread return becomes dormant without executing a zero-page instruction loop");
    }
    std::cout<<"PS2Recomp HLE threads, semaphore/event waits, state isolation and explicit refusals passed\n";
    std::filesystem::remove(cdvdDisc);
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){return run(argc,argv);}
#else
int main(int argc,char** argv){return run(argc,argv);}
#endif
