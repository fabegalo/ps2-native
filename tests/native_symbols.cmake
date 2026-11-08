# Audit the archive too: dead stripping must not hide an interpreter dependency.
foreach(kind IN ITEMS NATIVE)
    execute_process(COMMAND "${DUMPBIN}" /nologo /linkermember:2 "${${kind}}"
        RESULT_VARIABLE result OUTPUT_VARIABLE symbols ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${kind} symbol audit failed: ${error}")
    endif()
    string(REGEX MATCH "interpret_instruction|[?]step_micro@VuControl|[?]evaluate_micro_upper@VuControl|execute_instruction@[$]0[?]0|issue_micro_shared@[$]0[?]0" interpreter "${symbols}")
    if(kind STREQUAL "NATIVE" AND interpreter)
        message(FATAL_ERROR "Native runtime contains interpreter symbol: ${interpreter}")
    endif()
endforeach()
message(STATUS "Shared runtime archive excludes EE/IOP/VU interpreters")
