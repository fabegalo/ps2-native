// Host entry point of a game recompiled by PS2Recomp: the runtime, kernel,
// IOP, GS and audio are the pinned PS2Recomp ps2xRuntime, used unchanged.
//
//   ps2n-game-<id> <boot-elf> <cd-image>
// The working directory is the extracted disc (cdrom0:); <cd-image> holds the
// 2048-byte user sectors for raw LBN reads.
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "runtime/gs/gs_cpu_backend.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {
// PS2N_GS_TRACE: forwards to the PS2Recomp CPU backend and summarizes the
// image transfers it receives (destination, size, non-zero bytes).
class TracingBackend final : public GSRasterBackend {
public:
    void Initialize(uint8_t* vram, uint32_t size) override { inner_.Initialize(vram, size); }
    void Reset() override { inner_.Reset(); }
    void Submit(const GSPrimitiveBatch& batch) override { inner_.Submit(batch); }
    void LoadClut(const GSTex0Reg& tex0, const GSTexClutReg& clut) override { inner_.LoadClut(tex0, clut); }
    void BeginTransfer(const GSTransferCommand& command) override {
        report();
        current_ = command;
        bytes_ = nonzero_ = 0;
        inner_.BeginTransfer(command);
    }
    void UploadImage(const uint8_t* data, uint32_t size) override {
        bytes_ += size;
        for (uint32_t i = 0; i < size; ++i) nonzero_ += data[i] != 0;
        inner_.UploadImage(data, size);
    }
    void Flush() override { inner_.Flush(); }
    void TextureFlush() override { inner_.TextureFlush(); }
    void Sync(GSSyncReason reason) override { inner_.Sync(reason); }
    PresentationFrame Present(const GSPresentationRequest& request) override {
        auto frame = inner_.Present(request);
        if (frame && ++presents_ % 30 == 0) {
            std::size_t lit = 0;
            for (std::size_t i = 0; i + 3 < frame.pixels.size(); i += 4) lit += (frame.pixels[i] | frame.pixels[i + 1] | frame.pixels[i + 2]) != 0;
            const unsigned rows = frame.width ? static_cast<unsigned>(frame.pixels.size() / 4 / frame.width) : 0;
            std::printf("[ps2n-present] %u display=%x source=%x %ux%u rows=%u lit=%zu dispfb1=%llx display1=%llx pmode=%llx\n",
                        presents_, frame.displayFbp, frame.sourceFbp, frame.width, frame.height, rows, lit,
                        static_cast<unsigned long long>(request.dispfb1), static_cast<unsigned long long>(request.display1),
                        static_cast<unsigned long long>(request.pmode));
            std::fflush(stdout);
            if (lit && std::getenv("PS2N_GS_DUMP")) {
                const auto name = "present-" + std::to_string(presents_ / 30) + ".ppm";
                if (auto* f = std::fopen(name.c_str(), "wb")) {
                    std::fprintf(f, "P6\n%u %u\n255\n", frame.width, rows);
                    for (std::size_t i = 0; i + 3 < frame.pixels.size(); i += 4) std::fwrite(&frame.pixels[i], 1, 3, f);
                    std::fclose(f);
                }
            }
        }
        return frame;
    }
    bool ClearFramebuffer(const GSContext& context, uint32_t rgba) override { return inner_.ClearFramebuffer(context, rgba); }
    uint32_t ConsumeLocalToHostBytes(uint8_t* dst, uint32_t max) override { return inner_.ConsumeLocalToHostBytes(dst, max); }
    uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override {
        return inner_.ReadVram(psm, base, bw, x, y);
    }
    void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override {
        inner_.WriteVram(psm, base, bw, x, y, value);
    }
    void SnapshotVram(std::vector<uint8_t>& out) const override { inner_.SnapshotVram(out); }
    GSTransferSnapshot GetTransferSnapshot() const override { return inner_.GetTransferSnapshot(); }

private:
    void report() {
        if (bytes_ == 0 && current_.direction != 2) return;
        const auto& b = current_.bitbltbuf;
        std::printf("[ps2n-xfer] dir=%u sbp=%x spsm=%x dbp=%x dbw=%u dpsm=%x at %u,%u size %ux%u bytes=%u nonzero=%u\n",
                    current_.direction, b.sbp, b.spsm, b.dbp, b.dbw, b.dpsm, current_.trxpos.dsax,
                    current_.trxpos.dsay, current_.trxreg.rrw, current_.trxreg.rrh, bytes_, nonzero_);
        std::fflush(stdout);
    }
    GSCpuBackend inner_;
    GSTransferCommand current_{};
    uint32_t bytes_{}, nonzero_{}, presents_{};
};

// PS2N_GS_DUMP=tbp,tbw,psm,width,height,cbp: writes that texture, through
// its CT32 CSM1 CLUT for T4/T8, to gs-dump.ppm in the working directory.
void dump_texture(PS2Runtime& runtime, const char* spec) {
    unsigned tbp = 0, tbw = 0, psm = 0, w = 0, h = 0, cbp = 0;
    if (std::sscanf(spec, "%x,%u,%x,%u,%u,%x", &tbp, &tbw, &psm, &w, &h, &cbp) != 6 || !w || !h) return;
    std::vector<unsigned char> rgb(static_cast<std::size_t>(w) * h * 3);
    auto& gs = runtime.gs();
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            std::uint32_t c = gs.ReadVram(psm, tbp, tbw, x, y);
            if (psm == 0x14 || psm == 0x13) {
                // CSM1 CT32: entries 8-15 and 16-23 of each 32-entry group are swapped.
                const unsigned i = psm == 0x14 ? (c & 15u) : (c & 255u);
                const unsigned s = (i & ~0x18u) | ((i & 0x08u) << 1) | ((i & 0x10u) >> 1);
                c = gs.ReadVram(0, cbp, 1, s & 15u, s >> 4);
            }
            const auto p = (static_cast<std::size_t>(y) * w + x) * 3;
            rgb[p] = c & 0xFF; rgb[p + 1] = (c >> 8) & 0xFF; rgb[p + 2] = (c >> 16) & 0xFF;
        }
    static int index = 0;
    const auto name = "gs-dump-" + std::to_string(index++) + ".ppm";
    if (auto* f = std::fopen(name.c_str(), "wb")) {
        std::fprintf(f, "P6\n%u %u\n255\n", w, h);
        std::fwrite(rgb.data(), 1, rgb.size(), f);
        std::fclose(f);
    }
}

// PS2N_GS_TRACE: every 2 s, a summary of the PS2Recomp GS debug history
// (its last 512 events), for diagnosing what a game draws.
void trace_gs(PS2Runtime& runtime) {
    for (;;) {
        runtime.gs().clearDebugHistory();
        runtime.gs().setDebugHistoryPaused(false);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        runtime.gs().setDebugHistoryPaused(true);
        const auto history = runtime.gs().getDebugHistory();
        std::map<int, int> kinds;
        int textured = 0;
        std::map<std::string, int> detail;
        for (const auto& e : history) {
            ++kinds[static_cast<int>(e.kind)];
            char line[160]{};
            if (e.kind == GSDebugEventKind::Draw) {
                textured += e.prim.tme;
                std::snprintf(line, sizeof line, "draw prim=%d tme=%d fbp=%x psm=%x tbp=%x tpsm=%x %.0f-%.0f,%.0f-%.0f",
                              static_cast<int>(e.prim.type), e.prim.tme, e.frame.fbp, e.frame.psm, e.tex0.tbp0,
                              e.tex0.psm, e.xMin, e.xMax, e.yMin, e.yMax);
            } else if (e.kind == GSDebugEventKind::Transfer) {
                std::snprintf(line, sizeof line, "xfer dir=%u dbp=%x dpsm=%x %ux%u px=%u", e.trxdir, e.bitbltbuf.dbp,
                              e.bitbltbuf.dpsm, e.trxreg.rrw, e.trxreg.rrh, e.transferPixels);
            } else if (e.kind == GSDebugEventKind::Present) {
                std::snprintf(line, sizeof line, "present display=%x source=%x %ux%u", e.displayFbp, e.sourceFbp,
                              e.width, e.height);
            } else {
                continue;
            }
            ++detail[line];
        }
        std::cout << "[ps2n-gs] events=" << history.size() << " gif=" << kinds[0] << " reg=" << kinds[1]
                  << " draw=" << kinds[2] << " textured=" << textured << " xfer=" << kinds[3]
                  << " present=" << kinds[4] << " uploads=" << runtime.gs().nativeImageUploadCount() << "\n";
        // Guest threads: status (0 running .. 5 dormant), wait reason (1 sleep,
        // 2 sema, 3 event flag, 4 vsync, 5 external) and where each one is.
        const auto kernel = runtime.eeScheduler().snapshot();
        std::cout << "[ps2n-ee] cycle=" << kernel.eeCycle << " vsync=" << runtime.eeScheduler().currentVSyncTick()
                  << " running=" << kernel.runningThreadId << std::hex << " cpu.pc=" << runtime.cpu().pc;
        for (const auto& t : kernel.threads)
            std::cout << " t" << t.id << ":s" << +static_cast<int>(t.status) << "w" << +static_cast<int>(t.waitReason)
                      << "/" << t.waitId << "p" << std::dec << t.currentPriority << std::hex << "@" << t.pc << "<" << t.ra << "^" << t.sp;
        std::cout << std::dec << "\n";
        // PS2N_EE_WORDS=addr[,addr...]: guest RAM words, for following game data structures.
        if (const char* words = std::getenv("PS2N_EE_WORDS")) {
            const uint8_t* ram = runtime.memory().getRDRAM();
            std::cout << "[ps2n-ram]" << std::hex;
            for (const char* p = words; *p;) {
                char* end = nullptr;
                const unsigned long addr = std::strtoul(p, &end, 16) & 0x1FFFFFCu;
                if (end == p) break;
                uint32_t value = 0;
                std::memcpy(&value, ram + addr, sizeof value);
                std::cout << " " << addr << "=" << value;
                p = *end == ',' ? end + 1 : end;
            }
            std::cout << std::dec << "\n";
        }
        for (const auto& [line, count] : detail) std::cout << "[ps2n-gs]   " << count << "x " << line << "\n";
        // First textured draw: its TEX0, a row of texels and its CLUT block from VRAM.
        for (const auto& e : history) {
            if (e.kind != GSDebugEventKind::Draw || !e.prim.tme) continue;
            const auto& t = e.tex0;
            std::cout << "[ps2n-gs] tex0 tbp=" << std::hex << t.tbp0 << " tbw=" << +t.tbw << " psm=" << +t.psm
                      << " tw=" << +t.tw << " th=" << +t.th << " tcc=" << +t.tcc << " tfx=" << +t.tfx << " cbp=" << t.cbp
                      << " cpsm=" << +t.cpsm << " csm=" << +t.csm << " csa=" << +t.csa << " cld=" << +t.cld
                      << " fst=" << e.prim.fst << " abe=" << e.prim.abe << " alpha=" << e.alpha << " test=" << e.test
                      << "\n[ps2n-gs] texels";
            const int h = 1 << t.th, w = 1 << t.tw;
            for (int i = 0; i < 16; ++i)
                std::cout << ' ' << runtime.gs().ReadVram(t.psm, t.tbp0, t.tbw, (w * i) / 16, h / 2);
            std::cout << "\n[ps2n-gs] clut";
            for (int i = 0; i < 16; ++i) std::cout << ' ' << runtime.gs().ReadVram(t.cpsm, t.cbp, 1, i, 0);
            std::cout << std::dec << "\n";
            break;
        }
        std::cout.flush();
        if (const char* spec = std::getenv("PS2N_GS_DUMP")) dump_texture(runtime, spec);
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << (argc ? argv[0] : "ps2n-game") << " <boot-elf> <cd-image>\n";
        return 2;
    }
    try {
        const std::filesystem::path elf = std::filesystem::absolute(argv[1]);
        PS2Runtime runtime;
        if (!runtime.initialize(("ps2-native | " + elf.filename().string()).c_str())) {
            std::cerr << "PS2Recomp runtime initialization failed\n";
            return 1;
        }
        if (!runtime.loadELF(elf.string())) {
            std::cerr << "PS2Recomp could not load " << elf.string() << "\n";
            return 1;
        }
        auto paths = PS2Runtime::getIoPaths();
        paths.cdImage = std::filesystem::absolute(argv[2]);
        PS2Runtime::setIoPaths(paths);
        if (std::getenv("PS2N_GS_TRACE")) {
            runtime.gs().setRasterBackend(std::make_unique<TracingBackend>());
            std::thread(trace_gs, std::ref(runtime)).detach();
        }
        runtime.run();
    } catch (const std::exception& error) {
        std::cerr << "ps2n-game: " << error.what() << "\n";
        std::cerr.flush();
        std::_Exit(1);
    }
    // Same exit path as the PS2Recomp runner: guest threads are not joined.
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(0);
}
