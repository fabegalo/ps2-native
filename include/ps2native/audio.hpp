#pragma once
#include "ps2native/iop.hpp"
#include <filesystem>

namespace ps2native {
// Per-core 48 kHz stereo PCM from the runtime sink, without host resampling.
// Existing files are protected. Gaps/duplicate clock edges are refused.
void save_spu2_wav(const std::filesystem::path& path,std::span<const Iop::Spu2Sample> samples);
}
