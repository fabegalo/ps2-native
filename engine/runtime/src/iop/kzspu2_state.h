// What the engine needs from PCSX2's SPU2 beyond the kzspu2 API: its save state (PCSX2's own SPU2Savestate) and the stop of a
// running SPU2 DMA channel. kzspu2_state.cpp is compiled with the kzspu2 sources (their flags and PCSX2 headers).
// SPDX-License-Identifier: GPL-3.0+
#pragma once
#include <cstddef>
#include <cstdint>

std::size_t kzspu2StateSize();
// The SPU2 (registers, sound memory, both cores, DMA progress) into `out`, kzspu2StateSize() bytes.
void kzspu2SaveState(std::uint8_t *out);
// Back from a buffer kzspu2SaveState() wrote. False when it is not a state of this SPU2 version.
bool kzspu2LoadState(const std::uint8_t *in, std::size_t size);
// The channel of `core` (0 = IOP DMA 4, 1 = 7) was stopped by the game: as in PCSX2, its end no longer raises a DMA interrupt.
void kzspu2StopDma(int core);
