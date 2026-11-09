if(BUILD_TESTING)
    add_executable(ps2n-hle-fixture tests/hle_make_fixture.cpp)
    target_link_libraries(ps2n-hle-fixture PRIVATE ps2n_core)
    set(HLE_IMAGE "${FIXTURE_DIR}/hle.elf")
    set(HLE_ENTRIES "${FIXTURE_DIR}/hle.entries")
    set(HLE_SOURCE "${FIXTURE_DIR}/hle.cpp")
    add_custom_command(OUTPUT "${HLE_IMAGE}" "${HLE_ENTRIES}"
        COMMAND $<TARGET_FILE:ps2n-hle-fixture> "${HLE_IMAGE}" "${HLE_ENTRIES}"
        DEPENDS ps2n-hle-fixture VERBATIM)
    add_custom_command(OUTPUT "${HLE_SOURCE}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${HLE_IMAGE}" "${HLE_SOURCE}" --hybrid "--entries=${HLE_ENTRIES}"
        DEPENDS ps2n-aot "${HLE_IMAGE}" "${HLE_ENTRIES}" VERBATIM)
    add_executable(ps2n-hle-native-tests tests/hle.cpp "${HLE_SOURCE}")
    set_source_files_properties(tests/hle.cpp PROPERTIES COMPILE_DEFINITIONS PS2N_SDK_HLE=1)
    target_link_libraries(ps2n-hle-native-tests PRIVATE ps2n_sdk_runtime)
    target_include_directories(ps2n-hle-native-tests PRIVATE
        "${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xIOP/include"
        "${PS2N_PS2RECOMP_SOURCE_DIR}/ps2xIOP/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk")
    add_test(NAME hle-kernel-native COMMAND ps2n-hle-native-tests
        "${HLE_IMAGE}" "${CMAKE_CURRENT_BINARY_DIR}/hle-test")
    set_tests_properties(hle-kernel-native PROPERTIES TIMEOUT 60)
    add_executable(ps2n-sdk-runtime-hle-tests tests/ps2recomp_runtime_hle.cpp "${HLE_SOURCE}")
    target_link_libraries(ps2n-sdk-runtime-hle-tests PRIVATE ps2n_sdk_runtime)
    add_test(NAME sdk-runtime-hle-owner COMMAND ps2n-sdk-runtime-hle-tests "${HLE_IMAGE}")
    set_tests_properties(sdk-runtime-hle-owner PROPERTIES TIMEOUT 60)
endif()
