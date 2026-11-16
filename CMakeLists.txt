cmake_minimum_required(VERSION 3.21)
project(ps2-native VERSION 0.1.0 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJC)  # GLFW (raylib) and SDL3 CoreAudio, before any subdirectory
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

include(CTest)

# ps2-native: content identification, disc access, the library and the
# preparation/supervision tool. No CPU, device or graphics emulation here.
add_library(ps2n_content STATIC
    src/io.cpp src/sha256.cpp src/elf.cpp src/packed_elf.cpp src/disc.cpp src/process.cpp
    src/bios.cpp src/console.cpp src/nvm.cpp src/xff.cpp
    src/frontend/content.cpp src/frontend/library.cpp src/frontend/runtime.cpp)
target_include_directories(ps2n_content PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(MSVC)
    target_compile_options(ps2n_content PRIVATE /W4 /permissive-)
    target_compile_options(ps2n_content PUBLIC /utf-8)
else()
    target_compile_options(ps2n_content PRIVATE -Wall -Wextra -Wpedantic)
endif()


# Execution: the engine in engine/ (exoteric33/ps2-native-recomp, MIT). It is built
# with its own clang toolchain by its scripts; ps2n drives it and keeps the
# games, generated code and builds in PS2N_ENGINE_WORK.
set(PS2N_ENGINE_WORK "${CMAKE_BINARY_DIR}/engine" CACHE PATH "Engine games, generated code and builds")
add_executable(ps2n tools/ps2n.cpp)
target_link_libraries(ps2n PRIVATE ps2n_content)
target_compile_definitions(ps2n PRIVATE
    PS2N_ENGINE_DIR="${PROJECT_SOURCE_DIR}/engine"
    PS2N_ENGINE_WORK="${PS2N_ENGINE_WORK}"
    PS2N_EXE_SUFFIX="${CMAKE_EXECUTABLE_SUFFIX}")

# Desktop launcher (Qt 6 Quick). AUTO builds it only when Qt is found.
set(PS2N_LAUNCHER "AUTO" CACHE STRING "Build the ps2-native desktop launcher (Qt 6): AUTO, ON or OFF")
set_property(CACHE PS2N_LAUNCHER PROPERTY STRINGS AUTO ON OFF)
if(NOT PS2N_LAUNCHER STREQUAL "OFF")
    add_subdirectory(launcher)
endif()


if(BUILD_TESTING)
    add_executable(ps2n-packed-elf-tests tests/packed_elf_tests.cpp)
    target_link_libraries(ps2n-packed-elf-tests PRIVATE ps2n_content)
    add_test(NAME packed-elf COMMAND ps2n-packed-elf-tests)
    add_executable(ps2n-xff-tests tests/xff_tests.cpp)
    target_link_libraries(ps2n-xff-tests PRIVATE ps2n_content)
    add_test(NAME xff COMMAND ps2n-xff-tests)
    add_executable(ps2n-browser-tests tests/browser_tests.cpp)
    target_link_libraries(ps2n-browser-tests PRIVATE ps2n_content)
    add_test(NAME browser-firmware COMMAND ps2n-browser-tests)
    add_executable(ps2n-content-tests tests/content_tests.cpp)
    target_link_libraries(ps2n-content-tests PRIVATE ps2n_content)
    add_test(NAME content COMMAND ps2n-content-tests "${CMAKE_CURRENT_BINARY_DIR}/content-tests")
endif()
