// Host entry point of a game recompiled by PS2Recomp: the runtime, kernel,
// IOP, GS and audio are the pinned PS2Recomp ps2xRuntime, used unchanged.
//
//   ps2n-game-<id> <boot-elf> <cd-image>
// The working directory is the extracted disc (cdrom0:); <cd-image> holds the
// 2048-byte user sectors for raw LBN reads.
#include "ps2_runtime.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <thread>

namespace {
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
        if (std::getenv("PS2N_GS_TRACE")) std::thread(trace_gs, std::ref(runtime)).detach();
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
