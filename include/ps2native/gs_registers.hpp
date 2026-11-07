#pragma once
// GS privileged registers (physical 0x12000000..0x12001FFF) as seen by the EE,
// independent of any graphics backend, with a deterministic software GS subset.
//
// Modeled: SMODE1, SMODE2, SRFSH, SYNCH1, SYNCH2, SYNCV (write-only, 64-bit
// stores), CSR events/IMR, SIGNAL/FINISH/LABEL, display and software drawing.
// Full CSR reads still refuse unknown field/revision state. The low byte can
// be read independently, without inventing FIELD/NFIELD or chip identity.
#include "ps2native/device_timing.hpp"
#include "ps2native/framebuffer.hpp"
#include "ps2native/gs_vram.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace ps2native {
inline constexpr std::uint32_t kGsPrivilegedBase = 0x12000000u;
inline constexpr std::uint32_t kGsPrivilegedEnd = 0x12002000u;
inline constexpr std::uint32_t kGsSmode1 = 0x12000010u;
inline constexpr std::uint32_t kGsSmode2 = 0x12000020u;
inline constexpr std::uint32_t kGsSrfsh = 0x12000030u;
inline constexpr std::uint32_t kGsSynch1 = 0x12000040u;
inline constexpr std::uint32_t kGsSynch2 = 0x12000050u;
inline constexpr std::uint32_t kGsSyncv = 0x12000060u;
inline constexpr std::uint32_t kGsCsr = 0x12001000u;
inline constexpr std::uint32_t kGsPmode = 0x12000000u;
inline constexpr std::uint32_t kGsDispfb1 = 0x12000070u;
inline constexpr std::uint32_t kGsDisplay1 = 0x12000080u;
inline constexpr std::uint32_t kGsDispfb2 = 0x12000090u;
inline constexpr std::uint32_t kGsDisplay2 = 0x120000A0u;
inline constexpr std::uint32_t kGsBgcolor = 0x120000E0u;
inline constexpr std::uint32_t kGsImr = 0x12001010u;
inline constexpr std::uint32_t kGsSiglblid = 0x12001080u;

// SMODE1 fields used by the line clock.
inline constexpr std::uint64_t kSmode1Prst = 1ull << 16;
inline constexpr std::uint64_t kSmode1Sint = 1ull << 17;
inline constexpr unsigned kSmode1CmodShift = 13u;
// CSR write bits (ps2sdk GS_SET_CSR).
inline constexpr std::uint64_t kCsrSignal = 1ull << 0;
inline constexpr std::uint64_t kCsrFinish = 1ull << 1;
inline constexpr std::uint64_t kCsrHsint = 1ull << 2;
inline constexpr std::uint64_t kCsrVsint = 1ull << 3;
inline constexpr std::uint64_t kCsrEdwint = 1ull << 4;
inline constexpr std::uint64_t kCsrFlush = 1ull << 8;
inline constexpr std::uint64_t kCsrReset = 1ull << 9;

class GsRegisters {
    friend struct CheckpointAccess;
public:
    // HBLANK source for the EE timers: lines start at `epoch` and repeat
    // every `period` EE cycles; the first edge is one period after `epoch`.
    struct LineClock {
        std::uint64_t epoch{};
        std::uint64_t period{};
        bool operator==(const LineClock&) const = default;
    };
    // VBLANK source (IOP INTC IRQ 0/11, EE INTC VBON/VBOF). A field is the sum
    // of the SYNCV half-line fields (VFP, VFPE, VBP, VBPE, VDP, VS; ps2sdk
    // gs_privileged.h layout); VDP half-lines carry video, the others are the
    // vertical blank (the BIOS's NTSC SYNCV gives 525 = 480 + 45, ps2tek's
    // 240 visible and 22 blank lines). A half-line is half the line clock.
    // Hypothesis: the field starts with VBLANK start at the line clock epoch.
    struct VerticalClock {
        enum class State : std::uint8_t {
            Unknown, // no edge times can be given: `reason` says why
            Held,    // SMODE1.SINT = 1: video sync held, no edges
            Running
        };
        State state{State::Unknown};
        std::uint64_t epoch{};     // EE cycle of a VBLANK start
        std::uint64_t half_line{}; // EE cycles
        std::uint32_t field{};     // half-lines per field
        std::uint32_t blank{};     // half-lines from VBLANK start to VBLANK end
        std::string reason;
        // First VBLANK start (`end` false) or end at an EE cycle >= `from`.
        [[nodiscard]] std::uint64_t next_edge(std::uint64_t from, bool end) const;
    };
    // Called with the reason before a write changes the line clock; it may
    // throw DeviceRefusal to reject the write (nothing is modified then).
    using LineClockGuard = std::function<void()>;

    // Power-on state at EE cycle `now`: nothing written; the line clock is
    // NTSC from `now` (hypothesis, as before the GS was modeled).
    void reset(std::uint64_t now, std::optional<std::uint64_t> diagnostic_reset_syncv = {},
               std::optional<bool> diagnostic_reset_field = {});
    // Explicit experiment only, configured before guest activity. The normal
    // profile keeps SCANMSK unknown; reference-emulator reset is not a measurement.
    void diagnostic_scanmask_reset(unsigned mask);
    // Explicit hypothesis, disabled by default: GS reset invalidates texture
    // cache tags. This defines no VRAM data and fabricates no TEXFLUSH write.
    void diagnostic_texture_cache_reset_invalid();
    // Independent diagnostic arithmetic assumptions: bit0 nominal rational
    // sprite UV floored to 10.4; bit1 horizontal then vertical bilinear
    // truncation; bit2 nominal rational triangle UV floored to 10.4.
    // Bit3: linear host-to-local pixel stream for rectangles outside the
    // manual's alignment constraints (reference-emulator hypothesis).
    // RUNTIME_HELPERS_BEGIN
    // Bit7 assumes REPEAT/REPEAT CLAMP reset modes; MIN/MAX bits remain unknown.
    // Bit8 assumes SMODE2 padding has no effect; the full written word is retained.
    // RUNTIME_HELPERS_END
    // These do not establish physical GS DDA/filter precision. Color/depth
    // interpolation retains its strict precision checks in every mode.
    void diagnostic_texture_precision(unsigned modes);
    [[nodiscard]] std::uint64_t read(std::uint32_t physical, std::size_t bytes) const;
    [[nodiscard]] ObservedDoubleword observe_csr() const noexcept {
        auto result=ObservedDoubleword{events_,std::uint64_t{unknown_events_}|~std::uint64_t{0x1F}};
        if(field_) { result.value|=std::uint64_t{*field_}<<13u;result.unknown&=~(1ull<<13u); }
        return result;
    }
    void write(std::uint32_t physical, std::size_t bytes, std::uint64_t value, std::uint64_t now,
               const LineClockGuard& guard);
    // Throws DeviceRefusal when no HBLANK is generated or its timing is not modeled.
    [[nodiscard]] LineClock line_clock() const;
    [[nodiscard]] bool hblank_held() const noexcept { return line_.sync == Sync::Stopped; }
    [[nodiscard]] VerticalClock vertical_clock() const;
    // Incremented whenever the line or vertical clock may change.
    [[nodiscard]] std::uint64_t timing_generation() const noexcept { return generation_; }
    [[nodiscard]] bool interrupts_masked() const noexcept { return (imr_ & 0x1F00u) == 0x1F00u; }
    // Lazy event sampling at the common EE clock. Unknown timing taints the
    // corresponding flag/request; masking a source never erases its history.
    void advance(std::uint64_t now);
    struct InterruptRequest { bool pending{}, unknown{}; };
    [[nodiscard]] InterruptRequest take_interrupt_request(std::uint64_t now);
    // RUNTIME_HELPERS_BEGIN
    [[nodiscard]] std::optional<std::uint64_t> idle_event_horizon(std::uint64_t now) const;
    // RUNTIME_HELPERS_END
    // GS general register writes from GIF. Unsupported effects fail at use.
    void general_write(std::uint8_t index, std::uint64_t value);
    void general_write_observed(std::uint8_t index,ObservedDoubleword word);
    void image_write(std::uint64_t low, std::uint64_t high);
    [[nodiscard]] const GsVram& vram() const noexcept { return vram_; }
    // HOST_INPUT_BEGIN
    void set_gpu_backend(std::shared_ptr<GsGpu> backend) { vram_.set_gpu_backend(std::move(backend)); }
    [[nodiscard]] const std::shared_ptr<GsGpu>& gpu_backend() const noexcept { return vram_.gpu_backend(); }
    void synchronize_graphics() const { vram_.synchronize(); }
    // HOST_INPUT_END
    // Snapshot of a single progressive read circuit. No scaling or invented
    // display size/timing; disabled output and unsupported merge modes refuse.
    [[nodiscard]] Framebuffer scanout(std::uint64_t ee_cycle) const;
    // Value last written to general register `index` since reset.
    [[nodiscard]] std::optional<std::uint64_t> general(std::uint8_t index) const { return general_[index]; }
    [[nodiscard]] std::optional<ObservedDoubleword> general_observed(std::uint8_t index) const {
        if(!general_[index])return {};
        return ObservedDoubleword{*general_[index],general_unknown_[index]};
    }
    // Last value written to a modeled register since reset (for traces and tests).
    [[nodiscard]] std::optional<std::uint64_t> latched(std::uint32_t physical) const;

private:
    enum class Sync : std::uint8_t { Running, Stopped, UnsupportedMode };
    struct LineState {
        Sync sync{Sync::Running};
        unsigned cmod{2u};
        LineClock clock{};
        bool operator==(const LineState&) const = default;
    };
    static constexpr std::size_t kModeRegisters = 6u; // SMODE1..SYNCV
    void change_line_state(const LineState& next, const LineClockGuard& guard);
    void write_impl(std::uint32_t physical, std::size_t bytes, std::uint64_t value, std::uint64_t now,
                    const LineClockGuard& guard);
    [[nodiscard]] std::uint64_t required(std::uint8_t index) const;
    void vertex(std::uint8_t index, std::uint64_t value);
    void start_transfer(std::uint64_t value);
    void transfer_data(std::uint64_t value);
    void load_clut(std::uint64_t tex0);
    [[nodiscard]] std::uint32_t sample_texture(std::uint64_t tex0, std::uint64_t clamp, int u, int v);
    void texture_write_guard(unsigned address) const;
    void texture_transfer_write(unsigned address);
    void reset_drawing();
    void reset_events(std::uint64_t now);
    void raise_event(std::uint32_t event);
    void update_interrupt_request();
    void finish_transfer();

    std::array<std::optional<std::uint64_t>, kModeRegisters> mode_{};
    std::optional<std::uint64_t> diagnostic_reset_syncv_;
    std::optional<bool> diagnostic_reset_field_,field_;
    std::optional<unsigned> diagnostic_reset_scanmask_;
    bool diagnostic_reset_texture_cache_invalid_{};
    unsigned diagnostic_texture_precision_{};
    std::optional<std::uint64_t> csr_written_;
    std::array<std::optional<std::uint64_t>, 256> general_{};
    std::array<std::uint64_t,256> general_unknown_{};
    std::array<std::optional<std::uint64_t>, 15> display_{};
    GsVram vram_;
    struct Vertex {
        int x{}, y{}, u{}, v{};
        std::uint32_t z{};
        std::optional<std::uint64_t> rgbaq,st,uv;
        std::uint64_t rgbaq_unknown{};
        std::uint64_t st_unknown{};
    };
    std::optional<Vertex> first_vertex_;
    std::array<Vertex,2> triangle_vertices_{};
    unsigned triangle_vertices_count_{};
    std::array<std::optional<std::uint32_t>,256> clut_{};
    std::array<std::optional<unsigned>,2> clut_base_{};
    std::array<bool,512> sampled_pages_{};
    std::array<bool,512> dirty_texture_pages_{};
    std::uint64_t transfer_generation_{};
    std::optional<std::uint64_t> texture_flush_generation_;
    struct Transfer {
        bool active{};
        unsigned base{}, bw{}, psm{}, x{}, y{}, width{}, height{}, pixel{}, pending_bytes{};
        std::uint32_t pending{};
    } transfer_;
    std::uint32_t imr_{0x7F00u};
    std::uint32_t events_{}, unknown_events_{};
    std::uint64_t event_cycle_{};
    bool irq_pending_{}, irq_unknown_{}, finish_pending_{};
    std::uint64_t siglblid_{}, siglblid_known_{};
    LineState line_{};
    std::uint64_t generation_{};
};
} // namespace ps2native
