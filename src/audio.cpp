#include "ps2native/audio.hpp"
#include "ps2native/io.hpp"
#include <stdexcept>

namespace ps2native {
void save_spu2_wav(const std::filesystem::path& path,std::span<const Iop::Spu2Sample> samples) {
    if(file_exists(path))throw std::runtime_error("WAV output already exists");
    if(samples.size()>(128u*1024u*1024u-44u)/4u)throw std::runtime_error("WAV capture exceeds 128 MiB");
    std::vector<std::uint8_t> bytes;bytes.reserve(44u+samples.size()*4u);
    const auto word=[&](std::uint32_t value,unsigned size){for(unsigned b=0;b<size;++b)bytes.push_back(static_cast<std::uint8_t>(value>>(b*8u)));};
    const auto tag=[&](const char* value){for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<std::uint8_t>(value[i]));};
    tag("RIFF");word(36u+static_cast<std::uint32_t>(samples.size())*4u,4);tag("WAVE");tag("fmt ");word(16,4);
    word(1,2);word(2,2);word(48000,4);word(192000,4);word(4,2);word(16,2);tag("data");word(static_cast<std::uint32_t>(samples.size())*4u,4);
    for(std::size_t i=0;i<samples.size();++i){if(i && samples[i].cycle-samples[i-1].cycle!=768u)throw std::runtime_error("WAV samples are not contiguous 48 kHz clock edges");word(static_cast<std::uint16_t>(samples[i].left),2);word(static_cast<std::uint16_t>(samples[i].right),2);}
    write_file_atomically(path,bytes);
}
}
