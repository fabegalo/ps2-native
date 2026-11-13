#pragma once
#include "ps2x/iop/iop_spu2.h"
#include "raylib.h"
#include <chrono>

// One device instance belongs to one supervised console session.
class PCSX2Audio final : public ps2x::iop::IopSpu2Backend {
public:
    PCSX2Audio();
    ~PCSX2Audio() override;
    void reset(uint8_t*, uint32_t, uint64_t) override;
    uint16_t read16(uint32_t, uint64_t) override;
    void write16(uint32_t, uint16_t, uint64_t) override;
    void dma(int, uint16_t*, uint32_t, uint32_t, bool, uint64_t) override;
    uint32_t madr(int) override;
    void advance(uint64_t) override;
    uint64_t nextEvent() override;
    uint32_t takeEvents() override;
private:
    AudioStream stream_{};
    uint32_t events_ = 0;
    uint32_t ringFrames_ = 0;
    bool trace_ = false;
    uint64_t nextTrace_ = 36864000;
    uint64_t lastAdvance_ = 0, maxAdvance_ = 0;
    std::chrono::steady_clock::time_point traceTime_ = std::chrono::steady_clock::now();
};
