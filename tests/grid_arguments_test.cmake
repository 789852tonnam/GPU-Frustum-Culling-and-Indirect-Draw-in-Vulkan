# Invalid dimensions must fail during parsing, before a desktop or GPU is needed.
foreach(value IN ITEMS "" 0 -1 +20 1.5 20x20 65536 4294967296 999999999999999999999 --grouped)
    execute_process(COMMAND "${PROGRAM}" --grid-side "${value}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result STREQUAL "1" OR NOT error MATCHES "Invalid --grid-side" OR NOT output STREQUAL "")
        message(FATAL_ERROR "Grid '${value}' was not rejected at parsing: ${result}; ${output}; ${error}")
    endif()
endforeach()

execute_process(COMMAND "${PROGRAM}" --grid-side
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result STREQUAL "1" OR NOT error MATCHES "Invalid --grid-side" OR NOT output STREQUAL "")
    message(FATAL_ERROR "Missing grid dimension was not rejected: ${result}; ${output}; ${error}")
endif()
