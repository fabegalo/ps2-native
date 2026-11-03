cmake_minimum_required(VERSION 3.21)
project(ps2-native VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ps2n_core STATIC
    src/io.cpp src/elf.cpp src/relocations.cpp src/memory.cpp src/ee.cpp src/mips.cpp
    src/control_flow.cpp src/aot.cpp src/aot_runtime.cpp src/sha256.cpp src/bios.cpp
    src/interpreter.cpp src/boot.cpp src/ee_registers.cpp
    src/gs_registers.cpp src/rdram.cpp src/mch.cpp src/console.cpp src/iop.cpp src/nvm.cpp
    src/video_encoder.cpp src/gif.cpp)
target_include_directories(ps2n_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(MSVC)
    target_compile_options(ps2n_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(ps2n_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(ps2n-aot tools/ps2n-aot.cpp)
target_link_libraries(ps2n-aot PRIVATE ps2n_core)

# M2.0: EE boot from the reset vector of a user-supplied ROM0 (interpreter only).
add_executable(ps2n-boot tools/ps2n-boot.cpp)
target_link_libraries(ps2n-boot PRIVATE ps2n_core)

# Optional local hybrid runner for YOUR firmware: generate the translation with
#   ps2n-aot <your-rom0.bin> <out.cpp> --bios
# and configure with -DPS2N_BIOS_AOT_SOURCE=<out.cpp>. The generated file is
# derived from Sony firmware: keep it local, never commit or distribute it.
set(PS2N_BIOS_AOT_SOURCE "" CACHE FILEPATH "ps2n-aot --bios translation of a user-supplied ROM0")
if(PS2N_BIOS_AOT_SOURCE)
    add_executable(ps2n-boot-hybrid tools/ps2n-boot.cpp "${PS2N_BIOS_AOT_SOURCE}")
    target_compile_definitions(ps2n-boot-hybrid PRIVATE PS2N_BOOT_WITH_AOT=1)
    target_link_libraries(ps2n-boot-hybrid PRIVATE ps2n_core)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(ps2n-make-fixture tests/make_fixture.cpp)
    target_link_libraries(ps2n-make-fixture PRIVATE ps2n_core)
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

    # M2.0: synthetic ROM0 (tests/bios_program.hpp, no Sony content) booted from
    # the reset vector, interpreter-only and hybrid with its ps2n-aot --bios translation.
    set(M20_ROM "${FIXTURE_DIR}/synthetic-rom0.bin")
    set(M20_AOT "${FIXTURE_DIR}/synthetic_rom0_aot.cpp")
    add_custom_command(
        OUTPUT "${M20_ROM}"
        COMMAND $<TARGET_FILE:ps2n-make-fixture> "${M20_ROM}" bios
        DEPENDS ps2n-make-fixture VERBATIM)
    add_custom_command(
        OUTPUT "${M20_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${M20_ROM}" "${M20_AOT}" --bios
        DEPENDS ps2n-aot "${M20_ROM}" VERBATIM)
    add_executable(ps2n-boot-m20 tests/boot_m20.cpp "${M20_AOT}")
    target_link_libraries(ps2n-boot-m20 PRIVATE ps2n_core)
    add_test(NAME native-m20-boot COMMAND ps2n-boot-m20 "${M20_ROM}")

    set(M20_BLOCK_POINT "# block point 0x[0-9A-F]+ \\[bus: device not implemented\\] EE bus R32 \\(LW\\) at 0xB000F000 \\(physical 0x1000F000, INTC\\)")
    add_test(NAME cli-m20-boot-interpreter COMMAND ps2n-boot "${M20_ROM}" --trace=full)
    set_tests_properties(cli-m20-boot-interpreter PROPERTIES
        PASS_REGULAR_EXPRESSION "# engine interpreter only.*${M20_BLOCK_POINT}")
    add_executable(ps2n-boot-synthetic-hybrid tools/ps2n-boot.cpp "${M20_AOT}")
    target_compile_definitions(ps2n-boot-synthetic-hybrid PRIVATE PS2N_BOOT_WITH_AOT=1)
    target_link_libraries(ps2n-boot-synthetic-hybrid PRIVATE ps2n_core)
    add_test(NAME cli-m20-boot-hybrid COMMAND ps2n-boot-synthetic-hybrid "${M20_ROM}")
    set_tests_properties(cli-m20-boot-hybrid PROPERTIES
        PASS_REGULAR_EXPRESSION "# engine hybrid: AOT blocks [0-9]+ \\+ interpreter fallback.*${M20_BLOCK_POINT}")

    # M2.1: the synthetic ROM0 extended with MMI, COP0 Count, EE timers and the
    # 0x1000F500 store (blocks the real BIOS needs), plus EE register unit checks.
    set(M21_ROM "${FIXTURE_DIR}/synthetic-rom0-m21.bin")
    set(M21_AOT "${FIXTURE_DIR}/synthetic_rom0_m21_aot.cpp")
    add_custom_command(
        OUTPUT "${M21_ROM}"
        COMMAND $<TARGET_FILE:ps2n-make-fixture> "${M21_ROM}" bios-m21
        DEPENDS ps2n-make-fixture VERBATIM)
    add_custom_command(
        OUTPUT "${M21_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${M21_ROM}" "${M21_AOT}" --bios
        DEPENDS ps2n-aot "${M21_ROM}" VERBATIM)
    add_executable(ps2n-boot-m21 tests/boot_m21.cpp "${M21_AOT}")
    target_link_libraries(ps2n-boot-m21 PRIVATE ps2n_core)
    add_test(NAME native-m21-boot COMMAND ps2n-boot-m21 "${M21_ROM}")

    # M2.2: GS privileged registers and line clock, DEV9 latches, EE SIO and
    # LWC1/SWC1 in the synthetic ROM0, plus register-level unit checks.
    set(M22_ROM "${FIXTURE_DIR}/synthetic-rom0-m22.bin")
    set(M22_AOT "${FIXTURE_DIR}/synthetic_rom0_m22_aot.cpp")
    add_custom_command(
        OUTPUT "${M22_ROM}"
        COMMAND $<TARGET_FILE:ps2n-make-fixture> "${M22_ROM}" bios-m22
        DEPENDS ps2n-make-fixture VERBATIM)
    add_custom_command(
        OUTPUT "${M22_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${M22_ROM}" "${M22_AOT}" --bios
        DEPENDS ps2n-aot "${M22_ROM}" VERBATIM)
    add_executable(ps2n-boot-m22 tests/boot_m22.cpp "${M22_AOT}")
    target_link_libraries(ps2n-boot-m22 PRIVATE ps2n_core)
    add_test(NAME native-m22-boot COMMAND ps2n-boot-m22 "${M22_ROM}")

    # M2.3: MCH_RICM/MCH_DRD and the Direct RDRAM serial chain (InitDev for two
    # devices) in the synthetic ROM0, plus chain and controller unit checks.
    set(M23_ROM "${FIXTURE_DIR}/synthetic-rom0-m23.bin")
    set(M23_AOT "${FIXTURE_DIR}/synthetic_rom0_m23_aot.cpp")
    add_custom_command(
        OUTPUT "${M23_ROM}"
        COMMAND $<TARGET_FILE:ps2n-make-fixture> "${M23_ROM}" bios-m23
        DEPENDS ps2n-make-fixture VERBATIM)
    add_custom_command(
        OUTPUT "${M23_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${M23_ROM}" "${M23_AOT}" --bios
        DEPENDS ps2n-aot "${M23_ROM}" VERBATIM)
    add_executable(ps2n-boot-m23 tests/boot_m23.cpp "${M23_AOT}")
    target_link_libraries(ps2n-boot-m23 PRIVATE ps2n_core)
    add_test(NAME native-m23-boot COMMAND ps2n-boot-m23 "${M23_ROM}")

    # M2.4: RDRAM timing/test registers after InitDev and serial transactions
    # that commit on completion, plus chain and controller unit checks.
    set(M24_ROM "${FIXTURE_DIR}/synthetic-rom0-m24.bin")
    set(M24_AOT "${FIXTURE_DIR}/synthetic_rom0_m24_aot.cpp")
    add_custom_command(
        OUTPUT "${M24_ROM}"
        COMMAND $<TARGET_FILE:ps2n-make-fixture> "${M24_ROM}" bios-m24
        DEPENDS ps2n-make-fixture VERBATIM)
    add_custom_command(
        OUTPUT "${M24_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${M24_ROM}" "${M24_AOT}" --bios
        DEPENDS ps2n-aot "${M24_ROM}" VERBATIM)
    add_executable(ps2n-boot-m24 tests/boot_m24.cpp "${M24_AOT}")
    target_link_libraries(ps2n-boot-m24 PRIVATE ps2n_core)
    add_test(NAME native-m24-boot COMMAND ps2n-boot-m24 "${M24_ROM}")

    # M2.5: MCH control registers (F400..F4C0 latches, the F410 operation),
    # D_ENABLER from a console profile, SIO pin initialization, SETR keeping the
    # RDRAM registers and the chain enumeration end, plus unit checks.
    set(M25_ROM "${FIXTURE_DIR}/synthetic-rom0-m25.bin")
    set(M25_AOT "${FIXTURE_DIR}/synthetic_rom0_m25_aot.cpp")
    add_custom_command(
        OUTPUT "${M25_ROM}"
        COMMAND $<TARGET_FILE:ps2n-make-fixture> "${M25_ROM}" bios-m25
        DEPENDS ps2n-make-fixture VERBATIM)
    add_custom_command(
        OUTPUT "${M25_AOT}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${M25_ROM}" "${M25_AOT}" --bios
        DEPENDS ps2n-aot "${M25_ROM}" VERBATIM)
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

endif()
