# A checkpoint contains explicit fields, not host object layouts. Header/field
# changes require a new schema. Runtime-only fixes require reviewed opt-in.
file(GLOB checkpoint_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/ps2native/*.hpp")
list(SORT checkpoint_headers)
set(checkpoint_runtime_headers ${checkpoint_headers})
# This header contains only execution helpers/context, not serialized state.
# Its behavior still participates in the runtime fingerprint below.
list(FILTER checkpoint_headers EXCLUDE REGEX "/ee_semantics[.]hpp$")
# Decoded instruction metadata is execution-only too: no MipsOpcode or
# MipsInstruction is serialized by checkpoint_fields.inc or EEState.
# Extending the decoder must change the runtime identity, not the wire schema.
list(FILTER checkpoint_headers EXCLUDE REGEX "/mips[.]hpp$")
list(FILTER checkpoint_headers EXCLUDE REGEX "/vu_fixed[.]hpp$")
# Boot options/reports and host profiling counters are never serialized.
list(FILTER checkpoint_headers EXCLUDE REGEX "/(boot|profile)[.]hpp$")
# Offline code-generation artifacts contain no serialized machine state.
list(FILTER checkpoint_headers EXCLUDE REGEX "/aot_sources[.]hpp$")
# VU native artifacts and shared execution helpers have no checkpoint fields.
list(FILTER checkpoint_headers EXCLUDE REGEX "/(vu_aot|vu_aot_runtime|vu_micro_ops)[.]hpp$")
# Host GPU packets, handles and timers do not introduce guest wire fields.
list(FILTER checkpoint_headers EXCLUDE REGEX "/gs_gpu[.]hpp$")
set(checkpoint_schema "ps2n-complete-checkpoint-v1-le-fields\n")
foreach(header IN LISTS checkpoint_headers)
    file(READ "${header}" content)
    string(REPLACE "\r\n" "\n" content "${content}")
    # Host input callbacks have no wire fields. Keep existing checkpoint schema
    # identity; the full header still participates in the runtime fingerprint.
    # Audited execution-only helpers also add no fields. Strip only explicitly
    # marked additions; the surrounding declarations/fields remain hashed and
    # every byte still participates in the runtime fingerprint below.
    foreach(runtime_marker IN ITEMS HOST_INPUT RUNTIME_HELPERS)
        string(FIND "${content}" "    // ${runtime_marker}_BEGIN" host_begin)
        while(NOT host_begin EQUAL -1)
            string(FIND "${content}" "    // ${runtime_marker}_END\n" host_end)
            if(host_end LESS host_begin)
                message(FATAL_ERROR "Unbalanced runtime-only checkpoint header markers")
            endif()
            string(LENGTH "    // ${runtime_marker}_END\n" host_end_length)
            math(EXPR host_after "${host_end}+${host_end_length}")
            string(SUBSTRING "${content}" 0 ${host_begin} host_prefix)
            string(SUBSTRING "${content}" ${host_after} -1 host_suffix)
            set(content "${host_prefix}${host_suffix}")
            string(FIND "${content}" "    // ${runtime_marker}_BEGIN" host_begin)
        endwhile()
    endforeach()
    # Logically-const cache reads may discard provenance certificates. This
    # host constness qualifier changes neither a field type nor wire bytes;
    # preserve v1 checkpoints from before cached provenance was implemented.
    string(REPLACE "mutable std::unordered_map<std::uint32_t,StoredFpuRelation> fpu_relations_;"
        "std::unordered_map<std::uint32_t,StoredFpuRelation> fpu_relations_;" content "${content}")
    # GS materialization is logically const. Preserve the audited v1 fields
    # and fingerprint; all GPU behavior still changes the runtime identity.
    string(REPLACE "mutable std::vector<std::uint32_t> words_, known_;" "std::vector<std::uint32_t> words_, known_;" content "${content}")
    string(REPLACE "mutable std::uint64_t revision_{};" "std::uint64_t revision_{};" content "${content}")
    string(REPLACE "mutable std::uint64_t pixel_writes_{};" "std::uint64_t pixel_writes_{};" content "${content}")
    string(REPLACE "revision() const { synchronize(); return revision_; }" "revision() const noexcept { return revision_; }" content "${content}")
    string(REPLACE "pixel_writes() const { synchronize(); return pixel_writes_; }" "pixel_writes() const noexcept { return pixel_writes_; }" content "${content}")
    string(REPLACE "revision() const { submit_gpu(); return revision_; }" "revision() const noexcept { return revision_; }" content "${content}")
    string(REPLACE "pixel_writes() const { submit_gpu(); return pixel_writes_; }" "pixel_writes() const noexcept { return pixel_writes_; }" content "${content}")
    get_filename_component(name "${header}" NAME)
    string(APPEND checkpoint_schema "${name}\n${content}\n")
endforeach()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/checkpoint_fields.inc" content)
string(REPLACE "\r\n" "\n" content "${content}")
string(APPEND checkpoint_schema "${content}")
string(SHA256 checkpoint_schema_hash "${checkpoint_schema}")
file(GLOB checkpoint_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(SORT checkpoint_sources)
set(checkpoint_runtime "${checkpoint_schema_hash}\n${CMAKE_CXX_COMPILER_ID}\n${CMAKE_CXX_COMPILER_VERSION}\n${CMAKE_BUILD_TYPE}\n${CMAKE_CXX_FLAGS}\n${PS2N_ENABLE_PROFILING}\n")
string(APPEND checkpoint_runtime "${PS2N_ENABLE_VULKAN}\n${PS2N_ENABLE_DIRECTX}\n${PS2N_GS_DXC_SHA256}\n")
foreach(header IN LISTS checkpoint_runtime_headers)
    file(READ "${header}" content)
    string(REPLACE "\r\n" "\n" content "${content}")
    string(APPEND checkpoint_runtime "${content}\n")
endforeach()
foreach(source IN LISTS checkpoint_sources)
    file(READ "${source}" content)
    string(REPLACE "\r\n" "\n" content "${content}")
    string(APPEND checkpoint_runtime "${content}\n")
endforeach()
# Coefficient ROM data is part of audio execution, not the checkpoint layout.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/spu_gaussian.inc" audio_rom)
string(APPEND checkpoint_runtime "${audio_rom}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/spu_gaussian.inc")
# Shader source and embedded bytecode both affect guest-visible VRAM results.
file(GLOB gs_shader_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/gs_flat.*" "${CMAKE_CURRENT_SOURCE_DIR}/src/gs_flat_*.inc")
list(SORT gs_shader_files)
foreach(shader IN LISTS gs_shader_files)
    file(READ "${shader}" shader_content)
    string(REPLACE "\r\n" "\n" shader_content "${shader_content}")
    string(APPEND checkpoint_runtime "${shader_content}\n")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${gs_shader_files})
# Runtime-only headers must also trigger a reconfigure: filtering them out of
# the wire schema must never leave the runtime fingerprint stale after an edit.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${checkpoint_runtime_headers} ${checkpoint_sources} "${CMAKE_CURRENT_SOURCE_DIR}/src/checkpoint_fields.inc")
string(SHA256 checkpoint_runtime_hash "${checkpoint_runtime}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/checkpoint_build.hpp" CONTENT
"#pragma once\n#define PS2N_CHECKPOINT_SCHEMA \"${checkpoint_schema_hash}\"\n#define PS2N_CHECKPOINT_RUNTIME \"${checkpoint_runtime_hash}\"\n")
target_sources(ps2n_core PRIVATE src/checkpoint.cpp)
target_include_directories(ps2n_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_executable(ps2n-checkpoint tools/ps2n-checkpoint.cpp)
target_link_libraries(ps2n-checkpoint PRIVATE ps2n_core)
if(WIN32 AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    add_executable(ps2n-capture tools/ps2n-capture.cpp)
    target_link_libraries(ps2n-capture PRIVATE ps2n_core)
    set(PS2N_CAPTURE_IMAGE_SHA256 "" CACHE STRING "Explicit target executable SHA-256 for a matching live checkpoint save adapter")
    if(NOT PS2N_CAPTURE_IMAGE_SHA256 STREQUAL "")
        string(LENGTH "${PS2N_CAPTURE_IMAGE_SHA256}" capture_digest_length)
        if(NOT capture_digest_length EQUAL 64 OR NOT PS2N_CAPTURE_IMAGE_SHA256 MATCHES "^[0-9a-f]+$")
            message(FATAL_ERROR "PS2N_CAPTURE_IMAGE_SHA256 must pin exactly one executable")
        endif()
        add_library(ps2n-capture-adapter SHARED tools/checkpoint_capture_adapter.cpp src/checkpoint.cpp)
        target_include_directories(ps2n-capture-adapter PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
        target_compile_definitions(ps2n-capture-adapter PRIVATE PS2N_CHECKPOINT_SAVE_ONLY PS2N_CAPTURE_IMAGE_SHA256="${PS2N_CAPTURE_IMAGE_SHA256}")
        target_link_libraries(ps2n-capture-adapter PRIVATE ps2n_core)
    endif()
endif()
if(BUILD_TESTING)
    add_executable(ps2n-checkpoint-tests tests/checkpoint.cpp)
    target_link_libraries(ps2n-checkpoint-tests PRIVATE ps2n_core)
    add_test(NAME native-complete-checkpoint COMMAND ps2n-checkpoint-tests "${CMAKE_CURRENT_BINARY_DIR}/checkpoint-test")
    set_tests_properties(native-complete-checkpoint PROPERTIES TIMEOUT 90)
    add_test(NAME native-checkpoint-cli COMMAND "${CMAKE_COMMAND}"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/checkpoint-cli-test" "-DFIXTURE=$<TARGET_FILE:ps2n-make-fixture>"
        "-DRUNNER=$<TARGET_FILE:ps2n-run>" "-DCHECKPOINT=$<TARGET_FILE:ps2n-checkpoint>" "-DPROFILE_ENABLED=${PS2N_ENABLE_PROFILING}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/checkpoint_cli.cmake")
    set_tests_properties(native-checkpoint-cli PROPERTIES TIMEOUT 90)
endif()
