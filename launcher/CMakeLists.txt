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
target_link_libraries(ps2-native PRIVATE ps2n_frontend
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Svg)
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
        qml/AboutPage.qml)

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
add_dependencies(ps2-native ps2n-boot)
if(APPLE)
    # Inside the bundle so the launcher finds it when the .app is moved.
    add_custom_command(TARGET ps2-native POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_FILE:ps2n-boot> $<TARGET_FILE_DIR:ps2-native>/
        VERBATIM)
endif()

if(BUILD_TESTING)
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
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
            set_tests_properties(launcher-ui-import launcher-ui-restore PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${PS2N_QT_BIN}")
        endif()
    endif()
    set_tests_properties(launcher-ui-import launcher-ui-restore PROPERTIES
        ENVIRONMENT "${PS2N_LAUNCHER_TEST_ENV}" TIMEOUT 300)
endif()
