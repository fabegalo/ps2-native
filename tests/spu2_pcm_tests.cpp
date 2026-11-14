// Bit-exact SPU2 mix: ADPCM decoding through all five filters, Gaussian interpolation at
// non-unit pitch, exponential ADSR, reverb and both cores. The PCM hash must match on every
// platform and vector ISA (OVERRIDE_VECTOR_ISA selects SSE4/AVX/AVX2 on MSVC x64).
#include "kzspu2.h"
#include "ps2native/sha256.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
constexpr uint32_t kSpu2 = 0xBF900000u;
constexpr uint64_t kCyclesPerSample = 768;
uint64_t cycle = 1000;
std::vector<int16_t> pcm;
uint32_t dmaDone = 0;

void w16(uint32_t reg, uint16_t v) { kzspu2Write16(kSpu2 + reg, v, cycle); }
void w32(uint32_t reg, uint32_t v) { w16(reg, uint16_t(v >> 16)); w16(reg + 2, uint16_t(v)); }
void advance(uint64_t samples) {
    for (uint64_t i = 0; i < samples; i += 64) {
        cycle += 64 * kCyclesPerSample;
        kzspu2Advance(cycle);
        int16_t drain[4096];
        while (kzspu2ReadSamples(drain, 2048)) {}
    }
}
double rms(size_t from, size_t to) {
    double sum = 0;
    for (size_t i = from * 2; i < to * 2; ++i) sum += double(pcm[i]) * pcm[i];
    return std::sqrt(sum / double((to - from) * 2));
}
} // namespace

int main() {
    KzSpu2Config config;
    config.samples = [](void*, const int16_t* s, uint32_t frames) { pcm.insert(pcm.end(), s, s + frames * 2); };
    config.dmaComplete = [](void*, int) { ++dmaDone; };
    if (!kzspu2Init(config)) return 1;

    // Eight looping ADPCM blocks: filter b % 5, varied shifts, LCG nibbles.
    std::vector<uint16_t> adpcm(8 * 8);
    uint32_t seed = 12345;
    for (int b = 0; b < 8; ++b) {
        auto* block = reinterpret_cast<uint8_t*>(adpcm.data() + b * 8);
        block[0] = uint8_t(((b % 5) << 4) | (2 + (b * 3) % 9));
        block[1] = b == 0 ? 6 : b == 7 ? 3 : 2;
        for (int i = 2; i < 16; ++i) block[i] = uint8_t((seed = seed * 1103515245u + 12345u) >> 24);
    }
    w16(0x19A, 0x20);  // ATTR: DMA write
    w32(0x1A8, 0x5000);
    kzspu2DmaWrite(0, adpcm.data(), uint32_t(adpcm.size()), 0x100000, cycle);
    for (int i = 0; i < 200 && !dmaDone; ++i) advance(64);
    if (!dmaDone) { std::puts("FAIL: DMA did not complete"); return 1; }

    w16(0x19A, 0x80);  // ATTR: effects on
    w16(0x198, 0xFF0); w16(0x598, 0xFFC);  // MMIX as sceSdInit
    w16(0x760, 0x3FFF); w16(0x762, 0x3FFF); w16(0x788, 0x3FFF); w16(0x78A, 0x3FFF);
    w16(0x764, 0x3000); w16(0x766, 0x3000);  // EVOL
    w32(0x2E0, 0xE0000); w16(0x33C, 0xF);   // reverb work area
    const uint32_t offsets[] = {0x80, 0x60, 0x1800, 0x1700, 0x1500, 0x1400, 0x1300, 0x1200, 0x1100, 0x1000,
                                0x1680, 0x1580, 0x1480, 0x1380, 0x1280, 0x1180, 0x1080, 0x0F80, 0x0E80, 0x0D80, 0x0C80, 0x0B80};
    for (int i = 0; i < 22; ++i) w32(0x2E4 + i * 4, offsets[i]);
    const uint16_t volumes[] = {0x7000, 0x5000, 0xB000, 0x4000, 0xC000, 0x9000, 0x6000, 0xA000, 0x7FFF, 0x7FFF};
    for (int i = 0; i < 10; ++i) w16(0x774 + i * 2, volumes[i]);

    const struct { int core, voice; uint16_t pitch; } voices[] = {{0, 0, 0x0C00}, {0, 3, 0x1555}, {1, 2, 0x0800}};
    for (const auto& v : voices) {
        const uint32_t c = v.core * 0x400u, r = c + v.voice * 16;
        w16(r, 0x3000); w16(r + 2, 0x2000); w16(r + 4, v.pitch);
        w16(r + 6, 0xB088);  // ADSR1: exponential attack, decay, sustain level
        w16(r + 8, 0xD010);  // ADSR2: exponential sustain decrease, linear release
        w32(c + 0x1C0 + v.voice * 12, 0x5000);
    }
    w16(0x188, 1 | 8); w16(0x190, 1 | 8);    // core 0 dry
    w16(0x18C, 1 | 8); w16(0x194, 1 | 8);    // core 0 wet
    w16(0x400 + 0x188, 4); w16(0x400 + 0x190, 4);
    const size_t start = pcm.size() / 2;
    w16(0x1A0, 1 | 8); w16(0x400 + 0x1A0, 4);
    advance(24000);
    w16(0x1A4, 1 | 8); w16(0x400 + 0x1A4, 4);
    advance(14400);
    kzspu2Shutdown();

    const size_t end = pcm.size() / 2;
    const double on = rms(start + 2400, start + 24000), tail = rms(end - 4800, end);
    const auto hash = ps2native::sha256_hex({reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * 2});
    if (const char* out = std::getenv("PS2N_SPU2_PCM_OUT"))  // raw S16 stereo, for listening and diffing
        if (FILE* f = std::fopen(out, "wb")) { std::fwrite(pcm.data(), 2, pcm.size(), f); std::fclose(f); }
    std::printf("frames=%zu rms_on=%.1f rms_tail=%.1f sha256=%s\n", end, on, tail, hash.c_str());
    if (on < 500) { std::puts("FAIL: voices silent"); return 1; }
    constexpr const char* kExpected = "ac5db4520c88b13ae29a8f20a7cb5cce0e50c06a8916c603430f3e1725784818";
    if (hash != kExpected) {
        // Locate the first 1200-frame block that differs and show it, to diagnose the divergent path.
        static const uint64_t kBlocks[] = {
            0x7dbf3899dfcbe0beull, 0x296099c117326f10ull, 0x80dda82e575a46d4ull, 0x9229035f86791c21ull, 0xe547101d87d1c97dull, 0x5f7a744ed2134c6dull, 0xc8f8b81669fd2eefull, 0x5b6ec171f2b84599ull, 0x27c9dd826fa9c97aull, 0xf947734be6da8c88ull, 0xed7a953220f222d2ull, 0xcc0a66d0e473b1a4ull, 0xd8f455df4dbfeab2ull, 0x681719d0facded39ull, 0xa1387c4fcf4a4b7dull, 0x3a1c7c9cbccb0bd8ull, 0xec8e7c042f4e0601ull, 0xe147471e88c4ba77ull, 0x7df53e5a2e08a988ull, 0xb1858d2ea4f2b33bull, 0xd336c4ecc29482c3ull, 0x5d0de6b30ce1953dull, 0xaf3b93e43f58e486ull, 0x2e00b20ba42cdb2bull, 0x7a31c02c858965ffull, 0xa1063ae79e9d164cull, 0x885151abaa4ec097ull, 0x848d51688fd07648ull, 0x782e582366bdd4dull, 0xe1c9ce1e4c2f895full, 0x6bea631f8940284aull, 0x9949b3bbce880773ull, 0xcd1962cf6b2f0b7full};
        for (size_t b = 0; b * 1200 < end; ++b) {
            uint64_t h = 14695981039346656037ull;
            for (size_t i = b * 2400; i < std::min(pcm.size(), b * 2400 + 2400); ++i) h = (h ^ uint16_t(pcm[i])) * 1099511628211ull;
            if (b < sizeof kBlocks / sizeof *kBlocks ? h == kBlocks[b] : false) continue;
            std::printf("first divergent block %zu (frame %zu) hash=%llx:", b, b * 1200, (unsigned long long)h);
            for (size_t i = b * 2400; i < std::min(pcm.size(), b * 2400 + 2400); ++i) std::printf(" %d", pcm[i]);
            std::puts("");
            break;
        }
        std::puts("FAIL: PCM differs from the reference mix");
        return 1;
    }
    std::puts("SPU2 PCM regression passed");
    return 0;
}
