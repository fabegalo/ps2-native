cmake_minimum_required(VERSION 3.21)
project(ps2-native VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Execution: PS2Recomp, unmodified (recompiler, analyzer, runtime, IOP).
include(cmake/PS2Recomp.cmake)
include(cmake/PCSX2Components.cmake)
add_library(ps2n_pcsx2_audio STATIC src/host/pcsx2_audio.cpp)
target_include_directories(ps2n_pcsx2_audio PUBLIC "${PROJECT_SOURCE_DIR}/src/host")
target_link_libraries(ps2n_pcsx2_audio PRIVATE kzspu2 ps2_runtime)
target_link_libraries(ps2n_pcsx2_audio PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,kzspu2_sse4,kzspu2_avx,kzspu2_avx2>")

include(CTest)
include(cmake/ParallelGS.cmake)

# ps2-native: content identification, disc access, the library and the
# preparation/supervision tool. No CPU, device or graphics emulation here.
add_library(ps2n_content STATIC
    src/io.cpp src/sha256.cpp src/elf.cpp src/packed_elf.cpp src/disc.cpp src/process.cpp
    src/bios.cpp src/console.cpp src/nvm.cpp
    src/frontend/content.cpp src/frontend/library.cpp src/frontend/runtime.cpp)
target_include_directories(ps2n_content PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(MSVC)
    target_compile_options(ps2n_content PRIVATE /W4 /permissive-)
    target_compile_options(ps2n_content PUBLIC /utf-8)
else()
    target_compile_options(ps2n_content PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(PS2N_GAMES_DIR "${CMAKE_BINARY_DIR}/games" CACHE PATH "Prepared games (extracted disc, PS2Recomp config and output)")
foreach(_var IN ITEMS INCLUDE LIB LIBPATH PATH)
    set(PS2N_ENV_${_var} "$ENV{${_var}}")
endforeach()
configure_file(cmake/templates/build_environment.hpp.in generated/ps2n_build_environment.hpp @ONLY)
add_executable(ps2n tools/ps2n.cpp)
target_include_directories(ps2n PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(ps2n PRIVATE ps2n_content)
add_dependencies(ps2n ps2recomp_tools)
target_compile_definitions(ps2n PRIVATE
    PS2N_GAMES_DIR="${PS2N_GAMES_DIR}"
    PS2N_BINARY_DIR="${CMAKE_BINARY_DIR}"
    PS2N_CMAKE_COMMAND="${CMAKE_COMMAND}"
    PS2N_RECOMP_EXE="${PS2N_RECOMP_EXE}"
    PS2N_ANALYZER_EXE="${PS2N_ANALYZER_EXE}"
    PS2N_EXE_SUFFIX="${CMAKE_EXECUTABLE_SUFFIX}")

# Each prepared game becomes the target ps2n-game-<id>.
set(PS2N_BUILD_GAME "" CACHE STRING "Add this prepared game to the build; empty configures all")
if(NOT DEFINED PS2N_CONFIGURED_GAMES)
    set(PS2N_CONFIGURED_GAMES "" CACHE INTERNAL "Game targets retained to preserve Ninja dependency records")
endif()
if(PS2N_BUILD_GAME)
    list(APPEND PS2N_CONFIGURED_GAMES "${PS2N_BUILD_GAME}")
endif()
file(GLOB _ps2n_games LIST_DIRECTORIES true CONFIGURE_DEPENDS "${PS2N_GAMES_DIR}/*")
foreach(_game IN LISTS _ps2n_games)
    get_filename_component(_id "${_game}" NAME)
    if(PS2N_BUILD_GAME AND NOT _id IN_LIST PS2N_CONFIGURED_GAMES)
        continue()
    endif()
    if(IS_DIRECTORY "${_game}/generated" AND EXISTS "${_game}/game.txt")
        ps2n_add_game(${_id} "${_game}/generated")
        list(APPEND PS2N_CONFIGURED_GAMES "${_id}")
    endif()
endforeach()
list(REMOVE_DUPLICATES PS2N_CONFIGURED_GAMES)
set(PS2N_CONFIGURED_GAMES "${PS2N_CONFIGURED_GAMES}" CACHE INTERNAL "Game targets retained to preserve Ninja dependency records" FORCE)

# Prepared EE content modules compile independently of the original entry ELF.
file(GLOB _ps2n_ee_modules LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_BINARY_DIR}/ee-aot/*/g")
foreach(_generated IN LISTS _ps2n_ee_modules)
    get_filename_component(_module "${_generated}" DIRECTORY)
    get_filename_component(_identity "${_module}" NAME)
    ps2x_add_ee_aot_module(ps2n-ee-${_identity} "${_generated}")
endforeach()

# Desktop launcher (Qt 6 Quick). AUTO builds it only when Qt is found.
set(PS2N_LAUNCHER "AUTO" CACHE STRING "Build the ps2-native desktop launcher (Qt 6): AUTO, ON or OFF")
set_property(CACHE PS2N_LAUNCHER PROPERTY STRINGS AUTO ON OFF)
if(NOT PS2N_LAUNCHER STREQUAL "OFF")
    add_subdirectory(launcher)
endif()

if(BUILD_TESTING)
    add_executable(ps2n-sdk-gs-crt-tests tests/sdk_gs_crt_tests.cpp
        "${PS2N_PS2RECOMP_DIR}/ps2xTest/src/test_function_table.cpp")
    target_link_libraries(ps2n-sdk-gs-crt-tests PRIVATE ps2_runtime)
    add_test(NAME sdk-gs-crt COMMAND ps2n-sdk-gs-crt-tests)
    add_executable(ps2n-sdk-memory-card-io-tests tests/sdk_memory_card_io_tests.cpp
        "${PS2N_PS2RECOMP_DIR}/ps2xTest/src/test_function_table.cpp")
    target_include_directories(ps2n-sdk-memory-card-io-tests PRIVATE "${PS2N_PS2RECOMP_DIR}/ps2xRuntime/src/lib")
    target_link_libraries(ps2n-sdk-memory-card-io-tests PRIVATE ps2_runtime)
    add_test(NAME sdk-memory-card-io COMMAND ps2n-sdk-memory-card-io-tests)
    add_executable(ps2n-sdk-gs-presentation-tests tests/sdk_gs_presentation_tests.cpp)
    target_link_libraries(ps2n-sdk-gs-presentation-tests PRIVATE ps2_runtime)
    add_test(NAME sdk-gs-presentation COMMAND ps2n-sdk-gs-presentation-tests)
    add_test(NAME sdk-ee-module-codegen COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/ee_module_codegen_tests.py" "${PS2N_RECOMP_EXE}"
        "${PROJECT_SOURCE_DIR}/out/game-functional" "${CMAKE_COMMAND}" "${PS2N_PS2RECOMP_DIR}")
    foreach(_var IN ITEMS INCLUDE LIB LIBPATH PATH)
        string(REPLACE ";" "\\;" _value "${PS2N_ENV_${_var}}")
        list(APPEND _ee_test_environment "${_var}=${_value}")
    endforeach()
    set_tests_properties(sdk-ee-module-codegen PROPERTIES ENVIRONMENT "${_ee_test_environment}")
    set(_ee_fixture "${CMAKE_BINARY_DIR}/generated/ee-aot-test")
    set(_ee_fixture_sources "")
    foreach(_kind IN ITEMS branch likely)
        if(_kind STREQUAL "branch")
            set(_identity "31827b80145d43613b620329b6905c0f84bdfc36c37c306366cd0abb23aa81af")
        else()
            set(_identity "5ff2957621e8b9d19861546e47cd29e747de4829bc577778a30e2a07780f2053")
        endif()
        set(_kind_sources "${_ee_fixture}/${_kind}/g/register_functions.cpp"
            "${_ee_fixture}/${_kind}/g/ee_${_identity}_sub_00200000_0x200000.cpp"
            "${_ee_fixture}/${_kind}/g/ee_${_identity}_sub_00200010_0x200010.cpp")
        set_source_files_properties(${_kind_sources} PROPERTIES
            INCLUDE_DIRECTORIES "${_ee_fixture}/${_kind}/g")
        list(APPEND _ee_fixture_sources ${_kind_sources} "${_ee_fixture}/${_kind}/fixture.h")
    endforeach()
    add_custom_command(OUTPUT ${_ee_fixture_sources}
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/ee_module_fixture.py"
            "${PS2N_RECOMP_EXE}" "${_ee_fixture}"
        DEPENDS tests/ee_module_fixture.py "${PS2N_RECOMP_EXE}"
        VERBATIM)
    add_executable(ps2n-sdk-ee-module-tests tests/sdk_ee_module_tests.cpp ${_ee_fixture_sources}
        "${PS2N_PS2RECOMP_DIR}/ps2xTest/src/test_function_table.cpp")
    add_dependencies(ps2n-sdk-ee-module-tests ps2recomp_tools)
    target_include_directories(ps2n-sdk-ee-module-tests PRIVATE "${_ee_fixture}")
    target_link_libraries(ps2n-sdk-ee-module-tests PRIVATE ps2_runtime)
    add_test(NAME sdk-ee-module COMMAND ps2n-sdk-ee-module-tests)
    add_executable(ps2n-packed-elf-tests tests/packed_elf_tests.cpp)
    target_link_libraries(ps2n-packed-elf-tests PRIVATE ps2n_content)
    add_test(NAME packed-elf COMMAND ps2n-packed-elf-tests)
    add_executable(ps2n-browser-tests tests/browser_tests.cpp)
    target_link_libraries(ps2n-browser-tests PRIVATE ps2n_content)
    add_test(NAME browser-firmware COMMAND ps2n-browser-tests)
    add_executable(ps2n-sdk-function-table-tests tests/sdk_function_table_tests.cpp)
    target_include_directories(ps2n-sdk-function-table-tests PRIVATE "${PS2N_PS2RECOMP_DIR}/ps2xRecomp/include")
    add_test(NAME sdk-function-table COMMAND ps2n-sdk-function-table-tests)
    add_executable(ps2n-preparation-receipt-tests tests/preparation_receipt_tests.cpp)
    target_include_directories(ps2n-preparation-receipt-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(ps2n-preparation-receipt-tests PRIVATE ps2n_content)
    add_test(NAME preparation-receipt COMMAND ps2n-preparation-receipt-tests "${PROJECT_SOURCE_DIR}/out/game-functional")
    add_executable(ps2n-sdk-cd-image-tests tests/sdk_cd_image_tests.cpp)
    target_link_libraries(ps2n-sdk-cd-image-tests PRIVATE ps2_iop)
    target_include_directories(ps2n-sdk-cd-image-tests PRIVATE "${PS2N_PS2RECOMP_DIR}/ps2xIOP/src")
    add_test(NAME sdk-cd-image COMMAND ps2n-sdk-cd-image-tests)
    add_executable(ps2n-sdk-iop-rpc-tests tests/sdk_iop_rpc_tests.cpp)
    target_link_libraries(ps2n-sdk-iop-rpc-tests PRIVATE ps2_iop)
    target_include_directories(ps2n-sdk-iop-rpc-tests PRIVATE "${PS2N_PS2RECOMP_DIR}/ps2xIOP/tests")
    add_test(NAME sdk-iop-rpc COMMAND ps2n-sdk-iop-rpc-tests)
    add_executable(ps2n-sdk-iop-aot-tests tests/sdk_iop_aot_tests.cpp)
    target_link_libraries(ps2n-sdk-iop-aot-tests PRIVATE ps2_iop)
    target_include_directories(ps2n-sdk-iop-aot-tests PRIVATE
        "${PS2N_PS2RECOMP_DIR}/ps2xIOP/tests" "${PS2N_PS2RECOMP_DIR}/ps2xIOP/src")
    add_test(NAME sdk-iop-aot COMMAND ps2n-sdk-iop-aot-tests)
    set(_iop_fixture "${CMAKE_BINARY_DIR}/generated/iop-aot-test")
    set(_iop_fixture_sources "${_iop_fixture}/module.cpp")
    foreach(_bucket RANGE 0 15)
        if(_bucket LESS 10)
            set(_bucket "0${_bucket}")
        endif()
        list(APPEND _iop_fixture_sources "${_iop_fixture}/ops_${_bucket}.cpp")
    endforeach()
    add_custom_command(OUTPUT ${_iop_fixture_sources} "${_iop_fixture}/fixture.h"
        BYPRODUCTS "${_iop_fixture}/fixture.irx"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/iop_codegen_fixture.py"
            "${PS2N_PS2RECOMP_DIR}/ps2xIOP/tools/iop_aot.py" "${_iop_fixture}"
        DEPENDS tests/iop_codegen_fixture.py "${PS2N_PS2RECOMP_DIR}/ps2xIOP/tools/iop_aot.py"
        VERBATIM)
    add_executable(ps2n-sdk-iop-codegen-tests tests/sdk_iop_codegen_tests.cpp ${_iop_fixture_sources})
    target_include_directories(ps2n-sdk-iop-codegen-tests PRIVATE
        "${_iop_fixture}" "${PS2N_PS2RECOMP_DIR}/ps2xIOP/src")
    target_link_libraries(ps2n-sdk-iop-codegen-tests PRIVATE ps2_iop)
    add_test(NAME sdk-iop-codegen COMMAND ps2n-sdk-iop-codegen-tests)
    add_test(NAME sdk-iop-embedded COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/iop_embedded_tests.py"
        "${PS2N_PS2RECOMP_DIR}/ps2xIOP/tools/iop_aot.py" "${_iop_fixture}/fixture.irx"
        "${PROJECT_SOURCE_DIR}/out/game-functional")
    add_executable(ps2n-sdk-memory-tests tests/sdk_memory_tests.cpp
        "${PS2N_PS2RECOMP_DIR}/ps2xTest/src/test_function_table.cpp")
    target_link_libraries(ps2n-sdk-memory-tests PRIVATE ps2_runtime)
    target_include_directories(ps2n-sdk-memory-tests PRIVATE "${PS2N_PS2RECOMP_DIR}/ps2xIOP/src")
    target_include_directories(ps2n-sdk-memory-tests PRIVATE "${PS2N_PS2RECOMP_DIR}/ps2xRuntime/src/lib")
    add_test(NAME sdk-memory COMMAND ps2n-sdk-memory-tests)
    add_executable(ps2n-sdk-call-tests tests/sdk_call_tests.cpp)
    target_include_directories(ps2n-sdk-call-tests PRIVATE "${PS2N_PS2RECOMP_DIR}/ps2xRuntime/include")
    add_test(NAME sdk-calls COMMAND ps2n-sdk-call-tests)
    add_executable(ps2n-content-tests tests/content_tests.cpp)
    target_link_libraries(ps2n-content-tests PRIVATE ps2n_content)
    add_test(NAME content COMMAND ps2n-content-tests "${CMAKE_CURRENT_BINARY_DIR}/content-tests")
endif()
