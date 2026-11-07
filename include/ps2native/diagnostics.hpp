#pragma once
#include "ps2native/boot.hpp"
#include <filesystem>
#include <memory>
#include <string_view>

namespace ps2native {
// Optional host diagnostics, attached to the existing shared execution observer.
// The ring is bounded. Streaming is explicit and bounded by a byte budget.
class DiagnosticRecorder {
public:
    explicit DiagnosticRecorder(std::size_t capacity = 256);
    ~DiagnosticRecorder();
    DiagnosticRecorder(const DiagnosticRecorder&) = delete;
    DiagnosticRecorder& operator=(const DiagnosticRecorder&) = delete;
    void stream(const std::filesystem::path& path, std::uint64_t max_bytes);
    void instruction(const EEState& state, std::uint32_t pc, std::uint32_t word);
    void bus(const EEState& state, const BusAccess& access);
    void system(const EEState& state, std::string_view event);
    // Explicit physical backing-RAM inspection, never a CPU bus read.
    void watch_ram(std::uint32_t address, std::size_t bytes);
    void write(const std::filesystem::path& directory, const BootReport& report,
               const EEState& state, const Memory& memory);
    [[nodiscard]] std::uint64_t total_events() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
[[nodiscard]] std::string json_string(std::string_view text);
// Returns 0 for identical recorded events, 1 for a divergence. Throws for
// incomplete/invalid streams or incompatible ring coverage. Does not claim
// equivalence of device state that was not recorded.
int compare_diagnostics(const std::filesystem::path& first, const std::filesystem::path& second,
                        std::string& explanation);
    // RUNTIME_HELPERS_BEGIN
// Offline hints only: validate the stream and every attempted instruction
// against this image before discovering previously uncovered control flow.
[[nodiscard]] std::vector<std::uint32_t> discover_ee_entries(
    const ElfImage& image, const std::filesystem::path& events);
    // RUNTIME_HELPERS_END
} // namespace ps2native
