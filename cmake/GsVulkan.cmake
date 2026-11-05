# Optional headers only; the runtime loads Vulkan dynamically, so machines
# without a loader or a suitable GPU retain the CPU renderer.
option(PS2N_ENABLE_VULKAN "Build the experimental Vulkan GS backend when headers are available" ON)
target_sources(ps2n_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/gs_gpu.cpp")
if(PS2N_ENABLE_VULKAN)
    find_path(PS2N_VULKAN_INCLUDE_DIR vulkan/vulkan_core.h HINTS "$ENV{VULKAN_SDK}/Include" "$ENV{VULKAN_SDK}/include")
    if(PS2N_VULKAN_INCLUDE_DIR)
        target_sources(ps2n_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/gs_vulkan.cpp")
        target_include_directories(ps2n_core PRIVATE "${PS2N_VULKAN_INCLUDE_DIR}")
        target_compile_definitions(ps2n_core PRIVATE PS2N_HAS_VULKAN=1)
        find_package(Threads REQUIRED)
        target_link_libraries(ps2n_core PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
        message(STATUS "Experimental GS Vulkan: headers found; loader/device checked at runtime")
    else()
        message(STATUS "Experimental GS Vulkan: headers absent; CPU renderer remains available")
    endif()
endif()

# Offline authoring command. Ordinary builds consume the checked-in SPIR-V
# array and require neither a shader compiler nor a runtime SDK installation.
find_program(PS2N_GLSLC_EXECUTABLE NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(PS2N_GLSLC_EXECUTABLE)
    add_custom_target(ps2n-gs-shader
        COMMAND "${CMAKE_COMMAND}"
            "-DPS2N_GLSLC_EXECUTABLE=${PS2N_GLSLC_EXECUTABLE}"
            "-DPS2N_GS_SHADER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/shaders/gs_flat.comp"
            "-DPS2N_GS_SHADER_BINARY=${CMAKE_CURRENT_BINARY_DIR}/gs_flat.spv"
            "-DPS2N_GS_SHADER_INCLUDE=${CMAKE_CURRENT_SOURCE_DIR}/src/gs_flat_spv.inc"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileGsShader.cmake"
        COMMENT "Regenerating the original GS compute shader SPIR-V" VERBATIM)
endif()
