// Adapted from DirectXTK GamePad.cpp (MIT), Microsoft Corporation.
// Pinned source and license: licenses/DirectXTK/LICENSE.
#pragma once
#include <array>
#include <cstdint>
namespace ps2launcher {
inline std::uint8_t pad_axis_byte(std::int32_t value,std::int32_t deadzone,bool invert=false) {
    if(value>=-deadzone && value<=deadzone)return 0x80u;
    value=value<-32768?-32768:value>32767?32767:value;
    if(invert)return static_cast<std::uint8_t>(128-(value>=0?value*128/32767:value*127/32768));
    return static_cast<std::uint8_t>(128+(value>=0?value*127/32767:value*128/32768));
}
class GamepadRetry {
public:
    bool throttled(unsigned player,std::uint64_t time) const {
        if(player>=connected_.size())return true;
        if(connected_[player])return false;
        for(unsigned j=0;j<connected_.size();++j)if(!connected_[j]) {
            const auto delta=static_cast<std::int64_t>(time)-static_cast<std::int64_t>(last_read_[j]);
            const std::int64_t interval=j==player ? 1000 : 250;
            if(delta>=0 && delta<interval)return true;
        }
        return false;
    }
    void observed(unsigned player,bool connected,std::uint64_t time) {
        if(!connected || !connected_[player])last_read_[player]=time;
        connected_[player]=connected;
    }
private:
    std::array<bool,4> connected_{};
    std::array<std::uint64_t,4> last_read_{};
};
}
