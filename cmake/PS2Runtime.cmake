# The SDK class owns the existing console components in its native backend.
# The upstream standalone runtime, IOP and VU interpreters are not linked here.
add_library(ps2n_sdk_runtime STATIC src/ps2recomp_runtime.cpp)
target_include_directories(ps2n_sdk_runtime PUBLIC "${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xRuntime/include")
target_compile_definitions(ps2n_sdk_runtime PUBLIC PS2N_SDK_NATIVE_BACKEND=1)
target_link_libraries(ps2n_sdk_runtime PUBLIC ps2n_core_native)
add_dependencies(ps2n_sdk_runtime ps2n-verify-sdk)
