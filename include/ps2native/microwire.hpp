#pragma once
// Read-only 64 x 16 Microwire protocol. A READ has a start bit, opcode 10,
// six address bits, one dummy zero and MSB-first data on rising CLK edges.
// Reference: Microchip 93LC46B datasheet DS21712B, section 2.4.
#include "ps2native/device_timing.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace ps2native {
class Microwire64x16 {
public:
    enum class Presence { Unknown, Present, Disconnected };
    void reset() { *this=Microwire64x16{}; }
    void attach(std::span<const std::uint16_t> words) {
        if(words.size()!=64u)throw DeviceRefusal("Microwire image requires exactly 64 logical 16-bit words");
        reset();std::copy(words.begin(),words.end(),words_.begin());backed_=true;presence_=Presence::Present;
    }
    // Explicit diagnostic inputs, never inferred from missing image data.
    void diagnostic_presence(bool present) {reset();presence_=present ? Presence::Present : Presence::Disconnected;}
    [[nodiscard]] Presence presence() const noexcept {return presence_;}
    [[nodiscard]] bool selected() const noexcept {return selected_;}
    [[nodiscard]] std::uint64_t words_read() const noexcept {return words_read_;}
    [[nodiscard]] std::uint64_t commands() const noexcept {return commands_;}
    [[nodiscard]] std::optional<bool> data_out() const noexcept {
        if(presence_==Presence::Disconnected)return true; // explicitly assumed pulled-up open input
        if(presence_==Presence::Unknown || !selected_)return std::nullopt;
        return output_;
    }
    // Returns whether an active rising clock edge occurred. Repeated writes
    // and falling edges cannot shift a command or manufacture another bit.
    bool drive(bool select,bool clock,bool data) {
        const bool rising=select && clock && !clock_;
        clock_=clock;
        if(!select) {
            selected_=false;reading_=false;bits_=0;command_=0;output_.reset();return false;
        }
        if(!selected_) {selected_=true;reading_=false;bits_=0;command_=0;output_.reset();}
        if(!rising || presence_==Presence::Disconnected)return rising;
        if(reading_) {
            output_=backed_ ? std::optional<bool>{(words_[address_]&(1u<<bit_))!=0u} : std::nullopt;
            if(!bit_){bit_=15u;address_=(address_+1u)&63u;++words_read_;}else --bit_;
            return true;
        }
        if(!bits_ && !data)return true; // leading zero clocks do not start an instruction
        command_=static_cast<std::uint16_t>((command_<<1u)|(data ? 1u : 0u));
        if(++bits_==9u) {
            if((command_>>6u)!=6u)
                throw DeviceRefusal("Microwire programming/control opcode is not modeled; image remains read-only");
            address_=command_&63u;bit_=15u;reading_=true;output_=false;++commands_;
        }
        return true;
    }
private:
    std::array<std::uint16_t,64> words_{};
    Presence presence_{Presence::Unknown};
    bool backed_{},selected_{},clock_{},reading_{};
    unsigned bits_{},address_{},bit_{15};
    std::uint16_t command_{};
    std::optional<bool> output_;
    std::uint64_t words_read_{},commands_{};
};
} // namespace ps2native
