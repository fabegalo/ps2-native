# Exercise real CMake FILEPATH argument handling without duplicating the project
# or implementing a second filesystem boundary in CMake. The tool does the I/O.
foreach(required IN ITEMS PS2N_AOT PS2N_INPUT PS2N_OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
execute_process(COMMAND "${PS2N_AOT}" "${PS2N_INPUT}" "${PS2N_OUTPUT}" --bios
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "AOT via CMake FILEPATH failed (${result}): ${output}${error}")
endif()
