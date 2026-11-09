#pragma once
#include "ps2native/elf.hpp"
#include "ps2native/memory.hpp"
#include <charconv>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace ps2native::cli {
inline bool parse_execution_alias(std::string_view flag,std::optional<std::uint32_t>& alias) {
    constexpr std::string_view prefix="--execution-alias=";
    if(!flag.starts_with(prefix))return false;
    if(alias)throw std::runtime_error("duplicate execution alias");
    auto text=flag.substr(prefix.size());int base=10;
    if(text.starts_with("0x") || text.starts_with("0X")){text.remove_prefix(2);base=16;}
    std::uint32_t value{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value,base);
    if(text.empty() || parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() ||
       (value && value!=0x80000000u && value!=0xA0000000u))
        throw std::runtime_error("execution alias must be 0, 0x80000000 or 0xA0000000");
    alias=value;return true;
}
inline ElfImage ee_execution_alias(ElfImage image,std::uint32_t alias) {
    return ps2native::ee_execution_alias(std::move(image),alias);
}
} // namespace ps2native::cli
