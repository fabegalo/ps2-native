// See kzspu2_state.h. Built with the kzspu2 sources: PCSX2's headers, its forced prefix and its flags.
// SPDX-License-Identifier: GPL-3.0+

#include "kzspu2_state.h"

#include "IopCounters.h"
#include "IopHw.h"
#include "R3000A.h"
#include "SPU2/defs.h"
#include "SPU2/spu2.h"

#include <cstring>
#include <vector>

extern float DCFilterIn[2], DCFilterOut[2];

namespace {

constexpr std::uint32_t kMagic = 0x4B5A5332u;  // "KZS2", then the layout below

// Besides PCSX2's SPU2Savestate block: the IOP time the SPU2 runs on, its view of the DMA 4/7 registers (the busy bit decides whether
// a DMA end is reported), the SPU2's event counter and the DC filter.
struct Extra {
    std::uint32_t magic;
    std::uint32_t block_size;
    std::uint64_t cycle;
    std::uint64_t next_start;
    std::int32_t next_delta;
    psxCounter counters[NUM_COUNTERS];
    std::uint32_t dma[2][4];  // MADR, BCR, CHCR, TADR of channels 4 and 7
    float dc_in[2], dc_out[2];
};

std::uint32_t &dma_reg(int core, int reg) { return psxHu32((core ? 0x1500u : 0x10C0u) + reg * 4u); }

}  // namespace

std::size_t kzspu2StateSize() { return sizeof(Extra) + static_cast<std::size_t>(SPU2Savestate::SizeIt()); }

void kzspu2SaveState(std::uint8_t *out) {
    Extra e{};
    e.magic = kMagic;
    e.block_size = static_cast<std::uint32_t>(SPU2Savestate::SizeIt());
    e.cycle = psxRegs.cycle;
    e.next_start = psxNextStartCounter;
    e.next_delta = psxNextDeltaCounter;
    std::memcpy(e.counters, psxCounters, sizeof e.counters);
    for (int c = 0; c < 2; ++c)
        for (int r = 0; r < 4; ++r) e.dma[c][r] = dma_reg(c, r);
    std::memcpy(e.dc_in, DCFilterIn, sizeof e.dc_in);
    std::memcpy(e.dc_out, DCFilterOut, sizeof e.dc_out);
    std::memcpy(out, &e, sizeof e);
    // FreezeIt wants an aligned block (it holds V_Core); the caller's buffer need not be.
    std::vector<std::uint64_t> block((e.block_size + 7) / 8);
    SPU2Savestate::FreezeIt(*reinterpret_cast<SPU2Savestate::DataBlock *>(block.data()));
    std::memcpy(out + sizeof e, block.data(), e.block_size);
}

bool kzspu2LoadState(const std::uint8_t *in, std::size_t size) {
    Extra e;
    if (size != kzspu2StateSize()) return false;
    std::memcpy(&e, in, sizeof e);
    if (e.magic != kMagic || e.block_size != static_cast<std::uint32_t>(SPU2Savestate::SizeIt())) return false;
    std::vector<std::uint64_t> block((e.block_size + 7) / 8);
    std::memcpy(block.data(), in + sizeof e, e.block_size);
    SPU2Savestate::ThawIt(*reinterpret_cast<SPU2Savestate::DataBlock *>(block.data()));
    psxRegs.cycle = e.cycle;  // set, not advanced: the state may be from earlier than the run that loads it
    psxNextStartCounter = e.next_start;
    psxNextDeltaCounter = e.next_delta;
    std::memcpy(psxCounters, e.counters, sizeof e.counters);
    for (int c = 0; c < 2; ++c)
        for (int r = 0; r < 4; ++r) dma_reg(c, r) = e.dma[c][r];
    std::memcpy(DCFilterIn, e.dc_in, sizeof e.dc_in);
    std::memcpy(DCFilterOut, e.dc_out, sizeof e.dc_out);
    return true;
}

void kzspu2StopDma(int core) { dma_reg(core & 1, 2) &= ~0x01000000u; }
