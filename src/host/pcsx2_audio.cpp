#include "pcsx2_audio.h"
#include "kzspu2.h"
#include <SDL3/SDL.h>
#include <string>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
// Audio-clock pacing: the SPU2 waits while this much PCM (~43 ms at 48 kHz) is queued.
constexpr uint32_t kTargetFrames = 2048;
std::atomic<bool> deviceActive{false};
}

PCSX2Audio::PCSX2Audio() {
    if (deviceActive.exchange(true)) throw std::logic_error("Only one console SPU2 may be active");
    struct ReleaseOnFailure {
        bool committed = false;
        ~ReleaseOnFailure() { if (!committed) deviceActive = false; }
    } ownership;
    trace_ = std::getenv("PS2N_DEVICE_TRACE") != nullptr;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) throw std::runtime_error(std::string("SDL audio: ") + SDL_GetError());
    try { initializeSpu2(); }
    catch (...) { SDL_QuitSubSystem(SDL_INIT_AUDIO); throw; }
    // The SPU2 outputs signed 16-bit stereo at 48 kHz; SDL receives it unchanged.
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 48000};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
        [](void* p, SDL_AudioStream* stream, int additional, int) {
            auto& device = *static_cast<PCSX2Audio*>(p);
            if (additional <= 0) return;
            int16_t buffer[512 * 2];
            for (uint32_t wanted = static_cast<uint32_t>(additional) / 4u; wanted;) {
                const auto copied = kzspu2ReadSamples(buffer, std::min<uint32_t>(wanted, 512u));
                if (!copied) {
                    device.underrunFrames_.fetch_add(wanted, std::memory_order_relaxed);
                    break;
                }
                if (!SDL_PutAudioStreamData(stream, buffer, static_cast<int>(copied * 4u)))
                    device.outputFailed_.store(true, std::memory_order_relaxed);
                // Pair notification with the producer's predicate check, including spurious wakes.
                { std::lock_guard lock(device.pcmMutex_); }
                device.pcmConsumed_.notify_one();
                wanted -= copied;
            }
        }, this);
    if (!stream_) {
        const std::string error = SDL_GetError();
        kzspu2Shutdown();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        throw std::runtime_error("Could not open SPU2 PCM output: " + error);
    }
    ownership.committed = true;
}
void PCSX2Audio::initializeSpu2() {
    KzSpu2Config config;
    config.user = this;
    config.samples = [](void* p, const int16_t*, uint32_t frames) {
        auto& device = *static_cast<PCSX2Audio*>(p);
        std::unique_lock lock(device.pcmMutex_);
        // Backpressure never advances guest time or silently drops a block on a stalled device.
        if (!device.pcmConsumed_.wait_for(lock, std::chrono::milliseconds(250), [&] {
                return device.outputFailed_.load(std::memory_order_relaxed) ||
                       kzspu2AvailableFrames() + frames <= kTargetFrames;
            }))
            throw std::runtime_error("SPU2 PCM output stalled for 250 ms");
        if (device.outputFailed_.load(std::memory_order_relaxed))
            throw std::runtime_error("SDL SPU2 PCM submission failed");
    };
    config.irq = [](void* p) { static_cast<PCSX2Audio*>(p)->events_ |= 1u; };
    config.dmaComplete = [](void* p, int core) { static_cast<PCSX2Audio*>(p)->events_ |= 2u << core; };
    config.log = [](void*, int level, const char* text) {
        if (level) std::cerr << "[SPU2] " << text << '\n';
    };
    if (!kzspu2Init(config)) {
        throw std::runtime_error("PCSX2 SPU2 initialization failed");
    }
}
PCSX2Audio::~PCSX2Audio() {
    SDL_DestroyAudioStream(stream_);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    const auto status = kzspu2GetStatus();
    std::cerr << "[SPU2] frames=" << status.framesMixed << " dropped=" << status.framesDropped
              << " underrun=" << underrunFrames_.load() << " dma=" << status.dmaCount[0] << ',' << status.dmaCount[1]
              << " peak=" << status.peak[0] << ',' << status.peak[1] << '\n';
    kzspu2Shutdown();
    deviceActive = false;
}
void PCSX2Audio::reset(uint8_t* ram, uint32_t size, uint64_t cycle) {
    // SDL holds this same lock while invoking the consumer. Reset requires a quiescent ring.
    {
        if (!SDL_LockAudioStream(stream_)) throw std::runtime_error(SDL_GetError());
        struct Unlock { SDL_AudioStream* stream; ~Unlock() { SDL_UnlockAudioStream(stream); } } unlock{stream_};
        if (!SDL_ClearAudioStream(stream_)) throw std::runtime_error(SDL_GetError());
        kzspu2Shutdown();
        initializeSpu2(); // Also resets the monotonic clock and both ring positions for an IOP reboot.
        kzspu2Reset(cycle);
        kzspu2SetIopRam(ram, size);
        events_ = 0;
        underrunFrames_ = 0;
        outputFailed_ = false;
    }
    if (!SDL_ResumeAudioStreamDevice(stream_)) throw std::runtime_error(SDL_GetError());
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
                  << " underrun=" << underrunFrames_.load(std::memory_order_relaxed)
                  << " queued=" << kzspu2AvailableFrames() << " max_step=" << maxAdvance_
                  << " wall_ms=" << wallMs << '\n';
        maxAdvance_ = 0;
    }
}
uint64_t PCSX2Audio::nextEvent() { return kzspu2NextEventCycle(); }
uint32_t PCSX2Audio::takeEvents() { const auto e = events_; events_ = 0; return e; }
