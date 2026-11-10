# PS2Recomp is the execution layer, consumed unmodified at the commit pinned in
# deps.lock.json (as x360-native consumes ReXGlue):
#   ps2xRecomp/ps2xAnalyzer  built as their own top-level project (README flow)
#   ps2xRuntime/ps2xIOP      added here; each prepared game links ps2_runtime
include(ExternalProject)
include(FetchContent)

set(PS2N_LOCK_FILE "${CMAKE_CURRENT_LIST_DIR}/../deps.lock.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PS2N_LOCK_FILE}")
file(READ "${PS2N_LOCK_FILE}" _ps2n_lock)
string(JSON PS2N_PS2RECOMP_URL GET "${_ps2n_lock}" ps2recomp url)
string(JSON PS2N_PS2RECOMP_COMMIT GET "${_ps2n_lock}" ps2recomp commit)

set(_ps2n_submodule "${CMAKE_CURRENT_LIST_DIR}/../external/ps2recomp")
if(EXISTS "${_ps2n_submodule}/ps2xRuntime/CMakeLists.txt")
    set(_ps2n_default_source "${_ps2n_submodule}")
else()
    set(_ps2n_default_source "")
endif()
set(PS2N_PS2RECOMP_SOURCE_DIR "${_ps2n_default_source}" CACHE PATH
    "PS2Recomp checkout at the pinned commit (default: the external/ps2recomp submodule; empty: fetch it)")
if(PS2N_PS2RECOMP_SOURCE_DIR)
    set(_ps2recomp "${PS2N_PS2RECOMP_SOURCE_DIR}")
else()
    FetchContent_Declare(ps2recomp GIT_REPOSITORY "${PS2N_PS2RECOMP_URL}" GIT_TAG "${PS2N_PS2RECOMP_COMMIT}"
        GIT_PROGRESS FALSE SOURCE_SUBDIR ps2n-no-configure)
    FetchContent_MakeAvailable(ps2recomp)
    set(_ps2recomp "${ps2recomp_SOURCE_DIR}")
endif()

find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_ps2recomp}" rev-parse HEAD
    OUTPUT_VARIABLE _actual OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _result)
if(NOT _result EQUAL 0 OR NOT _actual STREQUAL PS2N_PS2RECOMP_COMMIT)
    message(FATAL_ERROR "PS2Recomp at ${_ps2recomp} is ${_actual}; deps.lock.json pins ${PS2N_PS2RECOMP_COMMIT}")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_ps2recomp}" status --porcelain --untracked-files=no
    OUTPUT_VARIABLE _changes OUTPUT_STRIP_TRAILING_WHITESPACE)
if(_changes)
    message(FATAL_ERROR "PS2Recomp checkout ${_ps2recomp} has local changes; it is used unmodified:\n${_changes}")
endif()
set(PS2N_PS2RECOMP_DIR "${_ps2recomp}")

# Recompiler and analyzer: the upstream top-level build, tools only.
set(PS2N_PS2RECOMP_TOOLS_DIR "${CMAKE_BINARY_DIR}/ps2recomp-tools")
ExternalProject_Add(ps2recomp_tools
    SOURCE_DIR "${_ps2recomp}"
    BINARY_DIR "${PS2N_PS2RECOMP_TOOLS_DIR}"
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DPS2X_BUILD_RUNTIME=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    BUILD_COMMAND "${CMAKE_COMMAND}" --build "${PS2N_PS2RECOMP_TOOLS_DIR}" --config Release --target ps2_recomp ps2_analyzer
    INSTALL_COMMAND ""
    BUILD_ALWAYS OFF
    BUILD_BYPRODUCTS "${PS2N_PS2RECOMP_TOOLS_DIR}/ps2xRecomp/ps2_recomp${CMAKE_EXECUTABLE_SUFFIX}"
        "${PS2N_PS2RECOMP_TOOLS_DIR}/ps2xAnalyzer/ps2_analyzer${CMAKE_EXECUTABLE_SUFFIX}")
set(PS2N_RECOMP_EXE "${PS2N_PS2RECOMP_TOOLS_DIR}/ps2xRecomp/ps2_recomp${CMAKE_EXECUTABLE_SUFFIX}")
set(PS2N_ANALYZER_EXE "${PS2N_PS2RECOMP_TOOLS_DIR}/ps2xAnalyzer/ps2_analyzer${CMAKE_EXECUTABLE_SUFFIX}")

# Runtime: the debug UI and verbose logs are upstream developer options.
set(PS2X_ENABLE_DEBUG_UI OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_SCCACHE OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_AGRESSIVE_LOGS OFF CACHE BOOL "" FORCE)
add_subdirectory("${_ps2recomp}/ps2xIOP" ps2recomp/iop EXCLUDE_FROM_ALL)
add_subdirectory("${_ps2recomp}/ps2xRuntime" ps2recomp/runtime EXCLUDE_FROM_ALL)

# One executable per prepared game: the generated PS2Recomp sources, the
# ps2-native host entry point and the unchanged ps2_runtime library.
function(ps2n_add_game id generated)
    file(GLOB _sources CONFIGURE_DEPENDS "${generated}/*.cpp")
    add_executable(ps2n-game-${id} EXCLUDE_FROM_ALL ${_sources} "${PROJECT_SOURCE_DIR}/tools/ps2n_game_main.cpp")
    target_include_directories(ps2n-game-${id} PRIVATE "${generated}")
    target_link_libraries(ps2n-game-${id} PRIVATE ps2_runtime)
    set_target_properties(ps2n-game-${id} PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 32)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/tools/ps2n_game_main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    if(MSVC)
        target_compile_options(ps2n-game-${id} PRIVATE /FS /bigobj)
        # Same workaround as ps2EntryRunner: WinAPI and raylib both define CloseWindow.
        target_link_options(ps2n-game-${id} PRIVATE /FORCE:MULTIPLE)
    endif()
    if(COMMAND ps2x_stage_ffmpeg_runtime_dlls)
        ps2x_stage_ffmpeg_runtime_dlls(ps2n-game-${id})
    endif()
    if(COMMAND EnableFastReleaseMode AND CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
        EnableFastReleaseMode(ps2n-game-${id})
    endif()
endfunction()
