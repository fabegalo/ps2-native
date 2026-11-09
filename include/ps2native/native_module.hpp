#pragma once
#include "ps2native/aot_runtime.hpp"

namespace ps2native {
// A module owns translated code only. ExecutionContext always belongs to the host.
struct NativeModuleDescriptor {
    std::uint32_t version;
    std::uint32_t size;
    const char* runtime_abi;
    const AotModule* module;
};
const char* native_runtime_abi() noexcept;
}
