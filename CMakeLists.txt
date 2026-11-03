cmake_minimum_required(VERSION 3.21)
project(ps2-native VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ps2n_core STATIC src/io.cpp src/elf.cpp src/memory.cpp src/ee.cpp src/aot.cpp)
target_include_directories(ps2n_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(MSVC)
    target_compile_options(ps2n_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(ps2n_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(ps2n-aot tools/ps2n-aot.cpp)
target_link_libraries(ps2n-aot PRIVATE ps2n_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(ps2n-make-fixture tests/make_fixture.cpp)
    set(FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
    set(FIXTURE_ELF "${FIXTURE_DIR}/synthetic.elf")
    set(FIXTURE_AOT "${FIXTURE_DIR}/synthetic_aot.cpp")

    add_custom_command(
        OUTPUT "${FIXTURE_ELF}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${FIXTURE_DIR}"
        COMMAND $<TARGET_FILE:ps2n-make-fixture> "${FIXTURE_ELF}"
        DEPENDS ps2n-make-fixture
        VERBATIM)
    add_custom_target(ps2n-fixture DEPENDS "${FIXTURE_ELF}")
    add_custom_command(
        OUTPUT "${FIXTURE_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${FIXTURE_ELF}" "${FIXTURE_AOT}"
        DEPENDS ps2n-aot "${FIXTURE_ELF}"
        VERBATIM)

    set(OPS_ELF "${FIXTURE_DIR}/ops.elf")
    set(OPS_AOT "${FIXTURE_DIR}/ops_aot.cpp")
    add_custom_command(
        OUTPUT "${OPS_ELF}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${FIXTURE_DIR}"
        COMMAND $<TARGET_FILE:ps2n-make-fixture> "${OPS_ELF}" ops
        DEPENDS ps2n-make-fixture
        VERBATIM)
    add_custom_command(
        OUTPUT "${OPS_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${OPS_ELF}" "${OPS_AOT}"
        DEPENDS ps2n-aot "${OPS_ELF}"
        VERBATIM)

    add_executable(ps2n-core-tests tests/core_tests.cpp)
    target_link_libraries(ps2n-core-tests PRIVATE ps2n_core)
    add_dependencies(ps2n-core-tests ps2n-fixture)
    add_test(NAME core-validation COMMAND ps2n-core-tests "${FIXTURE_ELF}")

    add_executable(ps2n-aot-smoke tests/aot_smoke.cpp "${FIXTURE_AOT}")
    target_link_libraries(ps2n-aot-smoke PRIVATE ps2n_core)
    add_test(NAME native-aot-smoke COMMAND ps2n-aot-smoke "${FIXTURE_ELF}")

    add_executable(ps2n-aot-ops-smoke tests/aot_ops_smoke.cpp "${OPS_AOT}")
    target_link_libraries(ps2n-aot-ops-smoke PRIVATE ps2n_core)
    add_test(NAME native-aot-ops COMMAND ps2n-aot-ops-smoke "${OPS_ELF}")
endif()
