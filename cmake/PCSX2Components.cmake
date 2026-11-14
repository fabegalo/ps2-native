# SPU2 only; video and EE/IOP execution remain in the pinned PS2Recomp.
# kzspu2 (PCSX2's SPU2 with KillzoneRecomp's IOP shims) is built here rather than through its MSVC-only
# CMakeLists, so the same locked sources compile with MSVC, GCC and Clang on x64 and ARM64.
set(_pcsx2 "${PROJECT_SOURCE_DIR}/external/pcsx2")
set(_kzspu2 "${PROJECT_SOURCE_DIR}/external/pcsx2-components/kzspu2")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/fetch-pcsx2-components.py" --check
    RESULT_VARIABLE _device_check)
if(NOT _device_check EQUAL 0)
    message(FATAL_ERROR "PCSX2 device sources are missing or modified; see verification output above")
endif()

# MSVC cannot compile PCSX2's NEON GSVector; Windows ARM64 builds kzspu2 alone with clang-cl (MSVC ABI).
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "ARM64")
    find_program(PS2N_CLANG_CL clang-cl HINTS "$ENV{VCINSTALLDIR}Tools/Llvm/ARM64/bin" "$ENV{VCINSTALLDIR}Tools/Llvm/bin")
    if(NOT PS2N_CLANG_CL)
        message(FATAL_ERROR "Windows ARM64 builds PCSX2's SPU2 with clang-cl, as PCSX2 does; install the Visual Studio C++ Clang tools")
    endif()
    include(ExternalProject)
    set(_kzspu2_dir "${CMAKE_BINARY_DIR}/kzspu2-clang")
    set(_kzspu2_lib "${_kzspu2_dir}/lib/kzspu2.lib")
    set(_kzspu2_args -DCMAKE_BUILD_TYPE=Release "-DPS2N_ROOT=${PROJECT_SOURCE_DIR}"
        "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=${_kzspu2_dir}/lib" "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE=${_kzspu2_dir}/lib")
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        set(_kzspu2_toolset CMAKE_GENERATOR "${CMAKE_GENERATOR}" CMAKE_GENERATOR_PLATFORM "${CMAKE_GENERATOR_PLATFORM}"
            CMAKE_GENERATOR_TOOLSET ClangCL)
    else()
        list(APPEND _kzspu2_args "-DCMAKE_C_COMPILER=${PS2N_CLANG_CL}" "-DCMAKE_CXX_COMPILER=${PS2N_CLANG_CL}")
    endif()
    ExternalProject_Add(kzspu2_clang SOURCE_DIR "${PROJECT_SOURCE_DIR}/cmake/kzspu2-clang" BINARY_DIR "${_kzspu2_dir}"
        ${_kzspu2_toolset} CMAKE_ARGS ${_kzspu2_args}
        BUILD_COMMAND "${CMAKE_COMMAND}" --build "${_kzspu2_dir}" --config Release --target kzspu2
        INSTALL_COMMAND "" BUILD_ALWAYS ON EXCLUDE_FROM_ALL ON BUILD_BYPRODUCTS "${_kzspu2_lib}")
    add_library(kzspu2 STATIC IMPORTED GLOBAL)
    set_target_properties(kzspu2 PROPERTIES IMPORTED_LOCATION "${_kzspu2_lib}")
    target_include_directories(kzspu2 INTERFACE "${_kzspu2}/include")
    add_dependencies(kzspu2 kzspu2_clang)
else()
    include("${CMAKE_CURRENT_LIST_DIR}/Kzspu2.cmake")
endif()

add_executable(kzspu2_test "${_kzspu2}/test/kzspu2_test.cpp")
target_link_libraries(kzspu2_test PRIVATE kzspu2)
target_compile_features(kzspu2_test PRIVATE cxx_std_20)
target_compile_definitions(kzspu2_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)

# Host audio output: SDL3 (Zlib), the same commit as ReXGlue, audio subsystem only.
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/fetch-pcsx2-components.py"
    --group host_libraries --check RESULT_VARIABLE _host_check)
if(NOT _host_check EQUAL 0)
    message(FATAL_ERROR "Pinned host libraries missing or modified; run scripts/fetch-pcsx2-components.py --group host_libraries")
endif()
foreach(_option SHARED TEST_LIBRARY INSTALL VIDEO GPU RENDER CAMERA JOYSTICK HAPTIC HIDAPI SENSOR POWER DIALOG TRAY)
    set(SDL_${_option} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_AUDIO ON CACHE BOOL "" FORCE)
set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
add_subdirectory("${PROJECT_SOURCE_DIR}/external/sdl3" sdl3 EXCLUDE_FROM_ALL SYSTEM)
