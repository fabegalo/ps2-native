#include "ps2native/frontend/runtime.hpp"

#include "ps2native/io.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <stdexcept>

namespace ps2native::frontend {
namespace {
constexpr std::array kCapabilities{
    CapabilityStatus{Capability::BootDiagnostic, "Boot diagnostic", true,
                     "Runs ps2n-boot on the selected ROM0 and reports where the firmware reaches unmodeled behavior."},
    CapabilityStatus{Capability::FirmwareBrowser, "PS2 Browser", false,
                     "The original firmware does not reach the Browser yet: its boot stops at hardware that is not modeled."},
    CapabilityStatus{Capability::DiscBoot, "Game boot", false,
                     "Play prepares verified native code and starts the original game with selective HLE; "
                     "successful game boot and gameplay compatibility remain unverified."},
    CapabilityStatus{Capability::Controller, "Controller input", false,
                     "Live keyboard/XInput mapping uses the shared session; commercial input compatibility remains unverified."},
    CapabilityStatus{Capability::Audio, "Audio", false,
                     "SPU2 synthesis and host PCM output use the shared session; commercial audio and several SPU2 effects remain unverified."},
    CapabilityStatus{Capability::MemoryCards, "Memory cards", false,
                     "Memory Card slots are reported empty; card emulation is not implemented."},
    CapabilityStatus{Capability::GsFaithful, "GS faithful renderer", false,
                     "An experimental GS model (local memory, sprites, scanout) runs only in diagnostics; it is "
                     "not a validated renderer yet."},
    CapabilityStatus{Capability::GsNative, "Native renderer", false,
                     "Depends on the faithful GS reference, which is not validated yet."},
};
constexpr std::string_view kResult = "# result ";
constexpr std::string_view kBlock = "# block point ";
constexpr std::string_view kSio = "# | ";
constexpr std::string_view kRomver = "# romver ";
constexpr std::string_view kConsole = "# console ";
constexpr std::string_view kEngine = "# engine ";
constexpr std::string_view kIop = "# IOP: ";
constexpr std::string_view kToolError = "ps2n-boot: ";
} // namespace

std::span<const CapabilityStatus> runtime_capabilities() noexcept { return kCapabilities; }
const CapabilityStatus& capability_status(Capability capability) noexcept {
    for (const auto& status : kCapabilities)
        if (status.capability == capability) return status;
    return kCapabilities.front();
}

std::vector<std::string> diagnostic_arguments(const DiagnosticRequest& request) {
    if (request.rom0.empty()) throw std::invalid_argument("No ROM0 selected");
    if (!request.nvm.empty() && request.console_profile.empty())
        throw std::invalid_argument("An NVM needs its console profile");
    std::vector<std::string> args{path_utf8(request.rom0)};
    if (!request.expected_sha256.empty()) args.push_back("--expect-sha256=" + request.expected_sha256);
    if (!request.console_profile.empty()) args.push_back("--console=" + request.console_profile);
    if (!request.nvm.empty()) args.push_back("--nvm=" + path_utf8(request.nvm));
    args.push_back("--max-instructions=" + std::to_string(request.max_instructions));
    args.push_back(request.full_trace ? "--trace=full" : "--trace=events");
    return args;
}

void parse_diagnostic_line(DiagnosticSummary& summary, std::string_view line) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.starts_with(kSio)) {
        summary.sio_lines.emplace_back(line.substr(kSio.size()));
    } else if (line.starts_with(kResult)) {
        // "# result <outcome>; retired N (aot A, interpreter I, engine switches S)"
        const auto rest = line.substr(kResult.size());
        const auto semicolon = rest.find(';');
        summary.outcome = std::string(rest.substr(0, semicolon));
        if (semicolon != std::string_view::npos) {
            const auto retired = rest.find("retired ", semicolon);
            if (retired != std::string_view::npos) {
                const auto digits = rest.substr(retired + 8);
                std::uint64_t value = 0;
                const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
                if (parsed.ec == std::errc{}) summary.retired = value;
            }
        }
        summary.finished = true;
    } else if (line.starts_with(kBlock)) {
        // "# block point 0xPC [category] reason"
        const auto rest = line.substr(kBlock.size());
        const auto space = rest.find(' ');
        summary.block_pc = std::string(rest.substr(0, space));
        if (space != std::string_view::npos) {
            const auto open = rest.find('[', space);
            const auto close = rest.find(']', open == std::string_view::npos ? space : open);
            if (open != std::string_view::npos && close != std::string_view::npos) {
                summary.block_category = std::string(rest.substr(open + 1, close - open - 1));
                summary.block_reason = std::string(rest.substr(std::min(rest.size(), close + 2)));
            } else {
                summary.block_reason = std::string(rest.substr(space + 1));
            }
        }
    } else if (line.starts_with(kRomver)) {
        summary.romver = std::string(line.substr(kRomver.size()));
    } else if (line.starts_with(kConsole)) {
        summary.console = std::string(line.substr(kConsole.size()));
    } else if (line.starts_with(kEngine)) {
        summary.engine = std::string(line.substr(kEngine.size()));
    } else if (line.starts_with(kIop)) {
        summary.iop = std::string(line.substr(kIop.size()));
    } else if (line.starts_with(kToolError)) {
        summary.tool_error = std::string(line.substr(kToolError.size()));
    }
}
} // namespace ps2native::frontend
