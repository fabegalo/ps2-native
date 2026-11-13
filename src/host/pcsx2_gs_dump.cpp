// Optional diagnostic: record the original GS command stream in PCSX2's
// uncompressed GS dump format (v2.8.2, state version 9) so a reference
// renderer can replay exactly what this runtime sent to its GS.
#include "pcsx2_gs_dump.h"
#include "gs_command_backend.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/ps2_memory.h"
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr uint32_t kStateVersion = 9;
constexpr size_t kVramSize = 4u * 1024u * 1024u;
// GSState::GetSaveStateSize(9): version, 15 environment registers, two
// 12-register contexts, 6 vertex registers, transfer buffer, VRAM, 4 GIF paths, Q.
constexpr size_t kStateSize = 4 + 15 * 8 + 2 * 12 * 8 + 6 * 8 + 4 * 2 + 4 * 2 + 8 * 3 + 16 + 4 * 3 + 1 +
                              kVramSize + 4 * (16 + 4) + 4;

class Pcsx2DumpBackend final : public GSRasterBackend, public GSCommandBackend {
public:
    Pcsx2DumpBackend(const GSRegisters& registers, std::string path, uint64_t startTick, uint64_t frames)
        : regs_(registers), path_(std::move(path)), start_(startTick), frames_(frames) {}
    ~Pcsx2DumpBackend() override { if (file_) std::fclose(file_); }

    void Initialize(uint8_t* vram, uint32_t size) override { inner_.Initialize(vram, size); }
    void Reset() override { inner_.Reset(); }
    void Submit(const GSPrimitiveBatch& batch) override { inner_.Submit(batch); }
    void LoadClut(const GSTex0Reg& tex0, const GSTexClutReg& clut) override { inner_.LoadClut(tex0, clut); }
    void BeginTransfer(const GSTransferCommand& command) override { inner_.BeginTransfer(command); }
    void UploadImage(const uint8_t* data, uint32_t size) override {
        // Image data inside a recorded packet is already in the stream; native
        // uploads arrive as register writes followed by this raw payload.
        if (!mirroringGsPacket()) recordImage(data, size);
        inner_.UploadImage(data, size);
    }
    void Flush() override { inner_.Flush(); }
    void TextureFlush() override { inner_.TextureFlush(); }
    void Sync(GSSyncReason reason) override { inner_.Sync(reason); }
    PresentationFrame Present(const GSPresentationRequest& request) override { return inner_.Present(request); }
    bool ClearFramebuffer(const GSContext& context, uint32_t rgba) override { return inner_.ClearFramebuffer(context, rgba); }
    uint32_t ConsumeLocalToHostBytes(uint8_t* dst, uint32_t max) override {
        const uint32_t bytes = inner_.ConsumeLocalToHostBytes(dst, max);
        std::lock_guard<std::mutex> lock(mutex_);
        if (active() && bytes) {
            const uint32_t qwords = (bytes + 15u) / 16u;
            put(uint8_t{2});
            put(qwords);
        }
        return bytes;
    }
    uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override {
        return inner_.ReadVram(psm, base, bw, x, y);
    }
    void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override {
        inner_.WriteVram(psm, base, bw, x, y, value);
    }
    void SnapshotVram(std::vector<uint8_t>& out) const override { inner_.SnapshotVram(out); }
    GSTransferSnapshot GetTransferSnapshot() const override { return inner_.GetTransferSnapshot(); }

    void submitGsPacket(const uint8_t* data, uint32_t size) override {
        std::lock_guard<std::mutex> lock(mutex_);
        advance();
        if (active()) transfer(data, size);
    }
    void writeGsRegister(uint8_t address, uint64_t value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        advance();
        if (!active()) return;
        // One PACKED A+D qword: NLOOP=1, EOP, NREG=1, REGS=A+D.
        const uint64_t packet[4] = {1ull | (1ull << 15) | (1ull << 60), 0xEull, value, address};
        transfer(reinterpret_cast<const uint8_t*>(packet), sizeof(packet));
    }

private:
    bool active() const { return file_ != nullptr; }
    template <class T> void put(const T& value) { write(&value, sizeof(value)); }
    void write(const void* data, size_t size) {
        if (std::fwrite(data, 1, size, file_) != size) throw std::runtime_error("PCSX2 GS dump write failed: " + path_);
    }
    void transfer(const uint8_t* data, uint32_t size) {
        if (!size) return;
        put(uint8_t{0});
        put(uint8_t{3}); // Path1New + 2 = GIF PATH3 in the replayer.
        put(size);
        write(data, size);
    }
    void recordImage(const uint8_t* data, uint32_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        advance();
        if (!active() || !size) return;
        const uint32_t qwords = (size + 15u) / 16u;
        std::vector<uint8_t> packet(16 + size_t(qwords) * 16);
        const uint64_t tag[2] = {qwords | (1ull << 15) | (2ull << 58), 0};
        std::memcpy(packet.data(), tag, sizeof(tag));
        std::memcpy(packet.data() + 16, data, size);
        transfer(packet.data(), static_cast<uint32_t>(packet.size()));
    }
    std::vector<uint8_t> privileged() const {
        std::vector<uint8_t> out(8192);
        const auto at = [&](size_t offset, uint64_t value) { std::memcpy(out.data() + offset, &value, 8); };
        at(0x00, regs_.pmode); at(0x10, regs_.smode1); at(0x20, regs_.smode2); at(0x30, regs_.srfsh);
        at(0x40, regs_.synch1); at(0x50, regs_.synch2); at(0x60, regs_.syncv);
        at(0x70, regs_.dispfb1); at(0x80, regs_.display1); at(0x90, regs_.dispfb2); at(0xA0, regs_.display2);
        at(0xB0, regs_.extbuf); at(0xC0, regs_.extdata); at(0xD0, regs_.extwrite); at(0xE0, regs_.bgcolor);
        at(0x1000, regs_.csr.load()); at(0x1010, regs_.imr); at(0x1040, regs_.busdir); at(0x1080, regs_.siglblid);
        return out;
    }
    // Emits VSync records in stream order; opens and closes the bounded window.
    void advance() {
        if (done_) return;
        const uint64_t tick = regs_.vsyncTick.load();
        if (tick == lastTick_) return;
        lastTick_ = tick;
        if (!file_) {
            if (tick < start_) return;
            open();
            return;
        }
        const auto regs = privileged();
        put(uint8_t{3});
        write(regs.data(), regs.size());
        put(uint8_t{1});
        put(static_cast<uint8_t>((regs_.csr.load() >> 13) & 1u));
        if (++written_ >= frames_) {
            std::fclose(file_);
            file_ = nullptr;
            done_ = true;
            std::fprintf(stderr, "[ps2n-gs-dump] wrote %llu vsyncs to %s\n",
                         static_cast<unsigned long long>(written_), path_.c_str());
        }
    }
    void open() {
        file_ = std::fopen(path_.c_str(), "wb");
        if (!file_) throw std::runtime_error("Cannot create PCSX2 GS dump: " + path_);
        std::vector<uint8_t> state(kStateSize);
        std::memcpy(state.data(), &kStateVersion, 4);
        std::vector<uint8_t> vram;
        inner_.SnapshotVram(vram);
        if (vram.size() != kVramSize) throw std::runtime_error("Unexpected GS VRAM size for PCSX2 dump");
        const size_t vramOffset = kStateSize - 4 - 4 * (16 + 4) - kVramSize;
        std::memcpy(state.data() + vramOffset, vram.data(), kVramSize);
        const float q = 1.0f;
        std::memcpy(state.data() + kStateSize - 4, &q, 4);

        const std::string serial = "SLUS-20115";
        const uint32_t header[9] = {kStateVersion, static_cast<uint32_t>(kStateSize), 36u,
                                    static_cast<uint32_t>(serial.size()), 0u, 0u, 0u,
                                    36u + static_cast<uint32_t>(serial.size()), 0u};
        put(0xFFFFFFFFu);
        put(static_cast<uint32_t>(sizeof(header) + serial.size()));
        write(header, sizeof(header));
        write(serial.data(), serial.size());
        write(state.data(), state.size());
        const auto regs = privileged();
        write(regs.data(), regs.size());
        std::fprintf(stderr, "[ps2n-gs-dump] recording from vsync %llu\n", static_cast<unsigned long long>(lastTick_));
    }

    GSCpuBackend inner_;
    const GSRegisters& regs_;
    std::string path_;
    uint64_t start_;
    uint64_t frames_;
    std::mutex mutex_;
    std::FILE* file_ = nullptr;
    uint64_t lastTick_ = ~uint64_t(0);
    uint64_t written_ = 0;
    bool done_ = false;
};
}

std::unique_ptr<GSRasterBackend> ps2n_pcsx2_gs_dump(const GSRegisters& registers, const std::string& path,
                                                    uint64_t startTick, uint64_t frames) {
    return std::make_unique<Pcsx2DumpBackend>(registers, path, startTick, frames);
}
