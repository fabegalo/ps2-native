#pragma once
#include "ps2native/diagnostics.hpp"
#include "ps2native/io.hpp"
#include <array>
#include <optional>
#include <charconv>
#include <iostream>

namespace ps2native::cli {
inline std::uint64_t decimal(std::string_view text, std::uint64_t limit) {
    std::uint64_t n{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), n);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || text.empty() || n > limit)
        throw std::runtime_error("invalid bounded decimal option");
    return n;
}
inline bool parse_spu2_effect_end(std::string_view flag,std::optional<std::array<std::uint16_t,2>>& values) {
    constexpr std::string_view prefix="--assume-spu2-effect-end-reset=";
    if(!flag.starts_with(prefix))return false;
    if(values)throw std::runtime_error("duplicate SPU2 initial EEA");
    const auto text=flag.substr(prefix.size());const auto colon=text.find(':');
    if(colon==std::string_view::npos)throw std::runtime_error("SPU2 initial EEA requires CORE0:CORE1 in decimal");
    values=std::array<std::uint16_t,2>{static_cast<std::uint16_t>(decimal(text.substr(0,colon),15)),
        static_cast<std::uint16_t>(decimal(text.substr(colon+1),15))};
    return true;
}
inline std::filesystem::path utf8_path(std::string_view s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}
struct DiagnosticOptions {
    std::filesystem::path directory;
    std::size_t capacity{256};
    std::uint64_t stream_bytes{};
    std::vector<std::pair<std::uint32_t,std::size_t>> ram;
    std::unique_ptr<DiagnosticRecorder> recorder;
    bool parse(std::string_view flag) {
        if (flag.starts_with("--diagnostic-dir=")) directory = utf8_path(flag.substr(17));
        else if (flag.starts_with("--diagnostic-ring=")) capacity = static_cast<std::size_t>(decimal(flag.substr(18), 1'000'000));
        else if (flag.starts_with("--diagnostic-stream-bytes=")) stream_bytes = decimal(flag.substr(26), 1ull << 40);
        else if (flag.starts_with("--diagnostic-ram=")) {
            const auto text = std::string(flag.substr(17)); const auto colon = text.find(':');
            if (colon == std::string::npos) throw std::runtime_error("RAM window requires ADDRESS:BYTES");
            std::size_t used{}; const auto address = std::stoull(text.substr(0,colon),&used,0);
            if (used != colon || address > UINT32_MAX) throw std::runtime_error("invalid RAM window address");
            ram.emplace_back(static_cast<std::uint32_t>(address), static_cast<std::size_t>(decimal(text.substr(colon+1),4096)));
        }
        else return false;
        return true;
    }
    void attach(BootOptions& options, std::span<const std::filesystem::path> inputs) {
        if (directory.empty()) {
            if (stream_bytes || capacity != 256 || !ram.empty()) throw std::runtime_error("diagnostic options require --diagnostic-dir");
            return;
        }
        // Prevent symlink/hardlink output aliases from replacing user inputs.
        for (const auto* name : {"events.ndjson", "full-events.ndjson", "state.json", "report.json", "report.txt", "frame.bmp", "complete"})
            for (const auto& input : inputs)
                if (!input.empty() && file_exists(directory / name) && files_equivalent(directory / name, input))
                    throw std::runtime_error("diagnostic output aliases an input");
        recorder = std::make_unique<DiagnosticRecorder>(capacity);
        if(file_exists(directory / "complete")) (void)remove_file(directory / "complete");
        for (const auto& [address,bytes] : ram) recorder->watch_ram(address,bytes);
        if (stream_bytes) recorder->stream(directory / "full-events.ndjson", stream_bytes);
        options.diagnostics = recorder.get(); options.max_trace_events = capacity;
    }
    void finish(const BootReport& report, const EEState& state, const Memory& memory) {
        if (recorder) {
            recorder->write(directory, report, state, memory);
            std::cout << "diagnostic_report=" << path_utf8(directory / "report.json") << '\n';
        }
    }
};
} // namespace ps2native::cli
