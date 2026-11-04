// Independently authored ROM0 fixture: masked GIF events, GS IRQ, real EE
// exception handler and ERET. No Sony firmware, HLE or injected host response.
#pragma once
#include "bios_program.hpp"

namespace gs_events_test {
struct Program { std::vector<std::uint8_t> rom; std::uint32_t stop; };
inline Program program() {
    using namespace m20;
    auto rom = build_firmware().rom;
    std::fill(rom.begin(), rom.begin() + kResetSize, 0u);
    Assembler a(kResetVector);
    // Read only the known GS bit of an otherwise unknown I_STAT. The CPU
    // must prove the unknown vertical bits disappear, without making them 0.
    a.la(t0, 0xB000F000u); a.lw(t1, 0, t0); a.ori(t2, zero, 1u);
    a.r(t1, t2, t1, 0, 0x24u); a.sw(t1, 0, t0);
    a.la(t0, 0xB000E020u); a.lw(t1, 0, t0); a.la(t2, 0xFFFFFC00u);
    a.la(t3, 0x80001800u); a.sw(t1, 0, t3); a.lw(t1, 0, t3); // actual stack spill/reload
    a.r(t1, t2, t1, 0, 0x24u); a.sw(t1, 0, t0); // retain unknown PCR bits outside CPC
    a.la(t0, 0xB2000010u); a.la(t1, 1u << 17); a.sd(t1, 0, t0); // SINT held
    a.la(t0, 0xB2001000u); a.ori(t1, zero, 31u); a.sw(t1, 0, t0);
    a.la(t0, 0xB000F000u); a.ori(t1, zero, 0x7FFFu); a.sw(t1, 0, t0);
    a.ori(t1, zero, 1u); a.sw(t1, 16, t0); // enable INTC GS source
    a.la(t0, 0x80001004u); a.sw(zero, 0, t0);
    a.la(t0, 0x10410401u); a.mtc0(t0, Status); // CU0, BEV, EIE, IM2, IE; no ERL
    const auto put64 = [&](unsigned at, std::uint64_t value) {
        for (unsigned b = 0; b < 8u; ++b) rom.at(at + b) = static_cast<std::uint8_t>(value >> (b * 8u));
    };
    put64(0x3000u, (1ull << 60) | (1ull << 15) | 2u); put64(0x3008u, 0xEu);
    put64(0x3010u, 0xFFFFFFFF12345678ull); put64(0x3018u, 0x60u);
    put64(0x3020u, 0u); put64(0x3028u, 0x61u);
    a.la(t0, kResetVector + 0x3000u); a.la(t1, 0xB0006000u);
    for (int offset : {0, 16, 32}) { a.lq(t2, offset, t0); a.sq(t2, 0, t1); }
    a.la(t0, 0xB2001010u); a.ori(t1, zero, 0x7C00u); a.sd(t1, 0, t0); // unmask both
    a.la(t0, 0xB2001080u); a.lw(t1, 0, t0); a.la(t0, 0x80001008u); a.sw(t1, 0, t0);
    for (unsigned i = 0; i < 8u; ++i) a.nop();
    const auto stop = a.here(); a.brk();
    const auto code = le_words(a.finish());
    if (code.size() >= 0x400u) throw std::runtime_error("GS events fixture overlaps interrupt vector");
    std::copy(code.begin(), code.end(), rom.begin());
    Assembler handler(kResetVector + 0x400u);
    handler.la(26, 0xB2001000u); handler.lbu(27, 0, 26);
    handler.la(26, 0x80001000u); handler.sw(27, 0, 26);
    handler.lw(27, 4, 26); handler.addiu(27, 27, 1u); handler.sw(27, 4, 26);
    handler.la(26, 0xB2001000u); handler.ori(27, zero, 3u); handler.sw(27, 0, 26);
    handler.la(26, 0xB000F000u); handler.ori(27, zero, 1u); handler.sw(27, 0, 26);
    handler.eret();
    const auto irq = le_words(handler.finish());
    std::copy(irq.begin(), irq.end(), rom.begin() + 0x400u);
    return {std::move(rom), stop};
}
} // namespace gs_events_test
