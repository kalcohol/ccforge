if(NOT DEFINED PROGRAM OR NOT DEFINED LOG)
    message(FATAL_ERROR "PROGRAM and LOG are required")
endif()
execute_process(COMMAND "${PROGRAM}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 60)
file(WRITE "${LOG}" "${output}\n${error}")
if(NOT "${result}" STREQUAL "1" OR
   NOT "${output}\n${error}" MATCHES "model mismatch:")
    message(FATAL_ERROR "Hive mutation did not fail through the model; see ${LOG}")
endif()
message(STATUS "Hive mutation was rejected by the independent model")
