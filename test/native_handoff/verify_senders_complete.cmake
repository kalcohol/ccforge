foreach(required_var IN ITEMS SOURCE_DIR BINARY_DIR FORGE_ROOT_DIR)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required")
    endif()
endforeach()
if(NOT DEFINED CXX_STANDARD OR CXX_STANDARD STREQUAL "")
    set(CXX_STANDARD 23)
endif()
if(NOT DEFINED GENERATOR OR GENERATOR STREQUAL "")
    set(GENERATOR "Ninja")
endif()
if(NOT DEFINED SENDERS_CASE)
    set(SENDERS_CASE complete)
endif()
if(SENDERS_CASE STREQUAL "complete")
    set(expected_probe COMPLETE)
elseif(SENDERS_CASE STREQUAL "missing_scheduler_allocator")
    set(expected_probe PARTIAL)
else()
    message(FATAL_ERROR "Unknown senders fixture case: ${SENDERS_CASE}")
endif()

set(configure_args
    "${CMAKE_COMMAND}"
    -G "${GENERATOR}"
    -S "${SOURCE_DIR}"
    -B "${BINARY_DIR}"
    -DCMAKE_CXX_STANDARD=${CXX_STANDARD}
    -DSENDERS_CASE=${SENDERS_CASE}
    -DFORGE_ROOT_DIR=${FORGE_ROOT_DIR})
if(DEFINED CXX_COMPILER AND NOT CXX_COMPILER STREQUAL "")
    list(APPEND configure_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT MAKE_PROGRAM STREQUAL "")
    list(APPEND configure_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()

file(REMOVE_RECURSE "${BINARY_DIR}")
execute_process(
    COMMAND ${configure_args}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)
set(configure_log "${configure_output}\n${configure_error}")
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR
        "${SENDERS_CASE} senders fixture configure failed:\n${configure_log}")
endif()
if(NOT configure_log MATCHES "CC Forge probe: SENDERS=${expected_probe}")
    message(FATAL_ERROR
        "${SENDERS_CASE} senders fixture was not classified ${expected_probe}:\n${configure_log}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${BINARY_DIR}"
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR
        "${SENDERS_CASE} senders fixture build failed:\n${build_output}\n${build_error}")
endif()
