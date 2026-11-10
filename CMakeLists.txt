cmake_minimum_required(VERSION 3.21)
project(ps2-native VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Execution: PS2Recomp, unmodified (recompiler, analyzer, runtime, IOP).
include(cmake/PS2Recomp.cmake)

# ps2-native: content identification, disc access, the library and the
# preparation/supervision tool. No CPU, device or graphics emulation here.
add_library(ps2n_content STATIC
    src/io.cpp src/sha256.cpp src/elf.cpp src/disc.cpp src/process.cpp
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
file(GLOB _ps2n_games LIST_DIRECTORIES true CONFIGURE_DEPENDS "${PS2N_GAMES_DIR}/*")
foreach(_game IN LISTS _ps2n_games)
    if(IS_DIRECTORY "${_game}/generated" AND EXISTS "${_game}/game.txt")
        get_filename_component(_id "${_game}" NAME)
        ps2n_add_game(${_id} "${_game}/generated")
    endif()
endforeach()

include(CTest)

# Desktop launcher (Qt 6 Quick). AUTO builds it only when Qt is found.
set(PS2N_LAUNCHER "AUTO" CACHE STRING "Build the ps2-native desktop launcher (Qt 6): AUTO, ON or OFF")
set_property(CACHE PS2N_LAUNCHER PROPERTY STRINGS AUTO ON OFF)
if(NOT PS2N_LAUNCHER STREQUAL "OFF")
    add_subdirectory(launcher)
endif()

if(BUILD_TESTING)
    add_executable(ps2n-content-tests tests/content_tests.cpp)
    target_link_libraries(ps2n-content-tests PRIVATE ps2n_content)
    add_test(NAME content COMMAND ps2n-content-tests "${CMAKE_CURRENT_BINARY_DIR}/content-tests")
endif()
