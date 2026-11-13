#include "pcsx2_audio.h"
#include "kzspu2.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

PCSX2Audio::PCSX2Audio() {
    trace_ = std::getenv("PS2N_DEVICE_TRACE") != nullptr;
    if (!IsAudioDeviceReady()) throw std::runtime_error("Host audio device is unavailable");
    KzSpu2Config config;
    config.user = this;
    config.samples = [](void* p, const int16_t*, uint32_t frames) {
        auto& device = *static_cast<PCSX2Audio*>(p);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        // Preserve PCM during catch-up; bound the wait if the host stops consuming.
        while (kzspu2AvailableFrames() + frames > device.ringFrames_ &&
               IsAudioStreamPlaying(device.stream_) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    ringFrames_ = config.ringFrames;
    config.irq = [](void* p) { static_cast<PCSX2Audio*>(p)->events_ |= 1u; };
    config.dmaComplete = [](void* p, int core) { static_cast<PCSX2Audio*>(p)->events_ |= 2u << core; };
    config.log = [](void*, int level, const char* text) {
        if (level) std::cerr << "[SPU2] " << text << '\n';
    };
    if (!kzspu2Init(config)) throw std::runtime_error("PCSX2 SPU2 initialization failed");
    stream_ = LoadAudioStream(48000, 16, 2);
    if (!IsAudioStreamValid(stream_)) {
        kzspu2Shutdown();
        throw std::runtime_error("Could not create SPU2 PCM output");
    }
    SetAudioStreamCallback(stream_, [](void* buffer, unsigned frames) {
        const auto copied = kzspu2ReadSamples(static_cast<int16_t*>(buffer), frames);
        std::memset(static_cast<int16_t*>(buffer) + copied * 2u, 0, (frames - copied) * 4u);
    });
    PlayAudioStream(stream_);
}
PCSX2Audio::~PCSX2Audio() {
    StopAudioStream(stream_);
    UnloadAudioStream(stream_);
    const auto status = kzspu2GetStatus();
    std::cerr << "[SPU2] frames=" << status.framesMixed << " dropped=" << status.framesDropped
              << " dma=" << status.dmaCount[0] << ',' << status.dmaCount[1]
              << " peak=" << status.peak[0] << ',' << status.peak[1] << '\n';
    kzspu2Shutdown();
}
void PCSX2Audio::reset(uint8_t* ram, uint32_t size, uint64_t cycle) {
    kzspu2Reset(cycle); kzspu2SetIopRam(ram, size); events_ = 0;
    nextTrace_ = cycle + 36864000;
    lastAdvance_ = cycle; maxAdvance_ = 0;
    traceTime_ = std::chrono::steady_clock::now();
}
uint16_t PCSX2Audio::read16(uint32_t a, uint64_t c) { return kzspu2Read16(a, c); }
void PCSX2Audio::write16(uint32_t a, uint16_t v, uint64_t c) { kzspu2Write16(a, v, c); }
void PCSX2Audio::dma(int core, uint16_t* ram, uint32_t words, uint32_t address, bool write, uint64_t c) {
    if (write) kzspu2DmaWrite(core, ram, words, address, c);
    else kzspu2DmaRead(core, ram, words, address, c);
}
uint32_t PCSX2Audio::madr(int core) { return kzspu2DmaMadr(core); }
void PCSX2Audio::advance(uint64_t c) {
    if (trace_) {
        if (c >= lastAdvance_) maxAdvance_ = std::max(maxAdvance_, c - lastAdvance_);
        lastAdvance_ = c;
    }
    kzspu2Advance(c);
    if (trace_ && c >= nextTrace_) {
        nextTrace_ = c + 36864000;
        const auto now = std::chrono::steady_clock::now();
        const auto wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - traceTime_).count();
        traceTime_ = now;
        const auto s = kzspu2GetStatus();
        std::cerr << "[SPU2] cycle=" << c << " frames=" << s.framesMixed
                  << " dropped=" << s.framesDropped << " dma=" << s.dmaCount[0] << ',' << s.dmaCount[1]
                  << " voices=" << s.keyedOnVoices[0] << ',' << s.keyedOnVoices[1]
                  << " attr=" << std::hex << s.coreAttr[0] << ',' << s.coreAttr[1] << std::dec
                  << " peak=" << s.peak[0] << ',' << s.peak[1]
                  << " queued=" << kzspu2AvailableFrames() << " max_step=" << maxAdvance_
                  << " wall_ms=" << wallMs << '\n';
        maxAdvance_ = 0;
    }
}
uint64_t PCSX2Audio::nextEvent() { return kzspu2NextEventCycle(); }
uint32_t PCSX2Audio::takeEvents() { const auto e = events_; events_ = 0; return e; }
