#include "emulator/imports/iop_cdvd.h"
#include "emulator/core/iop_kernel.h"
#include "emulator/core/iop_memory.h"
#include "ps2x/iop/iop_host.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

using namespace ps2x::iop;
using namespace ps2x::iop::detail;

class ImageHost final : public IopHost {
public:
    std::vector<uint8_t> image = std::vector<uint8_t>(25u * 2048u);
    bool readGuest(uint32_t, void*, size_t) const override { return false; }
    bool writeGuest(uint32_t, const void*, size_t) override { return false; }
    bool zeroGuest(uint32_t, size_t) override { return false; }
    bool normalizeGuestAddress(uint32_t, uint32_t&) const override { return false; }
    uint32_t allocateIopHandle(IopHandleKind) override { return 1; }
    uint32_t allocateGuest(uint32_t, uint32_t) override { return 0; }
    void freeGuest(uint32_t) override {}
    void audioCommand(uint32_t, uint32_t, GuestBuffer, GuestBuffer) override {}
    std::string hostPath(HostPathKind kind) const override { return kind == HostPathKind::CdImage ? "fixture.iso" : ""; }
    std::string translateGuestPath(std::string_view path) const override { return std::string(path); }
    uint64_t openHostFile(std::string_view) override { return 1; }
    bool hostFileSize(uint64_t, uint64_t& size) const override { size=image.size(); return true; }
    bool readHostFile(uint64_t, uint64_t offset, void* data, size_t size, size_t& read) override {
        read=0;
        if (offset > image.size() || size > image.size()-offset) return false;
        std::memcpy(data,image.data()+offset,size); read=size; return true;
    }
    void closeHostFile(uint64_t) override {}
    int32_t memoryCard(const MemoryCardRequest&) override { return -1; }
    bool hasGuestFunction(uint32_t) const override { return false; }
    bool invokeGuestFunction(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t*) override { return false; }
    void log(LogLevel,std::string_view) override {}
    void word(size_t offset, uint32_t value) { std::memcpy(image.data()+offset,&value,4); }
};

int main() {
    ImageHost host;
    IopMemory memory;
    IopKernel kernel(memory);
    IopCdvd cdvd(host,memory,kernel);
    const size_t pvd=16*2048;
    host.image[pvd]=1; host.image[pvd+6]=1;
    std::memcpy(host.image.data()+pvd+1,"CD001",5);
    host.image[pvd+156]=34;
    host.word(pvd+158,20); host.word(pvd+166,2048);
    const size_t entry=20*2048;
    host.image[entry]=44; host.image[entry+32]=10;
    host.word(entry+2,24); host.word(entry+10,13);
    std::memcpy(host.image.data()+entry+33,"TEST.BIN;1",10);
    const char path[]="cdrom0:\\test.bin;1";
    (void)memory.writeRam(0x1000,path,sizeof(path));
    IopCpuState cpu{};
    cpu.gpr[4]=0x1100; cpu.gpr[5]=0x1000;
    if (!cdvd.dispatchImport(10,cpu) || cpu.gpr[2]!=1 ||
        memory.read32(0x1100)!=24 || memory.read32(0x1104)!=13) {
        std::cerr << "IOP lookup did not use original ISO directory\n"; return 1;
    }
    host.word(24*2048,0x53494621u);
    const std::array<uint8_t,2048> destination{};
    (void)memory.writeRam(0x2000,destination.data(),destination.size());
    cpu.gpr[4]=memory.read32(0x1100); cpu.gpr[5]=1; cpu.gpr[6]=0x2000;
    if (!cdvd.dispatchImport(6,cpu) || cpu.gpr[2]!=1 || memory.read32(0x2000)!=0x53494621u) {
        std::cerr << "IOP search and sector read disagree\n"; return 1;
    }
    cpu.gpr[4]=0x1100; cpu.gpr[5]=0x1000;
    // A malformed ISO directory must fail without publishing a partial result.
    const uint32_t untouched=0xAABBCCDD;
    memory.write32(0x1100,untouched); host.image[entry]=33;
    if (!cdvd.dispatchImport(10,cpu) || cpu.gpr[2]!=0 || memory.read32(0x1100)!=untouched) return 1;
    host.image[entry]=44; host.image[pvd+1]='X';
    if (!cdvd.dispatchImport(10,cpu) || cpu.gpr[2]!=0 || memory.read32(0x1100)!=untouched) return 1;
    std::cout << "PASS original ISO lookup and malformed directory rejection\n";
}
