set(PS2N_IOP_AOT_SOURCES "" CACHE STRING "Semicolon-separated offline IOP AOT source dispatchers")

if(BUILD_TESTING)
    add_executable(ps2n-iop-aot-cli-tests tests/iop_aot_cli.cpp)
    target_include_directories(ps2n-iop-aot-cli-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools")
    target_link_libraries(ps2n-iop-aot-cli-tests PRIVATE ps2n_core)
    add_test(NAME native-iop-capture COMMAND ps2n-iop-aot-cli-tests "${CMAKE_CURRENT_BINARY_DIR}/iop-aot-capture")
    add_executable(ps2n-iop-aot-fixture tests/iop_aot_make_fixture.cpp)
    target_link_libraries(ps2n-iop-aot-fixture PRIVATE ps2n_core)
    set(IOP_IMAGE "${FIXTURE_DIR}/iop-aot.bin")
    set(IOP_SOURCE "${FIXTURE_DIR}/iop_aot.cpp")
    add_custom_command(OUTPUT "${IOP_IMAGE}"
        COMMAND $<TARGET_FILE:ps2n-iop-aot-fixture> "${IOP_IMAGE}"
        DEPENDS ps2n-iop-aot-fixture VERBATIM)
    add_custom_command(OUTPUT "${IOP_SOURCE}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${IOP_IMAGE}" "${IOP_SOURCE}" --iop --base=0 --entry=0x1000 --entry=0x80 --entry=0x1028
        DEPENDS ps2n-aot "${IOP_IMAGE}" VERBATIM)
    add_executable(ps2n-iop-aot-tests tests/iop_aot.cpp "${IOP_SOURCE}")
    target_link_libraries(ps2n-iop-aot-tests PRIVATE ps2n_core)
    add_test(NAME native-iop-aot COMMAND ps2n-iop-aot-tests)
endif()
