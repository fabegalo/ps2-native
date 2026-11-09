#include "ps2native/native_module.hpp"
#include "ps2native/native_module_abi.hpp"
namespace ps2native {
const char* native_runtime_abi() noexcept { return PS2N_HOST_NATIVE_ABI; }
}
