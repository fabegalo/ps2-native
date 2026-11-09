#include "ps2native/native_module.hpp"
#ifdef _WIN32
#define PS2N_MODULE_EXPORT __declspec(dllexport)
#else
#define PS2N_MODULE_EXPORT __attribute__((visibility("default")))
#endif
#ifdef PS2N_IOP_LOADABLE_MODULE
#include "sdk/iop_module_interface.hpp"
extern "C" const ps2native::IopAotModule* ps2n_iop_aot_v1();
extern "C" PS2N_MODULE_EXPORT const ps2native::NativeIopModuleDescriptor* ps2n_native_iop_module_v1() {
    static const ps2native::NativeIopModuleDescriptor descriptor{
        1,sizeof(ps2native::NativeIopModuleDescriptor),PS2N_NATIVE_ABI,ps2n_iop_aot_v1()};
    return &descriptor;
}
#else
extern "C" PS2N_MODULE_EXPORT const ps2native::NativeModuleDescriptor* ps2n_native_module_v1() {
    static const ps2native::NativeModuleDescriptor descriptor{
        1,sizeof(ps2native::NativeModuleDescriptor),PS2N_NATIVE_ABI,&ps2native::aot_module()};
    return &descriptor;
}

#endif
