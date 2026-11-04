#pragma once
#include "ps2native/control_flow.hpp"
#include <string>

namespace ps2native {
struct AnalysisHint {
    std::uint32_t start{}, end{};
    std::string name;
};
struct AnalysisOptions {
    std::vector<std::uint32_t> entries;
    std::vector<AotJumpTable> tables;
    // External hints, validated as addresses but not certified function extents.
    std::vector<AnalysisHint> ghidra;
};
struct AnalysisReport { std::string json, text, dot; };
[[nodiscard]] std::vector<AnalysisHint> parse_ghidra_csv(std::string_view text);
[[nodiscard]] AnalysisReport analyze_ee_image(const ElfImage& image, const AnalysisOptions& options = {});
// ELF/IRX section metadata only; no R5900 decoding of IOP instructions.
[[nodiscard]] AnalysisReport analyze_irx_metadata(std::span<const std::uint8_t> bytes);
} // namespace ps2native
