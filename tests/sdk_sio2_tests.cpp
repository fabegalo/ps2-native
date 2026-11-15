#include "iop_compat_test_support.h"
#include "emulator/core/iop_memory.h"
#include <functional>

using namespace iop_test;
using namespace ps2x::iop::detail;

namespace {
class PadHost final : public IopHost {
public:
    Host storage;
    bool connected = true;
    unsigned reads = 0;
    bool cardConnected = true;
    std::vector<uint8_t> card = std::vector<uint8_t>(528*16384,0xff);
    unsigned cardFlushes = 0;
    bool memoryCardPresent(int port,int slot) override { return cardConnected&&port==0&&slot==0; }
    bool readMemoryCard(int, uint32_t address,uint8_t* bytes,size_t size) override {
        if(address>card.size()||size>card.size()-address) return false;
        std::copy_n(card.begin()+address,size,bytes); return true;
    }
    bool writeMemoryCard(int,uint32_t address,const uint8_t* bytes,size_t size) override {
        if(address>card.size()||size>card.size()-address) return false;
        for(size_t i=0;i<size;++i) card[address+i]&=bytes[i]; return true;
    }
    bool eraseMemoryCard(int,uint32_t page) override {
        if(page%16||page>16384-16) return false;
        std::fill_n(card.begin()+page*528,16*528,0xff); return true;
    }
    bool flushMemoryCard(int) override { ++cardFlushes; return true; }
    unsigned entropy=0;
    bool memoryCardKeyset(int port,uint8_t* data,size_t size) override {
        if(port!=0||size!=72)return false;
        for(unsigned i=0;i<size;++i)data[i]=uint8_t(i+1);return true;
    }
    bool randomBytes(uint8_t* data,size_t size) override {
        for(size_t i=0;i<size;++i)data[i]=uint8_t(++entropy);return true;
    }
    uint8_t small = 0, large = 0;
    bool readPadState(int port, int slot, uint8_t* data, size_t size) override {
        if (!connected || port != 0 || slot != 0 || size < 32) return false;
        std::memset(data, 0, size);
        data[2] = 0xf7; data[3] = 0xbf; // Start and Cross, active-low.
        data[4] = 10; data[5] = 20; data[6] = 30; data[7] = 40;
        data[14] = 255;
        ++reads;
        return true;
    }
    void setPadRumble(int, int, uint8_t s, uint8_t l) override { small = s; large = l; }
    bool readGuest(uint32_t a, void* p, size_t n) const override { return storage.readGuest(a, p, n); }
    bool writeGuest(uint32_t a, const void* p, size_t n) override { return storage.writeGuest(a, p, n); }
    bool zeroGuest(uint32_t a, size_t n) override { return storage.zeroGuest(a, n); }
    bool normalizeGuestAddress(uint32_t a, uint32_t& n) const override { return storage.normalizeGuestAddress(a, n); }
    uint32_t allocateIopHandle(IopHandleKind k) override { return storage.allocateIopHandle(k); }
    uint32_t allocateGuest(uint32_t n, uint32_t a) override { return storage.allocateGuest(n, a); }
    void freeGuest(uint32_t a) override { storage.freeGuest(a); }
    void audioCommand(uint32_t s, uint32_t f, GuestBuffer a, GuestBuffer b) override { storage.audioCommand(s, f, a, b); }
    std::string hostPath(HostPathKind k) const override { return storage.hostPath(k); }
    std::string translateGuestPath(std::string_view p) const override { return storage.translateGuestPath(p); }
    uint64_t openHostFile(std::string_view p) override { return storage.openHostFile(p); }
    bool hostFileSize(uint64_t h, uint64_t& n) const override { return storage.hostFileSize(h, n); }
    bool readHostFile(uint64_t h, uint64_t a, void* p, size_t n, size_t& r) override { return storage.readHostFile(h, a, p, n, r); }
    void closeHostFile(uint64_t h) override { storage.closeHostFile(h); }
    int32_t memoryCard(const MemoryCardRequest& r) override { return storage.memoryCard(r); }
    bool hasGuestFunction(uint32_t a) const override { return storage.hasGuestFunction(a); }
    bool invokeGuestFunction(uint64_t t, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t* r) override {
        return storage.invokeGuestFunction(t, a, b, c, d, e, r);
    }
    void log(LogLevel, std::string_view) override {}
};
void rejects(const std::function<void()>& operation) {
    bool rejected = false;
    try { operation(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Unsupported or invalid transfer accepted");
}
std::vector<uint8_t> packet(IopMemory& memory, std::initializer_list<uint8_t> bytes, unsigned port = 0) {
    memory.write32(0x1f808200, (uint32_t(bytes.size()) << 8) | (uint32_t(bytes.size()) << 18) | port);
    memory.write32(0x1f808204, 0);
    for (auto byte : bytes) memory.write8(0x1f808260, byte);
    memory.write32(0x1f808280, memory.read32(0x1f808280));
    memory.write32(0x1f808268, 1);
    const auto irq = memory.takeDmaStart();
    require(irq && irq->irq == 17 && memory.read32(0x1f808280) == 1, "SIO2 source IRQ missing");
    memory.write32(0x1f808268, 1);
    require(!memory.takeDmaStart(), "Unacknowledged SIO2 source IRQ repeated");
    memory.write32(0x1f808280, 0);
    require(memory.read32(0x1f808280) == 1, "Zero write acknowledged SIO2 IRQ");
    memory.write32(0x1f808280, 1);
    require(memory.read32(0x1f808280) == 0, "Write-one acknowledgement failed");
    std::vector<uint8_t> reply;
    for (size_t i = 0; i < bytes.size(); ++i) reply.push_back(memory.read8(0x1f808264));
    return reply;
}
}

int main() try {
    PadHost host;
    IopMemory memory;
    memory.attachHost(&host);
    require(memory.read32(0x1f808268) == 0x3bc, "SIO2 reset control differs");
    auto digital = packet(memory, {1, 0x42, 0, 0, 0, 0, 0, 0, 0});
    require(digital == std::vector<uint8_t>({0xff, 0x41, 0x5a, 0xf7, 0xbf, 0xff, 0xff, 0xff, 0xff}), "Digital wire response differs");
    packet(memory, {1, 0x43, 0, 1, 0, 0, 0, 0, 0});
    auto info = packet(memory, {1, 0x45, 0, 0, 0, 0, 0, 0, 0});
    require(info == std::vector<uint8_t>({0xff, 0xf3, 0x5a, 3, 2, 0, 2, 1, 0}), "DualShock 2 identification differs");
    packet(memory, {1, 0x44, 0, 1, 3, 0, 0, 0, 0});
    packet(memory, {1, 0x4d, 0, 0, 1, 0xff, 0xff, 0xff, 0xff});
    packet(memory, {1, 0x43, 0, 0, 0, 0, 0, 0, 0});
    auto analog = packet(memory, {1, 0x42, 0, 1, 200, 0, 0, 0, 0});
    require(analog == std::vector<uint8_t>({0xff, 0x73, 0x5a, 0xf7, 0xbf, 10, 20, 30, 40}), "Analog state disconnected from host");
    require(host.small == 1 && host.large == 200, "Motor mapping disconnected from host");
    packet(memory, {1, 0x43, 0, 1, 0, 0, 0, 0, 0});
    packet(memory, {1, 0x4f, 0, 0xff, 0xff, 3, 0, 0, 0});
    packet(memory, {1, 0x43, 0, 0, 0, 0, 0, 0, 0});
    auto pressure = packet(memory, {1, 0x42, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    require(pressure[1] == 0x79 && pressure[15] == 255, "Pressure response lost host state");
    auto missing = packet(memory, {1, 0x42, 0, 0, 0}, 1);
    require(std::all_of(missing.begin(), missing.end(), [](auto b) { return b == 0xff; }), "Disconnected port fabricated a pad");
    require((memory.read32(0x1f80826c) & 0x20000) != 0, "Disconnected port status missing");
    memory.reset();
    require(host.small == 0 && host.large == 0, "Reset left rumble active");

    // Two command descriptors, separately padded DMA blocks, and delayed completion.
    std::array<uint8_t, 32> tx{};
    tx[0] = tx[16] = 1; tx[1] = tx[17] = 0x42;
    require(memory.writeRam(0x1000, tx.data(), tx.size()), "Cannot install DMA input");
    memory.write32(0x1f808200, (5u << 8) | (5u << 18));
    memory.write32(0x1f808204, (5u << 8) | (5u << 18) | 1u);
    memory.write32(0x1f808208, 0);
    memory.write32(0x1f801574, (1u << 23) | (1u << 20) | (1u << 21));
    memory.write32(0x1f801540, 0x1000); memory.write32(0x1f801544, 0x20004);
    memory.write32(0x1f801548, 0x01000201);
    const auto sent = memory.takeDmaStart();
    require(sent && sent->irq == 0x2c && sent->delayCycles == 2, "DMA11 completion timing differs");
    require(memory.read32(0x1f801540) == 0x1020 && (memory.read32(0x1f801548) & 0x1000000), "DMA11 address/start lost");
    memory.completeSio2Dma(sent->irq);
    require(!(memory.read32(0x1f801548) & 0x1000000) && (memory.read32(0x1f801574) & (1u << 28)), "DMA11 interrupt state lost");
    memory.write32(0x1f801550, 0x2000); memory.write32(0x1f801554, 0x20004);
    memory.write32(0x1f801558, 0x41000200);
    const auto received = memory.takeDmaStart();
    require(received && received->irq == 0x2d, "DMA12 IRQ lost");
    require(memory.read8(0x2001) == 0x41 && memory.read8(0x2011) == 0xff, "DMA blocks or disconnected port mixed");
    require(memory.read8(0x2005) == 0 && memory.ownsRamRange(0x2000, 32), "DMA padding/ownership lost");
    memory.completeSio2Dma(received->irq);
    require(!(memory.read32(0x1f801558) & 0x1000000), "DMA12 stayed busy");
    memory.write32(0x1f801574, (memory.read32(0x1f801574) & 0xffffff) | (1u << 28));
    require(!(memory.read32(0x1f801574) & (1u << 28)) && (memory.read32(0x1f801574) & (1u << 29)), "DMA acknowledgement cleared another channel");
    memory.write32(0x1f801574, 0);
    require(!memory.completeSio2Dma(0x2c), "Masked DMA interrupt was delivered");
    rejects([&] { memory.write32(0x1f801548, 0x01000401); });
    memory.write32(0x1f801540, 0x1ffffc);
    rejects([&] { memory.write32(0x1f801548, 0x01000201); });
    rejects([&] { packet(memory, {1, 0x99, 0, 0, 0}); });
    require(packet(memory,{0x81,0x11,0,0})==std::vector<uint8_t>({0,0,0x2b,0x55}),"Memory-card probe response differs");
    require(packet(memory,{0x81,0x26,0,0,0,0,0,0,0,0,0,0,0})==
        std::vector<uint8_t>({0,0,0x2b,0,2,16,0,0,0x40,0,0,0x52,0x55}),"Memory-card geometry differs");
    packet(memory,{0x81,0x22,1,0,0,0,1,0,0});
    packet(memory,{0x81,0x42,3,0x10,0x20,0x30,0,0,0});
    require(host.card[528]==0x10&&host.card[530]==0x30,"Serial memory-card write missed NAND");
    packet(memory,{0x81,0x23,1,0,0,0,1,0,0});
    require(packet(memory,{0x81,0x43,3,0,0,0,0,0,0})==
        std::vector<uint8_t>({0,0,0,0x2b,0x10,0x20,0x30,0,0x55}),"Serial memory-card read or XOR differs");
    packet(memory,{0x81,0x22,1,0,0,0,1,0,0});
    require(packet(memory,{0x81,0x42,1,0xff,0,0,0}).back()==0x66&&host.card[528]==0x10,
        "Invalid memory-card checksum modified NAND");
    require(packet(memory,{0x81,0x23,0,0x40,0,0,0x40,0,0}).back()==0x66,"Out-of-range memory-card page accepted");
    packet(memory,{0x81,0x21,0,0,0,0,0,0,0}); packet(memory,{0x81,0x82,0,0});
    require(host.card[528]==0xff,"Memory-card block erase did not reach NAND");
    packet(memory,{0x81,0x81,0,0}); require(host.cardFlushes==1,"Memory-card end did not persist NAND");
    require(packet(memory,{0x81,0xf3,0,0,0}).back()==0x55,"Card authentication reset failed");
    require(packet(memory,{0x81,0xf7,1,0,0}).back()==0x55,"Card keyset selection failed");
    require(packet(memory,{0x81,0xf0,0,0,0}).back()==0x55,"Card authentication did not start");
    const auto iv=packet(memory,{0x81,0xf0,1,0,0,0,0,0,0,0,0,0,0,0});
    require(iv[3]==0x2b&&iv[4]==64&&iv[11]==57&&iv[12]==0x78&&iv[13]==0x55,
        "SIO2 reversed the card IV incorrectly or returned a fabricated XOR response");
    packet(memory,{0x81,0xf0,2,0,0,0,0,0,0,0,0,0,0,0});
    packet(memory,{0x81,0xf0,3,0,0});
    packet(memory,{0x81,0xf0,4,0,0,0,0,0,0,0,0,0,0,0});
    packet(memory,{0x81,0xf0,5,0,0});
    require(packet(memory,{0x81,0xf0,6,1,0,0,0,0,0,0,0,0,0,0}).back()==0x66,
        "Card accepted an authentication message with a wrong checksum");
    require(packet(memory,{0x81,0xf0,0x14,0,0}).back()==0x66,"Card completed authentication after a failed transfer");
    packet(memory,{0x81,0x27,0x5a,0,0});
    require(packet(memory,{0x81,0x28,0,0,0})==std::vector<uint8_t>({0,0,0x2b,0x5a,0x5a}),"Memory-card terminator was lost");
    host.cardConnected=false;
    require(packet(memory,{0x81,0x11,0,0})==std::vector<uint8_t>(4,0xff),"Disconnected memory-card port answered");
    host.cardConnected=true;
    rejects([&] { packet(memory,{0x81,0x99,0,0}); });
    require(host.reads != 0, "SIO2 did not sample the host");
    // Synthetic IRX uses real import dispatch rather than writing DMA registers directly.
    const auto previousReads = host.reads;
    Irx image;
    image.words(0, {0x27bdffe0, 0xafbf001c, 0x3c081f80, 0x35088200,
        0x3c090014, 0x35290500, 0xad090000, 0xad000004,
        0x2404000b, 0x3c050001, 0x34a50300, 0x24060004,
        0x24070001, 0x24080001, 0xafa80010, 0x0c00405d, 0,
        0x2404000b, 0x0c00405f, 0, 0x2404000b, 0x0c004061, 0,
        0x3c080001, 0xad020320, 0x8fbf001c, 0x00001021, 0x27bd0020, 0x03e00008, 0});
    image.words(0x160, {0x41e00000, 0, 0x101, 0x63616d64, 0x006e616d,
        0x03e00008, 0x2400001c, 0x03e00008, 0x24000020, 0x03e00008, 0x24000005, 0, 0});
    image.words(0x300, {0x00004201, 0, 0, 0});
    image.install(host.storage);
    IopSubsystem iop(host);
    const auto loaded = iop.loadModuleBuffer(0x1000);
    require(loaded.moduleId > 0 && loaded.startResult == 0, "DMACMAN fixture failed to start");
    uint32_t finalAddress = 0;
    require(iop.readMemory(0x10320, &finalAddress, 4) && finalAddress == 0x10310, "DMACMAN did not execute DMA11");
    require(host.reads == previousReads + 1, "DMACMAN did not reach the shared pad source");
    iop.reset();
    Irx irq;
    irq.words(0, {0x27bdffe0, 0xafbf001c, 0x24040011, 0x24050000, 0x3c060001, 0x34c60400,
        0x24070000, 0x0c004075, 0, 0x24040011, 0x0c004077, 0, 0x0c004079, 0,
        0x3c081f80, 0x35088268, 0x24090001, 0xad090000,
        0x3c080001, 0x8d090440, 0, 0xad090444, 0x0c00407b, 0,
        0x8fbf001c, 0, 0x27bd0020, 0x00001021, 0x03e00008, 0});
    irq.words(0x1c0, {0x41e00000, 0, 0x101, 0x72746e69, 0x006e616d,
        0x03e00008, 0x24000004, 0x03e00008, 0x24000006,
        0x03e00008, 0x24000008, 0x03e00008, 0x24000009, 0, 0});
    irq.words(0x400, {0x3c080001, 0x8d090440, 0, 0x25290001, 0xad090440,
        0x3c081f80, 0x24090001, 0xad098280, 0x03e00008, 0});
    // Counter lives beyond the handler's final delay slot.
    irq.words(0x440, {0, 0});
    irq.install(host.storage);
    const auto irqLoaded = iop.loadModuleBuffer(0x1000);
    require(irqLoaded.moduleId > 0 && irqLoaded.startResult == 0, "SIO2 interrupt fixture failed");
    uint32_t counters[2]{};
    require(iop.readMemory(0x10440, counters, sizeof counters) && counters[0] == 1 && counters[1] == 0,
        "SIO2 IRQ was lost, repeated, or delivered while interrupts were disabled");
    std::cout << "SIO2 pad protocol, DMA, IRQ, bounds, reset and disconnect regressions passed\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
