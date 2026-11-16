// The console's SPU2 is PCSX2's (external/pcsx2, through kzspu2): its registers, both cores' 48 voices, ADPCM, Gaussian
// interpolation, ADSR, reverb, auto-DMA input and DMA timing. This file connects it to the engine's IOP: register accesses,
// DMA channels 4/7, interrupt line 9 and the IOP clock (36.864 MHz, one sample per 768 cycles). The mixed 48 kHz PCM goes
// to the host's sink unchanged.
// SPDX-License-Identifier: GPL-3.0+ (the binary links PCSX2's SPU2)

#include "spu2.hpp"

#include "hw.hpp"
#include "iop.hpp"
#include "kernel.hpp"
#include "kzspu2.h"
#include "kzspu2_state.h"
#include "ps2/os.hpp"
#include "ps2/state.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ps2::iop::spu2 {

namespace {

constexpr std::uint64_t kCyclesPerSample = 768;
constexpr std::uint64_t kBatch = 32;  // samples between two advances when nothing else is due (0.67 ms)
constexpr unsigned kSpuLine = 9;

std::function<void(const std::int16_t *, std::size_t)> g_sink;
bool g_tick_pending = false;
std::uint64_t g_next_tick = 0;

// A 48000 Hz 16-bit stereo WAV file written while the sound is made (as the engine's own SPU2 did): the header is brought up to
// date about once a second, so a run that is killed still leaves a file that plays up to that point.
class WavFile {
public:
    ~WavFile() { close(); }
    bool open(const std::string &path) {
        if (file_) return false;
        file_ = os::open(path, "wb");
        if (!file_) {
            std::fprintf(stderr, "[spu2] cannot write %s\n", path.c_str());
            return false;
        }
        path_ = path;
        frames_ = synced_ = 0;
        header();
        return true;
    }
    void write(const std::int16_t *samples, std::size_t frames) {
        if (!file_ || frames_ + frames > kMaxFrames) return;
        std::fwrite(samples, 4, frames, file_);
        frames_ += frames;
        if (frames_ - synced_ >= 48000) header();
    }
    void close() {
        if (!file_) return;
        header();
        std::fclose(file_);
        file_ = nullptr;
        std::fprintf(stderr, "[spu2] wrote %llu samples (%.2f s) to %s\n", static_cast<unsigned long long>(frames_), static_cast<double>(frames_) / 48000.0, path_.c_str());
    }

private:
    static constexpr std::uint64_t kMaxFrames = (0xFFFFFFFFull - 36) / 4;
    void header() {
        const std::uint32_t bytes = static_cast<std::uint32_t>(frames_ * 4);
        const std::uint32_t words[] = {0x46464952u, 36 + bytes, 0x45564157u, 0x20746D66u, 16,         0x00020001u,
                                       48000,       48000 * 4,  0x00100004u, 0x61746164u, bytes};
        std::fseek(file_, 0, SEEK_SET);
        std::fwrite(words, 4, sizeof words / 4, file_);
        std::fseek(file_, 0, SEEK_END);
        std::fflush(file_);
        synced_ = frames_;
    }
    std::FILE *file_ = nullptr;
    std::string path_;
    std::uint64_t frames_ = 0, synced_ = 0;
} g_wav;

void schedule_tick() {
    const std::uint64_t now = kernel::now();
    g_next_tick = std::min(now + kBatch * kCyclesPerSample, std::max(kzspu2NextEventCycle(), now + 1));
    g_tick_pending = true;
    kernel::schedule(g_next_tick, kernel::Job{kernel::kJobSpuTick});
}

void tick() {
    g_tick_pending = false;
    kzspu2Advance(kernel::now());
    schedule_tick();
}

void keep_ticking() {
    if (!g_tick_pending) schedule_tick();
}

void start() {
    KzSpu2Config config;
    config.irq = [](void *) { hw::raise_interrupt(kSpuLine); };
    config.dmaComplete = [](void *, int core) { kernel::schedule(kernel::now(), kernel::Job{kernel::kJobDmaDone, core ? 7u : 4u}); };
    config.samples = [](void *, const std::int16_t *stereo, std::uint32_t frames) {
        g_wav.write(stereo, frames);
        if (g_sink) g_sink(stereo, frames);
    };
    config.log = [](void *, int level, const char *text) {
        if (level) std::fprintf(stderr, "[spu2] %s\n", text);
    };
    config.ringFrames = 0;  // the PCM goes to the sink as it is made; no ring of its own
    kzspu2Init(config);
    kzspu2SetIopRam(ram_ptr(0), kRamSize);
}

}  // namespace

void reset() {
    kernel::register_job(kernel::kJobSpuTick, [](const kernel::Job &) { tick(); });
    static bool started = false;
    if (!started) {
        start();
        started = true;
        // the sink is set before the machine resets (host/window.cpp opens the sound device first)
        if (const std::string out = os::env("PS2_AUDIO_DUMP"); !out.empty() && !g_sink && g_wav.open(out)) {
            std::fprintf(stderr, "[spu2] writing the sound to %s (no sound device)\n", out.c_str());
        }
    }
    kzspu2Reset(kernel::now());
    g_tick_pending = false;
}

void restart_ticks() { g_tick_pending = false; }

std::uint16_t *ram() { return const_cast<std::uint16_t *>(kzspu2Memory()); }

std::uint32_t read(std::uint32_t a, unsigned size) {
    keep_ticking();
    std::uint32_t value = kzspu2Read16(a, kernel::now());
    if (size == 4) value |= static_cast<std::uint32_t>(kzspu2Read16(a + 2, kernel::now())) << 16;
    return value;
}

void write(std::uint32_t a, std::uint32_t v, unsigned size) {
    keep_ticking();
    kzspu2Write16(a, static_cast<std::uint16_t>(v), kernel::now());
    if (size == 4) kzspu2Write16(a + 2, static_cast<std::uint16_t>(v >> 16), kernel::now());
}

void dma(int core, std::uint32_t madr, std::uint32_t words, bool to_spu, int) {
    keep_ticking();
    auto *memory = reinterpret_cast<std::uint16_t *>(ram_ptr(madr));
    if (to_spu) kzspu2DmaWrite(core, memory, words * 2, madr, kernel::now());
    else kzspu2DmaRead(core, memory, words * 2, madr, kernel::now());
    keep_ticking();
}

void dma_stop(int core) { kzspu2StopDma(core); }

void sync_dma() {
    kzspu2Advance(kernel::now());
    for (int core = 0; core < 2; ++core)
        if (kzspu2DmaBusy(core)) hw::dma_progress(core ? 7u : 4u, kzspu2DmaMadr(core));
}

void set_sink(std::function<void(const std::int16_t *, std::size_t)> sink) { g_sink = std::move(sink); }

void dump_audio(const char *) { g_wav.close(); }

void dump_voices() {
    for (int c = 0; c < 2; ++c)
        for (int v = 0; v < 24; ++v) {
            const KzSpu2VoiceInfo i = kzspu2VoiceInfo(c, v);
            if (i.phase)
                std::fprintf(stderr, "[spu2] core %d voice %2d phase %u start 0x%05X next 0x%05X loop 0x%05X pitch 0x%04X env %d vol %d/%d\n", c, v, i.phase, i.startA,
                             i.nextA, i.loopA, i.pitch, i.envx, i.volL, i.volR);
        }
}

std::string summary() {
    const KzSpu2Status s = kzspu2GetStatus(true);
    char text[512];
    std::snprintf(text, sizeof text,
                  "sound chip:      PCSX2 SPU2, %" PRIu64 " frames mixed (%.1f s)\n"
                  "voices playing:  core 0: %u, core 1: %u (attr %04X / %04X)\n"
                  "interrupts:      %u SPU2 IRQs, DMA ends %u / %u\n"
                  "loudest sample:  %d / %d since the last report\n",
                  s.framesMixed, static_cast<double>(s.framesMixed) / 48000.0, s.keyedOnVoices[0], s.keyedOnVoices[1], s.coreAttr[0], s.coreAttr[1], s.irqCount,
                  s.dmaCount[0], s.dmaCount[1], s.peak[0], s.peak[1]);
    return text;
}

void state(ps2::state::Stream &s) {
    s.tag("spu2pcx2");
    std::vector<std::uint8_t> block(kzspu2StateSize());
    if (s.saving()) {
        kzspu2Advance(kernel::now());
        kzspu2SaveState(block.data());
    }
    s.bytes(block.data(), block.size());
    if (s.loading()) {
        if (!kzspu2LoadState(block.data(), block.size())) throw std::runtime_error("the SPU2 part of the save state is of another version");
    }
    s.pod(g_tick_pending);
    s.pod(g_next_tick);
}

}  // namespace ps2::iop::spu2
