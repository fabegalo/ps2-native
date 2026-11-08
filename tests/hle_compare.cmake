execute_process(COMMAND "${NATIVE}" "${IMAGE}" "${SCRATCH}"
    RESULT_VARIABLE native_exit OUTPUT_VARIABLE native_output ERROR_VARIABLE native_error TIMEOUT 25)
execute_process(COMMAND "${REFERENCE}" "${IMAGE}" "${SCRATCH}"
    RESULT_VARIABLE reference_exit OUTPUT_VARIABLE reference_output ERROR_VARIABLE reference_error TIMEOUT 25)
if(NOT native_exit EQUAL 0 OR NOT reference_exit EQUAL 0)
    message(FATAL_ERROR "HLE comparison failed: native=${native_exit}: ${native_error}; reference=${reference_exit}: ${reference_error}")
endif()
if(NOT native_output STREQUAL reference_output)
    message(FATAL_ERROR "HLE execution differs: ${native_output} / ${reference_output}")
endif()
message(STATUS "${native_output}")
