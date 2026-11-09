# ps2-native desktop launcher (Qt 6 Quick, LGPL-3.0 modules linked dynamically).
# Qt modules used: Core, Gui, Qml, Quick, QuickControls2 (Basic style), Svg,
# plus the runtime image-format plugin for the WebP banner when present.
# No GPL-only Qt module (Charts, Graphs, Quick 3D, Virtual Keyboard...) is used.
set(PS2N_QT_COMPONENTS Core Gui Qml Quick QuickControls2 Svg)
if(PS2N_LAUNCHER STREQUAL "ON")
    find_package(Qt6 6.4 REQUIRED COMPONENTS ${PS2N_QT_COMPONENTS})
else()
    find_package(Qt6 6.4 QUIET COMPONENTS ${PS2N_QT_COMPONENTS})
    if(NOT Qt6_FOUND)
        message(STATUS "ps2-native launcher: Qt 6.4+ (${PS2N_QT_COMPONENTS}) not found, launcher skipped. "
                       "Point CMAKE_PREFIX_PATH at a Qt 6 installation, or use -DPS2N_LAUNCHER=ON to require it.")
        return()
    endif()
endif()
message(STATUS "ps2-native launcher: Qt ${Qt6_VERSION} found, building ps2-native")

qt_add_executable(ps2-native WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/launcher.cpp src/launcher.hpp
    src/entry_model.cpp src/entry_model.hpp
    src/diagnostic_runner.cpp src/diagnostic_runner.hpp
    src/icon_provider.cpp src/icon_provider.hpp
    src/automation.cpp src/automation.hpp
    src/experimental_session.cpp src/experimental_session.hpp
    src/host_audio.cpp src/host_audio.hpp
    src/qt_support.hpp src/translation.hpp)
set_target_properties(ps2-native PROPERTIES
    AUTOMOC ON
    # Same directory as ps2n-boot (per configuration on multi-config generators).
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    MACOSX_BUNDLE_GUI_IDENTIFIER org.ps2native.launcher
    MACOSX_BUNDLE_BUNDLE_NAME ps2-native
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
target_compile_definitions(ps2-native PRIVATE PS2N_VERSION="${PROJECT_VERSION}")
target_include_directories(ps2-native PRIVATE src)
target_link_libraries(ps2-native PRIVATE ps2n_frontend_native
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Svg)
if(WIN32)
    target_link_libraries(ps2-native PRIVATE winmm)
else()
    find_package(Qt6 6.4 QUIET COMPONENTS Multimedia)
    if(TARGET Qt6::Multimedia)
        target_link_libraries(ps2-native PRIVATE Qt6::Multimedia)
        target_compile_definitions(ps2-native PRIVATE PS2N_QT_AUDIO)
    endif()
endif()
# Local, offline AOT only. No proprietary translation is shipped or compiled
# at runtime. Missing sources produce an explicit native-only diagnostic.
set(PS2N_SESSION_AOT_SOURCE "" CACHE FILEPATH "Local ELF AOT translation for experimental Qt sessions")
set(PS2N_SESSION_AUX_AOT_SOURCE "" CACHE FILEPATH "Optional second local ELF AOT translation")
set(PS2N_SESSION_KERNEL_AOT_SOURCE "" CACHE FILEPATH "Optional local loaded EE kernel AOT translation")
foreach(kind IN ITEMS AOT AUX_AOT KERNEL_AOT)
    if(PS2N_SESSION_${kind}_SOURCE)
        string(TOLOWER "${kind}" symbol)
        add_library(ps2n-session-${symbol} OBJECT)
        ps2n_target_aot_source(ps2n-session-${symbol} "${PS2N_SESSION_${kind}_SOURCE}")
        target_link_libraries(ps2n-session-${symbol} PRIVATE ps2n_core_native)
        target_compile_definitions(ps2n-session-${symbol} PRIVATE
            aot_module=session_${symbol}_module idle_aot_module=session_${symbol}_module
            run_recompiled=run_session_${symbol} run_idle_recompiled=run_session_idle_${symbol})
        target_link_libraries(ps2-native PRIVATE ps2n-session-${symbol})
        target_compile_definitions(ps2-native PRIVATE PS2N_SESSION_${kind})
    endif()
endforeach()
if(PS2N_BIOS_AOT_SOURCE)
    add_library(ps2n-session-bios OBJECT)
    ps2n_target_aot_source(ps2n-session-bios "${PS2N_BIOS_AOT_SOURCE}")
    target_compile_definitions(ps2n-session-bios PRIVATE aot_module=session_bios_aot_module
        run_recompiled=run_session_bios_recompiled)
    target_link_libraries(ps2n-session-bios PRIVATE ps2n_core_native)
    target_link_libraries(ps2-native PRIVATE ps2n-session-bios)
    target_compile_definitions(ps2-native PRIVATE PS2N_SESSION_BIOS_AOT)
endif()
if(TARGET ps2n_native_modules)
    target_link_libraries(ps2-native PRIVATE ps2n_native_modules)
endif()
if(MSVC)
    target_compile_options(ps2-native PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
else()
    target_compile_options(ps2-native PRIVATE -Wall -Wextra)
endif()

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(ps2-native
    URI PS2Native
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Icon.qml
        qml/HintToolTip.qml
        qml/AppButton.qml
        qml/IconButton.qml
        qml/Card.qml
        qml/Badge.qml
        qml/CompatBadge.qml
        qml/NavItem.qml
        qml/Sidebar.qml
        qml/PageHeader.qml
        qml/SectionTitle.qml
        qml/EmptyState.qml
        qml/SearchField.qml
        qml/ChipGroup.qml
        qml/InfoRow.qml
        qml/SettingRow.qml
        qml/AppSwitch.qml
        qml/AppComboBox.qml
        qml/AppScrollBar.qml
        qml/Spinner.qml
        qml/CoverArt.qml
        qml/GameCard.qml
        qml/GameRow.qml
        qml/Toasts.qml
        qml/DropOverlay.qml
        qml/CapabilityRow.qml
        qml/StatTile.qml
        qml/HomePage.qml
        qml/LibraryPage.qml
        qml/GameDetails.qml
        qml/BiosPage.qml
        qml/BiosCard.qml
        qml/BiosDetails.qml
        qml/DiagnosticPanel.qml
        qml/SettingsPage.qml
        qml/AboutPage.qml
        qml/ExperimentalWindow.qml)

# Artwork: the project banner (docs/assets) and the original application icon.
set_source_files_properties("${PROJECT_SOURCE_DIR}/docs/assets/ps2-native-banner.webp"
    PROPERTIES QT_RESOURCE_ALIAS assets/banner.webp)
set_source_files_properties(assets/app-icon.svg PROPERTIES QT_RESOURCE_ALIAS assets/app-icon.svg)
qt_add_resources(ps2-native ps2native_assets
    PREFIX /ps2native
    FILES "${PROJECT_SOURCE_DIR}/docs/assets/ps2-native-banner.webp" assets/app-icon.svg)

qt_add_resources(ps2-native translations PREFIX /i18n BASE i18n FILES i18n/pt_BR.json)
if(WIN32)
    enable_language(RC)
    target_sources(ps2-native PRIVATE assets/app-icon.rc)
endif()

# ps2n-boot is the diagnostic the launcher runs; keep it next to the launcher.
add_dependencies(ps2-native ps2n-native ps2n-dev)
if(APPLE)
    # Inside the bundle so the launcher finds it when the .app is moved.
    add_custom_command(TARGET ps2-native POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_FILE:ps2n-native> $<TARGET_FILE_DIR:ps2-native>/
        VERBATIM)
endif()

if(BUILD_TESTING)
    add_executable(ps2n-experimental-fixture ../tests/experimental_make_fixture.cpp)
    target_link_libraries(ps2n-experimental-fixture PRIVATE ps2n_core)
    set(session_rom "${FIXTURE_DIR}/experimental-rom0.bin")
    set(session_elf "${FIXTURE_DIR}/experimental-ee.elf")
    set(session_ee "${FIXTURE_DIR}/experimental_ee.cpp")
    set(session_iop "${FIXTURE_DIR}/experimental_iop.cpp")
    add_custom_command(OUTPUT "${session_rom}" "${session_elf}"
        COMMAND $<TARGET_FILE:ps2n-experimental-fixture> "${session_rom}" "${session_elf}"
        DEPENDS ps2n-experimental-fixture VERBATIM)
    add_custom_command(OUTPUT "${session_ee}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${session_elf}" "${session_ee}" --execution-alias=0x80000000
        DEPENDS ps2n-aot "${session_elf}" VERBATIM)
    add_custom_command(OUTPUT "${session_iop}"
        COMMAND $<TARGET_FILE:ps2n-aot> "${session_rom}" "${session_iop}" --iop --base=0x1FC00000 --entry=0xBFC00000
        DEPENDS ps2n-aot "${session_rom}" VERBATIM)
    add_library(ps2n-experimental-ee OBJECT "${session_ee}")
    target_link_libraries(ps2n-experimental-ee PRIVATE ps2n_core_native)
    target_compile_definitions(ps2n-experimental-ee PRIVATE aot_module=session_aot_module run_recompiled=run_session_recompiled)
    qt_add_executable(ps2n-experimental-tests
        ../tests/experimental_session.cpp
        "${session_iop}"
        src/launcher.cpp src/launcher.hpp
        src/entry_model.cpp src/entry_model.hpp
        src/diagnostic_runner.cpp src/diagnostic_runner.hpp
        src/experimental_session.cpp src/experimental_session.hpp
        src/host_audio.cpp src/host_audio.hpp)
    set_target_properties(ps2n-experimental-tests PROPERTIES AUTOMOC ON)
    target_include_directories(ps2n-experimental-tests PRIVATE src ../tests)
    target_link_libraries(ps2n-experimental-tests PRIVATE ps2n_frontend_native ps2n-experimental-ee Qt6::Quick)
    target_compile_definitions(ps2n-experimental-tests PRIVATE PS2N_SESSION_AOT=1)
    if(WIN32)
        target_link_libraries(ps2n-experimental-tests PRIVATE winmm)
    elseif(TARGET Qt6::Multimedia)
        target_link_libraries(ps2n-experimental-tests PRIVATE Qt6::Multimedia)
        target_compile_definitions(ps2n-experimental-tests PRIVATE PS2N_QT_AUDIO)
    endif()
    if(MSVC)
        target_compile_options(ps2n-experimental-tests PRIVATE /utf-8)
    endif()
    add_test(NAME launcher-experimental-session COMMAND ps2n-experimental-tests)
    # Scripted run of the real window (offscreen platform): import a synthetic
    # ROM0 and ISO, identify them, visit every page, persist; then a second run
    # restores the library from disk. Requires Qt's offscreen platform plugin.
    set(PS2N_LAUNCHER_TEST_DIR "${CMAKE_BINARY_DIR}/launcher-test")
    set(PS2N_LAUNCHER_ISO "${CMAKE_BINARY_DIR}/fixtures/synthetic-ps2.iso")
    add_custom_command(OUTPUT "${PS2N_LAUNCHER_ISO}"
        COMMAND $<TARGET_FILE:ps2n-frontend-tests> --write-iso "${PS2N_LAUNCHER_ISO}"
        DEPENDS ps2n-frontend-tests VERBATIM)
    add_custom_target(ps2n-launcher-fixtures ALL DEPENDS "${PS2N_LAUNCHER_ISO}" ps2n-frontend-rom0)
    add_test(NAME launcher-ui-clean COMMAND "${CMAKE_COMMAND}" -E rm -rf "${PS2N_LAUNCHER_TEST_DIR}")
    add_test(NAME launcher-ui-import COMMAND ps2-native --automate --data-dir "${PS2N_LAUNCHER_TEST_DIR}"
        --import "${CMAKE_BINARY_DIR}/fixtures/synthetic-rom0.bin" --import "${PS2N_LAUNCHER_ISO}"
        --expect-entries 2 --expect-valid 2 --run-diagnostic)
    add_test(NAME launcher-ui-restore COMMAND ps2-native --automate --data-dir "${PS2N_LAUNCHER_TEST_DIR}"
        --expect-entries 2 --expect-valid 2)
    set_tests_properties(launcher-ui-clean PROPERTIES FIXTURES_SETUP launcher_ui)
    set_tests_properties(launcher-ui-import PROPERTIES FIXTURES_REQUIRED launcher_ui FIXTURES_SETUP launcher_ui_library
        PASS_REGULAR_EXPRESSION "automation: passed" FAIL_REGULAR_EXPRESSION "automation: FAIL")
    set_tests_properties(launcher-ui-restore PROPERTIES FIXTURES_REQUIRED "launcher_ui;launcher_ui_library"
        PASS_REGULAR_EXPRESSION "automation: loaded 1 disc\\(s\\), 1 BIOS.*automation: passed"
        FAIL_REGULAR_EXPRESSION "automation: FAIL")
    set(PS2N_LAUNCHER_TEST_ENV "QT_QPA_PLATFORM=offscreen" "QT_QUICK_BACKEND=software")
    if(WIN32)
        get_target_property(PS2N_QT_CORE_LOCATION Qt6::Core IMPORTED_LOCATION_RELEASE)
        if(NOT PS2N_QT_CORE_LOCATION)
            get_target_property(PS2N_QT_CORE_LOCATION Qt6::Core LOCATION)
        endif()
        get_filename_component(PS2N_QT_BIN "${PS2N_QT_CORE_LOCATION}" DIRECTORY)
        get_filename_component(PS2N_QT_PREFIX "${PS2N_QT_BIN}" DIRECTORY)
        list(APPEND PS2N_LAUNCHER_TEST_ENV "QT_QPA_PLATFORM_PLUGIN_PATH=${PS2N_QT_PREFIX}/plugins/platforms"
            "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
            set_tests_properties(launcher-ui-import launcher-ui-restore launcher-experimental-session PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${PS2N_QT_BIN}")
        endif()
    endif()
    set_tests_properties(launcher-ui-import launcher-ui-restore launcher-experimental-session PROPERTIES
        ENVIRONMENT "${PS2N_LAUNCHER_TEST_ENV}" TIMEOUT 300)
endif()
