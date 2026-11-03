# Ninja compares /showIncludes byte-for-byte. Older CMake releases can decode
# a localized UTF-8 prefix as OEM, leaving header changes invisible to Ninja.
# Probe syntax only, retain the exact emitted bytes, and let Ninja track headers.
if(WIN32 AND MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    get_filename_component(_ps2n_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /nologo /Zs /showIncludes /std:c++20
        "/I${_ps2n_root}/include" "${_ps2n_root}/tools/ps2n-path.cpp"
        RESULT_VARIABLE _ps2n_prefix_result OUTPUT_VARIABLE _ps2n_prefix_output
        ERROR_VARIABLE _ps2n_prefix_error ENCODING NONE TIMEOUT 15)
    if(NOT "${_ps2n_prefix_result}" STREQUAL "0")
        message(FATAL_ERROR "Cannot probe MSVC include dependencies: ${_ps2n_prefix_error}")
    endif()
    string(REGEX MATCH "(^|[\r\n])([^\r\n]*: +)([A-Za-z]:[/\\\\]|[/\\\\][/\\\\])" _ps2n_match "${_ps2n_prefix_output}")
    if(NOT _ps2n_match)
        message(FATAL_ERROR "Cannot identify the MSVC /showIncludes prefix")
    endif()
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
    # Source encoding is explicit; the changed command also refreshes old AOT
    # dependency records made with a misdecoded /showIncludes prefix.
    add_compile_options(/utf-8)
endif()

# Keep native build tools on a short reference to the original AOT directory.
# No source is copied, included through a wrapper, or omitted from dependency tracking.
function(ps2n_target_aot_source target source)
    get_filename_component(aot_source "${source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    string(LENGTH "${aot_source}" source_length)
    if(WIN32 AND source_length GREATER 240)
        get_filename_component(aot_parent "${aot_source}" DIRECTORY)
        get_filename_component(aot_name "${aot_source}" NAME)
        string(SHA256 parent_key "${aot_parent}")
        string(SUBSTRING "${parent_key}" 0 24 parent_key)
        set(alias "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/aot/${parent_key}")
        set(short_source "${alias}/${aot_name}")
        string(LENGTH "${short_source}" short_length)
        if(short_length GREATER 240)
            message(FATAL_ERROR "The build directory or AOT basename leaves no short path for native build tools: ${short_source}")
        endif()
        get_property(helper GLOBAL PROPERTY PS2N_PATH_HELPER)
        if(NOT helper)
            get_filename_component(project_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
            set(helper "${CMAKE_BINARY_DIR}/CMakeFiles/ps2n-path${CMAKE_EXECUTABLE_SUFFIX}")
            # Bootstrap only the host I/O code, using the selected compiler/SDK.
            # try_compile references the original sources; COPY_FILE installs the
            # helper executable, never copies the AOT, firmware, or project tree.
            set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
            try_compile(helper_built "${CMAKE_BINARY_DIR}/CMakeFiles/path-bootstrap"
                SOURCES "${project_root}/tools/ps2n-path.cpp" "${project_root}/src/io.cpp"
                CMAKE_FLAGS "-DINCLUDE_DIRECTORIES:STRING=${project_root}/include"
                CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
                COPY_FILE "${helper}" OUTPUT_VARIABLE helper_log)
            if(NOT helper_built)
                message(FATAL_ERROR "Cannot build the host path helper: ${helper_log}")
            endif()
            set_property(GLOBAL PROPERTY PS2N_PATH_HELPER "${helper}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                "${project_root}/src/io.cpp" "${project_root}/include/ps2native/io.hpp"
                "${project_root}/tools/ps2n-path.cpp")
        endif()
        cmake_path(NATIVE_PATH alias native_alias)
        cmake_path(NATIVE_PATH aot_parent native_parent)
        execute_process(COMMAND "${helper}" alias "${native_alias}" "${native_parent}"
            RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
        if(NOT "${result}" STREQUAL "0")
            message(FATAL_ERROR "Cannot reference the original AOT directory: ${error}")
        endif()
        cmake_path(NATIVE_PATH aot_source native_source)
        cmake_path(NATIVE_PATH short_source native_short_source)
        execute_process(COMMAND "${helper}" equivalent "${native_source}" "${native_short_source}"
            RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
        if(NOT "${result}" STREQUAL "0")
            message(FATAL_ERROR "AOT source identity check failed: ${error}")
        endif()
        set(aot_source "${short_source}")
        message(STATUS "AOT source referenced through a build-local directory alias (no copy)")
    endif()
    target_sources(${target} PRIVATE "${aot_source}")
    set_property(TARGET ${target} PROPERTY PS2N_AOT_SOURCE_REFERENCE "${aot_source}")
endfunction()
