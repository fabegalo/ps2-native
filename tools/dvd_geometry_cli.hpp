#pragma once
#include "ps2native/disc.hpp"
#include <charconv>
#include <stdexcept>
#include <string_view>

namespace ps2native::cli {
struct DiagnosticDvdOptions {
    std::optional<std::uint32_t> layer0;
    std::optional<bool> opposite;
    bool parse(std::string_view flag) {
        constexpr std::string_view layer="--diagnostic-dvd-layer0=",track="--diagnostic-dvd-track=";
        if(flag.starts_with(layer)) {
            if(layer0)throw std::runtime_error("duplicate diagnostic DVD layer size");
            const auto text=flag.substr(layer.size());std::uint32_t value{};
            const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
            if(text.empty() || result.ec!=std::errc{} || result.ptr!=text.data()+text.size() || value<=16u || value>2295104u)
                throw std::runtime_error("diagnostic DVD layer size must be 17..2295104 sectors");
            layer0=value;return true;
        }
        if(flag.starts_with(track)) {
            if(opposite)throw std::runtime_error("duplicate diagnostic DVD track path");
            const auto text=flag.substr(track.size());
            if(text!="otp" && text!="ptp")throw std::runtime_error("diagnostic DVD track path must be otp or ptp");
            opposite=text=="otp";return true;
        }
        return false;
    }
    std::optional<DiscImage::DiagnosticDvdGeometry> geometry(bool disc) const {
        if(!layer0 && !opposite)return std::nullopt;
        if(!disc || !layer0 || !opposite)throw std::runtime_error("diagnostic DVD geometry requires --disc, layer size and track path");
        return DiscImage::DiagnosticDvdGeometry{*layer0,*opposite};
    }
};
}
