#include "gs_interface.hpp"
#include "context.hpp"
#include "runtime/gs/ps2_gs_memory.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "gs_command_backend.h"
#include "parallel_gs.h"
#include "runtime/ps2_memory.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <thread>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

// The CPU device is a test oracle: the production GPU path must not raster twice.
class ComparedDevice final : public GSRasterBackend, public GSCommandBackend {
public:
    explicit ComparedDevice(ParallelGS::GSInterface& device) : gpu(device) {}
    ParallelGS::GSInterface& gpu;
    GSCpuBackend cpu;
    unsigned packets = 0, registers = 0, draws = 0;
    bool failPacket = false;
    void submitGsPacket(const uint8_t* data, uint32_t size) override {
        if (failPacket) throw std::runtime_error("injected packet failure");
        ++packets;
        gpu.gif_transfer(0, data, size);
    }
    void writeGsRegister(uint8_t address, uint64_t value) override {
        ++registers;
        gpu.write_register(static_cast<ParallelGS::RegisterAddr>(address), value);
    }
    void Initialize(uint8_t* vram, uint32_t size) override { cpu.Initialize(vram, size); }
    void Reset() override { cpu.Reset(); }
    void Submit(const GSPrimitiveBatch& batch) override { ++draws; cpu.Submit(batch); }
    void LoadClut(const GSTex0Reg& tex0, const GSTexClutReg& clut) override { cpu.LoadClut(tex0, clut); }
    void BeginTransfer(const GSTransferCommand& command) override { cpu.BeginTransfer(command); }
    void UploadImage(const uint8_t* data, uint32_t size) override { cpu.UploadImage(data, size); }
    void Flush() override { cpu.Flush(); gpu.flush(); }
    void TextureFlush() override { cpu.TextureFlush(); }
    void Sync(GSSyncReason reason) override { cpu.Sync(reason); gpu.flush(); }
    PresentationFrame Present(const GSPresentationRequest& request) override { return cpu.Present(request); }
    bool ClearFramebuffer(const GSContext& context, uint32_t rgba) override { return cpu.ClearFramebuffer(context, rgba); }
    uint32_t ConsumeLocalToHostBytes(uint8_t* dst, uint32_t size) override { return cpu.ConsumeLocalToHostBytes(dst, size); }
    uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override {
        return cpu.ReadVram(psm, base, bw, x, y);
    }
    void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override {
        cpu.WriteVram(psm, base, bw, x, y, value);
    }
    void SnapshotVram(std::vector<uint8_t>& out) const override { cpu.SnapshotVram(out); }
    GSTransferSnapshot GetTransferSnapshot() const override { return cpu.GetTransferSnapshot(); }
};

static void frontendCommands(Vulkan::Device& device) {
    ParallelGS::GSInterface gpu;
    require(gpu.init(&device, {}), "command boundary GPU initialization failed");
    constexpr unsigned vramSize = 4 * 1024 * 1024;
    std::memset(gpu.map_vram_write(0, vramSize), 0, vramSize);
    gpu.end_vram_write(0, vramSize);
    std::vector<uint8_t> reference(vramSize);
    GS frontend;
    frontend.init(reference.data(), vramSize);
    auto owned = std::make_unique<ComparedDevice>(gpu);
    auto& backend = *owned;
    frontend.setRasterBackend(std::move(owned));
    // One PACKED A+D tag, including original Q and both vertex kicks.
    const uint64_t commands[][2] = {
        {uint64_t(1) << 16, GS_REG_FRAME_1},
        {uint64_t(1) << 32, GS_REG_ZBUF_1},
        {(uint64_t(63) << 16) | (uint64_t(63) << 48), GS_REG_SCISSOR_1},
        {0, GS_REG_XYOFFSET_1},
        {(1u << 16) | (1u << 17), GS_REG_TEST_1},
        {1, GS_REG_PRMODECONT},
        {GS_PRIM_SPRITE, GS_REG_PRIM},
        {0x3f80000080563412ull, GS_REG_RGBAQ},
        {uint64_t(4 * 16) | (uint64_t(6 * 16) << 16), GS_REG_XYZ2},
        {uint64_t(20 * 16) | (uint64_t(18 * 16) << 16), GS_REG_XYZ2},
    };
    std::vector<uint64_t> packet = {10ull | (1ull << 15) | (1ull << 60), 0xe};
    for (const auto& command : commands) packet.insert(packet.end(), command, command + 2);
    const auto* bytes = reinterpret_cast<const uint8_t*>(packet.data());
    const auto size = static_cast<uint32_t>(packet.size() * sizeof(uint64_t));
    for (bool packed : {false, true}) {
        const auto before = backend.packets;
        if (packed) require(frontend.processNativePackedGIFPacket(bytes, size), "valid packed packet rejected");
        else frontend.processGIFPacket(bytes, size);
        require(backend.packets == before + 1 && backend.registers == 0,
                "raw packet was lost or decoded register commands were duplicated");
        gpu.flush();
        require(std::memcmp(gpu.map_vram_read(0, vramSize), reference.data(), vramSize) == 0,
                "original GIF GPU rendering differs from SDK CPU VRAM");
    }
    require(backend.draws == 2, "CPU mirror lost vertex kicks");
    require(!frontend.processNativePackedGIFPacket(bytes, size - 16), "truncated packet accepted");
    require(backend.packets == 2, "rejected packed packet reached GPU");
    backend.failPacket = true;
    bool caught = false;
    try { frontend.processGIFPacket(bytes, size); }
    catch (const std::runtime_error&) { caught = true; }
    require(caught && !backend.mirroringGsPacket(), "packet failure left mirror suppression active");
    backend.failPacket = false;
    // Direct HLE register writes must still reach the device after both paths.
    frontend.writeRegister(GS_REG_RGBAQ, 0x3f800000ff112233ull);
    require(backend.registers == 1, "direct register command was suppressed");
    unsigned fidelityWidth = 0, fidelityHeight = 0;
    for (bool enhanced : {false, true}) {
        GSRegisters registers{};
        ParallelGS::Reg64<ParallelGS::SMODE1Bits> smode{};
        smode.desc.CMOD = ParallelGS::SMODE1Bits::CMOD_NTSC;
        smode.desc.LC = ParallelGS::SMODE1Bits::LC_ANALOG;
        registers.smode1 = smode.bits;
        registers.pmode = 1 | (1ull << 5) | (255ull << 8);
        registers.dispfb1 = 1ull << 9; // CT32, FBW=1.
        registers.display1 = 636ull | (25ull << 12) | (3ull << 23) | (255ull << 32) | (63ull << 44);
        std::vector<uint8_t> vram(vramSize);
        GS host;
        host.init(vram.data(), vramSize, &registers);
        host.setRasterBackend(ps2n_parallel_gs(registers, enhanced));
        host.processGIFPacket(bytes, size);
        host.refreshDisplaySnapshot();
        uint32_t mappedSize = 0;
        const auto* mapped = host.lockDisplaySnapshot(mappedSize);
        const bool equal = mappedSize == vramSize && !std::memcmp(mapped, reference.data(), vramSize);
        host.unlockDisplaySnapshot();
        require(equal, "production adapter lost or duplicated original rendering");
        uint32_t width = 0, height = 0;
        std::vector<uint8_t> pixels;
        for (unsigned frame = 0; frame < 6; ++frame) {
            registers.vsyncTick = frame;
            host.latchHostPresentationFrame();
            require(host.copyLatchedHostPresentationFrame(pixels, width, height), "production scanout missing");
            require(pixels.size() == size_t(width) * height * 4, "scanout byte count mismatch");
            size_t lit = 0;
            for (size_t i = 0; i < pixels.size(); i += 4)
                lit += pixels[i] == 0x12 && pixels[i + 1] == 0x34 && pixels[i + 2] == 0x56;
            require(lit > 0, "real scanout lost the sprite");
        }
        if (!enhanced) { fidelityWidth = width; fidelityHeight = height; }
        else require(width == 2 * fidelityWidth && height == 2 * fidelityHeight,
                     "Enhanced did not use high resolution scanout");
        // FIFO reads pack actual GPU VRAM with the existing SDK helper.
        host.writeRegister(GS_REG_BITBLTBUF, 1ull << 16);
        host.writeRegister(GS_REG_TRXPOS, 4ull | (6ull << 16));
        host.writeRegister(GS_REG_TRXREG, 4ull | (1ull << 32));
        host.writeRegister(GS_REG_TRXDIR, 1);
        uint32_t fifo[4]{};
        require(host.consumeLocalToHostBytes(reinterpret_cast<uint8_t*>(fifo), sizeof fifo) == sizeof fifo,
                "GPU FIFO byte count mismatch");
        for (auto pixel : fifo) require(pixel == 0x80563412, "GPU FIFO lost VRAM coherence");
        const uint32_t upload[4] = {0x12345678, 0x87654321, 0xaabbccdd, 0x11223344};
        host.uploadImageNative((0x100ull << 32) | (1ull << 48), 0,
                              4ull | (1ull << 32), 0,
                              reinterpret_cast<const uint8_t*>(upload), sizeof upload);
        for (unsigned x = 0; x < 4; ++x)
            require(host.ReadVram(0, 0x100, 1, x, 0) == upload[x], "native upload differs from GPU VRAM");
        host.writeRegister(GS_REG_BITBLTBUF, (0x200ull << 32) | (1ull << 48));
        host.writeRegister(GS_REG_TRXPOS, 0);
        host.writeRegister(GS_REG_TRXREG, 4ull | (1ull << 32));
        host.writeRegister(GS_REG_TRXDIR, 0);
        host.writeRegister(GS_REG_HWREG, 0x8765432112345678ull);
        host.writeRegister(GS_REG_HWREG, 0x11223344aabbccddull);
        for (unsigned x = 0; x < 4; ++x)
            require(host.ReadVram(0, 0x200, 1, x, 0) == upload[x], "direct HWREG duplicated its image data");
        host.writeRegister(GS_REG_BITBLTBUF, 0x100ull | (1ull << 16) | (0x300ull << 32) | (1ull << 48));
        host.writeRegister(GS_REG_TRXDIR, 2);
        for (unsigned x = 0; x < 4; ++x)
            require(host.ReadVram(0, 0x300, 1, x, 0) == upload[x], "local transfer lost coherence");
        host.writeRegister(GS_REG_SIGNAL, 0xffffffff12345678ull);
        host.writeRegister(GS_REG_FINISH, 0);
        require((registers.csr.load() & 3) == 3 && uint32_t(registers.siglblid) == 0x12345678,
                "frontend CSR/SIGNAL/FINISH ownership changed");
        host.reset();
        require(host.ReadVram(0, 0x300, 1, 3, 0) == upload[3], "GS reset lost local memory");
        uint32_t threadedPixel = 0;
        std::thread reader([&] { threadedPixel = host.ReadVram(0, 0x300, 1, 3, 0); });
        reader.join();
        require(threadedPixel == upload[3], "serialized Vulkan access from another host thread failed");
        std::cout << "PASS production host scanout " << width << 'x' << height
                  << ", VRAM, FIFO, recycled contexts, enhanced=" << enhanced << std::endl;
    }
    device.wait_idle();
    std::cout << "PASS original GIF/packed command boundary, full CPU/GPU VRAM, rejection and recovery" << std::endl;
}

static void drawAndRead(Vulkan::Device& device, ParallelGS::SuperSampling sampling) {
    using namespace ParallelGS;
    GSInterface gs;
    GSOptions options{};
    options.super_sampling = sampling;
    require(gs.init(&device, options), "paraLLEl-GS requirements or initialization failed");
    auto* initial = gs.map_vram_write(0, options.vram_size);
    require(initial != nullptr, "VRAM write mapping failed");
    std::memset(initial, 0, options.vram_size);
    gs.end_vram_write(0, options.vram_size);

    FRAMEBits frame{};
    frame.FBW = 1;
    frame.PSM = PSMCT32;
    gs.write_register(RegisterAddr::FRAME_1, frame);
    ZBUFBits depth{};
    depth.ZMSK = 1;
    gs.write_register(RegisterAddr::ZBUF_1, depth);
    SCISSORBits scissor{};
    scissor.SCAX1 = scissor.SCAY1 = 63;
    gs.write_register(RegisterAddr::SCISSOR_1, scissor);
    gs.write_register(RegisterAddr::XYOFFSET_1, XYOFFSETBits{});
    TESTBits test{};
    test.ZTE = 1;
    test.ZTST = 1;
    gs.write_register(RegisterAddr::TEST_1, test);
    PRMODECONTBits control{};
    control.AC = 1;
    gs.write_register(RegisterAddr::PRMODECONT, control);
    PRIMBits prim{};
    prim.PRIM = static_cast<uint32_t>(PRIMType::Sprite);
    gs.write_register(RegisterAddr::PRIM, prim);
    RGBAQBits color{};
    color.R = 0x12; color.G = 0x34; color.B = 0x56; color.A = 0x80; color.Q = 1.0f;
    gs.write_register(RegisterAddr::RGBAQ, color);
    XYZBits position{};
    position.X = 4 * 16; position.Y = 6 * 16;
    gs.write_register(RegisterAddr::XYZ2, position);
    position.X = 20 * 16; position.Y = 18 * 16;
    gs.write_register(RegisterAddr::XYZ2, position);
    gs.flush();

    std::vector<uint8_t> expected(options.vram_size);
    for (unsigned y = 6; y < 18; ++y)
        for (unsigned x = 4; x < 20; ++x)
            GSMem::WriteCT32(expected.data(), 0, 1, x, y, 0x80563412u);
    const void* actual = gs.map_vram_read(0, options.vram_size);
    require(actual && std::memcmp(actual, expected.data(), expected.size()) == 0,
            "GPU sprite/readback differs from the full CPU VRAM reference");
    std::cout << "PASS Vulkan GS sprite + 4 MiB coherent readback, samples=" << static_cast<unsigned>(sampling) << std::endl;
    device.wait_idle();
}

int main() try {
    GSMem::InitLookupTables();
    {
        // PATH2 header: A+D x4, then an IMAGE tag whose 4 qwords come in the next packet.
        uint64_t packet[12] = {4ull | (1ull << 60), 0xE};
        packet[10] = 4ull | (2ull << 58);
        const auto* bytes = reinterpret_cast<const uint8_t*>(packet);
        require(ps2n_whole_gif_tags(bytes, 96) == 80 && ps2n_whole_gif_tags(bytes, 80) == 80 &&
                ps2n_gif_loops_in(packet[10], 0) == 0 && ps2n_gif_loops_in(packet[10], 32) == 2 &&
                ps2n_gif_payload_bytes(3ull | (1ull << 58) | (3ull << 60)) == 80,
                "GIF packet framing differs from the frontend parser");
    }
    require(Vulkan::Context::init_loader(nullptr), "Vulkan loader unavailable");
    Vulkan::Context context;
    context.set_num_thread_indices(1);
    require(context.init_instance_and_device(nullptr, 0, nullptr, 0,
        Vulkan::CONTEXT_CREATION_ENABLE_PUSH_DESCRIPTOR_BIT |
        Vulkan::CONTEXT_CREATION_ENABLE_DESCRIPTOR_HEAP_BIT |
        Vulkan::CONTEXT_CREATION_ENABLE_DESCRIPTOR_BUFFER_BIT), "Vulkan device initialization failed");
    Vulkan::Device device;
    device.set_context(context);
    device.init_frame_contexts(2);
    std::cout << "GPU=" << device.get_gpu_properties().deviceName << std::endl;
    drawAndRead(device, ParallelGS::SuperSampling::X1);
    drawAndRead(device, ParallelGS::SuperSampling::X2);
    frontendCommands(device);
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
