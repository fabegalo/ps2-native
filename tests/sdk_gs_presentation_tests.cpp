#include "runtime/gs/gs_cpu_backend.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

static void samePixels(const PresentationFrame& a, const PresentationFrame& b) {
    if (a.width != b.width || a.height != b.height || a.pixels != b.pixels)
        throw std::runtime_error("Presentation changed pixels or dimensions");
}

int main() try {
    std::vector<uint8_t> vram(4 * 1024 * 1024);
    uint32_t seed = 0x12345678;
    for (auto& byte : vram) { seed = seed * 1664525u + 1013904223u; byte = seed >> 24; }
    GSCpuBackend backend;
    backend.Initialize(vram.data(), static_cast<uint32_t>(vram.size()));
    const auto drawStart = std::chrono::steady_clock::now();
    for (unsigned texture : {0u, 19u, 20u})
        for (bool filtered : {false, true})
            for (bool blend : {false, true}) {
                GSPrimitiveBatch batch{};
                batch.vertexCount = 2;
                auto& state = batch.state;
                state.prim.type = GS_PRIM_SPRITE;
                state.prim.tme = true;
                state.prim.fst = true;
                state.prim.abe = blend;
                state.linearFilter = filtered;
                state.textureWidth = state.textureHeight = 512;
                auto& ctx = state.context;
                ctx.frame = {1, 10, 0, 0};
                ctx.zbuf.zmask = true;
                ctx.scissor = {0, 639, 0, 511};
                ctx.test = 1u << 17;
                ctx.alpha = 0x44;
                ctx.tex0.tbp0 = 0x2000;
                ctx.tex0.tbw = 8;
                ctx.tex0.psm = texture;
                ctx.tex0.cbp = 0x1000;
                ctx.tex0.cld = 1;
                ctx.tex0.tcc = 1;
                batch.vertices[1].x = 640;
                batch.vertices[1].y = 512;
                batch.vertices[1].u = 500 * 16;
                batch.vertices[1].v = 400 * 16;
                batch.vertices[1].r = 180;
                batch.vertices[1].g = 90;
                batch.vertices[1].b = 45;
                batch.vertices[1].a = 128;
                backend.LoadClut(ctx.tex0, state.texclut);
                backend.TextureFlush();
                backend.Submit(batch);
            }
    uint64_t drawHash = 14695981039346656037ull;
    for (auto byte : vram) { drawHash ^= byte; drawHash *= 1099511628211ull; }
#if defined(_MSC_VER) && defined(_M_X64)
    // Full VRAM control recorded before enabling source-local aggressive inlining.
    if (drawHash != 0x9f8b243e8ffb83bcull) throw std::runtime_error("Sprite VRAM differs from MSVC x64 control");
#endif
    std::cout << "draw_hash=" << std::hex << drawHash << std::dec << " draw_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - drawStart).count() << '\n';
    GSPresentationRequest request{};
    request.display1 = request.display2 = (uint64_t(639) << 32) | (uint64_t(223) << 44);
    const auto start = std::chrono::steady_clock::now();
    unsigned comparisons = 0;
    for (unsigned psm : {0u, 1u, 2u, 10u})
        for (unsigned field : {0u, 1u, 3u})
            for (uint64_t mode : {3ull, 0x23ull, 0x8063ull, 0xff63ull}) {
                request.dispfb1 = request.dispfb2 = 1u | (10u << 9) | (psm << 15) |
                    (uint64_t(7) << 32) | (uint64_t(9) << 43);
                request.smode2 = field;
                request.vsyncTick = field & 1;
                request.pmode = 1;
                request.hasPreferredSource = false;
                const auto expected = backend.Present(request);
                request.pmode = mode;
                // Dual-circuit scanout ignores the single-circuit preferred-source hint.
                request.hasPreferredSource = true;
                request.preferredDestFbp = 1;
                request.preferredSource = {320, 10, 0, 0};
                const auto actual = backend.Present(request);
                samePixels(expected, actual);
                if (actual.usedPreferred || actual.displayFbp != expected.displayFbp || actual.sourceFbp != expected.sourceFbp)
                    throw std::runtime_error("Dual-circuit source selection changed");
                ++comparisons;
            }
    request.hasPreferredSource = false;
    request.smode2 = 0;
    request.dispfb1 = request.dispfb2 = 1u | (10u << 9);
    request.pmode = 1;
    const auto single = backend.Present(request);
    request.pmode = 0xa3; // Fixed zero blend factor selects BGCOLOR when SLBG is set.
    request.bgcolor = 0x563412;
    if (backend.Present(request).pixels == single.pixels)
        throw std::runtime_error("SLBG negative control did not differ");
    request.dispfb2 |= uint64_t(3) << 32;
    request.pmode = 2;
    const auto second = backend.Present(request);
    request.pmode = 0x23; // Different read origins must still use the second circuit.
    samePixels(second, backend.Present(request));
    for (uint64_t different : {2ull | (10ull << 9), 1ull | (9ull << 9),
                               1ull | (10ull << 9) | (2ull << 15)}) {
        request.dispfb2 = different;
        request.pmode = 2;
        const auto expected = backend.Present(request);
        request.pmode = 0x23;
        samePixels(expected, backend.Present(request));
    }
    request.dispfb2 = request.dispfb1;
    request.display2 = (uint64_t(319) << 32) | (uint64_t(127) << 44);
    const auto differentSize = backend.Present(request);
    const auto at = (200u * 640u + 500u) * 4u;
    if (differentSize.width != 640 || differentSize.height != 224 ||
        differentSize.pixels[at] != 0x12 || differentSize.pixels[at + 1] != 0x34 ||
        differentSize.pixels[at + 2] != 0x56 || differentSize.pixels[at + 3] != 255)
        throw std::runtime_error("Different circuit dimensions lost background composition");
    request.display2 = request.display1;
    request.dispfb1 = request.dispfb2 = 1u | (10u << 9) | (19u << 15);
    request.hasPreferredSource = true;
    request.pmode = 1;
    const auto unsupportedFallback = backend.Present(request);
    request.pmode = 3;
    const auto unsupportedDual = backend.Present(request);
    samePixels(unsupportedFallback, unsupportedDual);
    if (!unsupportedDual.usedPreferred) throw std::runtime_error("Unsupported format fallback changed");
    std::cout << "PASS " << comparisons << " full pixel comparisons, source hints, SLBG and read-origin controls; ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() << '\n';
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
