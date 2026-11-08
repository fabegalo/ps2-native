// Adapted from DirectXTK GamePad.cpp (MIT), Microsoft Corporation.
// Pinned source and license: third_party/DirectXTK/LICENSE.
#pragma once
#include <array>
#include <cstdint>
namespace ps2launcher {
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
