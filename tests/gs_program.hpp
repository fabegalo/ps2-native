// Original synthetic PS2 program: LQ/SQ -> GIF PATH3 -> GS VRAM -> scanout.
// This is a GS test card, never a replacement BIOS or Browser screen.
#pragma once
#include "bios_program.hpp"
#include <utility>

namespace m29 {
inline constexpr unsigned kWidth = 256u, kHeight = 160u;
inline constexpr std::uint32_t kBackground = 0x80241912u;
struct Program { std::vector<std::uint8_t> rom; std::uint32_t stop{}; };
inline Program build() {
    auto rom = m20::build_firmware().rom;
    m20::Assembler a(m20::kResetVector);
    using namespace m20;
    std::uint32_t data = 0x3000u;
    const auto put64 = [&](std::uint32_t at, std::uint64_t value) {
        for (unsigned b = 0; b < 8u; ++b) rom.at(at + b) = static_cast<std::uint8_t>(value >> (8u * b));
    };
    const auto privileged = [&](std::uint32_t address, std::uint64_t value) {
        put64(data, value);
        a.la(t0, address); a.la(t1, kResetVector + data);
        a.lq(t2, 0, t1); a.sd(t2, 0, t0); data += 16u;
    };
    privileged(0xB2000010u, 2u << 13); // NTSC line clock; synthetic short SYNCV below
    privileged(0xB2000020u, 0u); // progressive
    privileged(0xB2000000u, 1u | 4u | 32u | (255u << 8));
    privileged(0xB2000070u, 4u << 9); // FBP 0, FBW 256, PSMCT32, DBX/Y 0
    privileged(0xB2000080u, (std::uint64_t{kWidth - 1u} << 32) | (std::uint64_t{kHeight - 1u} << 44));
    // 2 display + 2 blank half-lines: tests event ordering, not a real TV mode.
    privileged(0xB2000060u, 2u | (2ull << 42));
    std::vector<std::pair<std::uint64_t, std::uint64_t>> commands;
    const auto ad = [&](unsigned reg, std::uint64_t value) { commands.emplace_back(value, reg); };
    ad(0x1A, 1u); ad(0x18, 0u); ad(0x4C, 4u << 16);
    ad(0x40, (std::uint64_t{kWidth - 1u} << 16) | (std::uint64_t{kHeight - 1u} << 48));
    ad(0x47, (1u << 16) | (1u << 17)); ad(0x4E, 1ull << 32);
    ad(0x45, 0u); ad(0x49, 0u); ad(0x22, 0u); ad(0x46, 1u); ad(0x4A, 0u);
    const auto rect = [&](unsigned x0, unsigned y0, unsigned x1, unsigned y1, std::uint32_t color) {
        ad(0, 6u); ad(1, color);
        ad(5, (x0 * 16u) | (std::uint64_t{y0 * 16u} << 16));
        ad(5, (x1 * 16u) | (std::uint64_t{y1 * 16u} << 16));
    };
    rect(0, 0, kWidth, kHeight, kBackground);
    rect(12, 12, 244, 16, 0x80E2B832u);
    rect(12, 132, 244, 136, 0x80E2B832u);
    // PS2 in a deliberately simple 5x7 pixel font, each dot is a GS sprite.
    constexpr const char* glyphs[] = {
        "11110100011000111110100001000010000",
        "01111100001000001110000010000111110",
        "11110000010000101110100001000011111"
    };
    for (unsigned g = 0; g < 3u; ++g)
        for (unsigned y = 0; y < 7u; ++y)
            for (unsigned x = 0; x < 5u; ++x)
                if (glyphs[g][y * 5u + x] == '1') {
                    const auto px = 40u + g * 64u + x * 10u, py = 34u + y * 10u;
                    rect(px, py, px + 8u, py + 8u, 0x80F5F0E8u);
                }
    for (unsigned i = 0; i < 8u; ++i)
        rect(24u + i * 26u, 116u, 46u + i * 26u, 124u, 0x800000FFu + i * 0x001D1D00u);
    const auto packet = data;
    put64(data, commands.size() | (1ull << 15) | (1ull << 60));
    put64(data + 8u, 0xEu); data += 16u;
    for (const auto& [lo, hi] : commands) { put64(data, lo); put64(data + 8u, hi); data += 16u; }
    a.la(t0, kResetVector + packet); a.la(t1, 0xB0006000u);
    a.addiu(t2, zero, static_cast<std::uint32_t>(commands.size() + 1u));
    a.label("packet");
    a.lq(t3, 0, t0); a.sq(t3, 0, t1); a.addiu(t0, t0, 16u);
    a.addiu(t2, t2, 0xFFFFu); a.bne(t2, zero, "packet"); a.nop();
    // A small dynamic RAM function proves that the hybrid run uses both engines.
    a.la(t0, 0x80002000u);
    a.la(t1, 0x240F0001u); a.sw(t1, 0, t0); // ADDIU t7,zero,1
    a.la(t1, 0x03E00008u); a.sw(t1, 4, t0); // JR ra
    a.sw(zero, 8, t0); a.jalr(t0); a.nop();
    a.addiu(t4, zero, 15000u);
    a.label("wait"); a.addiu(t4, t4, 0xFFFFu); a.bne(t4, zero, "wait"); a.nop();
    const auto stop = a.here(); a.brk();
    const auto code = le_words(a.finish());
    if (code.size() > kResetSize) throw std::runtime_error("synthetic GS reset code exceeds RESET");
    std::fill(rom.begin(), rom.begin() + kResetSize, 0u);
    std::copy(code.begin(), code.end(), rom.begin());
    return {std::move(rom), stop};
}
} // namespace m29
