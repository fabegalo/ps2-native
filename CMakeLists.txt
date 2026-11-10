cmake_minimum_required(VERSION 3.21)
project(ps2-native VERSION 0.0.1 LANGUAGES CXX)

include(cmake/AotSource.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

include(cmake/PS2Recomp.cmake)

add_library(ps2n_core SHARED
    src/io.cpp src/elf.cpp src/relocations.cpp src/memory.cpp src/memory_cache.cpp src/ee.cpp src/mips.cpp
    src/control_flow.cpp src/aot.cpp src/aot_runtime.cpp src/sha256.cpp src/bios.cpp
    src/ee_system.cpp src/boot.cpp src/diagnostics.cpp src/analysis.cpp src/ee_registers.cpp
    src/gs_registers.cpp src/rdram.cpp src/mch.cpp src/console.cpp src/iop.cpp src/nvm.cpp
    src/video_encoder.cpp src/gif.cpp src/gs_vram.cpp src/gs_renderer.cpp src/framebuffer.cpp src/vu_micro.cpp src/vu_aot.cpp
    src/audio.cpp src/iop_aot.cpp src/sdk_hle.cpp)
set_target_properties(ps2n_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(ps2n_core PUBLIC PS2N_NATIVE_ONLY=1)
target_include_directories(ps2n_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
include(cmake/GsVulkan.cmake)
include(cmake/GsDirectX.cmake)
option(PS2N_ENABLE_PROFILING "Compile optional host component timers (diagnostic builds only)" OFF)
if(PS2N_ENABLE_PROFILING)
    target_compile_definitions(ps2n_core PUBLIC PS2N_ENABLE_PROFILING=1)
endif()
if(MSVC)
    target_compile_options(ps2n_core PRIVATE /W4 /permissive-)
    # Large static translations specialize operands without depending on LTO.
    # COFF's ordinary section limit is too small for real guest images.
    target_compile_options(ps2n_core PUBLIC /utf-8 /bigobj)
else()
    target_compile_options(ps2n_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Launcher core: content identification, the persistent library and runtime
# contracts. No GUI dependency, so it builds and is tested on every host.
include(cmake/PS2Runtime.cmake)
add_library(ps2n_frontend STATIC src/frontend/content.cpp src/frontend/library.cpp src/frontend/runtime.cpp
    src/frontend/session.cpp)
target_link_libraries(ps2n_frontend PUBLIC ps2n_sdk_runtime)
if(MSVC)
    target_compile_options(ps2n_frontend PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(ps2n_frontend PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(ps2n-aot tools/ps2n-aot.cpp)
target_link_libraries(ps2n-aot PRIVATE ps2n_core)

# EE boot from a user-supplied ROM0; matching offline AOT is required.
add_executable(ps2n-boot tools/ps2n-boot.cpp)
target_link_libraries(ps2n-boot PRIVATE ps2n_sdk_runtime)

# Optional local AOT artifact for YOUR firmware: generate the translation with
#   ps2n-aot <your-rom0.bin> <out.cpp> --bios
# and configure with -DPS2N_BIOS_AOT_SOURCE=<out.cpp>. The generated file is
# derived from Sony firmware: keep it local, never commit or distribute it.
set(PS2N_BIOS_AOT_SOURCE "" CACHE FILEPATH "ps2n-aot --bios translation of a user-supplied ROM0")
if(PS2N_BIOS_AOT_SOURCE)
    add_library(ps2n_bios_aot OBJECT)
    ps2n_target_aot_source(ps2n_bios_aot "${PS2N_BIOS_AOT_SOURCE}")
    target_link_libraries(ps2n_bios_aot PRIVATE ps2n_core)
    target_link_libraries(ps2n-boot PRIVATE ps2n_bios_aot)
    target_compile_definitions(ps2n-boot PRIVATE PS2N_BOOT_WITH_AOT=1)
    add_executable(ps2n-boot-hybrid tools/ps2n-boot.cpp)
    target_compile_definitions(ps2n-boot-hybrid PRIVATE PS2N_BOOT_WITH_AOT=1)
    target_link_libraries(ps2n-boot-hybrid PRIVATE ps2n_sdk_runtime ps2n_bios_aot)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(ps2n-path tools/ps2n-path.cpp)
    target_link_libraries(ps2n-path PRIVATE ps2n_core)
    add_executable(ps2n-aot-build-paths tests/aot_build_paths.cpp)
    target_link_libraries(ps2n-aot-build-paths PRIVATE ps2n_core)
    option(PS2N_REQUIRE_NINJA_PATH_TEST "Fail configure if the Ninja AOT path test is unavailable" OFF)
    if(CMAKE_GENERATOR MATCHES "Ninja")
        set(PS2N_NINJA_EXECUTABLE "${CMAKE_MAKE_PROGRAM}")
    else()
        find_program(PS2N_NINJA_EXECUTABLE NAMES ninja)
    endif()
    if(PS2N_REQUIRE_NINJA_PATH_TEST AND NOT PS2N_NINJA_EXECUTABLE)
        message(FATAL_ERROR "Ninja is required for AOT path regression coverage")
    endif()
    add_test(NAME aot-long-path-build COMMAND ps2n-aot-build-paths
        "${CMAKE_CURRENT_BINARY_DIR}/aot-build-paths" "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_COMMAND}" $<TARGET_FILE:ps2n-make-fixture> $<TARGET_FILE:ps2n-aot>
        $<TARGET_FILE:ps2n-path> $<TARGET_FILE:ps2n_core> "${CMAKE_CXX_COMPILER}"
        "${PS2N_NINJA_EXECUTABLE}" "$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,Release>" $<TARGET_LINKER_FILE:ps2n_core>)
    set_tests_properties(aot-long-path-build PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" TIMEOUT 240 SKIP_RETURN_CODE 77)
    add_executable(ps2n-make-fixture tests/make_fixture.cpp)
    target_link_libraries(ps2n-make-fixture PRIVATE ps2n_core)
    add_dependencies(ps2n-aot-build-paths ps2n-make-fixture ps2n-aot ps2n-path)
    add_executable(ps2n-io-paths tests/io_paths.cpp)
    target_link_libraries(ps2n-io-paths PRIVATE ps2n_core)
    set(PS2N_TEST_UNC_ROOT "" CACHE PATH "Optional existing writable UNC share for I/O tests")
    add_test(NAME io-long-paths COMMAND ps2n-io-paths
        "${CMAKE_CURRENT_BINARY_DIR}/io-paths"
        $<TARGET_FILE:ps2n-aot> $<TARGET_FILE:ps2n-boot> $<TARGET_FILE:ps2n-make-fixture>
        "${CMAKE_COMMAND}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/io_cli.cmake"
        "${PS2N_TEST_UNC_ROOT}")
    set_tests_properties(io-long-paths PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" TIMEOUT 180)
    set(FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
    set(FIXTURE_ELF "${FIXTURE_DIR}/synthetic.elf")
    set(FIXTURE_AOT "${FIXTURE_DIR}/synthetic_aot.cpp")

    add_custom_command(
        OUTPUT "${FIXTURE_ELF}"
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

    add_executable(ps2n-aot-partitions tests/aot_partitions.cpp)
    target_link_libraries(ps2n-aot-partitions PRIVATE ps2n_core)
    add_dependencies(ps2n-aot-partitions ps2n-fixture)
    add_test(NAME native-aot-partitions COMMAND ps2n-aot-partitions "${FIXTURE_ELF}"
        "${CMAKE_CURRENT_BINARY_DIR}/aot-partitions" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_COMMAND}"
        $<TARGET_FILE:ps2n_core> "${CMAKE_CXX_COMPILER}" "${PS2N_NINJA_EXECUTABLE}"
        "$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,Release>" $<TARGET_LINKER_FILE:ps2n_core>)
    set_tests_properties(native-aot-partitions PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)

    add_executable(ps2n-aot-ops-smoke tests/aot_ops_smoke.cpp "${OPS_AOT}")
    target_link_libraries(ps2n-aot-ops-smoke PRIVATE ps2n_core)
    add_test(NAME native-aot-ops COMMAND ps2n-aot-ops-smoke "${OPS_ELF}")

    # M1.1: synthetic indirect functions, both ELF symbol discovery and
    # explicitly supplied roots for stripped ELFs. No external fixtures.
    foreach(FIXTURE_MODE IN ITEMS indirect indirect-nosym)
        set(INPUT_ELF "${FIXTURE_DIR}/${FIXTURE_MODE}.elf")
        add_custom_command(
            OUTPUT "${INPUT_ELF}"
            COMMAND $<TARGET_FILE:ps2n-make-fixture> "${INPUT_ELF}" "${FIXTURE_MODE}"
            DEPENDS ps2n-make-fixture
            VERBATIM)
    endforeach()

    set(SYMBOL_ELF "${FIXTURE_DIR}/indirect.elf")
    set(STRIPPED_ELF "${FIXTURE_DIR}/indirect-nosym.elf")
    set(SYMBOL_AOT "${FIXTURE_DIR}/indirect_aot.cpp")
    set(HINT_AOT "${FIXTURE_DIR}/indirect_hints_aot.cpp")
    set(NO_HINT_AOT "${FIXTURE_DIR}/indirect_noroots_aot.cpp")
    add_custom_command(
        OUTPUT "${SYMBOL_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${SYMBOL_ELF}" "${SYMBOL_AOT}"
        DEPENDS ps2n-aot "${SYMBOL_ELF}"
        VERBATIM)
    add_custom_command(
        OUTPUT "${HINT_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${STRIPPED_ELF}" "${HINT_AOT}"
            --entry=0x00100050 --entry=0x0010005c --entry=0x00100068
        DEPENDS ps2n-aot "${STRIPPED_ELF}"
        VERBATIM)
    add_custom_command(
        OUTPUT "${NO_HINT_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${STRIPPED_ELF}" "${NO_HINT_AOT}"
        DEPENDS ps2n-aot "${STRIPPED_ELF}"
        VERBATIM)

    add_executable(ps2n-aot-indirect-sym tests/aot_indirect.cpp "${SYMBOL_AOT}")
    target_link_libraries(ps2n-aot-indirect-sym PRIVATE ps2n_core)
    add_test(NAME native-indirect-sym COMMAND ps2n-aot-indirect-sym "${SYMBOL_ELF}" resolved)
    add_executable(ps2n-aot-indirect-hints tests/aot_indirect.cpp "${HINT_AOT}")
    target_link_libraries(ps2n-aot-indirect-hints PRIVATE ps2n_core)
    add_test(NAME native-indirect-hints COMMAND ps2n-aot-indirect-hints "${STRIPPED_ELF}" resolved)
    add_executable(ps2n-aot-indirect-unresolved tests/aot_indirect.cpp "${NO_HINT_AOT}")
    target_link_libraries(ps2n-aot-indirect-unresolved PRIVATE ps2n_core)
    add_test(NAME native-indirect-unresolved COMMAND ps2n-aot-indirect-unresolved "${STRIPPED_ELF}" unresolved)

    # M1.2: valid ELF32 R_MIPS_32 references and explicit jump table entries.
    foreach(FIXTURE_MODE IN ITEMS jump-table jump-table-nosym)
        set(JUMP_ELF "${FIXTURE_DIR}/${FIXTURE_MODE}.elf")
        add_custom_command(
            OUTPUT "${JUMP_ELF}"
            COMMAND $<TARGET_FILE:ps2n-make-fixture> "${JUMP_ELF}" "${FIXTURE_MODE}"
            DEPENDS ps2n-make-fixture VERBATIM)
    endforeach()
    set(JUMP_RELOC_ELF "${FIXTURE_DIR}/jump-table.elf")
    set(JUMP_STRIPPED_ELF "${FIXTURE_DIR}/jump-table-nosym.elf")
    set(JUMP_RELOC_AOT "${FIXTURE_DIR}/jump_reloc_aot.cpp")
    set(JUMP_HINT_AOT "${FIXTURE_DIR}/jump_hints_aot.cpp")
    set(JUMP_UNKNOWN_AOT "${FIXTURE_DIR}/jump_unknown_aot.cpp")
    add_custom_command(
        OUTPUT "${JUMP_RELOC_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${JUMP_RELOC_ELF}" "${JUMP_RELOC_AOT}"
        DEPENDS ps2n-aot "${JUMP_RELOC_ELF}" VERBATIM)
    add_custom_command(
        OUTPUT "${JUMP_HINT_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${JUMP_STRIPPED_ELF}" "${JUMP_HINT_AOT}" --table=0x00101000:2
        DEPENDS ps2n-aot "${JUMP_STRIPPED_ELF}" VERBATIM)
    add_custom_command(
        OUTPUT "${JUMP_UNKNOWN_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${JUMP_STRIPPED_ELF}" "${JUMP_UNKNOWN_AOT}"
        DEPENDS ps2n-aot "${JUMP_STRIPPED_ELF}" VERBATIM)

    add_executable(ps2n-jump-reloc tests/aot_jump_table.cpp "${JUMP_RELOC_AOT}")
    target_link_libraries(ps2n-jump-reloc PRIVATE ps2n_core)
    add_test(NAME native-jump-relocations COMMAND ps2n-jump-reloc "${JUMP_RELOC_ELF}" reloc)
    add_executable(ps2n-jump-hints tests/aot_jump_table.cpp "${JUMP_HINT_AOT}")
    target_link_libraries(ps2n-jump-hints PRIVATE ps2n_core)
    add_test(NAME native-jump-explicit COMMAND ps2n-jump-hints "${JUMP_STRIPPED_ELF}" explicit)
    add_executable(ps2n-jump-unresolved tests/aot_jump_table.cpp "${JUMP_UNKNOWN_AOT}")
    target_link_libraries(ps2n-jump-unresolved PRIVATE ps2n_core)
    add_test(NAME native-jump-unresolved COMMAND ps2n-jump-unresolved "${JUMP_STRIPPED_ELF}" unresolved)

    # M1.3: R_MIPS_HI16/LO16 and R_MIPS_26 evidence, SLTIU-bounded jump tables,
    # verified indirect entries and the R5900 integer subset (tests/m13_program.hpp).
    foreach(FIXTURE_MODE IN ITEMS m13 m13-stripped)
        add_custom_command(
            OUTPUT "${FIXTURE_DIR}/${FIXTURE_MODE}.elf"
            COMMAND $<TARGET_FILE:ps2n-make-fixture> "${FIXTURE_DIR}/${FIXTURE_MODE}.elf" "${FIXTURE_MODE}"
            DEPENDS ps2n-make-fixture VERBATIM)
    endforeach()
    set(M13_ELF "${FIXTURE_DIR}/m13.elf")
    set(M13_STRIPPED_ELF "${FIXTURE_DIR}/m13-stripped.elf")
    set(M13_RELOC_AOT "${FIXTURE_DIR}/m13_reloc_aot.cpp")
    set(M13_STRIPPED_AOT "${FIXTURE_DIR}/m13_stripped_aot.cpp")
    set(M13_HINT_AOT "${FIXTURE_DIR}/m13_hint_aot.cpp")
    add_custom_command(
        OUTPUT "${M13_RELOC_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${M13_ELF}" "${M13_RELOC_AOT}"
        DEPENDS ps2n-aot "${M13_ELF}" VERBATIM)
    add_custom_command(
        OUTPUT "${M13_STRIPPED_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${M13_STRIPPED_ELF}" "${M13_STRIPPED_AOT}"
        DEPENDS ps2n-aot "${M13_STRIPPED_ELF}" VERBATIM)
    # 0x00100400 is m13::kCallback: only relocations (or this hint) prove it is code.
    add_custom_command(
        OUTPUT "${M13_HINT_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${M13_STRIPPED_ELF}" "${M13_HINT_AOT}" --entry=0x00100400
        DEPENDS ps2n-aot "${M13_STRIPPED_ELF}" VERBATIM)
    foreach(M13_CASE IN ITEMS reloc stripped hint)
        if(M13_CASE STREQUAL "reloc")
            set(M13_INPUT "${M13_ELF}")
            set(M13_AOT "${M13_RELOC_AOT}")
            set(M13_MODE reloc)
            set(M13_TEST native-m13-relocations)
        elseif(M13_CASE STREQUAL "stripped")
            set(M13_INPUT "${M13_STRIPPED_ELF}")
            set(M13_AOT "${M13_STRIPPED_AOT}")
            set(M13_MODE stripped)
            set(M13_TEST native-m13-stripped)
        else()
            set(M13_INPUT "${M13_STRIPPED_ELF}")
            set(M13_AOT "${M13_HINT_AOT}")
            set(M13_MODE stripped-hint)
            set(M13_TEST native-m13-stripped-hint)
        endif()
        add_executable(ps2n-m13-${M13_CASE} tests/aot_m13.cpp "${M13_AOT}")
        target_link_libraries(ps2n-m13-${M13_CASE} PRIVATE ps2n_core)
        add_test(NAME ${M13_TEST} COMMAND ps2n-m13-${M13_CASE} "${M13_INPUT}" ${M13_MODE})
    endforeach()

    set(BOOT_PAYLOAD_IMAGE "${FIXTURE_DIR}/boot-payload.elf")
    set(BOOT_PAYLOAD_SOURCE "${FIXTURE_DIR}/boot_payload.cpp")
    add_custom_command(OUTPUT "${BOOT_PAYLOAD_IMAGE}"
        COMMAND $<TARGET_FILE:ps2n-make-fixture> "${BOOT_PAYLOAD_IMAGE}" bios-payload
        DEPENDS ps2n-make-fixture VERBATIM)
    add_custom_command(OUTPUT "${BOOT_PAYLOAD_SOURCE}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${BOOT_PAYLOAD_IMAGE}" "${BOOT_PAYLOAD_SOURCE}" --execution-alias=0x80000000
        DEPENDS ps2n-aot "${BOOT_PAYLOAD_IMAGE}" VERBATIM)
    add_library(ps2n-test-boot-payload OBJECT "${BOOT_PAYLOAD_SOURCE}")
    target_compile_definitions(ps2n-test-boot-payload PRIVATE aot_module=boot_payload_aot_module run_recompiled=run_boot_payload)
    target_link_libraries(ps2n-test-boot-payload PRIVATE ps2n_core)
    # M2.0: synthetic ROM0 (tests/bios_program.hpp, no Sony content) booted from
    # the reset vector with explicit offline roots and a translated RAM payload.
    set(M20_ROM "${FIXTURE_DIR}/synthetic-rom0.bin")
    set(M20_AOT "${FIXTURE_DIR}/synthetic_rom0_aot.cpp")
    add_custom_command(
        OUTPUT "${M20_ROM}" "${M20_ROM}.entries"
        COMMAND $<TARGET_FILE:ps2n-make-fixture> "${M20_ROM}" bios "${M20_ROM}.entries"
        DEPENDS ps2n-make-fixture VERBATIM)
    add_custom_command(
        OUTPUT "${M20_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${M20_ROM}" "${M20_AOT}" --bios "--entries=${M20_ROM}.entries"
        DEPENDS ps2n-aot "${M20_ROM}" "${M20_ROM}.entries" VERBATIM)
    add_executable(ps2n-boot-m20 tests/boot_m20.cpp "${M20_AOT}")
    target_link_libraries(ps2n-boot-m20 PRIVATE ps2n_core)
    add_test(NAME native-m20-boot COMMAND ps2n-boot-m20 "${M20_ROM}")

    set(M20_BLOCK_POINT "# block point 0x[0-9A-F]+ \\[bus: device not implemented\\] EE bus R32 \\(LW\\) at 0xB000F020 \\(physical 0x1000F020, INTC\\)")
    add_executable(ps2n-boot-missing-aot tools/ps2n-boot.cpp)
    target_link_libraries(ps2n-boot-missing-aot PRIVATE ps2n_sdk_runtime)
    add_test(NAME cli-m20-missing-ee-aot COMMAND ps2n-boot-missing-aot "${M20_ROM}")
    set_tests_properties(cli-m20-missing-ee-aot PROPERTIES PASS_REGULAR_EXPRESSION "missing EE AOT")
    add_executable(ps2n-boot-synthetic-native tools/ps2n-boot.cpp "${M20_AOT}")
    target_compile_definitions(ps2n-boot-synthetic-native PRIVATE PS2N_BOOT_WITH_AOT=1)
    target_link_libraries(ps2n-boot-synthetic-native PRIVATE ps2n_sdk_runtime)
    add_test(NAME cli-m20-missing-iop-aot COMMAND ps2n-boot-synthetic-native "${M20_ROM}")
    set_tests_properties(cli-m20-missing-iop-aot PROPERTIES PASS_REGULAR_EXPRESSION "missing IOP AOT")

    # M2.1: the synthetic ROM0 extended with MMI, COP0 Count, EE timers and the
    # 0x1000F500 store (blocks the real BIOS needs), plus EE register unit checks.
    set(M21_ROM "${FIXTURE_DIR}/synthetic-rom0-m21.bin")
    set(M21_AOT "${FIXTURE_DIR}/synthetic_rom0_m21_aot.cpp")
    add_custom_command(
        OUTPUT "${M21_ROM}" "${M21_ROM}.entries"
        COMMAND $<TARGET_FILE:ps2n-make-fixture> "${M21_ROM}" bios-m21 "${M21_ROM}.entries"
        DEPENDS ps2n-make-fixture VERBATIM)
    add_custom_command(
        OUTPUT "${M21_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${M21_ROM}" "${M21_AOT}" --bios "--entries=${M21_ROM}.entries"
        DEPENDS ps2n-aot "${M21_ROM}" "${M21_ROM}.entries" VERBATIM)
    add_executable(ps2n-boot-m21 tests/boot_m21.cpp "${M21_AOT}")
    target_link_libraries(ps2n-boot-m21 PRIVATE ps2n_core)
    add_test(NAME native-m21-boot COMMAND ps2n-boot-m21 "${M21_ROM}")

    # M2.2: GS privileged registers and line clock, DEV9 latches, EE SIO and
    # LWC1/SWC1 in the synthetic ROM0, plus register-level unit checks.
    set(M22_ROM "${FIXTURE_DIR}/synthetic-rom0-m22.bin")
    set(M22_AOT "${FIXTURE_DIR}/synthetic_rom0_m22_aot.cpp")
    add_custom_command(
        OUTPUT "${M22_ROM}" "${M22_ROM}.entries"
        COMMAND $<TARGET_FILE:ps2n-make-fixture> "${M22_ROM}" bios-m22 "${M22_ROM}.entries"
        DEPENDS ps2n-make-fixture VERBATIM)
    add_custom_command(
        OUTPUT "${M22_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${M22_ROM}" "${M22_AOT}" --bios "--entries=${M22_ROM}.entries"
        DEPENDS ps2n-aot "${M22_ROM}" "${M22_ROM}.entries" VERBATIM)
    add_executable(ps2n-boot-m22 tests/boot_m22.cpp "${M22_AOT}")
    target_link_libraries(ps2n-boot-m22 PRIVATE ps2n_core)
    add_test(NAME native-m22-boot COMMAND ps2n-boot-m22 "${M22_ROM}")

    # M2.3: MCH_RICM/MCH_DRD and the Direct RDRAM serial chain (InitDev for two
    # devices) in the synthetic ROM0, plus chain and controller unit checks.
    set(M23_ROM "${FIXTURE_DIR}/synthetic-rom0-m23.bin")
    set(M23_AOT "${FIXTURE_DIR}/synthetic_rom0_m23_aot.cpp")
    add_custom_command(
        OUTPUT "${M23_ROM}" "${M23_ROM}.entries"
        COMMAND $<TARGET_FILE:ps2n-make-fixture> "${M23_ROM}" bios-m23 "${M23_ROM}.entries"
        DEPENDS ps2n-make-fixture VERBATIM)
    add_custom_command(
        OUTPUT "${M23_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${M23_ROM}" "${M23_AOT}" --bios "--entries=${M23_ROM}.entries"
        DEPENDS ps2n-aot "${M23_ROM}" "${M23_ROM}.entries" VERBATIM)
    add_executable(ps2n-boot-m23 tests/boot_m23.cpp "${M23_AOT}")
    target_link_libraries(ps2n-boot-m23 PRIVATE ps2n_core)
    add_test(NAME native-m23-boot COMMAND ps2n-boot-m23 "${M23_ROM}")

    # M2.4: RDRAM timing/test registers after InitDev and serial transactions
    # that commit on completion, plus chain and controller unit checks.
    set(M24_ROM "${FIXTURE_DIR}/synthetic-rom0-m24.bin")
    set(M24_AOT "${FIXTURE_DIR}/synthetic_rom0_m24_aot.cpp")
    add_custom_command(
        OUTPUT "${M24_ROM}" "${M24_ROM}.entries"
        COMMAND $<TARGET_FILE:ps2n-make-fixture> "${M24_ROM}" bios-m24 "${M24_ROM}.entries"
        DEPENDS ps2n-make-fixture VERBATIM)
    add_custom_command(
        OUTPUT "${M24_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${M24_ROM}" "${M24_AOT}" --bios "--entries=${M24_ROM}.entries"
        DEPENDS ps2n-aot "${M24_ROM}" "${M24_ROM}.entries" VERBATIM)
    add_executable(ps2n-boot-m24 tests/boot_m24.cpp "${M24_AOT}")
    target_link_libraries(ps2n-boot-m24 PRIVATE ps2n_core)
    add_test(NAME native-m24-boot COMMAND ps2n-boot-m24 "${M24_ROM}")

    # M2.5: MCH control registers (F400..F4C0 latches, the F410 operation),
    # D_ENABLER from a console profile, SIO pin initialization, SETR keeping the
    # RDRAM registers and the chain enumeration end, plus unit checks.
    set(M25_ROM "${FIXTURE_DIR}/synthetic-rom0-m25.bin")
    set(M25_AOT "${FIXTURE_DIR}/synthetic_rom0_m25_aot.cpp")
    add_custom_command(
        OUTPUT "${M25_ROM}" "${M25_ROM}.entries"
        COMMAND $<TARGET_FILE:ps2n-make-fixture> "${M25_ROM}" bios-m25 "${M25_ROM}.entries"
        DEPENDS ps2n-make-fixture VERBATIM)
    add_custom_command(
        OUTPUT "${M25_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${M25_ROM}" "${M25_AOT}" --bios "--entries=${M25_ROM}.entries"
        DEPENDS ps2n-aot "${M25_ROM}" "${M25_ROM}.entries" VERBATIM)
    add_executable(ps2n-boot-m25 tests/boot_m25.cpp "${M25_AOT}")
    target_link_libraries(ps2n-boot-m25 PRIVATE ps2n_core)
    add_test(NAME native-m25-boot COMMAND ps2n-boot-m25 "${M25_ROM}")

    # M2.5: the IOP (MIPS I boot path, interrupt/DMA/CDVD registers it reaches)
    # and what the EE sees of it, plus the EE INTC and the MMI logical group.
    add_executable(ps2n-iop-m25 tests/iop_m25.cpp)
    target_link_libraries(ps2n-iop-m25 PRIVATE ps2n_core)
    add_test(NAME native-m25-iop COMMAND ps2n-iop-m25)

    # M2.6: external MECHACON NVM images (validation) and the configuration and
    # NVM S commands reading them.
    add_executable(ps2n-nvm-m26 tests/nvm_m26.cpp)
    target_link_libraries(ps2n-nvm-m26 PRIVATE ps2n_core)
    add_test(NAME native-m26-nvm COMMAND ps2n-nvm-m26)

    # M2.6: the IOP devices EECONF and THREADMAN reach (tool board, DEV9 revision,
    # expansion bay, i.LINK), the hardware timers, I_STAT and exception entry.
    add_executable(ps2n-iop-m26 tests/iop_m26.cpp)
    target_link_libraries(ps2n-iop-m26 PRIVATE ps2n_core)
    add_test(NAME native-m26-iop COMMAND ps2n-iop-m26)

    # M2.7: VBLANK from the GS vertical timing (IOP IRQ 0/11), the EE/IOP ordering,
    # the SIF registers, partly known values and the EE devices after the GS reset.
    add_executable(ps2n-iop-m27 tests/iop_m27.cpp)
    target_link_libraries(ps2n-iop-m27 PRIVATE ps2n_core)
    add_test(NAME native-m27-iop COMMAND ps2n-iop-m27)

    add_executable(ps2n-m28 tests/m28.cpp)
    target_link_libraries(ps2n-m28 PRIVATE ps2n_core)
    add_test(NAME native-m28-video-gif COMMAND ps2n-m28)

    # Launcher core (headless): probes of synthetic ROM0 and ISO images (one past
    # 4 GiB, sparse), Unicode long-path library persistence, runtime contracts.
    add_executable(ps2n-frontend-tests tests/frontend_tests.cpp)
    target_link_libraries(ps2n-frontend-tests PRIVATE ps2n_frontend)
    if(MSVC)
        target_compile_options(ps2n-frontend-tests PRIVATE /utf-8)
    endif()
    add_dependencies(ps2n-frontend-tests ps2n-fixture)
    add_custom_target(ps2n-frontend-rom0 DEPENDS "${M20_ROM}")
    add_dependencies(ps2n-frontend-tests ps2n-frontend-rom0)
    add_test(NAME launcher-core COMMAND ps2n-frontend-tests "${M20_ROM}" "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(launcher-core PROPERTIES TIMEOUT 120)

    set(M29_ROM "${FIXTURE_DIR}/synthetic-gs.bin")
    set(M29_AOT "${FIXTURE_DIR}/synthetic_gs_aot.cpp")
    add_custom_command(OUTPUT "${M29_ROM}"
        COMMAND $<TARGET_FILE:ps2n-make-fixture> "${M29_ROM}" gs
        DEPENDS ps2n-make-fixture VERBATIM)
    add_custom_command(OUTPUT "${M29_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${M29_ROM}" "${M29_AOT}" --bios
        DEPENDS ps2n-aot "${M29_ROM}" VERBATIM)
    add_executable(ps2n-gs-m29 tests/gs_m29.cpp "${M29_AOT}")
    target_link_libraries(ps2n-gs-m29 PRIVATE ps2n_core)
    foreach(GS_CASE IN ITEMS vram gif drawing symbolic-blend)
        add_test(NAME native-m29-${GS_CASE} COMMAND ps2n-gs-m29 ${GS_CASE})
    endforeach()
    add_test(NAME native-m29-gs COMMAND ps2n-gs-m29 guest "${M29_ROM}" "${CMAKE_CURRENT_BINARY_DIR}/synthetic-gs.bmp")
    set_tests_properties(native-m29-gs PROPERTIES TIMEOUT 30)

endif()

include(cmake/DirectElf.cmake)
include(cmake/BiosBoot.cmake)
include(cmake/VuAot.cmake)
include(cmake/IopAot.cmake)
include(cmake/DevTools.cmake)
include(cmake/Checkpoint.cmake)
add_executable(ps2n-gs-bench tools/ps2n-gs-bench.cpp)
target_link_libraries(ps2n-gs-bench PRIVATE ps2n_core)
target_include_directories(ps2n-gs-bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
if(BUILD_TESTING)
    add_executable(ps2n-gs-gpu-tests tests/gs_gpu.cpp)
    target_link_libraries(ps2n-gs-gpu-tests PRIVATE ps2n_core)
    add_test(NAME native-gs-gpu COMMAND ps2n-gs-gpu-tests "${CMAKE_CURRENT_BINARY_DIR}/gs-gpu-checkpoints")
    set_tests_properties(native-gs-gpu PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77)
    add_test(NAME native-gs-directx COMMAND ps2n-gs-gpu-tests "${CMAKE_CURRENT_BINARY_DIR}/gs-directx-checkpoints" directx)
    set_tests_properties(native-gs-directx PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77
        ENVIRONMENT "PS2N_GS_GPU_DIAGNOSTICS=trace")
    add_executable(ps2n-ipu-tables tests/ipu_tables.cpp)
    target_link_libraries(ps2n-ipu-tables PRIVATE ps2n_core)
    add_test(NAME native-ipu-tables COMMAND ps2n-ipu-tables)

    # Original VU1 bytes -> the existing offline AOT tool -> real host code.
    # Runtime registration is linked directly so static archive extraction
    # cannot discard the generated module's automatic registration.
    add_executable(ps2n-vu-aot-fixture tests/vu_aot_make_fixture.cpp)
    target_link_libraries(ps2n-vu-aot-fixture PRIVATE ps2n_core)
    set(VU_AOT_IMAGE "${FIXTURE_DIR}/vu-aot.bin")
    set(VU_AOT_SOURCE "${FIXTURE_DIR}/vu_aot.cpp")
    set(VU_EE_IMAGE "${FIXTURE_DIR}/vu-ee.elf")
    set(VU_EE_SOURCE "${FIXTURE_DIR}/vu_ee_aot.cpp")
    add_custom_command(OUTPUT "${VU_AOT_IMAGE}" "${VU_EE_IMAGE}"
        COMMAND $<TARGET_FILE:ps2n-vu-aot-fixture> "${VU_AOT_IMAGE}" vu
        COMMAND $<TARGET_FILE:ps2n-vu-aot-fixture> "${VU_EE_IMAGE}" ee
        DEPENDS ps2n-vu-aot-fixture VERBATIM)
    add_custom_command(OUTPUT "${VU_AOT_SOURCE}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${VU_AOT_IMAGE}" "${VU_AOT_SOURCE}" --vu1
            --entry=0 --entry=512 --entry=1024 --entry=2048 --entry=16376
        DEPENDS ps2n-aot "${VU_AOT_IMAGE}" VERBATIM)
    add_custom_command(OUTPUT "${VU_EE_SOURCE}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${VU_EE_IMAGE}" "${VU_EE_SOURCE}"
        DEPENDS ps2n-aot "${VU_EE_IMAGE}" VERBATIM)
    set(VU_CLI_IMAGE "${FIXTURE_DIR}/vu-cli.vu1")
    set(VU_CLI_SOURCE "${FIXTURE_DIR}/vu_cli_aot.cpp")
    add_custom_command(OUTPUT "${VU_CLI_IMAGE}"
        COMMAND $<TARGET_FILE:ps2n-vu-aot-fixture> "${VU_CLI_IMAGE}" cli
        DEPENDS ps2n-vu-aot-fixture VERBATIM)
    add_custom_command(OUTPUT "${VU_CLI_SOURCE}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${VU_CLI_IMAGE}" "${VU_CLI_SOURCE}" --vu1 --entry=0 --entry=16 --entry=32
        DEPENDS ps2n-aot "${VU_CLI_IMAGE}" VERBATIM)
    target_sources(ps2n-vu-aot-cli PRIVATE "${VU_CLI_SOURCE}" "${VU_EE_SOURCE}")
    add_executable(ps2n-vu-aot-tests tests/vu_aot.cpp "${VU_AOT_SOURCE}" "${VU_EE_SOURCE}")
    target_link_libraries(ps2n-vu-aot-tests PRIVATE ps2n_core)
    add_test(NAME native-vu1-aot COMMAND ps2n-vu-aot-tests "${CMAKE_CURRENT_BINARY_DIR}/vu-aot-checkpoints")
    set_tests_properties(native-vu1-aot PROPERTIES TIMEOUT 90)
    add_executable(ps2n-vu-aot-bench tests/vu_aot_bench.cpp "${VU_AOT_SOURCE}" "${VU_EE_SOURCE}")
    target_link_libraries(ps2n-vu-aot-bench PRIVATE ps2n_core)
endif()

include(cmake/NativeRuntime.cmake)
include(cmake/Hle.cmake)
include(cmake/NativeModule.cmake)

# Desktop launcher (Qt 6 Quick). Optional: AUTO builds it only when Qt is found,
# so headless and core-only builds never depend on a GUI toolkit.
set(PS2N_LAUNCHER "AUTO" CACHE STRING "Build the ps2-native desktop launcher (Qt 6): AUTO, ON or OFF")
set_property(CACHE PS2N_LAUNCHER PROPERTY STRINGS AUTO ON OFF)
if(NOT PS2N_LAUNCHER STREQUAL "OFF")
    add_subdirectory(launcher)
endif()

if(BUILD_TESTING)
    foreach(cpu IN ITEMS ee iop vu)
        add_library(ps2n-test-${cpu}-fixtures OBJECT tests/${cpu}_native_fixture.cpp)
        target_link_libraries(ps2n-test-${cpu}-fixtures PRIVATE ps2n_core)
    endforeach()
    # Thousands of constant-word IOP fixtures test correctness, not host throughput.
    # Avoid expensive inlining across that synthetic matrix; runtime stays Release.
    if(MSVC)
        target_compile_options(ps2n-test-iop-fixtures PRIVATE /Od /Ob0)
    else()
        target_compile_options(ps2n-test-iop-fixtures PRIVATE -O0)
    endif()
    foreach(target IN ITEMS ps2n-boot-m20 ps2n-boot-m21 ps2n-boot-m22 ps2n-boot-m23 ps2n-boot-m24 ps2n-boot-m25 ps2n-ee-context ps2n-ee-cache ps2n-ee-fpu-knowledge
            ps2n-ee-memory-copy ps2n-ee-mula ps2n-ee-sqrt ps2n-graphics-dma-scratchpad)
        target_link_libraries(${target} PRIVATE ps2n-test-ee-fixtures ps2n-test-iop-fixtures ps2n-test-vu-fixtures)
    endforeach()
    foreach(stage RANGE 20 25)
        target_link_libraries(ps2n-boot-m${stage} PRIVATE ps2n-test-boot-payload)
    endforeach()
    foreach(target IN ITEMS ps2n-iop-m25 ps2n-iop-m26 ps2n-iop-m27 ps2n-iop-memory-copy
            ps2n-iop-bit-memory ps2n-iop-knowledge ps2n-iop-hblank ps2n-vu-micro ps2n-vu-path1 ps2n-gs-events)
        target_link_libraries(${target} PRIVATE ps2n-test-iop-fixtures ps2n-test-vu-fixtures)
    endforeach()
endif()
