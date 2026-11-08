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
    target_link_libraries(ps2n-hle-native-tests PRIVATE ps2n_core_native)
    add_executable(ps2n-hle-reference-tests tests/hle.cpp)
    target_link_libraries(ps2n-hle-reference-tests PRIVATE ps2n_core)
    add_custom_target(ps2n-hle-test-fixtures DEPENDS "${HLE_IMAGE}" "${HLE_ENTRIES}")
    add_dependencies(ps2n-hle-reference-tests ps2n-hle-test-fixtures)
    add_test(NAME hle-kernel-equivalence COMMAND "${CMAKE_COMMAND}"
        "-DNATIVE=$<TARGET_FILE:ps2n-hle-native-tests>" "-DREFERENCE=$<TARGET_FILE:ps2n-hle-reference-tests>"
        "-DIMAGE=${HLE_IMAGE}" "-DSCRATCH=${CMAKE_CURRENT_BINARY_DIR}/hle-test"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/hle_compare.cmake")
    set_tests_properties(hle-kernel-equivalence PROPERTIES TIMEOUT 60)
endif()
