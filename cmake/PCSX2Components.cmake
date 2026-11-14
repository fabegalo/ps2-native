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

set(_spu2 "${_pcsx2}/pcsx2/SPU2")
set(_kzspu2_shared ${_spu2}/spu2sys.cpp ${_spu2}/ReadInput.cpp ${_spu2}/Reverb.cpp ${_spu2}/ADSR.cpp
    ${_spu2}/RegTable.cpp ${_spu2}/Dma.cpp "${_kzspu2}/shim/Kzspu2Shims.cpp" "${_kzspu2}/src/kzspu2.cpp")
# Per-ISA code (PCSX2's pcsx2SPU2SourcesUnshared): GSVector picks SSE/AVX on x64 and NEON on ARM64.
set(_kzspu2_unshared ${_spu2}/Mixer.cpp ${_spu2}/ReverbResample.cpp)
# MSVC reports its target; the Visual Studio generator keeps the host in CMAKE_SYSTEM_PROCESSOR.
set(_kzspu2_cpu "${CMAKE_SYSTEM_PROCESSOR}")
if(MSVC)
    set(_kzspu2_cpu "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
endif()
if(_kzspu2_cpu MATCHES "^(x86_64|AMD64|amd64|x64)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_kzspu2_x64 ON)
elseif(_kzspu2_cpu MATCHES "^(aarch64|arm64|ARM64)$")
    set(_kzspu2_x64 OFF)
else()
    message(FATAL_ERROR "SPU2: unsupported target processor ${_kzspu2_cpu}")
endif()
# MSVC x64 keeps kzspu2's runtime SSE4/AVX/AVX2 selection (its CPUID shim is MSVC-only).
# shortcut: GCC/Clang x64 build the SSE4.1 baseline PCSX2 requires; add the AVX variants with a portable CPUID shim.
set(_kzspu2_multi OFF)
if(MSVC AND _kzspu2_x64)
    set(_kzspu2_multi ON)
endif()

add_library(kzspu2_flags INTERFACE)
target_compile_features(kzspu2_flags INTERFACE cxx_std_20)
target_compile_definitions(kzspu2_flags INTERFACE NOMINMAX)
target_include_directories(kzspu2_flags INTERFACE "${_kzspu2}/shim/include" "${_kzspu2}/include" "${_pcsx2}"
    "${_pcsx2}/pcsx2" "${_pcsx2}/3rdparty/include" "${_pcsx2}/3rdparty/fmt/include" "${_pcsx2}/3rdparty/fast_float/include")
if(MSVC)
    target_compile_definitions(kzspu2_flags INTERFACE WINVER=0x0A00 _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN
        _CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS _UNICODE UNICODE)
    target_compile_options(kzspu2_flags INTERFACE /Zc:externConstexpr /Zc:__cplusplus /permissive- /Zc:preprocessor
        /utf-8 /Zo /GS /fp:contract /W1 /wd4005 /wd4996 "/FI${_kzspu2}/shim/include/kzspu2_prefix.h")
    if(_kzspu2_x64)
        target_compile_definitions(kzspu2_flags INTERFACE _M_X86=1 __SSE4_1__=1)
    endif()
else()
    # PCSX2's own GCC/Clang settings for this code: no strict aliasing, no fast-math.
    target_compile_options(kzspu2_flags INTERFACE -fno-strict-aliasing -w -include "${_kzspu2}/shim/include/kzspu2_prefix.h")
    if(_kzspu2_x64)
        target_compile_options(kzspu2_flags INTERFACE -msse4.1)
    endif()
endif()

if(_kzspu2_multi)
    add_library(kzspu2 STATIC ${_kzspu2_shared})
    target_compile_definitions(kzspu2 PRIVATE MULTI_ISA_SHARED_COMPILATION)
    set(_first 1)
    set(_isa_libs "")
    foreach(_isa sse4 avx avx2)
        add_library(kzspu2_${_isa} STATIC ${_kzspu2_unshared})
        target_link_libraries(kzspu2_${_isa} PRIVATE kzspu2_flags)
        target_compile_definitions(kzspu2_${_isa} PRIVATE MULTI_ISA_UNSHARED_COMPILATION=isa_${_isa} MULTI_ISA_IS_FIRST=${_first})
        if(_isa STREQUAL "avx")
            target_compile_options(kzspu2_${_isa} PRIVATE /arch:AVX)
        elseif(_isa STREQUAL "avx2")
            target_compile_options(kzspu2_${_isa} PRIVATE /arch:AVX2)
        endif()
        list(APPEND _isa_libs kzspu2_${_isa})
        set(_first 0)
    endforeach()
    list(JOIN _isa_libs "," _isa_libs)
    target_link_libraries(kzspu2 INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${_isa_libs}>")
else()
    add_library(kzspu2 STATIC ${_kzspu2_shared} ${_kzspu2_unshared})
endif()
target_link_libraries(kzspu2 PRIVATE kzspu2_flags)
target_include_directories(kzspu2 PUBLIC "${_kzspu2}/include")
find_package(Threads REQUIRED)
target_link_libraries(kzspu2 PUBLIC Threads::Threads)

add_executable(kzspu2_test "${_kzspu2}/test/kzspu2_test.cpp")
target_link_libraries(kzspu2_test PRIVATE kzspu2)
target_compile_features(kzspu2_test PRIVATE cxx_std_20)
target_compile_definitions(kzspu2_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
