foreach(required_var IN ITEMS
        SOURCE_DIR BINARY_DIR FORGE_ROOT_DIR SURFACE_CASE)
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

set(configure_args
    "${CMAKE_COMMAND}"
    -G "${GENERATOR}"
    -S "${SOURCE_DIR}"
    -B "${BINARY_DIR}"
    -DCMAKE_CXX_STANDARD=${CXX_STANDARD}
    -DFORGE_ROOT_DIR=${FORGE_ROOT_DIR}
    -DSURFACE_CASE=${SURFACE_CASE})
if(SURFACE_CASE STREQUAL "linalg_empty_namespace")
    set(expected_probes LINALG=BACKPORT)
else()
    if(NOT DEFINED EXECUTION_CASE OR EXECUTION_CASE STREQUAL "")
        message(FATAL_ERROR "EXECUTION_CASE is required")
    endif()
    list(APPEND configure_args "-DEXECUTION_CASE=${EXECUTION_CASE}")
    set(expected_probes SENDERS=PARTIAL SIMD=PARTIAL)
    if(SURFACE_CASE STREQUAL "no_shadow_root_no_mdspan")
        list(APPEND expected_probes MDSPAN_BASE=UNAVAILABLE MDSPAN_PADDED_LAYOUTS=UNAVAILABLE
            SUBMDSPAN=UNAVAILABLE LINALG=UNAVAILABLE)
    else()
        list(APPEND expected_probes MDSPAN_PADDED_LAYOUTS=PARTIAL SUBMDSPAN=PARTIAL LINALG=PARTIAL)
    endif()
    if(SURFACE_CASE MATCHES "^no_shadow_root(_no_mdspan)?$")
        list(APPEND expected_probes "CONSTANT_WRAPPER=(COMPLETE|PARTIAL)" HIVE=PARTIAL)
    endif()
endif()
if(DEFINED PARENT_CACHE AND NOT PARENT_CACHE STREQUAL "")
    list(APPEND configure_args -C "${PARENT_CACHE}")
endif()
if(DEFINED GENERATOR_PLATFORM AND NOT GENERATOR_PLATFORM STREQUAL "")
    list(APPEND configure_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT GENERATOR_TOOLSET STREQUAL "")
    list(APPEND configure_args -T "${GENERATOR_TOOLSET}")
endif()
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
set(case_label "${SURFACE_CASE}")
if(DEFINED EXECUTION_CASE AND NOT EXECUTION_CASE STREQUAL "")
    string(APPEND case_label "/${EXECUTION_CASE}")
endif()
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR
        "${case_label} probe-surface configure failed:\n${configure_log}")
endif()

foreach(expected_probe IN LISTS expected_probes)
    if(NOT configure_log MATCHES "CC Forge probe: ${expected_probe}")
        message(FATAL_ERROR
            "${case_label} did not report ${expected_probe}:\n${configure_log}")
    endif()
endforeach()

set(build_args --build "${BINARY_DIR}" --target probe_surface_consumer)
if(DEFINED TEST_CONFIG AND NOT TEST_CONFIG STREQUAL "")
    list(APPEND build_args --config "${TEST_CONFIG}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" ${build_args}
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR
        "${case_label} probe-surface build failed:\n${build_output}\n${build_error}")
endif()
