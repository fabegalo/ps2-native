#pragma once
#include "ps2native/aot_sources.hpp"
#include "ps2native/device_timing.hpp"
#include <span>
#include <string_view>
#include <memory>

namespace ps2native {
class Iop;
inline constexpr unsigned kIopAotAbi=1;
struct IopAotEntry {
    std::uint32_t address,word;
    void (*execute)(Iop&);
};
struct IopAotModule {
    std::string_view source_sha256,identity;
    std::span<const IopAotEntry> entries;
};
struct IopAotRegistration {
    explicit IopAotRegistration(const IopAotModule&);
    IopAotRegistration(const IopAotModule&,std::shared_ptr<const void> code_owner);
    ~IopAotRegistration();
    IopAotRegistration(const IopAotRegistration&)=delete;
    IopAotRegistration& operator=(const IopAotRegistration&)=delete;
};
[[nodiscard]] std::string iop_aot_identity(std::string_view source,std::span<const IopAotEntry> entries);
bool execute_iop_aot(Iop&,std::uint32_t word);
[[nodiscard]] std::string missing_iop_aot(std::uint32_t pc,std::uint32_t word);
[[nodiscard]] std::vector<std::uint8_t> encode_iop_aot_image(std::uint32_t base,std::span<const ObservedWord> words);
// Input is the already-loaded image at its actual address, after guest relocation.
// Never apply ELF/IRX relocations again to a loaded snapshot.
[[nodiscard]] AotSources generate_iop_aot_sources(std::span<const std::uint8_t> bytes,
    std::uint32_t base,std::span<const std::uint32_t> entries,const std::string& module_key,
    std::uint32_t partition_bytes=65536);
} // namespace ps2native
