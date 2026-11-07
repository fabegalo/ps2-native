#pragma once
#include "ps2native/aot_sources.hpp"
#include "ps2native/vu_aot_runtime.hpp"
#include <map>
#include <span>

namespace ps2native {
// A complete VU1 micro-memory observation, including unwritten/unknown bits.
// Addresses and entry roots use byte PCs (8-byte aligned), not MPG indices.
struct VuAotOptions {
    std::vector<std::uint32_t> entries{0};
    bool diagnostic_power_two_carry{};
};
struct VuAotInstruction {
    std::uint32_t lower{},upper{};
    std::uint32_t vf_reads{},vf_writes{};
    std::uint16_t vi_reads{},vi_writes{};
    bool reads_acc{},writes_acc{},reads_q{},writes_q{},reads_i{},writes_i{},reads_flags{},writes_flags{};
};
struct VuAotBoundary { std::uint32_t pc{}; std::string reason; };
struct VuAotAnalysis {
    std::map<std::uint32_t,VuAotInstruction> instructions;
    std::vector<VuAotBoundary> boundaries;
};
// Canonical, versioned identity covers every value/mask and the VU1 semantic
// contract. No runtime compiler or process-global firmware cache is involved.
// Raw 16 KiB dumps mean all bits are known; the versioned capture additionally
// preserves unknown bits. Malformed captures fail rather than guessing holes.
[[nodiscard]] VuAotImage parse_vu1_aot_image(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::uint8_t> encode_vu1_aot_image(const VuAotImage& image);
[[nodiscard]] VuAotAnalysis analyze_vu1_aot(const VuAotImage& image,const VuAotOptions& options = {});
[[nodiscard]] std::string generate_vu1_aot_cpp(const VuAotImage& image,const VuAotOptions& options = {});
[[nodiscard]] AotSources generate_vu1_aot_sources(const VuAotImage& image,const VuAotOptions& options,
    const std::string& module_key,std::uint32_t partition_bytes = 1024);
} // namespace ps2native
