#pragma once
// Contracts between the desktop launcher and the emulated system. The launcher
// manages the original system (BIOS, NVM, discs); it never replaces the
// firmware's own screens. What the runtime cannot do yet is reported here with
// its reason so the UI disables it explicitly instead of faking it.
//
// Today only the boot diagnostic exists: the ps2n-boot tool run as a child
// process (crash-isolated from the UI). Future runtime components (AOT game
// runtime, controller, audio, faithful GS and Native renderers) plug in behind
// RuntimeSession without changing the UI contract.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ps2native::frontend {
enum class Capability : std::uint8_t {
    BootDiagnostic,  // EE/IOP boot from the reset vector up to the first unmodeled behavior
    FirmwareBrowser, // the original Browser / System Configuration
    DiscBoot,        // booting a game disc through the firmware
    Controller,
    Audio,
    MemoryCards,
    GsFaithful,      // reference GS rendering
    GsNative         // enhanced Native rendering (same game logic)
};
struct CapabilityStatus {
    Capability capability;
    std::string_view name;
    bool available;
    std::string_view reason; // why it is unavailable (or what it does when available)
};
[[nodiscard]] std::span<const CapabilityStatus> runtime_capabilities() noexcept;
[[nodiscard]] const CapabilityStatus& capability_status(Capability capability) noexcept;

enum class RendererMode : std::uint8_t { GsFaithful, Native };

// Everything a future runtime session needs from the launcher. The launcher
// resolves it from the library; the session validates it again (hashes,
// ROMVER/profile, NVM) and fails explicitly.
struct SessionRequest {
    std::filesystem::path rom0;
    std::string console_profile;        // MODEL:MAINBOARD, empty: none
    std::filesystem::path nvm;          // empty: none
    std::optional<std::filesystem::path> disc; // empty: firmware only (Browser)
    RendererMode renderer{RendererMode::GsFaithful};
};
enum class SessionState : std::uint8_t { Idle, Starting, Running, Stopped, Failed };
// Implemented by the future runtime. No implementation exists today, and the
// launcher refuses to start one (capability_status(...).available is false).
class RuntimeSession {
public:
    virtual ~RuntimeSession() = default;
    virtual void start(const SessionRequest& request) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual SessionState state() const = 0;
    [[nodiscard]] virtual std::string last_error() const = 0;
};

// --- boot diagnostic (ps2n-boot) ---
struct DiagnosticRequest {
    std::filesystem::path rom0;
    std::string expected_sha256;   // the library's identification; ps2n-boot refuses a changed file
    std::string console_profile;   // empty: no console (board registers refuse reads)
    std::filesystem::path nvm;     // requires console_profile
    std::uint64_t max_instructions{10'000'000};
    bool full_trace{};
};
// Arguments after the program name, UTF-8 for flags and native for paths.
// Throws std::invalid_argument for an inconsistent request (NVM without console).
[[nodiscard]] std::vector<std::string> diagnostic_arguments(const DiagnosticRequest& request);

struct DiagnosticSummary {
    std::string romver;
    std::string console;
    std::string engine;
    std::string outcome;       // "block point", "instruction budget reached", "stop address reached"
    std::string block_pc;
    std::string block_category;
    std::string block_reason;
    std::uint64_t retired{};
    std::string iop;           // "N instructions, PC 0x..., stopped"
    std::vector<std::string> sio_lines; // EE SIO console output of the BIOS
    std::string tool_error;    // "ps2n-boot: ..." on stderr
    bool finished{};           // a "# result" line was seen
};
// Feeds one line of ps2n-boot output (stdout or stderr) into the summary.
void parse_diagnostic_line(DiagnosticSummary& summary, std::string_view line);
} // namespace ps2native::frontend
