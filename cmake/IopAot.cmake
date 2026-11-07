set(PS2N_IOP_AOT_SOURCES "" CACHE STRING "Semicolon-separated offline IOP AOT source dispatchers")
foreach(target IN ITEMS ps2n-boot ps2n-boot-hybrid ps2n-run ps2n-run-hybrid)
    if(TARGET ${target})
        foreach(source IN LISTS PS2N_IOP_AOT_SOURCES)
            ps2n_target_aot_source(${target} "${source}")
        endforeach()
    endif()
endforeach()
if(BUILD_TESTING)
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
