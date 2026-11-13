#include "parallel_gs.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/ps2_memory.h"
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <string>
#ifdef PS2N_PARALLEL_GS
#include "gs_command_backend.h"
#include "gs_interface.hpp"
#include "context.hpp"
#include "thread_id.hpp"
#include <mutex>
#include <iostream>

namespace {
class ParallelBackend final : public GSRasterBackend, public GSCommandBackend {
    struct DeviceLock : std::lock_guard<std::recursive_mutex> {
        explicit DeviceLock(std::recursive_mutex& mutex) : std::lock_guard<std::recursive_mutex>(mutex) {
            // EE and presentation use one serialized Granite command-pool index.
            Util::register_thread_index(0);
        }
    };
    static constexpr uint32_t vramSize = 4 * 1024 * 1024;
    const GSRegisters& registers_;
    bool enhanced_;
    mutable std::recursive_mutex mutex_;
    Vulkan::Context context_;
    Vulkan::Device device_;
    ParallelGS::GSInterface gpu_;
    GSCpuBackend memory_;
    uint8_t* vram_ = nullptr;
    Vulkan::BufferHandle readback_;
    bool directHwreg_ = false;
    bool localTransfer_ = false;
    uint64_t lastTraceTick_ = ~uint64_t(0);

    void readMemory() const {
        std::memcpy(vram_, const_cast<ParallelGS::GSInterface&>(gpu_).map_vram_read(0, vramSize), vramSize);
    }
    void writeMemory() {
        std::memcpy(gpu_.map_vram_write(0, vramSize), vram_, vramSize);
        gpu_.end_vram_write(0, vramSize);
    }
public:
    ParallelBackend(const GSRegisters& registers, bool enhanced) : registers_(registers), enhanced_(enhanced) {
        if (!Vulkan::Context::init_loader(nullptr)) throw std::runtime_error("Vulkan loader unavailable");
        context_.set_num_thread_indices(1);
        if (!context_.init_instance_and_device(nullptr, 0, nullptr, 0,
            Vulkan::CONTEXT_CREATION_ENABLE_PUSH_DESCRIPTOR_BIT |
            Vulkan::CONTEXT_CREATION_ENABLE_DESCRIPTOR_HEAP_BIT |
            Vulkan::CONTEXT_CREATION_ENABLE_DESCRIPTOR_BUFFER_BIT))
            throw std::runtime_error("paraLLEl-GS Vulkan device initialization failed");
        device_.set_context(context_);
        device_.init_frame_contexts(2);
        ParallelGS::GSOptions options{};
        options.super_sampling = enhanced ? ParallelGS::SuperSampling::X4 : ParallelGS::SuperSampling::X1;
        if (!gpu_.init(&device_, options)) throw std::runtime_error("paraLLEl-GS requirements unavailable");
        std::cout << "[ps2n-gpu] paraLLEl-GS Vulkan " << device_.get_gpu_properties().deviceName
                  << " mode=" << (enhanced ? "Enhanced" : "Fidelity") << '\n';
    }
    ~ParallelBackend() override { device_.wait_idle(); }
    void Initialize(uint8_t* vram, uint32_t size) override {
        DeviceLock lock(mutex_);
        if (!vram || size != vramSize) throw std::runtime_error("paraLLEl-GS requires 4 MiB console VRAM");
        vram_ = vram;
        memory_.Initialize(vram, size);
        writeMemory();
    }
    void Reset() override {
        DeviceLock lock(mutex_);
        gpu_.flush();
        device_.wait_idle();
        readMemory();
        gpu_.reset_context_state();
        memory_.Reset();
        directHwreg_ = false;
        localTransfer_ = false;
        writeMemory();
    }
    void submitGsPacket(const uint8_t* data, uint32_t size) override {
        DeviceLock lock(mutex_);
        gpu_.gif_transfer(0, data, size);
    }
    void writeGsRegister(uint8_t address, uint64_t value) override {
        DeviceLock lock(mutex_);
        gpu_.write_register(static_cast<ParallelGS::RegisterAddr>(address), value);
        directHwreg_ = address == GS_REG_HWREG;
    }
    // These notifications mirror commands already delivered above, including direct writes.
    void Submit(const GSPrimitiveBatch&) override {}
    void LoadClut(const GSTex0Reg&, const GSTexClutReg&) override {}
    void TextureFlush() override {}
    void BeginTransfer(const GSTransferCommand& command) override {
        DeviceLock lock(mutex_);
        localTransfer_ = command.direction == 2;
        // The CPU helper only packs FIFO data and tracks transfer progress; never rasterizes.
        if (command.direction == 1) readMemory();
        auto bookkeeping = command;
        if (bookkeeping.direction == 2) bookkeeping.direction = 3;
        memory_.BeginTransfer(bookkeeping);
    }
    void UploadImage(const uint8_t* data, uint32_t size) override {
        DeviceLock lock(mutex_);
        if (!mirroringGsPacket() && !directHwreg_) {
            // Native HLE image uploads have no original IMAGE tag.
            if (size % 16) throw std::runtime_error("unaligned native GS image upload");
            uint32_t offset = 0;
            while (offset < size) {
                const auto bytes = std::min<uint32_t>(size - offset, 0x7fff * 16);
                const uint64_t tag[2] = {uint64_t(bytes / 16) | (2ull << 58), 0};
                gpu_.gif_transfer(0, tag, sizeof tag);
                gpu_.gif_transfer(0, data + offset, bytes);
                offset += bytes;
            }
        }
        directHwreg_ = false;
        memory_.UploadImage(data, size);
    }
    void Flush() override { DeviceLock lock(mutex_); gpu_.flush(); }
    void Sync(GSSyncReason reason) override {
        DeviceLock lock(mutex_);
        gpu_.flush();
        if (reason == GSSyncReason::Finish || reason == GSSyncReason::Reset) device_.wait_idle();
    }
    PresentationFrame Present(const GSPresentationRequest& request) override {
        DeviceLock lock(mutex_);
        if (std::getenv("PS2N_GS_COMPARE") && (request.vsyncTick == 127 || request.vsyncTick == 239)) {
            readMemory();
            std::vector<uint8_t> snapshot;
            memory_.SnapshotVram(snapshot);
            ps2n_trace_vram(snapshot, request.vsyncTick);
        }
        auto& p = gpu_.get_priv_register_state();
        const uint64_t values[] = {request.pmode, registers_.smode1, request.smode2,
            registers_.srfsh, registers_.synch1, registers_.synch2, registers_.syncv,
            request.dispfb1, request.display1, request.dispfb2, request.display2,
            registers_.extbuf, registers_.extdata, registers_.extwrite, request.bgcolor};
        for (unsigned i = 0; i < 15; ++i) p.qwords_lo[2 * i] = values[i];
        p.qwords_hi[0] = registers_.csr.load(std::memory_order_acquire);
        p.qwords_hi[2] = registers_.imr;
        p.qwords_hi[8] = registers_.busdir;
        p.qwords_hi[16] = registers_.siglblid;
        ParallelGS::VSyncInfo info{};
        info.phase = (p.qwords_hi[0] >> 13) & 1;
        info.dst_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        info.dst_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
        info.dst_access = VK_ACCESS_2_TRANSFER_READ_BIT;
        info.high_resolution_scanout = enhanced_;
        info.force_progressive = enhanced_;
        auto scanout = gpu_.vsync(info);
        PresentationFrame frame;
        if (scanout.image) {
            frame.width = scanout.image->get_width();
            frame.height = scanout.image->get_height();
            frame.displayFbp = frame.sourceFbp = (request.pmode & 1 ? request.dispfb1 : request.dispfb2) & 0x1ff;
            frame.pixels.resize(size_t(frame.width) * frame.height * 4);
            if (!readback_ || readback_->get_create_info().size != frame.pixels.size()) {
                Vulkan::BufferCreateInfo buffer{};
                buffer.size = frame.pixels.size();
                buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                buffer.domain = Vulkan::BufferDomain::CachedHost;
                readback_ = device_.create_buffer(buffer);
                if (!readback_) throw std::runtime_error("GS scanout readback allocation failed");
            }
            auto cmd = device_.request_command_buffer();
            cmd->copy_image_to_buffer(*readback_, *scanout.image, 0, {},
                {frame.width, frame.height, 1}, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
            cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
            device_.submit(cmd);
            device_.wait_idle();
            const void* pixels = device_.map_host_buffer(*readback_, Vulkan::MEMORY_ACCESS_READ_BIT);
            if (!pixels) throw std::runtime_error("GS scanout mapping failed");
            std::memcpy(frame.pixels.data(), pixels, frame.pixels.size());
            device_.unmap_host_buffer(*readback_, Vulkan::MEMORY_ACCESS_READ_BIT);
        }
        scanout.image.reset();
        device_.next_frame_context();
        ps2n_trace_scanout(frame, request);
        if (std::getenv("PS2N_GS_TRACE") && (lastTraceTick_ == ~uint64_t(0) || request.vsyncTick >= lastTraceTick_ + 60)) {
            lastTraceTick_ = request.vsyncTick;
            std::printf("[ps2n-scanout] guest=%llu smode1=%llx smode2=%llx pmode=%llx %ux%u enhanced=%d\n",
                static_cast<unsigned long long>(request.vsyncTick),
                static_cast<unsigned long long>(values[1]), static_cast<unsigned long long>(request.smode2),
                static_cast<unsigned long long>(request.pmode), frame.width, frame.height, enhanced_);
            std::fflush(stdout);
        }
        return frame;
    }
    bool ClearFramebuffer(const GSContext& context, uint32_t rgba) override {
        DeviceLock lock(mutex_);
        readMemory();
        const bool result = memory_.ClearFramebuffer(context, rgba);
        if (result) writeMemory();
        return result;
    }
    uint32_t ConsumeLocalToHostBytes(uint8_t* dst, uint32_t size) override {
        DeviceLock lock(mutex_);
        return memory_.ConsumeLocalToHostBytes(dst, size);
    }
    uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override {
        DeviceLock lock(mutex_);
        readMemory();
        return memory_.ReadVram(psm, base, bw, x, y);
    }
    void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override {
        DeviceLock lock(mutex_);
        readMemory();
        memory_.WriteVram(psm, base, bw, x, y, value);
        writeMemory();
    }
    void SnapshotVram(std::vector<uint8_t>& out) const override {
        DeviceLock lock(mutex_);
        readMemory();
        memory_.SnapshotVram(out);
    }
    GSTransferSnapshot GetTransferSnapshot() const override {
        DeviceLock lock(mutex_);
        auto snapshot = memory_.GetTransferSnapshot();
        if (localTransfer_) snapshot.copiedPixels = snapshot.totalPixels;
        return snapshot;
    }
};
}
#endif

void ps2n_trace_vram(const std::vector<uint8_t>& vram, uint64_t tick) {
    for (size_t page = 0; page < vram.size(); page += 256 * 1024) {
        uint64_t hash = 14695981039346656037ull;
        size_t nonzero = 0;
        for (size_t i = page; i < std::min(vram.size(), page + 256 * 1024); ++i) {
            hash ^= vram[i]; hash *= 1099511628211ull;
            nonzero += vram[i] != 0;
        }
        std::printf("[ps2n-vram] guest=%llu offset=%zx hash=%llx nonzero=%zu\n",
            static_cast<unsigned long long>(tick), page, static_cast<unsigned long long>(hash), nonzero);
    }
}

void ps2n_trace_scanout(const PresentationFrame& frame, const GSPresentationRequest& request) {
    // Optional bounded captures, shared by every backend for visual comparison.
    static const char* shots = std::getenv("PS2N_GS_SHOT");
    static unsigned shotCount = 0;
    static uint64_t nextShot = 0;
    const auto anyLit = [&] {
        for (size_t i = 0; i + 3 < frame.pixels.size(); i += 4)
            if (frame.pixels[i] | frame.pixels[i + 1] | frame.pixels[i + 2]) return true;
        return false;
    };
    if (shots && frame && request.vsyncTick >= nextShot && shotCount < 12 && anyLit()) {
        nextShot = request.vsyncTick + 90;
        const auto path = std::string(shots) + "/shot-" + std::to_string(request.vsyncTick) + ".ppm";
        if (auto* file = std::fopen(path.c_str(), "wb")) {
            const size_t rows = frame.width ? frame.pixels.size() / 4 / frame.width : 0;
            std::fprintf(file, "P6\n%u %zu\n255\n", frame.width, rows);
            for (size_t i = 0; i + 3 < frame.pixels.size(); i += 4) std::fwrite(&frame.pixels[i], 1, 3, file);
            std::fclose(file);
            ++shotCount;
        }
    }
    static const bool trace = std::getenv("PS2N_GS_COMPARE") != nullptr;
    if (!trace) return;
    uint64_t hash = 14695981039346656037ull;
    size_t lit = 0;
    for (size_t i = 0; i + 3 < frame.pixels.size(); i += 4) {
        for (unsigned c = 0; c < 3; ++c) { hash ^= frame.pixels[i + c]; hash *= 1099511628211ull; }
        lit += (frame.pixels[i] | frame.pixels[i + 1] | frame.pixels[i + 2]) != 0;
    }
    std::printf("[ps2n-frame] guest=%llu size=%ux%u rgb=%llx lit=%zu pmode=%llx smode2=%llx dispfb1=%llx display1=%llx\n",
        static_cast<unsigned long long>(request.vsyncTick), frame.width, frame.height,
        static_cast<unsigned long long>(hash), lit, static_cast<unsigned long long>(request.pmode),
        static_cast<unsigned long long>(request.smode2), static_cast<unsigned long long>(request.dispfb1),
        static_cast<unsigned long long>(request.display1));
    std::fflush(stdout);
}

std::unique_ptr<GSRasterBackend> ps2n_parallel_gs(const GSRegisters& registers, bool enhanced) {
#ifdef PS2N_PARALLEL_GS
    return std::make_unique<ParallelBackend>(registers, enhanced);
#else
    throw std::runtime_error("paraLLEl-GS is unavailable in this build (PS2N_PARALLEL_GS=OFF)");
#endif
}
