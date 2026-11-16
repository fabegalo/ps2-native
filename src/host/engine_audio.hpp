// Host adapter reused from ps2-native PCSX2Audio (BSD-3-Clause).
#pragma once
#include <cstdint>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

struct SDL_AudioStream;

// One device instance belongs to one supervised console session.
class EngineAudio final {
public:
    explicit EngineAudio(bool output);
    ~EngineAudio();
    using FailureHandler = void (*)(void*) noexcept;
    void setFailureHandler(void*, FailureHandler);
    void checkOutput() const;
    void reset(uint8_t*, uint32_t, uint64_t);
    uint16_t read16(uint32_t, uint64_t);
    void write16(uint32_t, uint16_t, uint64_t);
    void dma(int, uint16_t*, uint32_t, uint32_t, bool, uint64_t);
    uint32_t madr(int);
    bool dmaBusy(int) const;
    void advance(uint64_t);
    uint64_t nextEvent();
    uint32_t takeEvents();
private:
    enum class OutputFailure { None, Submission, Stalled };
    void failOutput(OutputFailure) noexcept;
    void initializeSpu2();
    std::mutex pcmMutex_;
    std::condition_variable pcmConsumed_;
    std::atomic<OutputFailure> outputFailure_{OutputFailure::None};
    std::mutex failureMutex_;
    void* failureContext_ = nullptr;
    FailureHandler failureHandler_ = nullptr;
    SDL_AudioStream* stream_ = nullptr;
    std::atomic<uint64_t> underrunFrames_{0};
    uint32_t events_ = 0;
    bool trace_ = false;
    bool output_ = false;
    uint64_t nextTrace_ = 36864000;
    uint64_t lastAdvance_ = 0, maxAdvance_ = 0;
    std::chrono::steady_clock::time_point traceTime_ = std::chrono::steady_clock::now();
};
