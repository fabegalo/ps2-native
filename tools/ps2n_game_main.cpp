// Host entry point of a game recompiled by PS2Recomp: the runtime, kernel,
// IOP and video belong to the pinned PS2Recomp; SPU2 uses its device boundary.
//
//   ps2n-game-<id> <boot-elf> <cd-image>
// The working directory is the extracted disc (cdrom0:); <cd-image> holds the
// 2048-byte user sectors for raw LBN reads.
#include "ps2_runtime.h"
#include "pcsx2_audio.h"
#include "parallel_gs.h"
#include "pcsx2_gs_dump.h"
#include "runtime/host_devices.h"
#include "runtime/ee_scheduler.h"
#include "runtime/guest_executable_request.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "Kernel/Syscalls/Helpers/State.h"
#include "ps2native/bios.hpp"
#include "ps2native/io.hpp"
#include "ps2native/sha256.hpp"

#include <algorithm>
#include <atomic>
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

void ps2n_register_iop_modules();
void ps2n_register_ee_modules(EeScheduler& scheduler);
#include <vector>

void ps2n_hw_watch(uint8_t* rdram, uint32_t guest, std::vector<std::pair<uintptr_t, uint32_t>> functions);

namespace {
// PS2N_GS_TRACE: forwards to the PS2Recomp CPU backend and summarizes the
// image transfers it receives (destination, size, non-zero bytes).
class TracingBackend final : public GSRasterBackend {
public:
    void Initialize(uint8_t* vram, uint32_t size) override { inner_.Initialize(vram, size); }
    void Reset() override { inner_.Reset(); }
    void Submit(const GSPrimitiveBatch& batch) override {
        const auto start = std::chrono::steady_clock::now();
        inner_.Submit(batch);
        submitUs_.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count(), std::memory_order_relaxed);
    }
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
        const auto start = std::chrono::steady_clock::now();
        inner_.UploadImage(data, size);
        uploadUs_.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count(), std::memory_order_relaxed);
    }
    void Flush() override { inner_.Flush(); }
    void TextureFlush() override { inner_.TextureFlush(); }
    void Sync(GSSyncReason reason) override { inner_.Sync(reason); }
    PresentationFrame Present(const GSPresentationRequest& request) override {
        const auto start = std::chrono::steady_clock::now();
        if (std::getenv("PS2N_GS_COMPARE") && (request.vsyncTick == 127 || request.vsyncTick == 239)) {
            std::vector<uint8_t> snapshot;
            inner_.SnapshotVram(snapshot);
            ps2n_trace_vram(snapshot, request.vsyncTick);
        }
        auto frame = inner_.Present(request);
        ps2n_trace_scanout(frame, request);
        presentUs_.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count(), std::memory_order_relaxed);
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
        std::printf("[ps2n-xfer] dir=%u sbp=%x spsm=%x dbp=%x dbw=%u dpsm=%x at %u,%u size %ux%u bytes=%u nonzero=%u submit_us=%llu upload_us=%llu present_us=%llu\n",
                    current_.direction, b.sbp, b.spsm, b.dbp, b.dbw, b.dpsm, current_.trxpos.dsax,
                    current_.trxpos.dsay, current_.trxreg.rrw, current_.trxreg.rrh, bytes_, nonzero_,
                    static_cast<unsigned long long>(submitUs_.exchange(0, std::memory_order_relaxed)),
                    static_cast<unsigned long long>(uploadUs_.exchange(0, std::memory_order_relaxed)),
                    static_cast<unsigned long long>(presentUs_.exchange(0, std::memory_order_relaxed)));
        std::fflush(stdout);
    }
    GSCpuBackend inner_;
    GSTransferCommand current_{};
    uint32_t bytes_{}, nonzero_{}, presents_{};
    std::atomic<uint64_t> submitUs_{0}, uploadUs_{0}, presentUs_{0};
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
void trace_gs(PS2Runtime& runtime, std::stop_token stop) {
    while (!stop.stop_requested()) {
        runtime.gs().clearDebugHistory();
        runtime.gs().setDebugHistoryPaused(false);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (stop.stop_requested()) return;
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
                  << " running=" << kernel.runningThreadId << std::hex << " cpu.pc=" << runtime.cpu().pc
                  << " heap=" << runtime.guestHeapBase() << "-" << runtime.guestHeapEnd();
        for (const auto& t : kernel.threads)
            std::cout << " t" << t.id << ":s" << +static_cast<int>(t.status) << "w" << +static_cast<int>(t.waitReason)
                      << "/" << t.waitId << "p" << std::dec << t.currentPriority << "i" << t.invocationDepth << std::hex << "@" << t.pc << "<" << t.ra << "^" << t.sp;
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
        // PS2N_RPC_TRACE: SIF RPC calls since the last report.
        if (std::getenv("PS2N_RPC_TRACE")) {
            static uint64_t lastSeq = 0;
            std::vector<SifRpcDebugEvent> events;
            {
                std::lock_guard lock(g_rpc_mutex);
                for (const auto& e : g_sif_rpc_debug_history)
                    if (e.seq > lastSeq) events.push_back(e);
            }
            std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.seq < b.seq; });
            for (const auto& e : events) {
                lastSeq = std::max(lastSeq, e.seq);
                std::cout << "[ps2n-rpc] " << e.op << std::hex << " sid=" << e.sid << " fn=" << e.rpcNum
                          << " flags=" << e.flags << " send=" << e.sendSize << ":";
                for (uint32_t i = 0; i < e.sendPreviewSize; ++i) std::cout << " " << +e.sendPreview[i];
                std::cout << " recv=" << e.recvSize << ":";
                for (uint32_t i = 0; i < e.recvPreviewSize; ++i) std::cout << " " << +e.recvPreview[i];
                std::cout << std::dec << " res=" << e.result << "\n";
            }
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

int run_game(int argc, char** argv) {
    const bool prepareExec = argc == 9 && std::string_view(argv[8]) == "--prepare-exec";
    const bool browser = (argc == 8 || prepareExec) && std::string_view(argv[2]) == "--bios";
    if (argc != 3 && !browser) {
        std::cerr << "Usage: " << (argc ? argv[0] : "ps2n-game") << " <boot-elf> <cd-image>\n";
        return 2;
    }
    try {
        const auto path = [](const char* value) { return std::filesystem::path(std::u8string(value, value + std::strlen(value))); };
        const std::filesystem::path elf = std::filesystem::absolute(path(argv[1]));
        std::optional<ps2native::BrowserFirmware> sourceFirmware;
        if (browser) {
            sourceFirmware.emplace(ps2native::load_browser_firmware(ps2native::read_file(path(argv[3])), argv[5],
                ps2native::read_file(path(argv[6])), {std::string(argv[4])}, {std::string(argv[7])}));
            const auto& firmware = *sourceFirmware;
            if (ps2native::sha256_file(elf) != ps2native::sha256_hex(firmware.program.bytes))
                throw std::runtime_error("OSDSYS executable does not belong to the selected ROM0");
            PS2RomProfile profile;
            profile.id = firmware.bios.sha256;
            profile.provider = "ps2-native ROM0";
            profile.matcher.elfName = elf.filename().string();
            profile.matcher.entryPoint = firmware.program.entry;
            for (const auto& module : firmware.bios.modules) {
                const auto first = firmware.bios.rom0.begin() + module.offset;
                profile.files[module.name] = std::vector<std::uint8_t>(first, first + module.size);
            }
            PS2RomDevice::registerProfile(std::move(profile));
            std::cerr << "[firmware] original OSDSYS ROM0=" << firmware.bios.sha256
                      << " profile=" << firmware.console_profile << " NVM=" << firmware.nvm.sha256 << '\n';
        }
        if (std::getenv("PS2N_DIAGNOSTIC_IOP_DECODED"))
            std::cerr << "[IOP-AOT] diagnostic control: using existing decoded executor\n";
        else
            ps2n_register_iop_modules();
        PS2Runtime runtime;
        if (!runtime.initialize(("ps2-native | " + elf.filename().string()).c_str())) {
            std::cerr << "PS2Recomp runtime initialization failed\n";
            return 1;
        }
        if (!runtime.loadELF(elf.string())) {
            std::cerr << "PS2Recomp could not load " << elf.string() << "\n";
            return 1;
        }
        if (const char* watch = std::getenv("PS2N_EE_HWWATCH")) {
            std::vector<std::pair<uintptr_t, uint32_t>> functions;
            for (uint32_t address = 0x100000u; address < 0x01000000u; address += 4u)
                if (runtime.hasFunction(address))
                    functions.emplace_back(reinterpret_cast<uintptr_t>(runtime.lookupFunction(address)), address);
            ps2n_hw_watch(runtime.memory().getRDRAM(), static_cast<uint32_t>(std::strtoul(watch, nullptr, 16)), std::move(functions));
        }
        if (browser) runtime.eeScheduler().setExecutableArguments({"rom0:OSDSYS"});
        if (!prepareExec) ps2n_register_ee_modules(runtime.eeScheduler());
        auto paths = PS2Runtime::getIoPaths();
        paths.cdImage = browser ? std::filesystem::path{} : std::filesystem::absolute(path(argv[2]));
        PS2Runtime::setIoPaths(paths);
        PCSX2Audio audio;
        PS2HostDevices devices{&audio};
        ps2SetHostDevices(&runtime, &devices);
        struct DetachDevices {
            PS2Runtime& runtime;
            ~DetachDevices() {
                ps2SetHostDevices(&runtime, nullptr);
            }
        } detach{runtime};
        std::jthread diagnostics;
        const char* gsBackend = std::getenv("PS2N_GS_BACKEND");
        const char* gsMode = std::getenv("PS2N_GS_MODE");
        const bool enhanced = gsMode && std::strcmp(gsMode, "enhanced") == 0;
        if (gsMode && !enhanced && std::strcmp(gsMode, "fidelity") != 0)
            throw std::runtime_error("PS2N_GS_MODE must be fidelity or enhanced");
        const bool parallel = gsBackend && std::strcmp(gsBackend, "parallel") == 0;
        if (gsBackend && !parallel && std::strcmp(gsBackend, "cpu") != 0)
            throw std::runtime_error("PS2N_GS_BACKEND must be cpu or parallel");
        if (enhanced && !parallel) throw std::runtime_error("Enhanced requires the optional paraLLEl-GS device");
        if (parallel) runtime.gs().setRasterBackend(ps2n_parallel_gs(runtime.memory().gs(), enhanced));
        if (!parallel && (std::getenv("PS2N_GS_TRACE") || std::getenv("PS2N_GS_COMPARE")))
            runtime.gs().setRasterBackend(std::make_unique<TracingBackend>());
        if (const char* dump = std::getenv("PS2N_GS_PCSX2_DUMP")) {
            if (parallel) throw std::runtime_error("PS2N_GS_PCSX2_DUMP records the CPU backend stream");
            const char* at = std::getenv("PS2N_GS_PCSX2_DUMP_AT");
            const char* frames = std::getenv("PS2N_GS_PCSX2_DUMP_FRAMES");
            runtime.gs().setRasterBackend(ps2n_pcsx2_gs_dump(runtime.memory().gs(), dump,
                at ? std::strtoull(at, nullptr, 10) : 0, frames ? std::strtoull(frames, nullptr, 10) : 8));
        }
        if (std::getenv("PS2N_GS_TRACE")) {
            diagnostics = std::jthread([&](std::stop_token stop) { trace_gs(runtime, stop); });
        }
        try {
            runtime.run();
        } catch (const GuestExecutableRequest& request) {
            if (request.syscall == 7 && sourceFirmware && sourceFirmware->unpacked) {
                const auto& image = *sourceFirmware->unpacked;
                const auto entry = request.arguments[0];
                const auto* ram = runtime.memory().getRDRAM();
                ps2native::verify_ee_payload(image, entry, std::span(ram, PS2_RAM_SIZE));
                const auto identity = ps2native::sha256_hex(image.bytes);
                std::cerr << "[guest-exec:verified] sha256=" << identity << " bytes=" << image.bytes.size()
                          << " base=" << image.load_address << " entry=" << entry << '\n';
                if (prepareExec || std::getenv("PS2N_PREPARE_EXEC")) {
                    const auto prepared = elf.parent_path().parent_path();
                    if (elf.parent_path().filename() != "cd" || prepared.filename() != "bios-" + sourceFirmware->bios.sha256)
                        throw std::runtime_error("Executable preparation requires a revision-isolated prepared content directory");
                    const auto directory = prepared / "exec";
                    ps2native::create_directories(directory);
                    const auto binary = directory / (identity + ".bin");
                    if (!std::filesystem::exists(binary) || ps2native::sha256_file(binary) != identity)
                        ps2native::write_file_atomically(binary, image.bytes);
                    const auto metadata = "ps2n-exec-image-v1\nsha256=" + identity +
                        "\nparent_sha256=" + sourceFirmware->bios.sha256 +
                        "\nload_address=" + std::to_string(image.load_address) +
                        "\nsize=" + std::to_string(image.bytes.size()) + "\nentry=" + std::to_string(entry) + "\n";
                    ps2native::write_file_atomically(directory / "current.txt",
                        std::span(reinterpret_cast<const std::uint8_t*>(metadata.data()), metadata.size()));
                    std::cerr << "[guest-exec:prepared] sha256=" << identity << '\n';
                    if (prepareExec) return 0;
                }
            }
            throw;
        }
    } catch (const std::exception& error) {
        std::cerr << "ps2n-game: " << error.what() << "\n";
        std::cerr.flush();
        std::_Exit(1);
    }
    // Device/session destructors have run after the guest thread joined.
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(0);
}

#ifdef _WIN32
int wmain(int argc, wchar_t** wide) {
    try {
        std::vector<std::string> arguments;
        for (int i = 0; i < argc; ++i) {
            const auto value = std::filesystem::path(wide[i]).u8string();
            arguments.emplace_back(value.begin(), value.end());
        }
        std::vector<char*> pointers;
        for (auto& value : arguments) pointers.push_back(value.data());
        pointers.push_back(nullptr);
        return run_game(argc, pointers.data());
    } catch (const std::exception& error) {
        std::cerr << "ps2n-game: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char** argv) { return run_game(argc, argv); }
#endif
