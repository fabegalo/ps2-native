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
    if(!alias)return image;
    if(alias!=0x80000000u && alias!=0xA0000000u)throw std::runtime_error("invalid EE RAM execution alias");
    // Raw pointers keep their guest values. Do not rebase symbolic evidence.
    if(!image.function_entries.empty() || !image.relocated_code_pointers.empty() ||
       !image.rejected_code_pointers.empty() || !image.address_pairs.empty() ||
       !image.unused_relocation_counts.empty() || image.ignored_rela_sections || image.unpaired_lo16_relocations)
        throw std::runtime_error("execution alias requires a stripped image without symbol/relocation evidence");
    for(auto& segment:image.segments) {
        if(segment.vaddr>=Memory::kMainRamBytes || segment.memory_size>Memory::kMainRamBytes-segment.vaddr)
            throw std::runtime_error("execution alias requires physical EE RAM segments");
        segment.vaddr|=alias;
    }
    image.entry|=alias;
    return image;
}
} // namespace ps2native::cli
