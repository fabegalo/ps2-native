#include "Kernel/Stubs/MemoryCard.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ps2_rom_device.h"
#include <fstream>
#include <iostream>

static void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
static std::vector<uint8_t> bytes(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    const auto scratch=std::filesystem::absolute(argv[1]);
    if(!std::filesystem::create_directories(scratch)) return 2;
    const auto originalPaths=PS2Runtime::getIoPaths();
    bool success=false;
    try {
        auto paths=originalPaths; paths.mcRoot=scratch/"mc0"; PS2Runtime::setIoPaths(paths);
        std::vector<uint8_t> ram(PS2_RAM_SIZE); R5900Context context{};
        PS2Runtime::configureIoPathsFromElf((scratch/"disc/BOOT.ELF").string());
        require(PS2Runtime::getIoPaths().mcRoot==paths.mcRoot,"Loading an ELF replaced the console's selected card root");
        ps2_stubs::sceMcInit(ram.data(),&context,nullptr);
        require(!std::filesystem::exists(paths.mcRoot/".ps2n-card.raw"),"Libmc initialization eagerly created a card before device access");
        std::memcpy(ram.data()+0x2000,"/missing-save",14);
        SET_GPR_U32(&context,4,0); SET_GPR_U32(&context,5,0); SET_GPR_U32(&context,6,0x2000);
        SET_GPR_U32(&context,7,0); SET_GPR_U32(&context,8,1); SET_GPR_U32(&context,9,0x3000);
        ps2_stubs::sceMcGetDir(ram.data(),&context,nullptr);
        require(ps2_stubs::getMemoryCardDebugSnapshot().lastResult==0,
            "A directory query for a missing save must return zero matches");
        std::memcpy(ram.data()+0x2000,"/save",6);
        SET_GPR_U32(&context,4,0); SET_GPR_U32(&context,5,0); SET_GPR_U32(&context,6,0x2000);
        SET_GPR_U32(&context,7,PS2_FIO_O_RDWR|PS2_FIO_O_CREAT|PS2_FIO_O_TRUNC);
        ps2_stubs::sceMcOpen(ram.data(),&context,nullptr);
        const auto fd=ps2_stubs::getMemoryCardDebugSnapshot().lastResult;
        require(fd>0,"Memory-card fixture could not open a save");
        for(unsigned i=0;i<16;++i) ram[0x1000+i]=uint8_t(i+1);
        const auto write=[&] {
            SET_GPR_U32(&context,4,fd); SET_GPR_U32(&context,5,0x1000); SET_GPR_U32(&context,6,16);
            ps2_stubs::sceMcWrite(ram.data(),&context,nullptr);
            return ps2_stubs::getMemoryCardDebugSnapshot().lastResult;
        };
        require(write()==16,"Successful memory-card write lost its byte count");
        const auto image=paths.mcRoot/".ps2n-card.raw"; const auto before=bytes(image);
        require(!before.empty(),"Successful memory-card write was not persisted");
        SET_GPR_U32(&context,4,fd); SET_GPR_U32(&context,5,0); SET_GPR_U32(&context,6,PS2_FIO_SEEK_SET);
        ps2_stubs::sceMcSeek(ram.data(),&context,nullptr);
        auto pending=image; pending+=".pending";
        require(std::filesystem::create_directory(pending),"Cannot inject persistence failure");
        ram[0x1000]=0xa5;
        require(write()==-5,"Persistence failure was acknowledged as a successful memory-card write");
        require(bytes(image)==before,"Failed transaction replaced the last valid card image");
        require(std::filesystem::remove(pending),"Cannot remove the owned failure injection");
        require(write()==16,"Memory-card write did not recover after persistence became available");
        SET_GPR_U32(&context,4,fd); SET_GPR_U32(&context,5,0); SET_GPR_U32(&context,6,PS2_FIO_SEEK_SET);
        ps2_stubs::sceMcSeek(ram.data(),&context,nullptr);
        SET_GPR_U32(&context,5,0x3000); SET_GPR_U32(&context,6,16);
        ps2_stubs::sceMcRead(ram.data(),&context,nullptr);
        require(ps2_stubs::getMemoryCardDebugSnapshot().lastResult==16&&std::memcmp(ram.data()+0x3000,ram.data()+0x1000,16)==0,
            "Successful memory-card write changed its payload");
        {
            PS2Vfs vfs; PS2RomDevice rom; PS2VfsMounts mounts{{},{},paths.mcRoot};
            const auto file=vfs.open("mc0:/save",PS2_FIO_O_RDWR,mounts,rom);
            require(file>=0,"VFS could not open the libmc save");
            std::array<uint8_t,16> payload{};
            require(vfs.read(file,payload.data(),payload.size())==16&&payload[0]==0xa5,"VFS and libmc observed different card data");
            const uint8_t change=0x3c;
            require(vfs.seek(file,0,SEEK_SET)==0&&vfs.write(file,&change,1)==1,"VFS card write failed");
            std::memcpy(ram.data()+0x2100,"/renamed",9); R5900Context rename{};
            SET_GPR_U32(&rename,4,0); SET_GPR_U32(&rename,5,0); SET_GPR_U32(&rename,6,0x2000); SET_GPR_U32(&rename,7,0x2100);
            ps2_stubs::sceMcRename(ram.data(),&rename,nullptr);
            require(ps2_stubs::getMemoryCardDebugSnapshot().lastResult==0&&vfs.seek(file,0,SEEK_SET)==0&&
                vfs.read(file,payload.data(),1)==1&&payload[0]==change,"Rename invalidated an open card handle in another API");
            require(vfs.close(file)==0,"VFS card close failed");
            SET_GPR_U32(&context,4,fd); SET_GPR_U32(&context,5,0); SET_GPR_U32(&context,6,PS2_FIO_SEEK_SET);
            ps2_stubs::sceMcSeek(ram.data(),&context,nullptr);
            SET_GPR_U32(&context,5,0x3000); SET_GPR_U32(&context,6,1);
            ps2_stubs::sceMcRead(ram.data(),&context,nullptr);
            require(ram[0x3000]==change,"Libmc did not observe VFS card write");
            std::vector<uint8_t> nand(before.size());
            require(ps2_stubs::mcNandRead(0,0,nand.data(),nand.size())&&nand==bytes(image),"SIO2 NAND and persisted VFS/libmc state differ");
            require(!std::filesystem::exists(paths.mcRoot/"save"),"VFS created a second save outside the NAND image");
            const auto second=vfs.open("mc1:/suffix;1",PS2_FIO_O_RDWR|PS2_FIO_O_CREAT,mounts,rom);
            require(second>=0&&vfs.write(second,&change,1)==1&&vfs.close(second)==0,"Second memory-card slot failed");
            PS2VfsStat status{};
            require(vfs.stat("mc1:/suffix;1",mounts,rom,status)&&status.size==1&&!vfs.stat("mc1:/suffix",mounts,rom,status),
                "Card filename was mistaken for an ISO revision suffix");
        }
        ps2_stubs::sceMcEnd(ram.data(),&context,nullptr);
        success=true;
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; }
    // Exercise the same flush/lease release used by the runtime's session teardown.
    auto pending=scratch/"mc0/.ps2n-card.raw.pending";
    std::error_code ignored;
    if(std::filesystem::is_directory(pending,ignored)&&std::filesystem::is_empty(pending,ignored)) std::filesystem::remove(pending,ignored);
    R5900Context cleanup{};
    try { ps2_stubs::sceMcEnd(nullptr,&cleanup,nullptr); } catch(const std::exception&) {}
    try { ps2_stubs::mcCloseStorage(); }
    catch(const std::exception& failure) { std::cerr << failure.what() << '\n'; success=false; }
    PS2Runtime::setIoPaths(originalPaths);
    std::error_code error; std::filesystem::remove_all(scratch,error);
    if(error) { std::cerr << "Memory-card fixture cleanup failed: " << error.message() << '\n'; return 1; }
    if(success) std::cout << "Memory-card payload, failed persistence, last valid image and retry passed\n";
    return success?0:1;
}
