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
    a.la(t0,0x50400004u); a.mtc0(t0,Status); // CU2, no pending VU operations
    a.ori(t1,zero,0xA0Au);
    a.raw((0x12u<<26)|(6u<<21)|(t1<<16)|(28u<<11)|1u); // CTC2.I FBRST
    a.raw((0x12u<<26)|(2u<<21)|(t1<<16)|(28u<<11)); // CFC2.NI FBRST
    a.la(t0,0x8000100Cu); a.sw(t1,0,t0);
    a.ori(t1,zero,0x421u); a.raw((0x12u<<26)|(6u<<21)|(t1<<16)|(2u<<11));
    a.ori(t1,zero,0x1234u); a.raw((0x12u<<26)|(6u<<21)|(t1<<16)|(3u<<11));
    a.raw(0x4B0003FFu|(3u<<16)|(2u<<11)); // VISWR.x VI03,(VI02)x
    a.raw(0x48240800u); // QMFC2 a0,VF01 (undefined power-on lanes)
    a.la(t0,0x80002000u); a.sq(4,0,t0); a.lq(5,0,t0);
    a.raw(0x48A51000u); // QMTC2 a1,VF02 preserves unknown bits
    a.raw(0x4BE000ECu); // VSUB.xyzw VF03,VF00,VF00, exact zero
    a.ori(t1,zero,0x402u); a.raw((0x12u<<26)|(6u<<21)|(t1<<16)|(2u<<11));
    a.raw(0x4BE21B7Du); // VSQI.xyzw VF03,(VI02++) -> VU1 VF02
    a.raw(0x4A000030u|(2u<<16)|(2u<<11)|(4u<<6)); // VIADD VI04,VI02,VI02
    a.raw(0x48C08000u); // CTC2 zero,STATUS: clear sticky flags, retain current Z
    a.ori(t1,zero,0x1234u); a.raw((0x12u<<26)|(6u<<21)|(t1<<16)|(18u<<11));
    a.la(t1,0xABCDEF21u);
    for(const auto index : {20u,21u,22u,27u}) a.raw((0x12u<<26)|(6u<<21)|(t1<<16)|(index<<11));
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
    put64(0x3040u,0x2000000001000101ull); put64(0x3048u,0x30000000FFFFFFFFull);
    put64(0x3050u,0x5566778811223344ull); put64(0x3058u,0xDDEEFF0099AABBCCull);
    put64(0x3080u,0x040003FF01000402ull); put64(0x3088u,0x0500000207004321ull);
    a.la(t0,0xB0003820u); a.ori(t1,zero,2); a.sw(t1,0,t0);
    a.la(t0,kResetVector+0x3080u); a.lq(t2,0,t0); a.la(t1,0xB0004000u); a.sq(t2,0,t1);
    a.la(t0,0xB0003C10u); a.ori(t1,zero,1); a.sw(t1,0,t0);
    a.la(t0,kResetVector+0x3040u); a.la(t1,0xB0005000u);
    for(int offset : {0,16}) {a.lq(t2,offset,t0);a.sq(t2,0,t1);}
    a.la(t0,0xB0003D30u);a.lw(t1,0,t0);a.la(t0,0x80001010u);a.sw(t1,0,t0);
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
