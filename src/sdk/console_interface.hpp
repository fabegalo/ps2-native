// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ps2native/console.hpp"
#include "ps2native/nvm.hpp"
#include "ps2native/io.hpp"
#include <memory>

namespace ps2native {
class Memory;
void configure_hle_console(Memory&,const ConsoleHardware*,std::shared_ptr<const NvmImage>);
void configure_hle_spu2_quiescent_reset(Memory&);
struct HleConsoleInput {
    const ConsoleHardware* hardware{};
    std::shared_ptr<const NvmImage> nvm;
};
inline HleConsoleInput load_hle_console(std::string_view profile,const std::filesystem::path& nvm,const std::string& hash) {
    HleConsoleInput input;
    if(!profile.empty()) {
        input.hardware=find_console_hardware(profile);
        if(!input.hardware)throw std::runtime_error("Unknown or ambiguous console profile");
    }
    if(!nvm.empty()) {
        if(!input.hardware || hash.empty())throw std::runtime_error("NVM requires an identified console profile and SHA-256");
        input.nvm=std::make_shared<const NvmImage>(load_nvm_image(read_file(nvm),*input.hardware,NvmLoadOptions{hash}));
    } else if(!hash.empty())throw std::runtime_error("NVM hash requires its original image path");
    return input;
}
}
