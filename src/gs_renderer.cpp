// Independently implemented from register/VRAM specifications, no emulator code.
#include "ps2native/gs_registers.hpp"
#include <algorithm>
#include <string>

namespace ps2native {
std::uint64_t GsRegisters::required(std::uint8_t index) const {
    if (!general_[index]) throw DeviceRefusal("GS drawing/transfer register " + std::to_string(index) +
                                             " has not been written since reset");
    return *general_[index];
}
void GsRegisters::reset_drawing() {
    general_ = {}; display_ = {}; first_vertex_.reset(); transfer_ = {};
    imr_ = 0x7F00u;
}
void GsRegisters::start_transfer(std::uint64_t value) {
    if (value & ~3ull) throw DeviceRefusal("GS TRXDIR bits above 1 are reserved");
    const auto direction = static_cast<unsigned>(value);
    if (direction == 3u) { transfer_ = {}; return; }
    const auto area = required(0x52);
    if (area & ~0xFFF00000FFFull) throw DeviceRefusal("GS TRXREG reserved bits set");
    const auto width = static_cast<unsigned>(area & 0xFFFu);
    const auto height = static_cast<unsigned>((area >> 32) & 0xFFFu);
    if (!width || !height) { transfer_ = {}; return; } // empty transfer, inherited hypothesis
    if (direction == 1u) throw DeviceRefusal("GS local-to-host transfer is not modeled");
    const auto buf = required(0x50), pos = required(0x51);
    const auto base = static_cast<unsigned>((buf >> 32) & 0x3FFFu) * 256u;
    const auto bw = static_cast<unsigned>((buf >> 48) & 63u) * 64u;
    const auto psm = static_cast<unsigned>((buf >> 56) & 63u);
    const auto x = static_cast<unsigned>((pos >> 32) & 2047u);
    const auto y = static_cast<unsigned>((pos >> 48) & 2047u);
    (void)GsVram::address(base, bw, psm, x, y);
    if (width % (psm == 1u ? 8u : 2u))
        throw DeviceRefusal("GS transfer width violates the PSMCT32/24 hardware alignment");
    const auto order = static_cast<unsigned>((pos >> 59) & 3u);
    if (direction == 0u) {
        // TRXPOS.DIR applies only to local copies; uploads always run left to
        // right, top to bottom (GS manual 4.1.2).
        transfer_ = {true, base, bw, psm, x, y, width, height, 0u, 0u, 0u};
        return;
    }
    const auto sb = static_cast<unsigned>(buf & 0x3FFFu) * 256u;
    const auto sw = static_cast<unsigned>((buf >> 16) & 63u) * 64u;
    const auto sp = static_cast<unsigned>((buf >> 24) & 63u);
    const auto sx = static_cast<unsigned>(pos & 2047u), sy = static_cast<unsigned>((pos >> 16) & 2047u);
    (void)GsVram::address(sb, sw, sp, sx, sy);
    if (sp != psm) throw DeviceRefusal("GS local-to-local mixed formats are not modeled");
    // DIR selects traversal, including overlapping source/destination regions.
    for (unsigned row = 0; row < height; ++row)
        for (unsigned col = 0; col < width; ++col) {
            const auto dx = (order & 2u) ? width - 1u - col : col;
            const auto dy = (order & 1u) ? height - 1u - row : row;
            const auto pixel = vram_.read(sb, sw, sp, (sx + dx) & 2047u, (sy + dy) & 2047u);
            vram_.write(base, bw, psm, (x + dx) & 2047u, (y + dy) & 2047u, pixel);
        }
    transfer_ = {};
}
void GsRegisters::transfer_data(std::uint64_t data) {
    auto& t = transfer_;
    // GS manual HWREG: outside an active host-to-local transfer, no operation.
    if (!t.active) return;
    const unsigned bytes = t.psm == 1u ? 3u : 4u;
    for (unsigned i = 0; i < 8u && t.active; ++i) {
        t.pending |= static_cast<std::uint32_t>((data >> (8u * i)) & 255u) << (8u * t.pending_bytes++);
        if (t.pending_bytes != bytes) continue;
        vram_.write(t.base, t.bw, t.psm, (t.x + t.pixel % t.width) & 2047u,
                     (t.y + t.pixel / t.width) & 2047u, t.pending);
        t.pending = 0u; t.pending_bytes = 0u;
        if (++t.pixel == t.width * t.height) t.active = false;
    }
}
void GsRegisters::image_write(std::uint64_t low, std::uint64_t high) {
    transfer_data(low);
    if (transfer_.active) transfer_data(high); // final quadword padding is discarded
}
void GsRegisters::vertex(std::uint8_t index, std::uint64_t value) {
    const auto prim = required(0x00);
    const auto control = required(0x1A);
    const auto mode = (control & 1u) ? prim : required(0x1B);
    const auto type = static_cast<unsigned>(prim & 7u);
    if (type != 0u && type != 6u) throw DeviceRefusal("GS primitive is not modeled (point/sprite only)");
    if (mode & (8u | 16u | 32u | 64u | 128u))
        throw DeviceRefusal("GS shading, texture, fog, blending or antialiasing is not modeled");
    const auto context = static_cast<std::uint8_t>((mode >> 9) & 1u);
    const auto offset = required(static_cast<std::uint8_t>(0x18u + context));
    const Vertex v{static_cast<int>(value & 0xFFFFu) - static_cast<int>(offset & 0xFFFFu),
                   static_cast<int>((value >> 16) & 0xFFFFu) - static_cast<int>((offset >> 32) & 0xFFFFu)};
    if (type == 6u && !first_vertex_) { first_vertex_ = v; return; }
    // XYZ3/XYZF3 advance the vertex queue without a drawing kick.
    if (index == 0x0Cu || index == 0x0Du) { first_vertex_.reset(); return; }
    const auto frame = required(static_cast<std::uint8_t>(0x4Cu + context));
    const auto scissor = required(static_cast<std::uint8_t>(0x40u + context));
    const auto test = required(static_cast<std::uint8_t>(0x47u + context));
    const auto zbuf = required(static_cast<std::uint8_t>(0x4Eu + context));
    if ((test & 1u) || (test & (1u << 14)) ||
        ((test & (1u << 16)) && ((test >> 17) & 3u) != 1u) || !(zbuf & (1ull << 32)))
        throw DeviceRefusal("GS alpha/depth tests or depth writes are not modeled");
    if (required(0x45) != 0u || required(0x49) != 0u || required(0x22) != 0u || required(0x46) != 1u)
        throw DeviceRefusal("GS dithering, PABE, scan mask or unclamped colors are not modeled");
    auto color = static_cast<std::uint32_t>(required(0x01));
    if (required(static_cast<std::uint8_t>(0x4Au + context)) & 1u) color |= 0x80000000u;
    const auto base = static_cast<unsigned>(frame & 511u) * 8192u;
    const auto bw = static_cast<unsigned>((frame >> 16) & 63u) * 64u;
    const auto psm = static_cast<unsigned>((frame >> 24) & 63u);
    (void)GsVram::address(base, bw, psm, 0u, 0u);
    const auto ceil16 = [](int n) { return n >= 0 ? (n + 15) / 16 : n / 16; };
    const auto floor16 = [](int n) { return n >= 0 ? n / 16 : (n - 15) / 16; };
    int left, top, right, bottom;
    if (type == 0u) {
        left = floor16(v.x); top = floor16(v.y); right = left + 1; bottom = top + 1;
    } else {
        left = ceil16(std::min(first_vertex_->x, v.x)); top = ceil16(std::min(first_vertex_->y, v.y));
        right = ceil16(std::max(first_vertex_->x, v.x)); bottom = ceil16(std::max(first_vertex_->y, v.y));
    }
    left = std::max(left, static_cast<int>(scissor & 2047u));
    right = std::min(right, static_cast<int>((scissor >> 16) & 2047u) + 1);
    top = std::max(top, static_cast<int>((scissor >> 32) & 2047u));
    bottom = std::min(bottom, static_cast<int>((scissor >> 48) & 2047u) + 1);
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            vram_.write(base, bw, psm, static_cast<unsigned>(x), static_cast<unsigned>(y), color,
                         static_cast<std::uint32_t>(frame >> 32));
    first_vertex_.reset();
}
Framebuffer GsRegisters::scanout(std::uint64_t cycle) const {
    if (!display_[0]) throw DeviceRefusal("GS scanout: PMODE not written");
    const auto pmode = *display_[0];
    const auto en = pmode & 3u;
    if (!en) throw DeviceRefusal("GS scanout: read circuits disabled");
    if (en == 3u) throw DeviceRefusal("GS scanout: dual-circuit merge is not modeled");
    if (((pmode >> 2) & 7u) != 1u || (en == 1u && (!(pmode & 32u) || ((pmode >> 8) & 255u) != 255u)))
        throw DeviceRefusal("GS scanout: CRTMD or alpha merge mode is not modeled");
    if (!mode_[1]) throw DeviceRefusal("GS scanout: SMODE2 not written");
    if (*mode_[1] & 1u) throw DeviceRefusal("GS scanout: interlaced fields are not modeled");
    if (*mode_[1] & 12u) throw DeviceRefusal("GS scanout: DPMS suspends output");
    const unsigned circuit = en == 1u ? 7u : 9u;
    if (!display_[circuit] || !display_[circuit + 1u])
        throw DeviceRefusal("GS scanout: DISPFB/DISPLAY not written for enabled circuit");
    const auto fb = *display_[circuit], display = *display_[circuit + 1u];
    const auto magh = static_cast<unsigned>((display >> 23) & 15u) + 1u;
    const auto magv = static_cast<unsigned>((display >> 27) & 3u) + 1u;
    const auto dw = static_cast<unsigned>((display >> 32) & 4095u) + 1u;
    const auto dh = static_cast<unsigned>((display >> 44) & 2047u) + 1u;
    if (dw % magh || dh % magv) throw DeviceRefusal("GS scanout: fractional display magnification is not modeled");
    Framebuffer f;
    f.width = dw / magh; f.height = dh / magv; f.stride = f.width * 4u;
    f.ee_cycle = cycle; f.revision = vram_.revision();
    const auto clock = vertical_clock();
    if (clock.state == VerticalClock::State::Running)
        f.timing = FrameTiming{clock.epoch, clock.half_line, clock.field, clock.blank};
    const auto base = static_cast<unsigned>(fb & 511u) * 8192u;
    const auto bw = static_cast<unsigned>((fb >> 9) & 63u) * 64u;
    const auto psm = static_cast<unsigned>((fb >> 15) & 31u);
    GsVram::validate_format(psm);
    const auto x0 = static_cast<unsigned>((fb >> 32) & 2047u), y0 = static_cast<unsigned>((fb >> 43) & 2047u);
    if (x0 + f.width > 2048u || y0 + f.height > 2048u)
        throw DeviceRefusal("GS scanout: wrapping display rectangle is not modeled");
    f.rgba.reserve(static_cast<std::size_t>(f.stride) * f.height);
    for (unsigned y = 0; y < f.height; ++y)
        for (unsigned x = 0; x < f.width; ++x) {
            // PCRTC single-circuit RGB does not consume framebuffer alpha.
            const auto pixel = vram_.read(base, bw, 1u, x0 + x, y0 + y);
            for (unsigned c = 0; c < 3u; ++c) f.rgba.push_back(static_cast<std::uint8_t>(pixel >> (c * 8u)));
            f.rgba.push_back(255u);
        }
    return f;
}
} // namespace ps2native
