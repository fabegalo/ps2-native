// The GS raster thread must leave VRAM, readbacks and transfers identical to
// the synchronous CPU backend for the same call sequence.
#include "gs_async_backend.h"
#include "runtime/gs/gs_cpu_backend.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
void check(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}

GSPrimitiveBatch sprite(unsigned psm, bool filtered, bool blend, uint16_t x1) {
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
    ctx.tex0.psm = psm;
    ctx.tex0.cbp = 0x1000;
    ctx.tex0.cld = 1;
    ctx.tex0.tcc = 1;
    batch.vertices[1].x = x1;
    batch.vertices[1].y = 512;
    batch.vertices[1].u = 500 * 16;
    batch.vertices[1].v = 400 * 16;
    batch.vertices[1].r = 180;
    batch.vertices[1].g = 90;
    batch.vertices[1].b = 45;
    batch.vertices[1].a = 128;
    return batch;
}

// Drives one backend through draws, a host->local upload, direct writes and a
// readback that must observe every earlier command.
std::vector<uint32_t> drive(GSRasterBackend& backend) {
    std::vector<uint32_t> reads;
    for (unsigned psm : {0u, 19u, 20u})
        for (bool filtered : {false, true})
            for (bool blend : {false, true}) {
                const auto batch = sprite(psm, filtered, blend, static_cast<uint16_t>(320 + psm * 8));
                backend.LoadClut(batch.state.context.tex0, batch.state.texclut);
                backend.TextureFlush();
                backend.Submit(batch);
                reads.push_back(backend.ReadVram(0, 0, 10, 100 + psm, 200));
            }
    GSTransferCommand upload{};
    upload.bitbltbuf.dbp = 0x3000;
    upload.bitbltbuf.dbw = 1;
    upload.bitbltbuf.dpsm = 0;
    upload.trxreg.rrw = 16;
    upload.trxreg.rrh = 16;
    upload.direction = 0;
    backend.BeginTransfer(upload);
    std::vector<uint8_t> pixels(16 * 16 * 4);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<uint8_t>(i * 7);
    backend.UploadImage(pixels.data(), static_cast<uint32_t>(pixels.size()));
    for (uint32_t i = 0; i < 64; ++i) backend.WriteVram(0, 0x3800, 1, i, i / 8, 0xA5000000u | i);
    reads.push_back(backend.ReadVram(0, 0x3000, 1, 5, 7));
    reads.push_back(backend.ReadVram(0, 0x3800, 1, 9, 1));
    reads.push_back(backend.GetTransferSnapshot().totalPixels);
    backend.Sync(GSSyncReason::DebugReadback);
    return reads;
}

std::vector<uint8_t> seeded() {
    std::vector<uint8_t> vram(4 * 1024 * 1024);
    uint32_t seed = 0x12345678;
    for (auto& byte : vram) { seed = seed * 1664525u + 1013904223u; byte = seed >> 24; }
    return vram;
}
}

int main() try {
    auto syncVram = seeded();
    GSCpuBackend sync;
    sync.Initialize(syncVram.data(), static_cast<uint32_t>(syncVram.size()));
    const auto syncReads = drive(sync);

    auto asyncVram = seeded();
    std::vector<uint8_t> snapshot;
    std::vector<uint32_t> asyncReads;
    {
        GSAsyncBackend async(std::make_unique<GSCpuBackend>());
        async.Initialize(asyncVram.data(), static_cast<uint32_t>(asyncVram.size()));
        const auto start = std::chrono::steady_clock::now();
        asyncReads = drive(async);
        async.SnapshotVram(snapshot);
        std::cout << "async_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count() << '\n';
    }
    check(asyncReads == syncReads, "GS thread readbacks differ from the synchronous backend");
    check(asyncVram == syncVram, "GS thread VRAM differs from the synchronous backend");
    check(snapshot.size() == asyncVram.size() && std::memcmp(snapshot.data(), asyncVram.data(), snapshot.size()) == 0,
          "GS thread snapshot misses queued commands");

    // A burst larger than the queue bounds must block the producer, not drop work.
    auto burstVram = seeded();
    {
        GSAsyncBackend async(std::make_unique<GSCpuBackend>());
        async.Initialize(burstVram.data(), static_cast<uint32_t>(burstVram.size()));
        for (uint32_t i = 0; i < 70000; ++i) async.WriteVram(0, 0x3C00, 4, i % 256, (i / 256) % 256, i);
        check(async.ReadVram(0, 0x3C00, 4, 69999 % 256, (69999 / 256) % 256) == 69999u, "Bounded queue dropped writes");
    }
    std::cout << "PASS GS thread: VRAM, readbacks, transfers and bounded queue match the synchronous backend\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
