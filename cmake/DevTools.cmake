add_executable(ps2n-debug tools/ps2n-debug.cpp)
target_link_libraries(ps2n-debug PRIVATE ps2n_core)
add_executable(ps2n-analyze tools/ps2n-analyze.cpp)
target_link_libraries(ps2n-analyze PRIVATE ps2n_core)
add_executable(ps2n-dev tools/ps2n-dev.cpp)
target_sources(ps2n-dev PRIVATE tools/ps2recomp_prepare.cpp)
target_link_libraries(ps2n-dev PRIVATE ps2_analyzer_lib nlohmann_json::nlohmann_json)
file(READ "${PS2N_LOCK_FILE}" sdk_lock)
string(REPLACE "\r\n" "\n" sdk_lock "${sdk_lock}")
string(SHA256 sdk_lock_hash "${sdk_lock}")
string(JSON sdk_revision GET "${sdk_lock}" ps2recomp_sdk commit)
target_compile_definitions(ps2n-dev PRIVATE
    PS2N_PS2RECOMP_REVISION="${sdk_revision}"
    PS2N_PS2RECOMP_LOCK_SHA256="${sdk_lock_hash}")
target_link_libraries(ps2n-dev PRIVATE ps2n_core)
target_compile_definitions(ps2n-dev PRIVATE
    PS2N_DEV_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}"
    PS2N_DEV_CMAKE="${CMAKE_COMMAND}")

include(${CMAKE_CURRENT_LIST_DIR}/DevContent.cmake)
if(BUILD_TESTING)
    add_executable(ps2n-sdk-prepare-tests tests/ps2recomp_prepare.cpp)
    target_link_libraries(ps2n-sdk-prepare-tests PRIVATE ps2n_core nlohmann_json::nlohmann_json)
    add_dependencies(ps2n-sdk-prepare-tests ps2n-dev)
    set(sdk_native_test_args "")
    if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
        list(APPEND sdk_native_test_args "${CMAKE_CXX_COMPILER}"
            "${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xRuntime/include"
            "${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xIOP/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/ps2recomp_native.cpp")
    endif()
    add_test(NAME sdk-prepare-pipeline COMMAND ps2n-sdk-prepare-tests
        $<TARGET_FILE:ps2n-dev> "${CMAKE_CURRENT_BINARY_DIR}/ps2recomp-integration" ${sdk_native_test_args})
    set_tests_properties(sdk-prepare-pipeline PROPERTIES TIMEOUT 120 RESOURCE_LOCK ps2n_sdk_prepare)
    add_test(NAME sdk-lock-controls COMMAND "${CMAKE_COMMAND}"
        "-DPS2N_LOCK_FILE=${PS2N_LOCK_FILE}" "-DPS2N_PS2RECOMP_SOURCE_DIR=${PS2N_PS2RECOMP_SOURCE_DIR}"
        "-DSCRATCH=${CMAKE_CURRENT_BINARY_DIR}/ps2recomp-integration"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/ps2recomp_lock.cmake")
    set_tests_properties(sdk-lock-controls PROPERTIES TIMEOUT 30 RESOURCE_LOCK ps2n_sdk_prepare)
    add_executable(ps2n-autotests-fixture tests/ps2autotests_fixture.cpp)
    target_link_libraries(ps2n-autotests-fixture PRIVATE ps2n_core)
    set(autotests_expected "${CMAKE_CURRENT_SOURCE_DIR}/tests/ps2autotests/alu.expected")
    set(autotests_elf "${FIXTURE_DIR}/ps2autotests-alu.elf")
    set(autotests_aot "${FIXTURE_DIR}/ps2autotests-alu.cpp")
    add_custom_command(OUTPUT "${autotests_elf}"
        COMMAND $<TARGET_FILE:ps2n-autotests-fixture> "${autotests_expected}" "${autotests_elf}"
        DEPENDS ps2n-autotests-fixture "${autotests_expected}" VERBATIM)
    add_custom_command(OUTPUT "${autotests_aot}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${autotests_elf}" "${autotests_aot}" --hybrid
        DEPENDS ps2n-aot "${autotests_elf}" VERBATIM)
    add_executable(ps2n-autotests-alu tests/ps2autotests_alu.cpp "${autotests_aot}")
    target_link_libraries(ps2n-autotests-alu PRIVATE ps2n_core)
    add_test(NAME hardware-autotests-ee-alu COMMAND ps2n-autotests-alu "${autotests_expected}" "${autotests_elf}")
    set_tests_properties(hardware-autotests-ee-alu PROPERTIES TIMEOUT 30)
    add_executable(ps2n-analysis-tests tests/analysis_tests.cpp)
    target_link_libraries(ps2n-analysis-tests PRIVATE ps2n_core)
    add_test(NAME dev-analysis COMMAND ps2n-analysis-tests)
    set_tests_properties(dev-analysis PROPERTIES TIMEOUT 30)
    add_executable(ps2n-dev-diagnostics tests/dev_diagnostics.cpp "${M20_AOT}")
    target_link_libraries(ps2n-dev-diagnostics PRIVATE ps2n_core)
    add_test(NAME dev-diagnostics COMMAND ps2n-dev-diagnostics "${CMAKE_CURRENT_BINARY_DIR}/dev-diagnostics" "${M20_ROM}")
    set_tests_properties(dev-diagnostics PROPERTIES TIMEOUT 45)
    add_executable(ps2n-dev-cli-tests tests/dev_cli.cpp)
    target_link_libraries(ps2n-dev-cli-tests PRIVATE ps2n_core)
    add_dependencies(ps2n-dev-cli-tests ps2n-dev ps2n-debug)
    add_test(NAME dev-cli-pipeline COMMAND ps2n-dev-cli-tests
        $<TARGET_FILE:ps2n-dev> $<TARGET_FILE:ps2n-debug>
        "${CMAKE_CURRENT_BINARY_DIR}/dev-cli" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_COMMAND}"
        "${CMAKE_GENERATOR}" "${CMAKE_CXX_COMPILER}" "${CMAKE_MAKE_PROGRAM}" "${CMAKE_GENERATOR_PLATFORM}")
    set_tests_properties(dev-cli-pipeline PROPERTIES TIMEOUT 600)
    add_test(NAME dev-native-pipeline COMMAND ps2n-dev-cli-tests
        $<TARGET_FILE:ps2n-dev> $<TARGET_FILE:ps2n-debug>
        "${CMAKE_CURRENT_BINARY_DIR}/dev-cli" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_COMMAND}"
        "${CMAKE_GENERATOR}" "${CMAKE_CXX_COMPILER}" "${CMAKE_MAKE_PROGRAM}" "${CMAKE_GENERATOR_PLATFORM}" --native-only)
    set_tests_properties(dev-native-pipeline PROPERTIES TIMEOUT 600)
    set_tests_properties(dev-cli-pipeline dev-native-pipeline PROPERTIES RESOURCE_LOCK ps2n_dev_cli_build)
endif()
