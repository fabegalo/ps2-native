#pragma once
#include "ps2native/device_timing.hpp"
#include <array>
#include <limits>
#include <optional>

namespace ps2native {
// PS2SDK smapregs.h: TX/RX FIFO CTRL bit 0 requests a self-clearing reset.
// Completion latency is an explicit experiment, not a measured reset default.
class SmapFifos {
    friend struct CheckpointAccess;
    struct Fifo {
        std::optional<std::uint8_t> control;
        std::optional<std::uint64_t> complete_at;
        std::optional<bool> empty;
    };
    std::array<Fifo,2> fifos_{};
    std::uint32_t reset_cycles_{};
    Fifo& advance(bool receive,std::uint64_t now) {
        auto& fifo=fifos_[receive ? 1u : 0u];
        if(fifo.complete_at && now>=*fifo.complete_at) {
            fifo.control=0;fifo.complete_at.reset();fifo.empty=true;
        }
        return fifo;
    }
public:
    void reset() {fifos_={};reset_cycles_=0;}
    void power_reset() {fifos_={};}
    void diagnostic_reset_cycles(std::uint32_t cycles) {
        if(!cycles || fifos_[0].control || fifos_[1].control)
            throw DeviceRefusal("SMAP reset timing requires positive IOP cycles before FIFO use");
        reset_cycles_=cycles;
    }
    void write(bool receive,ObservedWord word,std::uint64_t now) {
        if(word.unknown || word.value!=1u)
            throw DeviceRefusal("SMAP FIFO supports only a known RESET request; DMA and data transfers are not modeled");
        if(!reset_cycles_)
            throw DeviceRefusal("SMAP FIFO reset completion needs explicit --assume-smap-reset-cycles=N");
        if(now>std::numeric_limits<std::uint64_t>::max()-reset_cycles_)
            throw DeviceRefusal("SMAP FIFO reset deadline overflows the IOP clock");
        auto& fifo=fifos_[receive ? 1u : 0u];
        fifo.control=1;fifo.empty.reset();fifo.complete_at=now+reset_cycles_;
    }
    ObservedWord read(bool receive,std::uint64_t now) {
        const auto& fifo=advance(receive,now);
        if(!fifo.control)throw DeviceRefusal("SMAP FIFO control before a known write has no established value");
        return {*fifo.control,0xFCu}; // RESET/DMAEN known; unused byte bits remain unknown.
    }
    std::optional<bool> empty(bool receive,std::uint64_t now) {return advance(receive,now).empty;}
    bool valid() const {
        for(const auto& fifo:fifos_) {
            if(fifo.control && *fifo.control>1u)return false;
            if(fifo.control && !reset_cycles_)return false;
            if(fifo.control==1u && !fifo.complete_at)return false;
            if(fifo.complete_at && (!reset_cycles_ || fifo.control!=1u || fifo.empty))return false;
            if(fifo.control==0u && (!fifo.empty || !*fifo.empty))return false;
            if(!fifo.control && (fifo.complete_at || fifo.empty))return false;
        }
        return true;
    }
};
} // namespace ps2native
