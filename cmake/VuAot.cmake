set(PS2N_VU0_AOT_SOURCES "" CACHE STRING "Semicolon-separated offline ps2n-aot --vu0 source dispatchers")
# VU1 uses the existing content-verified AOT source manifest/build machinery.
# These are local artifacts; no compiler is launched by the runtime.
set(PS2N_VU1_AOT_SOURCES "" CACHE STRING "Semicolon-separated offline ps2n-aot --vu1 source dispatchers")
foreach(target IN ITEMS ps2n-boot ps2n-boot-hybrid ps2n-run ps2n-run-hybrid)
    if(TARGET ${target})
        foreach(source IN LISTS PS2N_VU1_AOT_SOURCES PS2N_VU0_AOT_SOURCES)
            ps2n_target_aot_source(${target} "${source}")
        endforeach()
    endif()
endforeach()
if(BUILD_TESTING)
    add_executable(ps2n-vu0-aot-fixture tests/vu0_aot_make_fixture.cpp)
    target_link_libraries(ps2n-vu0-aot-fixture PRIVATE ps2n_core)
    set(VU0_IMAGE "${FIXTURE_DIR}/vu0-aot.bin")
    set(VU0_SOURCE "${FIXTURE_DIR}/vu0_aot.cpp")
    add_custom_command(OUTPUT "${VU0_IMAGE}"
        COMMAND $<TARGET_FILE:ps2n-vu0-aot-fixture> "${VU0_IMAGE}"
        DEPENDS ps2n-vu0-aot-fixture VERBATIM)
    add_custom_command(OUTPUT "${VU0_SOURCE}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${VU0_IMAGE}" "${VU0_SOURCE}" --vu0 --entry=0
        DEPENDS ps2n-aot "${VU0_IMAGE}" VERBATIM)
    add_executable(ps2n-vu0-aot-tests tests/vu0_aot.cpp "${VU0_SOURCE}")
    target_link_libraries(ps2n-vu0-aot-tests PRIVATE ps2n_core)
    add_test(NAME native-vu0-aot COMMAND ps2n-vu0-aot-tests "${VU0_IMAGE}")
    add_executable(ps2n-vu-aot-cli tests/vu_aot_cli.cpp)
    target_link_libraries(ps2n-vu-aot-cli PRIVATE ps2n_core)
    add_test(NAME native-vu1-aot-cli COMMAND ps2n-vu-aot-cli "${CMAKE_CURRENT_BINARY_DIR}/vu-aot-cli-owned")
    set_tests_properties(native-vu1-aot-cli PROPERTIES TIMEOUT 30)
endif()
