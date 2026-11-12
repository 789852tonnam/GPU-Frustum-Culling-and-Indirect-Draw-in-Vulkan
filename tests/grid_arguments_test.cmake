# Invalid scene sizes must fail during parsing, before a desktop or GPU is needed.
foreach(argument IN ITEMS --grid-side --group-level)
    foreach(value IN ITEMS "" 0 -1 +20 1.5 20x20 65536 4294967296 999999999999999999999 --grouped)
        execute_process(COMMAND "${PROGRAM}" "${argument}" "${value}"
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
        if(NOT result STREQUAL "1" OR NOT error MATCHES "Invalid ${argument}" OR NOT output STREQUAL "")
            message(FATAL_ERROR "${argument} '${value}' was not rejected at parsing: ${result}; ${output}; ${error}")
        endif()
    endforeach()

    execute_process(COMMAND "${PROGRAM}" "${argument}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result STREQUAL "1" OR NOT error MATCHES "Invalid ${argument}" OR NOT output STREQUAL "")
        message(FATAL_ERROR "Missing ${argument} was not rejected: ${result}; ${output}; ${error}")
    endif()
endforeach()

execute_process(COMMAND "${PROGRAM}" --group-level 16
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result STREQUAL "1" OR NOT error MATCHES "Invalid --group-level" OR NOT output STREQUAL "")
    message(FATAL_ERROR "Excess group level was not rejected: ${result}; ${output}; ${error}")
endif()
