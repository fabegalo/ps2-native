// Compare compiled VU1 pairs with the PCSX2 interpreter, including live pipelines.
#include "pcsx2_vu1_aot_step.h"
#include "pcsx2_vu1_bridge.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

extern void vu1Exec(VURegs*);

namespace {
alignas(16) std::array<u8, 0x4000> micro{}, memory{};
constexpr u32 nop = 0x8000033cu;
unsigned nativeCalls = 0;

template <u32 Immediate>
u32 block(VURegs* vu, u64 end) {
    ++nativeCalls;
    if (!pcsx2vu1_aot_ops::pair<0, 0x800002ffu, Immediate>(vu, end)) return 1;
    if (!pcsx2vu1_aot_ops::pair<8, 0x400002ffu, nop>(vu, end)) return 2;
    pcsx2vu1_aot_ops::pair<16, 0x000002ffu, nop>(vu, end);
    return 3;
}

void invalidation() {
#ifdef _WIN32
    _putenv_s("PS2N_VU1_AOT", "1");
    _putenv_s("PS2N_VU1_AOT_VERIFY", "1");
#else
    setenv("PS2N_VU1_AOT", "1", 1);
    setenv("PS2N_VU1_AOT_VERIFY", "1", 1);
#endif
    u32 words[]{0x3f800000u, 0x800002ffu, nop, 0x400002ffu, nop, 0x000002ffu};
    std::memcpy(micro.data(), words, sizeof words);
    const pcsx2vu1::aot::Block first{
        pcsx2vu1::aot::blockKey(0, micro.data(), sizeof words), 0, 3, &block<0x3f800000u>};
    pcsx2vu1::aot::registerBlocks(&first, 1);
    auto run = [&](u64 generation, u32 value, unsigned calls) {
        const auto result = pcsx2vu1::run(micro.data(), memory.data(), 0, 0, 0,
            false, false, 100, [](const u8*, u32) {}, generation);
        if (!result.finished || VU1.VI[REG_I].UL != value || nativeCalls != calls)
            throw std::runtime_error("VU1 block cache reused stale microcode");
    };
    run(1, words[0], 1);
    words[0] = 0x40000000u;
    std::memcpy(micro.data(), words, sizeof words);
    run(2, words[0], 1); // New bytes have no translation: the identified interpreter path must run.
    const pcsx2vu1::aot::Block second{
        pcsx2vu1::aot::blockKey(0, micro.data(), sizeof words), 0, 3, &block<0x40000000u>};
    pcsx2vu1::aot::registerBlocks(&second, 1);
    run(2, words[0], 2); // Registration also invalidates a cached miss.
    words[0] = 0x3f800000u;
    std::memcpy(micro.data(), words, sizeof words);
    run(3, words[0], 3);
}

template <u32 Upper, u32 Lower = nop>
void compare(u64 end = ~0ull) {
    VU1.VI[REG_TPC].UL = 0;
    const u32 words[]{Lower, Upper};
    std::memcpy(micro.data(), words, sizeof words);
    std::array<VURegs, 2> before, expected;
    std::memcpy(before.data(), vuRegs, sizeof vuRegs);
    const auto dataBefore = memory;
    const auto vifBefore = vif1Regs;
    vu1Exec(&VU1);
    std::memcpy(expected.data(), vuRegs, sizeof vuRegs);
    const auto dataExpected = memory;
    const auto vifExpected = vif1Regs;
    const bool continues = VU1.VI[REG_TPC].UL == 8 &&
        (VU0.VI[REG_VPU_STAT].UL & 0x100u) && VU1.cycle < end;
    std::memcpy(vuRegs, before.data(), sizeof vuRegs);
    memory = dataBefore;
    vif1Regs = vifBefore;
    const bool actual = pcsx2vu1_aot_ops::pair<0, Upper, Lower>(&VU1, end);
    if (std::memcmp(vuRegs, expected.data(), sizeof vuRegs) || memory != dataExpected ||
        std::memcmp(&vif1Regs, &vifExpected, sizeof vif1Regs) || actual != continues) {
        const auto* a = reinterpret_cast<const unsigned char*>(vuRegs);
        const auto* b = reinterpret_cast<const unsigned char*>(expected.data());
        size_t at = 0;
        while (at < sizeof vuRegs && a[at] == b[at]) ++at;
        std::cerr << "upper=0x" << std::hex << Upper << " lower=0x" << Lower << std::dec
                  << " first byte=" << at << " fmac offset=" << offsetof(VURegs, fmac)
                  << " stride=" << sizeof(VURegs) << '\n';
        throw std::runtime_error("Native VU1 state differs from the interpreter");
    }
}
}

int main() {
    try {
        std::fesetround(FE_TOWARDZERO);
        std::memset(vuRegs, 0, sizeof vuRegs);
        VU1.Micro = micro.data();
        VU1.Mem = memory.data();
        VU1.idx = 1;
        VU0.VI[REG_VPU_STAT].UL = 0x100;
        VU1.VF[0].f.w = 1.0f;
        for (unsigned r = 1; r < 32; ++r)
            for (unsigned lane = 0; lane < 4; ++lane)
                VU1.VF[r].F[lane] = float(int(r * 4 + lane) - 50) * 0.125f;
        constexpr u32 edges[]{0, 0x80000000u, 1, 0x007fffffu, 0x00800000u,
            0x3f800000u, 0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0x7fc00001u};
        for (unsigned i = 0; i < 256; ++i) {
            for (unsigned lane = 0; lane < 4; ++lane) {
                VU1.VF[1].UL[lane] = edges[(i + lane) % std::size(edges)];
                VU1.VF[2].UL[lane] = edges[(i / std::size(edges) + lane) % std::size(edges)];
            }
            compare<0x01e208e8u>(); // ADD vf3, vf1, vf2
            compare<0x01e310eau>(); // MUL vf3, vf2, vf3: RAW dependency
            compare<0x01e208e8u, 0x81e3133cu>(); // Lower write to the same VF is discarded.
            compare<0x01e208e8u, 0x81e41b3cu>(); // Lower reads the previous value of upper's destination.
            compare<0x01e310e8u, 0x8000033du>(); // MR32 lower special table
            compare<0x01c20afeu>(); // OPMULA writes ACC.
            compare<0x01c208eeu>(); // OPMSUB consumes ACC.
            compare<0x01c209ffu>(); // CLIP flags
            compare<0x000002ffu, 0x80000c3eu>(); // RINIT
            compare<0x000002ffu, 0x81e4043cu>(); // RNEXT
            compare<0x000002ffu, 0x80020bbcu>(); // DIV
            compare<0x000002ffu, 0x800003bfu>(); // WAITQ
            compare<0x01e208dcu>(); // MULq
            compare<0x000002ffu, 0x80020bbeu>(); // RSQRT
            compare<0x000002ffu, 0x80020f7eu>(); // ESUM
            compare<0x000002ffu, 0x800007bfu>(); // WAITP
            compare<0x81e208e8u, 0x3f800000u>(); // I bit
            compare<0x000002ffu, 0x40000001u>(); // branch and delay
            compare<0x000002ffu>();
            compare<0x000002ffu>(VU1.cycle + 1); // exact budget boundary
        }
        compare<0x400002ffu>(); // E and its delay flush
        compare<0x000002ffu>();
        invalidation();
        std::cout << "VU1 native pair equivalence passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
