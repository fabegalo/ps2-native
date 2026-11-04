add_executable(ps2n-debug tools/ps2n-debug.cpp)
target_link_libraries(ps2n-debug PRIVATE ps2n_core)
add_executable(ps2n-analyze tools/ps2n-analyze.cpp)
target_link_libraries(ps2n-analyze PRIVATE ps2n_core)
add_executable(ps2n-dev tools/ps2n-dev.cpp)
target_link_libraries(ps2n-dev PRIVATE ps2n_core)
target_compile_definitions(ps2n-dev PRIVATE
    PS2N_DEV_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}"
    PS2N_DEV_CMAKE="${CMAKE_COMMAND}")

include(${CMAKE_CURRENT_LIST_DIR}/DevContent.cmake)
if(BUILD_TESTING)
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
endif()
