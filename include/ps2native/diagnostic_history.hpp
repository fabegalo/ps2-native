#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace ps2native {
using DeviceEventSink = std::function<void(std::string_view, std::uint64_t, std::string_view)>;
// Existing device histories retain their unlimited default. Diagnostics can
// bound them and observe events at their source without changing device state.
template<class T> class DiagnosticHistory {
    friend struct CheckpointAccess;
public:
    void configure(std::size_t limit, std::function<void(const T&)> sink) {
        (void)items(); limit_ = limit; sink_ = std::move(sink);
        if (limit_ && data_.size() > limit_) data_.erase(data_.begin(),data_.end()-limit_);
        next_ = 0;
    }
    void push_back(T item) {
        if (sink_) sink_(item);
        if (!limit_ || data_.size() < limit_) data_.push_back(std::move(item));
        else { data_[next_] = std::move(item); next_ = (next_+1)%limit_; }
    }
    void clear() { data_.clear(); next_ = 0; }
    [[nodiscard]] const std::vector<T>& items() const {
        if (next_) { std::rotate(data_.begin(),data_.begin()+next_,data_.end()); next_ = 0; }
        return data_;
    }
private:
    mutable std::vector<T> data_;
    mutable std::size_t next_{};
    std::size_t limit_{};
    std::function<void(const T&)> sink_;
};
} // namespace ps2native
