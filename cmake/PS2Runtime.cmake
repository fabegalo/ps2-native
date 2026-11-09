# The SDK class owns the existing console components in its native backend.
# The upstream standalone runtime, IOP and VU interpreters are not linked here.
add_library(ps2n_sdk_runtime INTERFACE)
target_sources(ps2n_core PRIVATE src/ps2recomp_runtime.cpp)
target_include_directories(ps2n_sdk_runtime INTERFACE "${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xRuntime/include")
target_compile_definitions(ps2n_sdk_runtime INTERFACE PS2N_SDK_NATIVE_BACKEND=1)
target_compile_definitions(ps2n_core PUBLIC PS2N_SDK_NATIVE_BACKEND=1)
target_link_libraries(ps2n_sdk_runtime INTERFACE ps2n_core_native)
add_dependencies(ps2n_core ps2n-verify-sdk)

set(_sdk_kernel "${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xRuntime/src/lib/Kernel")
set(_sdk_hle_sources "${_sdk_kernel}/EeScheduler.cpp")
foreach(name IN ITEMS Dispatcher Thread Sync System Interrupt Deci2)
    list(APPEND _sdk_hle_sources "${_sdk_kernel}/Syscalls/${name}.cpp")
endforeach()
list(APPEND _sdk_hle_sources "${_sdk_kernel}/Stubs/SIF.cpp" "${_sdk_kernel}/Stubs/MemoryCard.cpp" "${_sdk_kernel}/Stubs/Pad.cpp")
set_source_files_properties(${_sdk_hle_sources} src/sdk_hle.cpp PROPERTIES
    COMPILE_DEFINITIONS "PS2N_SDK_HLE=1"
    INCLUDE_DIRECTORIES "${_sdk_kernel}/Syscalls;${CMAKE_CURRENT_SOURCE_DIR}/src")
target_sources(ps2n_core PRIVATE ${_sdk_hle_sources} src/sdk/sif_transport.cpp src/sdk/ee_float.cpp)
target_include_directories(ps2n_core PRIVATE
    "${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xRuntime/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Reuse the upstream IRX loader/relocator against canonical IOP RAM.
# Its independent CPU, RAM, device model and interpreter are not linked.
set(_iop_loader "${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xIOP/src/emulator/services/iop_module_loader.cpp")
set(_iop_kernel "${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xIOP/src/emulator/core/iop_kernel.cpp")
set(_iop_cdvd "${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xIOP/src/emulator/imports/iop_cdvd.cpp")
set_source_files_properties("${_iop_loader}" "${_iop_kernel}" "${_iop_cdvd}" PROPERTIES COMPILE_DEFINITIONS PS2N_SDK_NATIVE_BACKEND=1)
target_sources(ps2n_core PRIVATE "${_iop_loader}" "${_iop_kernel}" "${_iop_cdvd}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/iop_services.cpp"
    "${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xIOP/src/ps2_path.cpp"
    "${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xIOP/src/modules/mcserv.cpp")

set(_iop_native_services "${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xIOP/src/emulator")
target_sources(ps2n_core PRIVATE "${_iop_native_services}/iop_emulator.cpp"
    "${_iop_native_services}/core/iop_memory.cpp" "${_iop_native_services}/services/iop_rpc.cpp")
foreach(name IN ITEMS heaplib imports intrman ioman loadcore stdio sysclib sysmem timrman vblank)
    target_sources(ps2n_core PRIVATE "${_iop_native_services}/imports/iop_${name}.cpp")
endforeach()
target_include_directories(ps2n_core PRIVATE
    "${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xIOP/include"
    "${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xIOP/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk")
add_dependencies(ps2n_core ps2n-verify-sdk)

# The SDK emits the platform's existing native semantics for its decoded code.
# Generated execution shares the same guarded AOT dispatch and console state.
target_sources(ps2_recomp_lib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools/ps2recomp_codegen.cpp")
target_include_directories(ps2_recomp_lib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")
target_compile_definitions(ps2_recomp_lib PRIVATE PS2N_PS2RECOMP_NATIVE_CODEGEN=1)
target_link_libraries(ps2_recomp_lib PRIVATE ps2n_core)

set(native_codegen_contract "ps2runtime-native-v1\n")
foreach(input IN ITEMS tools/ps2recomp_codegen.cpp tools/ps2recomp_codegen.hpp tools/ps2recomp_prepare.cpp
        src/aot.cpp src/mips.cpp src/elf.cpp include/ps2native/aot.hpp include/ps2native/mips.hpp)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${input}" content)
    string(REPLACE "\r\n" "\n" content "${content}")
    string(APPEND native_codegen_contract "${input}\n${content}\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${input}")
endforeach()
string(SHA256 PS2N_PS2RECOMP_BACKEND_SHA256 "${native_codegen_contract}")

# Accept the same preparation receipt in existing ELF/session AOT slots.
function(ps2n_target_sdk_source target source)
    get_filename_component(receipt_path "${source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(parent "${receipt_path}" DIRECTORY)
    file(READ "${receipt_path}" receipt)
    string(JSON schema GET "${receipt}" schema)
    string(JSON backend GET "${receipt}" native_backend)
    string(JSON expected_lock GET "${receipt}" sdk_lock_sha256)
    string(JSON expected_backend GET "${receipt}" native_backend_sha256)
    set(actual_lock "${PS2N_PS2RECOMP_CODEGEN_LOCK_SHA256}")
    if(NOT schema STREQUAL "ps2n-ps2recomp-prepare-v1" OR NOT backend STREQUAL "ps2runtime-native-v1"
            OR NOT expected_lock STREQUAL actual_lock OR NOT expected_backend STREQUAL PS2N_PS2RECOMP_BACKEND_SHA256)
        message(FATAL_ERROR "SDK preparation/backend identity differs: ${receipt_path}")
    endif()
    string(JSON count LENGTH "${receipt}" files)
    if(count LESS 4)
        message(FATAL_ERROR "SDK preparation is incomplete: ${receipt_path}")
    endif()
    math(EXPR last "${count}-1")
    foreach(index RANGE ${last})
        string(JSON name MEMBER "${receipt}" files ${index})
        if(IS_ABSOLUTE "${name}" OR name MATCHES "(^|/)[.][.](/|$)" OR name MATCHES "\\\\")
            message(FATAL_ERROR "SDK receipt path escapes its preparation directory")
        endif()
        string(JSON expected GET "${receipt}" files "${name}")
        file(SHA256 "${parent}/${name}" actual)
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "SDK prepared output changed: ${name}")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${parent}/${name}")
    endforeach()
    foreach(name IN ITEMS ps2_recompiled_functions.cpp register_functions.cpp ps2_recompiled_functions.h ps2_recompiled_stubs.h)
        string(JSON expected GET "${receipt}" files "generated/${name}")
    endforeach()
    ps2n_target_aot_source(${target} "${parent}/generated/ps2_recompiled_functions.cpp")
    get_property(reference TARGET ${target} PROPERTY PS2N_AOT_SOURCE_REFERENCE)
    get_filename_component(headers "${reference}" DIRECTORY)
    _ps2n_target_aot_file(${target} "${parent}/generated/register_functions.cpp")
    target_include_directories(${target} PRIVATE "${headers}")
    target_compile_definitions(${target} PRIVATE sdk_aot_module=aot_module)
    target_link_libraries(${target} PRIVATE ps2n_sdk_runtime)
    add_dependencies(${target} ps2n-verify-sdk)
    set_property(TARGET ${target} PROPERTY PS2N_AOT_SOURCE_REFERENCE "${receipt_path}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${receipt_path}")
endfunction()
