# One SDK checkout and one incremental build; never fetch another runtime.
set(PS2N_LOCK_FILE "${CMAKE_CURRENT_LIST_DIR}/../deps.lock.json")
set(_sdk_default "${CMAKE_CURRENT_SOURCE_DIR}/out/ps2recomp-reference")
set(PS2N_PS2RECOMP_SOURCE_DIR "${_sdk_default}" CACHE PATH "Checkout of the PS2Recomp revision in deps.lock.json, with listed patches applied")
include("${CMAKE_CURRENT_LIST_DIR}/VerifyPS2Recomp.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PS2N_LOCK_FILE}")

include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(BUILD_DWARFDUMP OFF)
set(BUILD_DWARFEXAMPLE OFF)
set(BUILD_DWARFGEN OFF)
set(BUILD_SHARED OFF)
set(BUILD_NON_SHARED ON)
# First declaration wins over the upstream tags; verify each tag's resolved
# commit before its CMake code is executed. Shallow clones bound disk usage.
file(READ "${PS2N_LOCK_FILE}" _lock)

# Runtime-only SDK patches change discovery/runtime identity, not emitted game code.
string(JSON _codegen_lock REMOVE "${_lock}" ps2recomp_sdk patches)
string(JSON _patch_count LENGTH "${_lock}" ps2recomp_sdk patches)
if(_patch_count GREATER 0)
    math(EXPR _patch_last "${_patch_count}-1")
    foreach(_patch_index RANGE ${_patch_last})
        string(JSON _patch GET "${_lock}" ps2recomp_sdk patches ${_patch_index})
        string(JSON _patch_files GET "${_patch}" files)
        if(_patch_files MATCHES "ps2xRecomp/|ps2xAnalyzer/")
            string(APPEND _codegen_lock "\n${_patch}")
        endif()
    endforeach()
endif()
string(SHA256 PS2N_PS2RECOMP_CODEGEN_LOCK_SHA256 "${_codegen_lock}")

foreach(name IN ITEMS elfio toml11 fmt libdwarf rabbitizer nlohmann_json)
    string(JSON repo GET "${_lock}" dependencies ${name} repository)
    string(JSON tag GET "${_lock}" dependencies ${name} tag)
    string(JSON expected GET "${_lock}" dependencies ${name} commit)
    FetchContent_Declare(${name} GIT_REPOSITORY "https://github.com/${repo}.git"
        GIT_TAG "${tag}" GIT_SHALLOW TRUE GIT_PROGRESS FALSE
        SOURCE_SUBDIR ps2n-verify-before-configure)
    FetchContent_GetProperties(${name})
    if(NOT ${name}_POPULATED)
        FetchContent_MakeAvailable(${name})
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${${name}_SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE actual OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE result)
    if(NOT result EQUAL 0 OR NOT actual STREQUAL expected)
        message(FATAL_ERROR "PS2Recomp dependency ${name}: expected ${expected}, got ${actual}")
    endif()
    list(APPEND _sdk_verify_args "-DPS2N_SDK_${name}_DIR=${${name}_SOURCE_DIR}")
    if(EXISTS "${${name}_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${${name}_SOURCE_DIR}" "${${name}_BINARY_DIR}" EXCLUDE_FROM_ALL)
    endif()
endforeach()
add_custom_target(ps2n-verify-sdk
    COMMAND "${CMAKE_COMMAND}" "-DPS2N_LOCK_FILE=${PS2N_LOCK_FILE}"
        "-DPS2N_PS2RECOMP_SOURCE_DIR=${PS2N_PS2RECOMP_SOURCE_DIR}" ${_sdk_verify_args}
        -P "${CMAKE_CURRENT_LIST_DIR}/VerifyPS2Recomp.cmake" VERBATIM)

# Preserve the host's floating-point/security/ISA policy; do not apply the
# SDK's global AVX2, fast-math or disabled security checks.
set(PS2N_PS2RECOMP_INTEGRATION ON)
enable_language(C)
add_subdirectory("${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xRecomp" ps2recomp/recomp EXCLUDE_FROM_ALL)
add_subdirectory("${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xAnalyzer" ps2recomp/analyzer EXCLUDE_FROM_ALL)
add_dependencies(ps2_recomp_lib ps2n-verify-sdk)
add_dependencies(ps2_analyzer_lib ps2n-verify-sdk)
